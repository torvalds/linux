/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2010 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_RBD_TYPES_H
#define CEPH_RBD_TYPES_H

#include <linux/types.h>

/*
 * rbd image 'foo' consists of objects
 *   foo.rbd      - image metadata
 *   foo.00000000
 *   foo.00000001
 *   ...          - data
 */

#define RBD_SUFFIX		".rbd"
#define RBD_DIRECTORY           "rbd_directory"
#define RBD_INFO                "rbd_info"

#define RBD_DEFAULT_OBJ_ORDER	22   /* 4MB */
#define RBD_MIN_OBJ_ORDER       16
#define RBD_MAX_OBJ_ORDER       30

#define RBD_MAX_OBJ_NAME_LEN	96
#define RBD_MAX_SEG_NAME_LEN	128

#define RBD_COMP_NONE		0
#define RBD_CRYPT_NONE		0

#define RBD_HEADER_TEXT		"<<< Rados Block Device Image >>>\n"
#define RBD_HEADER_SIGNATURE	"RBD"
#define RBD_HEADER_VERSION	"001.005"

struct rbd_image_snap_ondisk {
	__le64 id;
	__le64 image_size;
} __attribute__((packed));

struct rbd_image_header_ondisk {
	char text[40];
	char block_name[24];
	char signature[4];
	char version[8];
	struct {
		__u8 order;
		__u8 crypt_type;
		__u8 comp_type;
		__u8 unused;
	} __attribute__((packed)) options;
	__le64 image_size;
	__le64 snap_seq;
	__le32 snap_count;
	__le32 reserved;
	__le64 snap_names_len;
	struct rbd_image_snap_ondisk snaps[0];
} __attribute__((packed));


#endif
