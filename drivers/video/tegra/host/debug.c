/*
 * drivers/video/tegra/dc/dc.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
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

#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/io.h>

#include "dev.h"

#ifdef CONFIG_DEBUG_FS

enum {
	NVHOST_DBG_STATE_CMD = 0,
	NVHOST_DBG_STATE_DATA = 1,
};

static int nvhost_debug_handle_cmd(struct seq_file *s, u32 val, int *count)
{
	unsigned mask;
	unsigned subop;

	switch (val >> 28) {
	case 0x0:
		mask = val & 0x3f;
		if (mask) {
			seq_printf(s, "SETCL(class=%03x, offset=%03x, mask=%02x, [",
				   val >> 6 & 0x3ff, val >> 16 & 0xfff, mask);
			*count = hweight8(mask);
			return NVHOST_DBG_STATE_DATA;
		} else {
			seq_printf(s, "SETCL(class=%03x)\n", val >> 6 & 0x3ff);
			return NVHOST_DBG_STATE_CMD;
		}

	case 0x1:
		seq_printf(s, "INCR(offset=%03x, [", val >> 16 & 0x3ff);
		*count = val & 0xffff;
		return NVHOST_DBG_STATE_DATA;

	case 0x2:
		seq_printf(s, "NOMINCR(offset=%03x, [", val >> 16 & 0x3ff);
		*count = val & 0xffff;
		return NVHOST_DBG_STATE_DATA;

	case 0x3:
		mask = val & 0xffff;
		seq_printf(s, "MASK(offset=%03x, mask=%03x, [",
			   val >> 16 & 0x3ff, mask);
		*count = hweight16(mask);
		return NVHOST_DBG_STATE_DATA;

	case 0x4:
		seq_printf(s, "IMM(offset=%03x, data=%03x)\n",
			   val >> 16 & 0x3ff, val & 0xffff);
		return NVHOST_DBG_STATE_CMD;

	case 0x5:
		seq_printf(s, "RESTART(offset=%08x)\n", val << 4);
		return NVHOST_DBG_STATE_CMD;

	case 0x6:
		seq_printf(s, "GATHER(offset=%03x, insert=%d, type=%d, count=%04x, addr=[",
			   val >> 16 & 0x3ff, val >> 15 & 0x1, val >> 15 & 0x1,
			   val & 0x3fff);
		*count = 1;
		return NVHOST_DBG_STATE_DATA;

	case 0xe:
		subop = val >> 24 & 0xf;
		if (subop == 0)
			seq_printf(s, "ACQUIRE_MLOCK(index=%d)\n", val & 0xff);
		else if (subop == 1)
			seq_printf(s, "RELEASE_MLOCK(index=%d)\n", val & 0xff);
		else
			seq_printf(s, "EXTEND_UNKNOWN(%08x)\n", val);

		return NVHOST_DBG_STATE_CMD;

	case 0xf:
		seq_printf(s, "DONE()\n");
		return NVHOST_DBG_STATE_CMD;

	default:
		return NVHOST_DBG_STATE_CMD;
	}
}

static int nvhost_debug_show(struct seq_file *s, void *unused)
{
	struct nvhost_master *m = s->private;
	int i;

	nvhost_module_busy(&m->mod);

	for (i = 0; i < NVHOST_NUMCHANNELS; i++) {
		void __iomem *regs = m->channels[i].aperture;
		u32 dmaput, dmaget, dmactrl;
		u32 cbstat, cbread;
		u32 fifostat;
		u32 val, base;
		unsigned start, end;
		unsigned wr_ptr, rd_ptr;
		int state;
		int count = 0;

		dmaput = readl(regs + HOST1X_CHANNEL_DMAPUT);
		dmaget = readl(regs + HOST1X_CHANNEL_DMAGET);
		dmactrl = readl(regs + HOST1X_CHANNEL_DMACTRL);
		cbread = readl(m->aperture + HOST1X_SYNC_CBREAD(i));
		cbstat = readl(m->aperture + HOST1X_SYNC_CBSTAT(i));

		if (dmactrl != 0x0 || !m->channels[i].cdma.push_buffer.mapped) {
			seq_printf(s, "%d: inactive\n\n", i);
			continue;
		}

		switch (cbstat) {
		case 0x00010008:
			seq_printf(s, "%d: waiting on syncpt %d val %d\n",
				   i, cbread >> 24, cbread & 0xffffff);
			break;

		case 0x00010009:
			base = cbread >> 15 & 0xf;

			val = readl(m->aperture + HOST1X_SYNC_SYNCPT_BASE(base)) & 0xffff;
			val += cbread & 0xffff;

			seq_printf(s, "%d: waiting on syncpt %d val %d\n",
				   i, cbread >> 24, val);
			break;

		default:
			seq_printf(s, "%d: active class %02x, offset %04x, val %08x\n",
				   i, cbstat >> 16, cbstat & 0xffff, cbread);
			break;
		}

		fifostat = readl(regs + HOST1X_CHANNEL_FIFOSTAT);
		if ((fifostat & 1 << 10) == 0 ) {

			writel(0x0, m->aperture + HOST1X_SYNC_CFPEEK_CTRL);
			writel(1 << 31 | i << 16, m->aperture + HOST1X_SYNC_CFPEEK_CTRL);
			rd_ptr = readl(m->aperture + HOST1X_SYNC_CFPEEK_PTRS) & 0x1ff;
			wr_ptr = readl(m->aperture + HOST1X_SYNC_CFPEEK_PTRS) >> 16 & 0x1ff;

			start = readl(m->aperture + HOST1X_SYNC_CF_SETUP(i)) & 0x1ff;
			end = (readl(m->aperture + HOST1X_SYNC_CF_SETUP(i)) >> 16) & 0x1ff;

			state = NVHOST_DBG_STATE_CMD;

			do {
				writel(0x0, m->aperture + HOST1X_SYNC_CFPEEK_CTRL);
				writel(1 << 31 | i << 16 | rd_ptr, m->aperture + HOST1X_SYNC_CFPEEK_CTRL);
				val = readl(m->aperture + HOST1X_SYNC_CFPEEK_READ);

				switch (state) {
				case NVHOST_DBG_STATE_CMD:
					seq_printf(s, "%d: %08x:", i, val);

					state = nvhost_debug_handle_cmd(s, val, &count);
					if (state == NVHOST_DBG_STATE_DATA && count == 0) {
						state = NVHOST_DBG_STATE_CMD;
						seq_printf(s, "])\n");
					}
					break;

				case NVHOST_DBG_STATE_DATA:
					count--;
					seq_printf(s, "%08x%s", val, count > 0 ? ", " : "])\n");
					if (count == 0)
						state = NVHOST_DBG_STATE_CMD;
					break;
				}

				if (rd_ptr == end)
					rd_ptr = start;
				else
					rd_ptr++;


			} while (rd_ptr != wr_ptr);

			if (state == NVHOST_DBG_STATE_DATA)
				seq_printf(s, ", ...])\n");
		}
		seq_printf(s, "\n");
	}

	nvhost_module_idle(&m->mod);
	return 0;
}


static int nvhost_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvhost_debug_show, inode->i_private);
}

static const struct file_operations nvhost_debug_fops = {
	.open		= nvhost_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

void nvhost_debug_init(struct nvhost_master *master)
{
	debugfs_create_file("tegra_host", S_IRUGO, NULL, master, &nvhost_debug_fops);
}
#else
void nvhost_debug_add(struct nvhost_master *master)
{
}

#endif

