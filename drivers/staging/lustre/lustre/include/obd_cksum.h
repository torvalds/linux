/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef __OBD_CKSUM
#define __OBD_CKSUM
#include "../../include/linux/libcfs/libcfs.h"
#include "lustre/lustre_idl.h"

static inline unsigned char cksum_obd2cfs(cksum_type_t cksum_type)
{
	switch (cksum_type) {
	case OBD_CKSUM_CRC32:
		return CFS_HASH_ALG_CRC32;
	case OBD_CKSUM_ADLER:
		return CFS_HASH_ALG_ADLER32;
	case OBD_CKSUM_CRC32C:
		return CFS_HASH_ALG_CRC32C;
	default:
		CERROR("Unknown checksum type (%x)!!!\n", cksum_type);
		LBUG();
	}
	return 0;
}

/* The OBD_FL_CKSUM_* flags is packed into 5 bits of o_flags, since there can
 * only be a single checksum type per RPC.
 *
 * The OBD_CHECKSUM_* type bits passed in ocd_cksum_types are a 32-bit bitmask
 * since they need to represent the full range of checksum algorithms that
 * both the client and server can understand.
 *
 * In case of an unsupported types/flags we fall back to ADLER
 * because that is supported by all clients since 1.8
 *
 * In case multiple algorithms are supported the best one is used. */
static inline obd_flag cksum_type_pack(cksum_type_t cksum_type)
{
	unsigned int    performance = 0, tmp;
	obd_flag	flag = OBD_FL_CKSUM_ADLER;

	if (cksum_type & OBD_CKSUM_CRC32) {
		tmp = cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_CRC32));
		if (tmp > performance) {
			performance = tmp;
			flag = OBD_FL_CKSUM_CRC32;
		}
	}
	if (cksum_type & OBD_CKSUM_CRC32C) {
		tmp = cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_CRC32C));
		if (tmp > performance) {
			performance = tmp;
			flag = OBD_FL_CKSUM_CRC32C;
		}
	}
	if (cksum_type & OBD_CKSUM_ADLER) {
		tmp = cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_ADLER));
		if (tmp > performance) {
			performance = tmp;
			flag = OBD_FL_CKSUM_ADLER;
		}
	}
	if (unlikely(cksum_type && !(cksum_type & (OBD_CKSUM_CRC32C |
						   OBD_CKSUM_CRC32 |
						   OBD_CKSUM_ADLER))))
		CWARN("unknown cksum type %x\n", cksum_type);

	return flag;
}

static inline cksum_type_t cksum_type_unpack(obd_flag o_flags)
{
	switch (o_flags & OBD_FL_CKSUM_ALL) {
	case OBD_FL_CKSUM_CRC32C:
		return OBD_CKSUM_CRC32C;
	case OBD_FL_CKSUM_CRC32:
		return OBD_CKSUM_CRC32;
	default:
		break;
	}

	return OBD_CKSUM_ADLER;
}

/* Return a bitmask of the checksum types supported on this system.
 * 1.8 supported ADLER it is base and not depend on hw
 * Client uses all available local algos
 */
static inline cksum_type_t cksum_types_supported_client(void)
{
	cksum_type_t ret = OBD_CKSUM_ADLER;

	CDEBUG(D_INFO, "Crypto hash speed: crc %d, crc32c %d, adler %d\n",
	       cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_CRC32)),
	       cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_CRC32C)),
	       cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_ADLER)));

	if (cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_CRC32C)) > 0)
		ret |= OBD_CKSUM_CRC32C;
	if (cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_CRC32)) > 0)
		ret |= OBD_CKSUM_CRC32;

	return ret;
}

/* Server uses algos that perform at 50% or better of the Adler */
static inline cksum_type_t cksum_types_supported_server(void)
{
	int	     base_speed;
	cksum_type_t    ret = OBD_CKSUM_ADLER;

	CDEBUG(D_INFO, "Crypto hash speed: crc %d, crc32c %d, adler %d\n",
	       cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_CRC32)),
	       cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_CRC32C)),
	       cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_ADLER)));

	base_speed = cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_ADLER)) / 2;

	if (cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_CRC32C)) >=
	    base_speed)
		ret |= OBD_CKSUM_CRC32C;
	if (cfs_crypto_hash_speed(cksum_obd2cfs(OBD_CKSUM_CRC32)) >=
	    base_speed)
		ret |= OBD_CKSUM_CRC32;

	return ret;
}


/* Select the best checksum algorithm among those supplied in the cksum_types
 * input.
 *
 * Currently, calling cksum_type_pack() with a mask will return the fastest
 * checksum type due to its benchmarking at libcfs module load.
 * Caution is advised, however, since what is fastest on a single client may
 * not be the fastest or most efficient algorithm on the server.  */
static inline cksum_type_t cksum_type_select(cksum_type_t cksum_types)
{
	return cksum_type_unpack(cksum_type_pack(cksum_types));
}

/* Checksum algorithm names. Must be defined in the same order as the
 * OBD_CKSUM_* flags. */
#define DECLARE_CKSUM_NAME char *cksum_name[] = {"crc32", "adler", "crc32c"}

#endif /* __OBD_H */
