/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015 Etnaviv Project
 */

#ifndef ETNAVIV_DUMP_H
#define ETNAVIV_DUMP_H

#include <linux/types.h>

enum {
	ETDUMP_MAGIC = 0x414e5445,
	ETDUMP_BUF_REG = 0,
	ETDUMP_BUF_MMU,
	ETDUMP_BUF_RING,
	ETDUMP_BUF_CMD,
	ETDUMP_BUF_BOMAP,
	ETDUMP_BUF_BO,
	ETDUMP_BUF_END,
};

struct etnaviv_dump_object_header {
	__le32 magic;
	__le32 type;
	__le32 file_offset;
	__le32 file_size;
	__le64 iova;
	__le32 data[2];
};

/* Registers object, an array of these */
struct etnaviv_dump_registers {
	__le32 reg;
	__le32 value;
};

#ifdef __KERNEL__
struct etnaviv_gem_submit;
void etnaviv_core_dump(struct etnaviv_gem_submit *submit);
#endif

#endif
