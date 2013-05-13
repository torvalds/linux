/* Copyright (C) 2004-2005  SBE, Inc.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <asm/uaccess.h>
#include "pmcc4_sysdep.h"
#include "sbecom_inline_linux.h"
#include "pmcc4_private.h"
#include "sbeproc.h"

extern void sbecom_get_brdinfo(ci_t *, struct sbe_brd_info *, u_int8_t *);
extern struct s_hdw_info hdw_info[MAX_BOARDS];

void sbecom_proc_brd_cleanup(ci_t *ci)
{
	if (ci->dir_dev) {
		char dir[7 + SBE_IFACETMPL_SIZE + 1];
		snprintf(dir, sizeof(dir), "driver/%s", ci->devname);
		remove_proc_entry("info", ci->dir_dev);
		remove_proc_entry(dir, NULL);
		ci->dir_dev = NULL;
	}
}

static void sbecom_proc_get_brdinfo(ci_t *ci, struct sbe_brd_info *bip)
{
	hdw_info_t *hi = &hdw_info[ci->brdno];
	u_int8_t *bsn = 0;

	switch (hi->promfmt)
	{
	case PROM_FORMAT_TYPE1:
		bsn = (u_int8_t *) hi->mfg_info.pft1.Serial;
		break;
	case PROM_FORMAT_TYPE2:
		bsn = (u_int8_t *) hi->mfg_info.pft2.Serial;
		break;
	}

	sbecom_get_brdinfo (ci, bip, bsn);

	pr_devel(">> sbecom_get_brdinfo: returned, first_if %p <%s> last_if %p <%s>\n",
		 bip->first_iname, bip->first_iname,
		 bip->last_iname, bip->last_iname);
}

/*
 * Describe the driver state through /proc
 */
static int sbecom_proc_get_sbe_info(struct seq_file *m, void *v)
{
	ci_t       *ci = m->private;
	char       *spd;
	struct sbe_brd_info *bip;

	if (!(bip = OS_kmalloc(sizeof(struct sbe_brd_info))))
		return -ENOMEM;

	pr_devel(">> sbecom_proc_get_sbe_info: entered\n");

	sbecom_proc_get_brdinfo(ci, bip);

	seq_puts(m, "Board Type:    ");
	switch (bip->brd_id) {
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C1T3):
		seq_puts(m, "wanPMC-C1T3");
		break;
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPTMC_256T3_E1):
		seq_puts(m, "wanPTMC-256T3 <E1>");
		break;
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPTMC_256T3_T1):
		seq_puts(m, "wanPTMC-256T3 <T1>");
		break;
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPTMC_C24TE1):
		seq_puts(m, "wanPTMC-C24TE1");
		break;

	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C4T1E1):
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C4T1E1_L):
		seq_puts(m, "wanPMC-C4T1E1");
		break;
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C2T1E1):
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C2T1E1_L):
		seq_puts(m, "wanPMC-C2T1E1");
		break;
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C1T1E1):
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPMC_C1T1E1_L):
		seq_puts(m, "wanPMC-C1T1E1");
		break;

	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C4T1E1):
		seq_puts(m, "wanPCI-C4T1E1");
		break;
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C2T1E1):
		seq_puts(m, "wanPCI-C2T1E1");
		break;
	case SBE_BOARD_ID(PCI_VENDOR_ID_SBE, PCI_DEVICE_ID_WANPCI_C1T1E1):
		seq_puts(m, "wanPCI-C1T1E1");
		break;

	default:
		seq_puts(m, "unknown");
		break;
	}

	seq_printf(m, "  [%08X]\n", bip->brd_id);

	seq_printf(m, "Board Number:  %d\n", bip->brdno);
	seq_printf(m, "Hardware ID:   0x%02X\n", ci->hdw_bid);
	seq_printf(m, "Board SN:      %06X\n", bip->brd_sn);
	seq_printf(m, "Board MAC:     %pMF\n", bip->brd_mac_addr);
	seq_printf(m, "Ports:         %d\n", ci->max_port);
	seq_printf(m, "Channels:      %d\n", bip->brd_chan_cnt);
#if 1
	seq_printf(m, "Interface:     %s -> %s\n",
		   bip->first_iname, bip->last_iname);
#else
	seq_printf(m, "Interface:     <not available> 1st %p lst %p\n",
		   bip->first_iname, bip->last_iname);
#endif

	switch (bip->brd_pci_speed) {
	case BINFO_PCI_SPEED_33:
		spd = "33Mhz";
		break;
	case BINFO_PCI_SPEED_66:
		spd = "66Mhz";
		break;
	default:
		spd = "<not available>";
		break;
	}
	seq_printf(m, "PCI Bus Speed: %s\n", spd);
	seq_printf(m, "Release:       %s\n", ci->release);

#ifdef SBE_PMCC4_ENABLE
	{
		extern int cxt1e1_max_mru;
#if 0
		extern int max_chans_used;
		extern int cxt1e1_max_mtu;
#endif
		extern int max_rxdesc_used, max_txdesc_used;

		seq_printf(m, "\ncxt1e1_max_mru:         %d\n", cxt1e1_max_mru);
#if 0
		seq_printf(m, "\nmax_chans_used:  %d\n", max_chans_used);
		seq_printf(m, "cxt1e1_max_mtu:         %d\n", cxt1e1_max_mtu);
#endif
		seq_printf(m, "max_rxdesc_used: %d\n", max_rxdesc_used);
		seq_printf(m, "max_txdesc_used: %d\n", max_txdesc_used);
	}
#endif

	kfree(bip);

	pr_devel(">> proc_fs: finished\n");
	return 0;
}

/*
 * seq_file wrappers for procfile show routines.
 */
static int sbecom_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sbecom_proc_get_sbe_info, PDE_DATA(inode));
}

static const struct file_operations sbecom_proc_fops = {
	.open		= sbecom_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * Initialize the /proc subsystem for the specific SBE driver
 */
int __init sbecom_proc_brd_init(ci_t *ci)
{
	char dir[7 + SBE_IFACETMPL_SIZE + 1];

	snprintf(dir, sizeof(dir), "driver/%s", ci->devname);
	ci->dir_dev = proc_mkdir(dir, NULL);
	if (!ci->dir_dev) {
		pr_err("Unable to create directory /proc/driver/%s\n", ci->devname);
		goto fail;
	}

	if (!proc_create_data("info", S_IFREG | S_IRUGO, ci->dir_dev,
			      &sbecom_proc_fops, ci)) {
		pr_err("Unable to create entry /proc/driver/%s/info\n", ci->devname);
		goto fail;
	}
	return 0;

fail:
	sbecom_proc_brd_cleanup(ci);
	return 1;
}
