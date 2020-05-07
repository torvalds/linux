/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef _LINUX_BYTEORDER_LITTLE_ENDIAN_H
#define _LINUX_BYTEORDER_LITTLE_ENDIAN_H

#ifndef __LITTLE_ENDIAN
	#define __LITTLE_ENDIAN 1234
#endif
#ifndef __LITTLE_ENDIAN_BITFIELD
	#define __LITTLE_ENDIAN_BITFIELD
#endif

#include <byteorder/swab.h>

#ifndef __constant_htonl
	#define __constant_htonl(x) ___constant_swab32((x))
	#define __constant_ntohl(x) ___constant_swab32((x))
	#define __constant_htons(x) ___constant_swab16((x))
	#define __constant_ntohs(x) ___constant_swab16((x))
	#define __constant_cpu_to_le64(x) ((__u64)(x))
	#define __constant_le64_to_cpu(x) ((__u64)(x))
	#define __constant_cpu_to_le32(x) ((__u32)(x))
	#define __constant_le32_to_cpu(x) ((__u32)(x))
	#define __constant_cpu_to_le16(x) ((__u16)(x))
	#define __constant_le16_to_cpu(x) ((__u16)(x))
	#define __constant_cpu_to_be64(x) ___constant_swab64((x))
	#define __constant_be64_to_cpu(x) ___constant_swab64((x))
	#define __constant_cpu_to_be32(x) ___constant_swab32((x))
	#define __constant_be32_to_cpu(x) ___constant_swab32((x))
	#define __constant_cpu_to_be16(x) ___constant_swab16((x))
	#define __constant_be16_to_cpu(x) ___constant_swab16((x))
	#define __cpu_to_le64(x) ((__u64)(x))
	#define __le64_to_cpu(x) ((__u64)(x))
	#define __cpu_to_le32(x) ((__u32)(x))
	#define __le32_to_cpu(x) ((__u32)(x))
	#define __cpu_to_le16(x) ((__u16)(x))
	#define __le16_to_cpu(x) ((__u16)(x))
	#define __cpu_to_be64(x) __swab64((x))
	#define __be64_to_cpu(x) __swab64((x))
	#define __cpu_to_be32(x) __swab32((x))
	#define __be32_to_cpu(x) __swab32((x))
	#define __cpu_to_be16(x) __swab16((x))
	#define __be16_to_cpu(x) __swab16((x))
	#define __cpu_to_le64p(x) (*(__u64 *)(x))
	#define __le64_to_cpup(x) (*(__u64 *)(x))
	#define __cpu_to_le32p(x) (*(__u32 *)(x))
	#define __le32_to_cpup(x) (*(__u32 *)(x))
	#define __cpu_to_le16p(x) (*(__u16 *)(x))
	#define __le16_to_cpup(x) (*(__u16 *)(x))
	#define __cpu_to_be64p(x) __swab64p((x))
	#define __be64_to_cpup(x) __swab64p((x))
	#define __cpu_to_be32p(x) __swab32p((x))
	#define __be32_to_cpup(x) __swab32p((x))
	#define __cpu_to_be16p(x) __swab16p((x))
	#define __be16_to_cpup(x) __swab16p((x))
	#define __cpu_to_le64s(x) do {} while (0)
	#define __le64_to_cpus(x) do {} while (0)
	#define __cpu_to_le32s(x) do {} while (0)
	#define __le32_to_cpus(x) do {} while (0)
	#define __cpu_to_le16s(x) do {} while (0)
	#define __le16_to_cpus(x) do {} while (0)
	#define __cpu_to_be64s(x) __swab64s((x))
	#define __be64_to_cpus(x) __swab64s((x))
	#define __cpu_to_be32s(x) __swab32s((x))
	#define __be32_to_cpus(x) __swab32s((x))
	#define __cpu_to_be16s(x) __swab16s((x))
	#define __be16_to_cpus(x) __swab16s((x))
#endif /* __constant_htonl */

#include <byteorder/generic.h>

#endif /* _LINUX_BYTEORDER_LITTLE_ENDIAN_H */
