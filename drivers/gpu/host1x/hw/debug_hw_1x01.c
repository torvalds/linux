/*
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
 *
 * Copyright (C) 2011-2013 NVIDIA Corporation
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "../dev.h"
#include "../debug.h"
#include "../cdma.h"
#include "../channel.h"

static void host1x_debug_show_channel_cdma(struct host1x *host,
					   struct host1x_channel *ch,
					   struct output *o)
{
	struct host1x_cdma *cdma = &ch->cdma;
	u32 dmaput, dmaget, dmactrl;
	u32 cbstat, cbread;
	u32 val, base, baseval;

	dmaput = host1x_ch_readl(ch, HOST1X_CHANNEL_DMAPUT);
	dmaget = host1x_ch_readl(ch, HOST1X_CHANNEL_DMAGET);
	dmactrl = host1x_ch_readl(ch, HOST1X_CHANNEL_DMACTRL);
	cbread = host1x_sync_readl(host, HOST1X_SYNC_CBREAD(ch->id));
	cbstat = host1x_sync_readl(host, HOST1X_SYNC_CBSTAT(ch->id));

	host1x_debug_output(o, "%u-%s: ", ch->id, dev_name(ch->dev));

	if (HOST1X_CHANNEL_DMACTRL_DMASTOP_V(dmactrl) ||
	    !ch->cdma.push_buffer.mapped) {
		host1x_debug_output(o, "inactive\n\n");
		return;
	}

	if (HOST1X_SYNC_CBSTAT_CBCLASS_V(cbstat) == HOST1X_CLASS_HOST1X &&
	    HOST1X_SYNC_CBSTAT_CBOFFSET_V(cbstat) ==
			HOST1X_UCLASS_WAIT_SYNCPT)
		host1x_debug_output(o, "waiting on syncpt %d val %d\n",
				    cbread >> 24, cbread & 0xffffff);
	else if (HOST1X_SYNC_CBSTAT_CBCLASS_V(cbstat) ==
				HOST1X_CLASS_HOST1X &&
		 HOST1X_SYNC_CBSTAT_CBOFFSET_V(cbstat) ==
				HOST1X_UCLASS_WAIT_SYNCPT_BASE) {
		base = (cbread >> 16) & 0xff;
		baseval =
			host1x_sync_readl(host, HOST1X_SYNC_SYNCPT_BASE(base));
		val = cbread & 0xffff;
		host1x_debug_output(o, "waiting on syncpt %d val %d (base %d = %d; offset = %d)\n",
				    cbread >> 24, baseval + val, base,
				    baseval, val);
	} else
		host1x_debug_output(o, "active class %02x, offset %04x, val %08x\n",
				    HOST1X_SYNC_CBSTAT_CBCLASS_V(cbstat),
				    HOST1X_SYNC_CBSTAT_CBOFFSET_V(cbstat),
				    cbread);

	host1x_debug_output(o, "DMAPUT %08x, DMAGET %08x, DMACTL %08x\n",
			    dmaput, dmaget, dmactrl);
	host1x_debug_output(o, "CBREAD %08x, CBSTAT %08x\n", cbread, cbstat);

	show_channel_gathers(o, cdma);
	host1x_debug_output(o, "\n");
}

static void host1x_debug_show_channel_fifo(struct host1x *host,
					   struct host1x_channel *ch,
					   struct output *o)
{
	u32 val, rd_ptr, wr_ptr, start, end;
	unsigned int data_count = 0;

	host1x_debug_output(o, "%u: fifo:\n", ch->id);

	val = host1x_ch_readl(ch, HOST1X_CHANNEL_FIFOSTAT);
	host1x_debug_output(o, "FIFOSTAT %08x\n", val);
	if (HOST1X_CHANNEL_FIFOSTAT_CFEMPTY_V(val)) {
		host1x_debug_output(o, "[empty]\n");
		return;
	}

	host1x_sync_writel(host, 0x0, HOST1X_SYNC_CFPEEK_CTRL);
	host1x_sync_writel(host, HOST1X_SYNC_CFPEEK_CTRL_ENA_F(1) |
			   HOST1X_SYNC_CFPEEK_CTRL_CHANNR_F(ch->id),
			   HOST1X_SYNC_CFPEEK_CTRL);

	val = host1x_sync_readl(host, HOST1X_SYNC_CFPEEK_PTRS);
	rd_ptr = HOST1X_SYNC_CFPEEK_PTRS_CF_RD_PTR_V(val);
	wr_ptr = HOST1X_SYNC_CFPEEK_PTRS_CF_WR_PTR_V(val);

	val = host1x_sync_readl(host, HOST1X_SYNC_CF_SETUP(ch->id));
	start = HOST1X_SYNC_CF_SETUP_BASE_V(val);
	end = HOST1X_SYNC_CF_SETUP_LIMIT_V(val);

	do {
		host1x_sync_writel(host, 0x0, HOST1X_SYNC_CFPEEK_CTRL);
		host1x_sync_writel(host, HOST1X_SYNC_CFPEEK_CTRL_ENA_F(1) |
				   HOST1X_SYNC_CFPEEK_CTRL_CHANNR_F(ch->id) |
				   HOST1X_SYNC_CFPEEK_CTRL_ADDR_F(rd_ptr),
				   HOST1X_SYNC_CFPEEK_CTRL);
		val = host1x_sync_readl(host, HOST1X_SYNC_CFPEEK_READ);

		if (!data_count) {
			host1x_debug_output(o, "%08x: ", val);
			data_count = show_channel_command(o, val, NULL);
		} else {
			host1x_debug_cont(o, "%08x%s", val,
					  data_count > 1 ? ", " : "])\n");
			data_count--;
		}

		if (rd_ptr == end)
			rd_ptr = start;
		else
			rd_ptr++;
	} while (rd_ptr != wr_ptr);

	if (data_count)
		host1x_debug_cont(o, ", ...])\n");
	host1x_debug_output(o, "\n");

	host1x_sync_writel(host, 0x0, HOST1X_SYNC_CFPEEK_CTRL);
}

static void host1x_debug_show_mlocks(struct host1x *host, struct output *o)
{
	unsigned int i;

	host1x_debug_output(o, "---- mlocks ----\n");

	for (i = 0; i < host1x_syncpt_nb_mlocks(host); i++) {
		u32 owner =
			host1x_sync_readl(host, HOST1X_SYNC_MLOCK_OWNER(i));
		if (HOST1X_SYNC_MLOCK_OWNER_CH_OWNS_V(owner))
			host1x_debug_output(o, "%u: locked by channel %u\n",
				i, HOST1X_SYNC_MLOCK_OWNER_CHID_V(owner));
		else if (HOST1X_SYNC_MLOCK_OWNER_CPU_OWNS_V(owner))
			host1x_debug_output(o, "%u: locked by cpu\n", i);
		else
			host1x_debug_output(o, "%u: unlocked\n", i);
	}

	host1x_debug_output(o, "\n");
}
