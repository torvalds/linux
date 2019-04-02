/*
 *  FUJITSU Extended Socket Network Device driver
 *  Copyright (c) 2015-2016 FUJITSU LIMITED
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

/* defs support for fjes driver */

#ifdef CONFIG_DE_FS

#include <linux/defs.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>

#include "fjes.h"

static struct dentry *fjes_de_root;

static const char * const ep_status_string[] = {
	"unshared",
	"shared",
	"waiting",
	"complete",
};

static int fjes_dbg_status_show(struct seq_file *m, void *v)
{
	struct fjes_adapter *adapter = m->private;
	struct fjes_hw *hw = &adapter->hw;
	int max_epid = hw->max_epid;
	int my_epid = hw->my_epid;
	int epidx;

	seq_puts(m, "EPID\tSTATUS           SAME_ZONE        CONNECTED\n");
	for (epidx = 0; epidx < max_epid; epidx++) {
		if (epidx == my_epid) {
			seq_printf(m, "ep%d\t%-16c %-16c %-16c\n",
				   epidx, '-', '-', '-');
		} else {
			seq_printf(m, "ep%d\t%-16s %-16c %-16c\n",
				   epidx,
				   ep_status_string[fjes_hw_get_partner_ep_status(hw, epidx)],
				   fjes_hw_epid_is_same_zone(hw, epidx) ? 'Y' : 'N',
				   fjes_hw_epid_is_shared(hw->hw_info.share, epidx) ? 'Y' : 'N');
		}
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(fjes_dbg_status);

void fjes_dbg_adapter_init(struct fjes_adapter *adapter)
{
	const char *name = dev_name(&adapter->plat_dev->dev);
	struct dentry *pfile;

	adapter->dbg_adapter = defs_create_dir(name, fjes_de_root);
	if (!adapter->dbg_adapter) {
		dev_err(&adapter->plat_dev->dev,
			"defs entry for %s failed\n", name);
		return;
	}

	pfile = defs_create_file("status", 0444, adapter->dbg_adapter,
				    adapter, &fjes_dbg_status_fops);
	if (!pfile)
		dev_err(&adapter->plat_dev->dev,
			"defs status for %s failed\n", name);
}

void fjes_dbg_adapter_exit(struct fjes_adapter *adapter)
{
	defs_remove_recursive(adapter->dbg_adapter);
	adapter->dbg_adapter = NULL;
}

void fjes_dbg_init(void)
{
	fjes_de_root = defs_create_dir(fjes_driver_name, NULL);
	if (!fjes_de_root)
		pr_info("init of defs failed\n");
}

void fjes_dbg_exit(void)
{
	defs_remove_recursive(fjes_de_root);
	fjes_de_root = NULL;
}

#endif /* CONFIG_DE_FS */
