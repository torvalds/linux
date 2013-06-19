/* arch/arm/mach-msm/last_radio_log.c
 *
 * Extract the log from a modem crash though SMEM
 *
 * Copyright (C) 2007 Google, Inc.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>

#include "smd_private.h"

static void *radio_log_base;
static size_t radio_log_size;

extern void *smem_item(unsigned id, unsigned *size);

static ssize_t last_radio_log_read(struct file *file, char __user *buf,
			size_t len, loff_t *offset)
{
	return simple_read_from_buffer(buf, len, offset,
				radio_log_base, radio_log_size);
}

static struct file_operations last_radio_log_fops = {
	.read = last_radio_log_read,
	.llseek = default_llseek,
};

void msm_init_last_radio_log(struct module *owner)
{
	struct proc_dir_entry *entry;

	if (last_radio_log_fops.owner) {
		pr_err("%s: already claimed\n", __func__);
		return;
	}

	radio_log_base = smem_item(SMEM_CLKREGIM_BSP, &radio_log_size);
	if (!radio_log_base) {
		pr_err("%s: could not retrieve SMEM_CLKREGIM_BSP\n", __func__);
		return;
	}

	entry = proc_create("last_radio_log", S_IRUGO, NULL,
				&last_radio_log_fops);
	if (!entry) {
		pr_err("%s: could not create proc entry for radio log\n",
				__func__);
		return;
	}

	pr_err("%s: last radio log is %d bytes long\n", __func__,
		radio_log_size);
	last_radio_log_fops.owner = owner;
	proc_set_size(entry, radio_log_size);
}
EXPORT_SYMBOL(msm_init_last_radio_log);
