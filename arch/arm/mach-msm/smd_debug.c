/* arch/arm/mach-msm/smd_debug.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
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
#include <linux/list.h>

#include <mach/msm_iomap.h>

#include "smd_private.h"

#if defined(CONFIG_DEBUG_FS)

static char *chstate(unsigned n)
{
	switch (n) {
	case SMD_SS_CLOSED:
		return "CLOSED";
	case SMD_SS_OPENING:
		return "OPENING";
	case SMD_SS_OPENED:
		return "OPENED";
	case SMD_SS_FLUSHING:
		return "FLUSHING";
	case SMD_SS_CLOSING:
		return "CLOSING";
	case SMD_SS_RESET:
		return "RESET";
	case SMD_SS_RESET_OPENING:
		return "ROPENING";
	default:
		return "UNKNOWN";
	}
}


static int dump_ch(char *buf, int max, struct smd_channel *ch)
{
	volatile struct smd_half_channel *s = ch->send;
	volatile struct smd_half_channel *r = ch->recv;

	return scnprintf(
		buf, max,
		"ch%02d:"
		" %8s(%05d/%05d) %c%c%c%c%c%c%c <->"
		" %8s(%05d/%05d) %c%c%c%c%c%c%c '%s'\n", ch->n,
		chstate(s->state), s->tail, s->head,
		s->fDSR ? 'D' : 'd',
		s->fCTS ? 'C' : 'c',
		s->fCD ? 'C' : 'c',
		s->fRI ? 'I' : 'i',
		s->fHEAD ? 'W' : 'w',
		s->fTAIL ? 'R' : 'r',
		s->fSTATE ? 'S' : 's',
		chstate(r->state), r->tail, r->head,
		r->fDSR ? 'D' : 'd',
		r->fCTS ? 'R' : 'r',
		r->fCD ? 'C' : 'c',
		r->fRI ? 'I' : 'i',
		r->fHEAD ? 'W' : 'w',
		r->fTAIL ? 'R' : 'r',
		r->fSTATE ? 'S' : 's',
		ch->name
		);
}

static int debug_read_stat(char *buf, int max)
{
	char *msg;
	int i = 0;

	msg = smem_find(ID_DIAG_ERR_MSG, SZ_DIAG_ERR_MSG);

	if (raw_smsm_get_state(SMSM_STATE_MODEM) & SMSM_RESET)
		i += scnprintf(buf + i, max - i,
			       "smsm: ARM9 HAS CRASHED\n");

	i += scnprintf(buf + i, max - i, "smsm: a9: %08x a11: %08x\n",
		       raw_smsm_get_state(SMSM_STATE_MODEM),
		       raw_smsm_get_state(SMSM_STATE_APPS));
#ifdef CONFIG_ARCH_MSM_SCORPION
	i += scnprintf(buf + i, max - i, "smsm dem: apps: %08x modem: %08x "
		       "qdsp6: %08x power: %08x time: %08x\n",
		       raw_smsm_get_state(SMSM_STATE_APPS_DEM),
		       raw_smsm_get_state(SMSM_STATE_MODEM_DEM),
		       raw_smsm_get_state(SMSM_STATE_QDSP6_DEM),
		       raw_smsm_get_state(SMSM_STATE_POWER_MASTER_DEM),
		       raw_smsm_get_state(SMSM_STATE_TIME_MASTER_DEM));
#endif
	if (msg) {
		msg[SZ_DIAG_ERR_MSG - 1] = 0;
		i += scnprintf(buf + i, max - i, "diag: '%s'\n", msg);
	}
	return i;
}

static int debug_read_mem(char *buf, int max)
{
	unsigned n;
	struct smem_shared *shared = (void *) MSM_SHARED_RAM_BASE;
	struct smem_heap_entry *toc = shared->heap_toc;
	int i = 0;

	i += scnprintf(buf + i, max - i,
		       "heap: init=%d free=%d remain=%d\n",
		       shared->heap_info.initialized,
		       shared->heap_info.free_offset,
		       shared->heap_info.heap_remaining);

	for (n = 0; n < SMEM_NUM_ITEMS; n++) {
		if (toc[n].allocated == 0)
			continue;
		i += scnprintf(buf + i, max - i,
			       "%04d: offset %08x size %08x\n",
			       n, toc[n].offset, toc[n].size);
	}
	return i;
}

static int debug_read_ch(char *buf, int max)
{
	struct smd_channel *ch;
	unsigned long flags;
	int i = 0;

	spin_lock_irqsave(&smd_lock, flags);
	list_for_each_entry(ch, &smd_ch_list_dsp, ch_list)
		i += dump_ch(buf + i, max - i, ch);
	list_for_each_entry(ch, &smd_ch_list_modem, ch_list)
		i += dump_ch(buf + i, max - i, ch);
	list_for_each_entry(ch, &smd_ch_closed_list, ch_list)
		i += dump_ch(buf + i, max - i, ch);
	spin_unlock_irqrestore(&smd_lock, flags);

	return i;
}

static int debug_read_version(char *buf, int max)
{
	struct smem_shared *shared = (void *) MSM_SHARED_RAM_BASE;
	unsigned version = shared->version[VERSION_MODEM];
	return sprintf(buf, "%d.%d\n", version >> 16, version & 0xffff);
}

static int debug_read_build_id(char *buf, int max)
{
	unsigned size;
	void *data;

	data = smem_item(SMEM_HW_SW_BUILD_ID, &size);
	if (!data)
		return 0;

	if (size >= max)
		size = max;
	memcpy(buf, data, size);

	return size;
}

static int debug_read_alloc_tbl(char *buf, int max)
{
	struct smd_alloc_elm *shared;
	int n, i = 0;

	shared = smem_find(ID_CH_ALLOC_TBL, sizeof(*shared) * 64);

	for (n = 0; n < 64; n++) {
		if (shared[n].ref_count == 0)
			continue;
		i += scnprintf(buf + i, max - i,
			       "%03d: %-20s cid=%02d type=%03d "
			       "kind=%02d ref_count=%d\n",
			       n, shared[n].name, shared[n].cid,
			       shared[n].ctype & 0xff,
			       (shared[n].ctype >> 8) & 0xf,
			       shared[n].ref_count);
	}

	return i;
}

#define DEBUG_BUFMAX 4096
static char debug_buffer[DEBUG_BUFMAX];

static ssize_t debug_read(struct file *file, char __user *buf,
			  size_t count, loff_t *ppos)
{
	int (*fill)(char *buf, int max) = file->private_data;
	int bsize = fill(debug_buffer, DEBUG_BUFMAX);
	return simple_read_from_buffer(buf, count, ppos, debug_buffer, bsize);
}

static const struct file_operations debug_ops = {
	.read = debug_read,
	.open = simple_open,
	.llseek = default_llseek,
};

static void debug_create(const char *name, umode_t mode,
			 struct dentry *dent,
			 int (*fill)(char *buf, int max))
{
	debugfs_create_file(name, mode, dent, fill, &debug_ops);
}

static int smd_debugfs_init(void)
{
	struct dentry *dent;

	dent = debugfs_create_dir("smd", 0);
	if (IS_ERR(dent))
		return 1;

	debug_create("ch", 0444, dent, debug_read_ch);
	debug_create("stat", 0444, dent, debug_read_stat);
	debug_create("mem", 0444, dent, debug_read_mem);
	debug_create("version", 0444, dent, debug_read_version);
	debug_create("tbl", 0444, dent, debug_read_alloc_tbl);
	debug_create("build", 0444, dent, debug_read_build_id);

	return 0;
}

late_initcall(smd_debugfs_init);
#endif


#define MAX_NUM_SLEEP_CLIENTS		64
#define MAX_SLEEP_NAME_LEN		8

#define NUM_GPIO_INT_REGISTERS		6
#define GPIO_SMEM_NUM_GROUPS		2
#define GPIO_SMEM_MAX_PC_INTERRUPTS	8

struct tramp_gpio_save {
	unsigned int enable;
	unsigned int detect;
	unsigned int polarity;
};

struct tramp_gpio_smem {
	uint16_t num_fired[GPIO_SMEM_NUM_GROUPS];
	uint16_t fired[GPIO_SMEM_NUM_GROUPS][GPIO_SMEM_MAX_PC_INTERRUPTS];
	uint32_t enabled[NUM_GPIO_INT_REGISTERS];
	uint32_t detection[NUM_GPIO_INT_REGISTERS];
	uint32_t polarity[NUM_GPIO_INT_REGISTERS];
};


void smsm_print_sleep_info(void)
{
	unsigned long flags;
	uint32_t *ptr;
#ifndef CONFIG_ARCH_MSM_SCORPION
	struct tramp_gpio_smem *gpio;
	struct smsm_interrupt_info *int_info;
#endif


	spin_lock_irqsave(&smem_lock, flags);

	ptr = smem_alloc(SMEM_SMSM_SLEEP_DELAY, sizeof(*ptr));
	if (ptr)
		pr_info("SMEM_SMSM_SLEEP_DELAY: %x\n", *ptr);

	ptr = smem_alloc(SMEM_SMSM_LIMIT_SLEEP, sizeof(*ptr));
	if (ptr)
		pr_info("SMEM_SMSM_LIMIT_SLEEP: %x\n", *ptr);

	ptr = smem_alloc(SMEM_SLEEP_POWER_COLLAPSE_DISABLED, sizeof(*ptr));
	if (ptr)
		pr_info("SMEM_SLEEP_POWER_COLLAPSE_DISABLED: %x\n", *ptr);

#ifndef CONFIG_ARCH_MSM_SCORPION
	int_info = smem_alloc(SMEM_SMSM_INT_INFO, sizeof(*int_info));
	if (int_info)
		pr_info("SMEM_SMSM_INT_INFO %x %x %x\n",
			int_info->interrupt_mask,
			int_info->pending_interrupts,
			int_info->wakeup_reason);

	gpio = smem_alloc(SMEM_GPIO_INT, sizeof(*gpio));
	if (gpio) {
		int i;
		for (i = 0; i < NUM_GPIO_INT_REGISTERS; i++)
			pr_info("SMEM_GPIO_INT: %d: e %x d %x p %x\n",
				i, gpio->enabled[i], gpio->detection[i],
				gpio->polarity[i]);

		for (i = 0; i < GPIO_SMEM_NUM_GROUPS; i++)
			pr_info("SMEM_GPIO_INT: %d: f %d: %d %d...\n",
				i, gpio->num_fired[i], gpio->fired[i][0],
				gpio->fired[i][1]);
	}
#else
#endif
	spin_unlock_irqrestore(&smem_lock, flags);
}

