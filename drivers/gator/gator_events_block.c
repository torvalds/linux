/**
 * Copyright (C) ARM Limited 2010-2015. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"
#include <trace/events/block.h>

#define BLOCK_RQ_WR		0
#define BLOCK_RQ_RD		1

#define BLOCK_TOTAL		(BLOCK_RQ_RD+1)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
#define EVENTWRITE REQ_RW
#else
#define EVENTWRITE REQ_WRITE
#endif

static ulong block_rq_wr_enabled;
static ulong block_rq_rd_enabled;
static ulong block_rq_wr_key;
static ulong block_rq_rd_key;
static atomic_t blockCnt[BLOCK_TOTAL];
static int blockGet[BLOCK_TOTAL * 4];

/* Tracepoint changed in 3.15 backported to older kernels. The Makefile tries to autodetect the correct value, but if it fails change the #if below */
#if OLD_BLOCK_RQ_COMPLETE
GATOR_DEFINE_PROBE(block_rq_complete, TP_PROTO(struct request_queue *q, struct request *rq))
#else
GATOR_DEFINE_PROBE(block_rq_complete, TP_PROTO(struct request_queue *q, struct request *rq, unsigned int nr_bytes))
#endif
{
	int write;
	unsigned int size;

	if (!rq)
		return;

	write = rq->cmd_flags & EVENTWRITE;
#if OLD_BLOCK_RQ_COMPLETE
	size = rq->resid_len;
#else
	size = nr_bytes;
#endif

	if (!size)
		return;

	if (write) {
		if (block_rq_wr_enabled)
			atomic_add(size, &blockCnt[BLOCK_RQ_WR]);
	} else {
		if (block_rq_rd_enabled)
			atomic_add(size, &blockCnt[BLOCK_RQ_RD]);
	}
}

static int gator_events_block_create_files(struct super_block *sb, struct dentry *root)
{
	struct dentry *dir;

	/* block_complete_wr */
	dir = gatorfs_mkdir(sb, root, "Linux_block_rq_wr");
	if (!dir)
		return -1;
	gatorfs_create_ulong(sb, dir, "enabled", &block_rq_wr_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &block_rq_wr_key);

	/* block_complete_rd */
	dir = gatorfs_mkdir(sb, root, "Linux_block_rq_rd");
	if (!dir)
		return -1;
	gatorfs_create_ulong(sb, dir, "enabled", &block_rq_rd_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &block_rq_rd_key);

	return 0;
}

static int gator_events_block_start(void)
{
	/* register tracepoints */
	if (block_rq_wr_enabled || block_rq_rd_enabled)
		if (GATOR_REGISTER_TRACE(block_rq_complete))
			goto fail_block_rq_exit;
	pr_debug("gator: registered block event tracepoints\n");

	return 0;

	/* unregister tracepoints on error */
fail_block_rq_exit:
	pr_err("gator: block event tracepoints failed to activate, please verify that tracepoints are enabled in the linux kernel\n");

	return -1;
}

static void gator_events_block_stop(void)
{
	if (block_rq_wr_enabled || block_rq_rd_enabled)
		GATOR_UNREGISTER_TRACE(block_rq_complete);
	pr_debug("gator: unregistered block event tracepoints\n");

	block_rq_wr_enabled = 0;
	block_rq_rd_enabled = 0;
}

static int gator_events_block_read(int **buffer, bool sched_switch)
{
	int len, value, data = 0;

	if (!on_primary_core())
		return 0;

	len = 0;
	if (block_rq_wr_enabled && (value = atomic_read(&blockCnt[BLOCK_RQ_WR])) > 0) {
		atomic_sub(value, &blockCnt[BLOCK_RQ_WR]);
		blockGet[len++] = block_rq_wr_key;
		/* Indicates to Streamline that value bytes were written now, not since the last message */
		blockGet[len++] = 0;
		blockGet[len++] = block_rq_wr_key;
		blockGet[len++] = value;
		data += value;
	}
	if (block_rq_rd_enabled && (value = atomic_read(&blockCnt[BLOCK_RQ_RD])) > 0) {
		atomic_sub(value, &blockCnt[BLOCK_RQ_RD]);
		blockGet[len++] = block_rq_rd_key;
		/* Indicates to Streamline that value bytes were read now, not since the last message */
		blockGet[len++] = 0;
		blockGet[len++] = block_rq_rd_key;
		blockGet[len++] = value;
		data += value;
	}

	if (buffer)
		*buffer = blockGet;

	return len;
}

static struct gator_interface gator_events_block_interface = {
	.create_files = gator_events_block_create_files,
	.start = gator_events_block_start,
	.stop = gator_events_block_stop,
	.read = gator_events_block_read,
};

int gator_events_block_init(void)
{
	block_rq_wr_enabled = 0;
	block_rq_rd_enabled = 0;

	block_rq_wr_key = gator_events_get_key();
	block_rq_rd_key = gator_events_get_key();

	return gator_events_install(&gator_events_block_interface);
}
