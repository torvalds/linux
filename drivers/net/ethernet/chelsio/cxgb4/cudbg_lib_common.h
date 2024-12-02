/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2017 Chelsio Communications.  All rights reserved.
 */

#ifndef __CUDBG_LIB_COMMON_H__
#define __CUDBG_LIB_COMMON_H__

#define CUDBG_SIGNATURE 67856866 /* CUDB in ascii */

enum cudbg_dump_type {
	CUDBG_DUMP_TYPE_MINI = 1,
};

enum cudbg_compression_type {
	CUDBG_COMPRESSION_NONE = 1,
	CUDBG_COMPRESSION_ZLIB,
};

struct cudbg_hdr {
	u32 signature;
	u32 hdr_len;
	u16 major_ver;
	u16 minor_ver;
	u32 data_len;
	u32 hdr_flags;
	u16 max_entities;
	u8 chip_ver;
	u8 dump_type:3;
	u8 reserved1:1;
	u8 compress_type:4;
	u32 reserved[8];
};

struct cudbg_entity_hdr {
	u32 entity_type;
	u32 start_offset;
	u32 size;
	int hdr_flags;
	u32 sys_warn;
	u32 sys_err;
	u8 num_pad;
	u8 flag;             /* bit 0 is used to indicate ext data */
	u8 reserved1[2];
	u32 next_ext_offset; /* pointer to next extended entity meta data */
	u32 reserved[5];
};

struct cudbg_ver_hdr {
	u32 signature;
	u16 revision;
	u16 size;
};

struct cudbg_buffer {
	u32 size;
	u32 offset;
	char *data;
};

struct cudbg_error {
	int sys_err;
	int sys_warn;
	int app_err;
};

#define CDUMP_MAX_COMP_BUF_SIZE ((64 * 1024) - 1)
#define CUDBG_CHUNK_SIZE ((CDUMP_MAX_COMP_BUF_SIZE / 1024) * 1024)

int cudbg_get_buff(struct cudbg_init *pdbg_init,
		   struct cudbg_buffer *pdbg_buff, u32 size,
		   struct cudbg_buffer *pin_buff);
void cudbg_put_buff(struct cudbg_init *pdbg_init,
		    struct cudbg_buffer *pin_buff);
void cudbg_update_buff(struct cudbg_buffer *pin_buff,
		       struct cudbg_buffer *pout_buff);
#endif /* __CUDBG_LIB_COMMON_H__ */
