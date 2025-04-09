/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for AMD network controllers and boards
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef _EFX_FW_FORMATS_H
#define _EFX_FW_FORMATS_H

/* Header layouts of firmware update images recognised by Efx NICs.
 * The sources-of-truth for these layouts are AMD internal documents
 * and sfregistry headers, neither of which are available externally
 * nor usable directly by the driver.
 *
 * While each format includes a 'magic number', these are at different
 * offsets in the various formats, and a legal header for one format
 * could have the right value in whichever field occupies that offset
 * to match another format's magic.
 * Besides, some packaging formats (such as CMS/PKCS#7 signed images)
 * prepend a header for which finding the size is a non-trivial task;
 * rather than trying to parse those headers, we search byte-by-byte
 * through the provided firmware image looking for a valid header.
 * Thus, format recognition has to include validation of the checksum
 * field, even though the firmware will validate that itself before
 * applying the image.
 */

/* EF10 (Medford2, X2) "reflash" header format.  Defined in SF-121352-AN */
#define EFX_REFLASH_HEADER_MAGIC_OFST 0
#define EFX_REFLASH_HEADER_MAGIC_LEN 4
#define EFX_REFLASH_HEADER_MAGIC_VALUE 0x106F1A5

#define EFX_REFLASH_HEADER_VERSION_OFST 4
#define EFX_REFLASH_HEADER_VERSION_LEN 4
#define EFX_REFLASH_HEADER_VERSION_VALUE 4

#define EFX_REFLASH_HEADER_FIRMWARE_TYPE_OFST 8
#define EFX_REFLASH_HEADER_FIRMWARE_TYPE_LEN 4
#define EFX_REFLASH_FIRMWARE_TYPE_BOOTROM 0x2
#define EFX_REFLASH_FIRMWARE_TYPE_BUNDLE 0xd

#define EFX_REFLASH_HEADER_FIRMWARE_SUBTYPE_OFST 12
#define EFX_REFLASH_HEADER_FIRMWARE_SUBTYPE_LEN 4

#define EFX_REFLASH_HEADER_PAYLOAD_SIZE_OFST 16
#define EFX_REFLASH_HEADER_PAYLOAD_SIZE_LEN 4

#define EFX_REFLASH_HEADER_LENGTH_OFST 20
#define EFX_REFLASH_HEADER_LENGTH_LEN 4

/* Reflash trailer */
#define EFX_REFLASH_TRAILER_CRC_OFST 0
#define EFX_REFLASH_TRAILER_CRC_LEN 4

#define EFX_REFLASH_TRAILER_LEN	\
	(EFX_REFLASH_TRAILER_CRC_OFST + EFX_REFLASH_TRAILER_CRC_LEN)

/* EF100 "SmartNIC image" header format.
 * Defined in sfregistry "src/layout/snic_image_hdr.h".
 */
#define EFX_SNICIMAGE_HEADER_MAGIC_OFST 16
#define EFX_SNICIMAGE_HEADER_MAGIC_LEN 4
#define EFX_SNICIMAGE_HEADER_MAGIC_VALUE 0x541C057A

#define EFX_SNICIMAGE_HEADER_VERSION_OFST 20
#define EFX_SNICIMAGE_HEADER_VERSION_LEN 4
#define EFX_SNICIMAGE_HEADER_VERSION_VALUE 1

#define EFX_SNICIMAGE_HEADER_LENGTH_OFST 24
#define EFX_SNICIMAGE_HEADER_LENGTH_LEN 4

#define EFX_SNICIMAGE_HEADER_PARTITION_TYPE_OFST 36
#define EFX_SNICIMAGE_HEADER_PARTITION_TYPE_LEN 4

#define EFX_SNICIMAGE_HEADER_PARTITION_SUBTYPE_OFST 40
#define EFX_SNICIMAGE_HEADER_PARTITION_SUBTYPE_LEN 4

#define EFX_SNICIMAGE_HEADER_PAYLOAD_SIZE_OFST 60
#define EFX_SNICIMAGE_HEADER_PAYLOAD_SIZE_LEN 4

#define EFX_SNICIMAGE_HEADER_CRC_OFST 64
#define EFX_SNICIMAGE_HEADER_CRC_LEN 4

#define EFX_SNICIMAGE_HEADER_MINLEN 256

/* EF100 "SmartNIC bundle" header format.  Defined in SF-122606-TC */
#define EFX_SNICBUNDLE_HEADER_MAGIC_OFST 0
#define EFX_SNICBUNDLE_HEADER_MAGIC_LEN 4
#define EFX_SNICBUNDLE_HEADER_MAGIC_VALUE 0xB1001001

#define EFX_SNICBUNDLE_HEADER_VERSION_OFST 4
#define EFX_SNICBUNDLE_HEADER_VERSION_LEN 4
#define EFX_SNICBUNDLE_HEADER_VERSION_VALUE 1

#define EFX_SNICBUNDLE_HEADER_BUNDLE_TYPE_OFST 8
#define EFX_SNICBUNDLE_HEADER_BUNDLE_TYPE_LEN 4

#define EFX_SNICBUNDLE_HEADER_BUNDLE_SUBTYPE_OFST 12
#define EFX_SNICBUNDLE_HEADER_BUNDLE_SUBTYPE_LEN 4

#define EFX_SNICBUNDLE_HEADER_LENGTH_OFST 20
#define EFX_SNICBUNDLE_HEADER_LENGTH_LEN 4

#define EFX_SNICBUNDLE_HEADER_CRC_OFST 224
#define EFX_SNICBUNDLE_HEADER_CRC_LEN 4

#define EFX_SNICBUNDLE_HEADER_LEN	\
	(EFX_SNICBUNDLE_HEADER_CRC_OFST + EFX_SNICBUNDLE_HEADER_CRC_LEN)

#endif /* _EFX_FW_FORMATS_H */
