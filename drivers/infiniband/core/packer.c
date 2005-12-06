/*
 * Copyright (c) 2004 Topspin Corporation.  All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: packer.c 1349 2004-12-16 21:09:43Z roland $
 */

#include <linux/string.h>

#include <rdma/ib_pack.h>

static u64 value_read(int offset, int size, void *structure)
{
	switch (size) {
	case 1: return                *(u8  *) (structure + offset);
	case 2: return be16_to_cpup((__be16 *) (structure + offset));
	case 4: return be32_to_cpup((__be32 *) (structure + offset));
	case 8: return be64_to_cpup((__be64 *) (structure + offset));
	default:
		printk(KERN_WARNING "Field size %d bits not handled\n", size * 8);
		return 0;
	}
}

/**
 * ib_pack - Pack a structure into a buffer
 * @desc:Array of structure field descriptions
 * @desc_len:Number of entries in @desc
 * @structure:Structure to pack from
 * @buf:Buffer to pack into
 *
 * ib_pack() packs a list of structure fields into a buffer,
 * controlled by the array of fields in @desc.
 */
void ib_pack(const struct ib_field        *desc,
	     int                           desc_len,
	     void                         *structure,
	     void                         *buf)
{
	int i;

	for (i = 0; i < desc_len; ++i) {
		if (desc[i].size_bits <= 32) {
			int shift;
			u32 val;
			__be32 mask;
			__be32 *addr;

			shift = 32 - desc[i].offset_bits - desc[i].size_bits;
			if (desc[i].struct_size_bytes)
				val = value_read(desc[i].struct_offset_bytes,
						 desc[i].struct_size_bytes,
						 structure) << shift;
			else
				val = 0;

			mask = cpu_to_be32(((1ull << desc[i].size_bits) - 1) << shift);
			addr = (__be32 *) buf + desc[i].offset_words;
			*addr = (*addr & ~mask) | (cpu_to_be32(val) & mask);
		} else if (desc[i].size_bits <= 64) {
			int shift;
			u64 val;
			__be64 mask;
			__be64 *addr;

			shift = 64 - desc[i].offset_bits - desc[i].size_bits;
			if (desc[i].struct_size_bytes)
				val = value_read(desc[i].struct_offset_bytes,
						 desc[i].struct_size_bytes,
						 structure) << shift;
			else
				val = 0;

			mask = cpu_to_be64((~0ull >> (64 - desc[i].size_bits)) << shift);
			addr = (__be64 *) ((__be32 *) buf + desc[i].offset_words);
			*addr = (*addr & ~mask) | (cpu_to_be64(val) & mask);
		} else {
			if (desc[i].offset_bits % 8 ||
			    desc[i].size_bits   % 8) {
				printk(KERN_WARNING "Structure field %s of size %d "
				       "bits is not byte-aligned\n",
				       desc[i].field_name, desc[i].size_bits);
			}

			if (desc[i].struct_size_bytes)
				memcpy(buf + desc[i].offset_words * 4 +
				       desc[i].offset_bits / 8,
				       structure + desc[i].struct_offset_bytes,
				       desc[i].size_bits / 8);
			else
				memset(buf + desc[i].offset_words * 4 +
				       desc[i].offset_bits / 8,
				       0,
				       desc[i].size_bits / 8);
		}
	}
}
EXPORT_SYMBOL(ib_pack);

static void value_write(int offset, int size, u64 val, void *structure)
{
	switch (size * 8) {
	case 8:  *(    u8 *) (structure + offset) = val; break;
	case 16: *(__be16 *) (structure + offset) = cpu_to_be16(val); break;
	case 32: *(__be32 *) (structure + offset) = cpu_to_be32(val); break;
	case 64: *(__be64 *) (structure + offset) = cpu_to_be64(val); break;
	default:
		printk(KERN_WARNING "Field size %d bits not handled\n", size * 8);
	}
}

/**
 * ib_unpack - Unpack a buffer into a structure
 * @desc:Array of structure field descriptions
 * @desc_len:Number of entries in @desc
 * @buf:Buffer to unpack from
 * @structure:Structure to unpack into
 *
 * ib_pack() unpacks a list of structure fields from a buffer,
 * controlled by the array of fields in @desc.
 */
void ib_unpack(const struct ib_field        *desc,
	       int                           desc_len,
	       void                         *buf,
	       void                         *structure)
{
	int i;

	for (i = 0; i < desc_len; ++i) {
		if (!desc[i].struct_size_bytes)
			continue;

		if (desc[i].size_bits <= 32) {
			int shift;
			u32  val;
			u32  mask;
			__be32 *addr;

			shift = 32 - desc[i].offset_bits - desc[i].size_bits;
			mask = ((1ull << desc[i].size_bits) - 1) << shift;
			addr = (__be32 *) buf + desc[i].offset_words;
			val = (be32_to_cpup(addr) & mask) >> shift;
			value_write(desc[i].struct_offset_bytes,
				    desc[i].struct_size_bytes,
				    val,
				    structure);
		} else if (desc[i].size_bits <= 64) {
			int shift;
			u64  val;
			u64  mask;
			__be64 *addr;

			shift = 64 - desc[i].offset_bits - desc[i].size_bits;
			mask = (~0ull >> (64 - desc[i].size_bits)) << shift;
			addr = (__be64 *) buf + desc[i].offset_words;
			val = (be64_to_cpup(addr) & mask) >> shift;
			value_write(desc[i].struct_offset_bytes,
				    desc[i].struct_size_bytes,
				    val,
				    structure);
		} else {
			if (desc[i].offset_bits % 8 ||
			    desc[i].size_bits   % 8) {
				printk(KERN_WARNING "Structure field %s of size %d "
				       "bits is not byte-aligned\n",
				       desc[i].field_name, desc[i].size_bits);
			}

			memcpy(structure + desc[i].struct_offset_bytes,
			       buf + desc[i].offset_words * 4 +
			       desc[i].offset_bits / 8,
			       desc[i].size_bits / 8);
		}
	}
}
EXPORT_SYMBOL(ib_unpack);
