/*
 *  Linux MegaRAID driver for SAS based RAID controllers
 *
 *  Copyright (c) 2003-2018  LSI Corporation.
 *  Copyright (c) 2003-2018  Avago Technologies.
 *  Copyright (c) 2003-2018  Broadcom Inc.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *  Authors: Broadcom Inc.
 *           Kashyap Desai <kashyap.desai@broadcom.com>
 *           Sumit Saxena <sumit.saxena@broadcom.com>
 *           Shivasharan S <shivasharan.srikanteshwara@broadcom.com>
 *
 *  Send feedback to: megaraidlinux.pdl@broadcom.com
 */
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/compat.h>
#include <linux/irq_poll.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>

#include "megaraid_sas_fusion.h"
#include "megaraid_sas.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

struct dentry *megasas_debugfs_root;

static ssize_t
megasas_debugfs_read(struct file *filp, char __user *ubuf, size_t cnt,
		      loff_t *ppos)
{
	struct megasas_debugfs_buffer *debug = filp->private_data;

	if (!debug || !debug->buf)
		return 0;

	return simple_read_from_buffer(ubuf, cnt, ppos, debug->buf, debug->len);
}

static int
megasas_debugfs_raidmap_open(struct inode *inode, struct file *file)
{
	struct megasas_instance *instance = inode->i_private;
	struct megasas_debugfs_buffer *debug;
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	debug = kzalloc(sizeof(struct megasas_debugfs_buffer), GFP_KERNEL);
	if (!debug)
		return -ENOMEM;

	debug->buf = (void *)fusion->ld_drv_map[(instance->map_id & 1)];
	debug->len = fusion->drv_map_sz;
	file->private_data = debug;

	return 0;
}

static int
megasas_debugfs_release(struct inode *inode, struct file *file)
{
	struct megasas_debug_buffer *debug = file->private_data;

	if (!debug)
		return 0;

	file->private_data = NULL;
	kfree(debug);
	return 0;
}

static const struct file_operations megasas_debugfs_raidmap_fops = {
	.owner		= THIS_MODULE,
	.open           = megasas_debugfs_raidmap_open,
	.read           = megasas_debugfs_read,
	.release        = megasas_debugfs_release,
};

/*
 * megasas_init_debugfs :	Create debugfs root for megaraid_sas driver
 */
void megasas_init_debugfs(void)
{
	megasas_debugfs_root = debugfs_create_dir("megaraid_sas", NULL);
	if (!megasas_debugfs_root)
		pr_info("Cannot create debugfs root\n");
}

/*
 * megasas_exit_debugfs :	Remove debugfs root for megaraid_sas driver
 */
void megasas_exit_debugfs(void)
{
	debugfs_remove_recursive(megasas_debugfs_root);
}

/*
 * megasas_setup_debugfs :	Setup debugfs per Fusion adapter
 * instance:				Soft instance of adapter
 */
void
megasas_setup_debugfs(struct megasas_instance *instance)
{
	char name[64];
	struct fusion_context *fusion;

	fusion = instance->ctrl_context;

	if (fusion) {
		snprintf(name, sizeof(name),
			 "scsi_host%d", instance->host->host_no);
		if (!instance->debugfs_root) {
			instance->debugfs_root =
				debugfs_create_dir(name, megasas_debugfs_root);
			if (!instance->debugfs_root) {
				dev_err(&instance->pdev->dev,
					"Cannot create per adapter debugfs directory\n");
				return;
			}
		}

		snprintf(name, sizeof(name), "raidmap_dump");
		instance->raidmap_dump =
			debugfs_create_file(name, S_IRUGO,
					    instance->debugfs_root, instance,
					    &megasas_debugfs_raidmap_fops);
		if (!instance->raidmap_dump) {
			dev_err(&instance->pdev->dev,
				"Cannot create raidmap debugfs file\n");
			debugfs_remove(instance->debugfs_root);
			return;
		}
	}

}

/*
 * megasas_destroy_debugfs :	Destroy debugfs per Fusion adapter
 * instance:					Soft instance of adapter
 */
void megasas_destroy_debugfs(struct megasas_instance *instance)
{
	debugfs_remove_recursive(instance->debugfs_root);
}

#else
void megasas_init_debugfs(void)
{
}
void megasas_exit_debugfs(void)
{
}
void megasas_setup_debugfs(struct megasas_instance *instance)
{
}
void megasas_destroy_debugfs(struct megasas_instance *instance)
{
}
#endif /*CONFIG_DEBUG_FS*/
