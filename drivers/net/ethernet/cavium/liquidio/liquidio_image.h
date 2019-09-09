/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more details.
 ***********************************************************************/
#ifndef _LIQUIDIO_IMAGE_H_
#define _LIQUIDIO_IMAGE_H_

#define LIO_MAX_FW_TYPE_LEN     (8)
#define LIO_MAX_FW_FILENAME_LEN (256)
#define LIO_FW_DIR              "liquidio/"
#define LIO_FW_BASE_NAME        "lio_"
#define LIO_FW_NAME_SUFFIX      ".bin"
#define LIO_FW_NAME_TYPE_NIC    "nic"
#define LIO_FW_NAME_TYPE_AUTO   "auto"
#define LIO_FW_NAME_TYPE_NONE   "none"
#define LIO_MAX_FIRMWARE_VERSION_LEN 16

#define LIO_MAX_BOOTCMD_LEN 1024
#define LIO_MAX_IMAGES 16
#define LIO_NIC_MAGIC 0x434E4943     /* "CNIC" */
struct octeon_firmware_desc {
	__be64 addr;
	__be32 len;
	__be32 crc32;         /* crc32 of image */
};

/* Following the header is a list of 64-bit aligned binary images,
 * as described by the desc field.
 * Numeric fields are in network byte order.
 */
struct octeon_firmware_file_header {
	__be32 magic;
	char version[LIO_MAX_FIRMWARE_VERSION_LEN];
	char bootcmd[LIO_MAX_BOOTCMD_LEN];
	__be32 num_images;
	struct octeon_firmware_desc desc[LIO_MAX_IMAGES];
	__be32 pad;
	__be32 crc32;         /* header checksum */
};

#endif /* _LIQUIDIO_IMAGE_H_ */
