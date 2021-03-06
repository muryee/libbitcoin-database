/**
 * Copyright (c) 2011-2015 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * libbitcoin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef LIBBITCOIN_DATABASE_SLAB_LIST_IPP
#define LIBBITCOIN_DATABASE_SLAB_LIST_IPP

#include <bitcoin/database/memory/memory.hpp>

namespace libbitcoin {
namespace database {

/**
 * Item for slab_hash_table. A chained list with the key included.
 *
 * Stores the key, next position and user data.
 * With the starting item, we can iterate until the end using the
 * next_position() method.
 */
template <typename HashType>
class slab_row
{
public:
    static BC_CONSTEXPR size_t position_size = sizeof(file_offset);
    static BC_CONSTEXPR size_t hash_size = std::tuple_size<HashType>::value;
    static BC_CONSTEXPR file_offset value_begin = hash_size + position_size;

    slab_row(slab_manager& manager, file_offset position);

    file_offset create(const HashType& key, const size_t value_size,
        const file_offset next);

    /// Does this match?
    bool compare(const HashType& key) const;

    /// The actual user data.
    const memory_ptr data() const;

    /// Position of next item in the chained list.
    file_offset next_position() const;

    /// Write a new next position.
    void write_next_position(file_offset next);

private:
    const memory_ptr raw_next_data() const;
    const memory_ptr raw_data(file_offset offset) const;

    file_offset position_;
    slab_manager& manager_;
    mutable shared_mutex mutex_;
};

template <typename HashType>
slab_row<HashType>::slab_row(slab_manager& manager,
    const file_offset position)
  : manager_(manager), position_(position)
{
    static_assert(position_size == 8, "Invalid file_offset size.");
}

template <typename HashType>
file_offset slab_row<HashType>::create(const HashType& key,
    const size_t value_size, const file_offset next)
{
    const file_offset info_size = key.size() + position_size;

    // Create new slab.
    //   [ HashType ]
    //   [ next:8   ]
    //   [ value... ]
    const size_t slab_size = info_size + value_size;
    position_ = manager_.new_slab(slab_size);

    // Write to slab.
    const auto memory = raw_data(0);
    const auto key_data = REMAP_ADDRESS(memory);
    auto serial = make_serializer(key_data);
    serial.write_data(key);

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    unique_lock(mutex_);
    serial.template write_little_endian<file_offset>(next);

    return position_;
    ///////////////////////////////////////////////////////////////////////////
}

template <typename HashType>
bool slab_row<HashType>::compare(const HashType& key) const
{
    // Key data is at the start.
    const auto memory = raw_data(0);
    return std::equal(key.begin(), key.end(), REMAP_ADDRESS(memory));
}

template <typename HashType>
const memory_ptr slab_row<HashType>::data() const
{
    // Value data is at the end.
    return raw_data(value_begin);
}

template <typename HashType>
file_offset slab_row<HashType>::next_position() const
{
    const auto memory = raw_next_data();
    const auto next_address = REMAP_ADDRESS(memory);

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    shared_lock(mutex_);
    return from_little_endian_unsafe<file_offset>(next_address);
    ///////////////////////////////////////////////////////////////////////////
}

template <typename HashType>
void slab_row<HashType>::write_next_position(file_offset next)
{
    const auto memory = raw_next_data();
    auto serial = make_serializer(REMAP_ADDRESS(memory));

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    unique_lock(mutex_);
    serial.template write_little_endian<file_offset>(next);
    ///////////////////////////////////////////////////////////////////////////
}

template <typename HashType>
const memory_ptr slab_row<HashType>::raw_data(file_offset offset) const
{
    auto memory = manager_.get(position_);
    REMAP_INCREMENT(memory, offset);
    return memory;
}

template <typename HashType>
const memory_ptr slab_row<HashType>::raw_next_data() const
{
    // Next position is after key data.
    return raw_data(hash_size);
}

} // namespace database
} // namespace libbitcoin

#endif
