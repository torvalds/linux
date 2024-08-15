// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * Copyright (C) 2011-2013 NVIDIA Corporation
 */

#include "../dev.h"
#include "../debug.h"
#include "../cdma.h"
#include "../channel.h"

#define HOST1X_DEBUG_MAX_PAGE_OFFSET 102400

enum {
	HOST1X_OPCODE_SETCLASS	= 0x00,
	HOST1X_OPCODE_INCR	= 0x01,
	HOST1X_OPCODE_NONINCR	= 0x02,
	HOST1X_OPCODE_MASK	= 0x03,
	HOST1X_OPCODE_IMM	= 0x04,
	HOST1X_OPCODE_RESTART	= 0x05,
	HOST1X_OPCODE_GATHER	= 0x06,
	HOST1X_OPCODE_SETSTRMID = 0x07,
	HOST1X_OPCODE_SETAPPID  = 0x08,
	HOST1X_OPCODE_SETPYLD   = 0x09,
	HOST1X_OPCODE_INCR_W    = 0x0a,
	HOST1X_OPCODE_NONINCR_W = 0x0b,
	HOST1X_OPCODE_GATHER_W  = 0x0c,
	HOST1X_OPCODE_RESTART_W = 0x0d,
	HOST1X_OPCODE_EXTEND	= 0x0e,
};

enum {
	HOST1X_OPCODE_EXTEND_ACQUIRE_MLOCK	= 0x00,
	HOST1X_OPCODE_EXTEND_RELEASE_MLOCK	= 0x01,
};

#define INVALID_PAYLOAD				0xffffffff

static unsigned int show_channel_command(struct output *o, u32 val,
					 u32 *payload)
{
	unsigned int mask, subop, num, opcode;

	opcode = val >> 28;

	switch (opcode) {
	case HOST1X_OPCODE_SETCLASS:
		mask = val & 0x3f;
		if (mask) {
			host1x_debug_cont(o, "SETCL(class=%03x, offset=%03x, mask=%02x, [",
					    val >> 6 & 0x3ff,
					    val >> 16 & 0xfff, mask);
			return hweight8(mask);
		}

		host1x_debug_cont(o, "SETCL(class=%03x)\n", val >> 6 & 0x3ff);
		return 0;

	case HOST1X_OPCODE_INCR:
		num = val & 0xffff;
		host1x_debug_cont(o, "INCR(offset=%03x, [",
				    val >> 16 & 0xfff);
		if (!num)
			host1x_debug_cont(o, "])\n");

		return num;

	case HOST1X_OPCODE_NONINCR:
		num = val & 0xffff;
		host1x_debug_cont(o, "NONINCR(offset=%03x, [",
				    val >> 16 & 0xfff);
		if (!num)
			host1x_debug_cont(o, "])\n");

		return num;

	case HOST1X_OPCODE_MASK:
		mask = val & 0xffff;
		host1x_debug_cont(o, "MASK(offset=%03x, mask=%03x, [",
				    val >> 16 & 0xfff, mask);
		if (!mask)
			host1x_debug_cont(o, "])\n");

		return hweight16(mask);

	case HOST1X_OPCODE_IMM:
		host1x_debug_cont(o, "IMM(offset=%03x, data=%03x)\n",
				    val >> 16 & 0xfff, val & 0xffff);
		return 0;

	case HOST1X_OPCODE_RESTART:
		host1x_debug_cont(o, "RESTART(offset=%08x)\n", val << 4);
		return 0;

	case HOST1X_OPCODE_GATHER:
		host1x_debug_cont(o, "GATHER(offset=%03x, insert=%d, type=%d, count=%04x, addr=[",
				    val >> 16 & 0xfff, val >> 15 & 0x1,
				    val >> 14 & 0x1, val & 0x3fff);
		return 1;

#if HOST1X_HW >= 6
	case HOST1X_OPCODE_SETSTRMID:
		host1x_debug_cont(o, "SETSTRMID(offset=%06x)\n",
				  val & 0x3fffff);
		return 0;

	case HOST1X_OPCODE_SETAPPID:
		host1x_debug_cont(o, "SETAPPID(appid=%02x)\n", val & 0xff);
		return 0;

	case HOST1X_OPCODE_SETPYLD:
		*payload = val & 0xffff;
		host1x_debug_cont(o, "SETPYLD(data=%04x)\n", *payload);
		return 0;

	case HOST1X_OPCODE_INCR_W:
	case HOST1X_OPCODE_NONINCR_W:
		host1x_debug_cont(o, "%s(offset=%06x, ",
				  opcode == HOST1X_OPCODE_INCR_W ?
					"INCR_W" : "NONINCR_W",
				  val & 0x3fffff);
		if (*payload == 0) {
			host1x_debug_cont(o, "[])\n");
			return 0;
		} else if (*payload == INVALID_PAYLOAD) {
			host1x_debug_cont(o, "unknown)\n");
			return 0;
		} else {
			host1x_debug_cont(o, "[");
			return *payload;
		}

	case HOST1X_OPCODE_GATHER_W:
		host1x_debug_cont(o, "GATHER_W(count=%04x, addr=[",
				  val & 0x3fff);
		return 2;
#endif

	case HOST1X_OPCODE_EXTEND:
		subop = val >> 24 & 0xf;
		if (subop == HOST1X_OPCODE_EXTEND_ACQUIRE_MLOCK)
			host1x_debug_cont(o, "ACQUIRE_MLOCK(index=%d)\n",
					    val & 0xff);
		else if (subop == HOST1X_OPCODE_EXTEND_RELEASE_MLOCK)
			host1x_debug_cont(o, "RELEASE_MLOCK(index=%d)\n",
					    val & 0xff);
		else
			host1x_debug_cont(o, "EXTEND_UNKNOWN(%08x)\n", val);
		return 0;

	default:
		host1x_debug_cont(o, "UNKNOWN\n");
		return 0;
	}
}

static void show_gather(struct output *o, dma_addr_t phys_addr,
			unsigned int words, struct host1x_cdma *cdma,
			dma_addr_t pin_addr, u32 *map_addr)
{
	/* Map dmaget cursor to corresponding mem handle */
	u32 offset = phys_addr - pin_addr;
	unsigned int data_count = 0, i;
	u32 payload = INVALID_PAYLOAD;

	/*
	 * Sometimes we're given different hardware address to the same
	 * page - in these cases the offset will get an invalid number and
	 * we just have to bail out.
	 */
	if (offset > HOST1X_DEBUG_MAX_PAGE_OFFSET) {
		host1x_debug_output(o, "[address mismatch]\n");
		return;
	}

	for (i = 0; i < words; i++) {
		dma_addr_t addr = phys_addr + i * 4;
		u32 val = *(map_addr + offset / 4 + i);

		if (!data_count) {
			host1x_debug_output(o, "    %pad: %08x: ", &addr, val);
			data_count = show_channel_command(o, val, &payload);
		} else {
			host1x_debug_cont(o, "%08x%s", val,
					    data_count > 1 ? ", " : "])\n");
			data_count--;
		}
	}
}

static void show_channel_gathers(struct output *o, struct host1x_cdma *cdma)
{
	struct push_buffer *pb = &cdma->push_buffer;
	struct host1x_job *job;

	list_for_each_entry(job, &cdma->sync_queue, list) {
		unsigned int i;

		host1x_debug_output(o, "JOB, syncpt %u: %u timeout: %u num_slots: %u num_handles: %u\n",
				    job->syncpt->id, job->syncpt_end, job->timeout,
				    job->num_slots, job->num_unpins);

		show_gather(o, pb->dma + job->first_get, job->num_slots * 2, cdma,
			    pb->dma + job->first_get, pb->mapped + job->first_get);

		for (i = 0; i < job->num_cmds; i++) {
			struct host1x_job_gather *g;
			u32 *mapped;

			if (job->cmds[i].is_wait)
				continue;

			g = &job->cmds[i].gather;

			if (job->gather_copy_mapped)
				mapped = (u32 *)job->gather_copy_mapped;
			else
				mapped = host1x_bo_mmap(g->bo);

			if (!mapped) {
				host1x_debug_output(o, "[could not mmap]\n");
				continue;
			}

			host1x_debug_output(o, "  GATHER at %pad+%#x, %d words\n",
					    &g->base, g->offset, g->words);

			show_gather(o, g->base + g->offset, g->words, cdma,
				    g->base, mapped);

			if (!job->gather_copy_mapped)
				host1x_bo_munmap(g->bo, mapped);
		}
	}
}

#if HOST1X_HW >= 6
#include "debug_hw_1x06.c"
#else
#include "debug_hw_1x01.c"
#endif

static const struct host1x_debug_ops host1x_debug_ops = {
	.show_channel_cdma = host1x_debug_show_channel_cdma,
	.show_channel_fifo = host1x_debug_show_channel_fifo,
	.show_mlocks = host1x_debug_show_mlocks,
};
