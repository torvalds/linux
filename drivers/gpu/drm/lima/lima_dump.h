/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2020 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_DUMP_H__
#define __LIMA_DUMP_H__

#include <linux/types.h>

/**
 * dump file format for all the information to start a lima task
 *
 * top level format
 * | magic code "LIMA" | format version | num tasks | data size |
 * | reserved | reserved | reserved | reserved |
 * | task 1 ID | task 1 size | num chunks | reserved | task 1 data |
 * | task 2 ID | task 2 size | num chunks | reserved | task 2 data |
 * ...
 *
 * task data format
 * | chunk 1 ID | chunk 1 size | reserved | reserved | chunk 1 data |
 * | chunk 2 ID | chunk 2 size | reserved | reserved | chunk 2 data |
 * ...
 *
 */

#define LIMA_DUMP_MAJOR 1
#define LIMA_DUMP_MINOR 0

#define LIMA_DUMP_MAGIC 0x414d494c

struct lima_dump_head {
	__u32 magic;
	__u16 version_major;
	__u16 version_minor;
	__u32 num_tasks;
	__u32 size;
	__u32 reserved[4];
};

#define LIMA_DUMP_TASK_GP   0
#define LIMA_DUMP_TASK_PP   1
#define LIMA_DUMP_TASK_NUM  2

struct lima_dump_task {
	__u32 id;
	__u32 size;
	__u32 num_chunks;
	__u32 reserved;
};

#define LIMA_DUMP_CHUNK_FRAME         0
#define LIMA_DUMP_CHUNK_BUFFER        1
#define LIMA_DUMP_CHUNK_PROCESS_NAME  2
#define LIMA_DUMP_CHUNK_PROCESS_ID    3
#define LIMA_DUMP_CHUNK_NUM           4

struct lima_dump_chunk {
	__u32 id;
	__u32 size;
	__u32 reserved[2];
};

struct lima_dump_chunk_buffer {
	__u32 id;
	__u32 size;
	__u32 va;
	__u32 reserved;
};

struct lima_dump_chunk_pid {
	__u32 id;
	__u32 size;
	__u32 pid;
	__u32 reserved;
};

#endif
