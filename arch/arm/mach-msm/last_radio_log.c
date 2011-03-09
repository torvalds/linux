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
	loff_t pos = *offset;
	ssize_t count;

	if (pos >= radio_log_size)
		return 0;

	count = min(len, (size_t)(radio_log_size - pos));
	if (copy_to_user(buf, radio_log_base + pos, count)) {
		pr_err("%s: copy to user failed\n", __func__);
		return -EFAULT;
	}

	*offset += count;
	return count;
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

	entry = create_proc_entry("last_radio_log", S_IFREG | S_IRUGO, NULL);
	if (!entry) {
		pr_err("%s: could not create proc entry for radio log\n",
				__func__);
		return;
	}

	pr_err("%s: last radio log is %d bytes long\n", __func__,
		radio_log_size);
	last_radio_log_fops.owner = owner;
	entry->proc_fops = &last_radio_log_fops;
	entry->size = radio_log_size;
}
EXPORT_SYMBOL(msm_init_last_radio_log);
