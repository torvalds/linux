// SPDX-License-Identifier: GPL-2.0
/*
 * MUSB OTG driver debugfs support
 *
 * Copyright 2010 Nokia Corporation
 * Contact: Felipe Balbi <felipe.balbi@nokia.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <linux/uaccess.h>

#include "musb_core.h"
#include "musb_debug.h"

struct musb_register_map {
	char			*name;
	unsigned		offset;
	unsigned		size;
};

static const struct musb_register_map musb_regmap[] = {
	{ "FAddr",	MUSB_FADDR,	8 },
	{ "Power",	MUSB_POWER,	8 },
	{ "Frame",	MUSB_FRAME,	16 },
	{ "Index",	MUSB_INDEX,	8 },
	{ "Testmode",	MUSB_TESTMODE,	8 },
	{ "TxMaxPp",	MUSB_TXMAXP,	16 },
	{ "TxCSRp",	MUSB_TXCSR,	16 },
	{ "RxMaxPp",	MUSB_RXMAXP,	16 },
	{ "RxCSR",	MUSB_RXCSR,	16 },
	{ "RxCount",	MUSB_RXCOUNT,	16 },
	{ "IntrRxE",	MUSB_INTRRXE,	16 },
	{ "IntrTxE",	MUSB_INTRTXE,	16 },
	{ "IntrUsbE",	MUSB_INTRUSBE,	8 },
	{ "DevCtl",	MUSB_DEVCTL,	8 },
	{ "VControl",	0x68,		32 },
	{ "HWVers",	0x69,		16 },
	{ "LinkInfo",	MUSB_LINKINFO,	8 },
	{ "VPLen",	MUSB_VPLEN,	8 },
	{ "HS_EOF1",	MUSB_HS_EOF1,	8 },
	{ "FS_EOF1",	MUSB_FS_EOF1,	8 },
	{ "LS_EOF1",	MUSB_LS_EOF1,	8 },
	{ "SOFT_RST",	0x7F,		8 },
	{ "DMA_CNTLch0",	0x204,	16 },
	{ "DMA_ADDRch0",	0x208,	32 },
	{ "DMA_COUNTch0",	0x20C,	32 },
	{ "DMA_CNTLch1",	0x214,	16 },
	{ "DMA_ADDRch1",	0x218,	32 },
	{ "DMA_COUNTch1",	0x21C,	32 },
	{ "DMA_CNTLch2",	0x224,	16 },
	{ "DMA_ADDRch2",	0x228,	32 },
	{ "DMA_COUNTch2",	0x22C,	32 },
	{ "DMA_CNTLch3",	0x234,	16 },
	{ "DMA_ADDRch3",	0x238,	32 },
	{ "DMA_COUNTch3",	0x23C,	32 },
	{ "DMA_CNTLch4",	0x244,	16 },
	{ "DMA_ADDRch4",	0x248,	32 },
	{ "DMA_COUNTch4",	0x24C,	32 },
	{ "DMA_CNTLch5",	0x254,	16 },
	{ "DMA_ADDRch5",	0x258,	32 },
	{ "DMA_COUNTch5",	0x25C,	32 },
	{ "DMA_CNTLch6",	0x264,	16 },
	{ "DMA_ADDRch6",	0x268,	32 },
	{ "DMA_COUNTch6",	0x26C,	32 },
	{ "DMA_CNTLch7",	0x274,	16 },
	{ "DMA_ADDRch7",	0x278,	32 },
	{ "DMA_COUNTch7",	0x27C,	32 },
	{ "ConfigData",	MUSB_CONFIGDATA,8 },
	{ "BabbleCtl",	MUSB_BABBLE_CTL,8 },
	{ "TxFIFOsz",	MUSB_TXFIFOSZ,	8 },
	{ "RxFIFOsz",	MUSB_RXFIFOSZ,	8 },
	{ "TxFIFOadd",	MUSB_TXFIFOADD,	16 },
	{ "RxFIFOadd",	MUSB_RXFIFOADD,	16 },
	{ "EPInfo",	MUSB_EPINFO,	8 },
	{ "RAMInfo",	MUSB_RAMINFO,	8 },
	{  }	/* Terminating Entry */
};

static int musb_regdump_show(struct seq_file *s, void *unused)
{
	struct musb		*musb = s->private;
	unsigned		i;

	seq_printf(s, "MUSB (M)HDRC Register Dump\n");
	pm_runtime_get_sync(musb->controller);

	for (i = 0; i < ARRAY_SIZE(musb_regmap); i++) {
		switch (musb_regmap[i].size) {
		case 8:
			seq_printf(s, "%-12s: %02x\n", musb_regmap[i].name,
					musb_readb(musb->mregs, musb_regmap[i].offset));
			break;
		case 16:
			seq_printf(s, "%-12s: %04x\n", musb_regmap[i].name,
					musb_readw(musb->mregs, musb_regmap[i].offset));
			break;
		case 32:
			seq_printf(s, "%-12s: %08x\n", musb_regmap[i].name,
					musb_readl(musb->mregs, musb_regmap[i].offset));
			break;
		}
	}

	pm_runtime_mark_last_busy(musb->controller);
	pm_runtime_put_autosuspend(musb->controller);
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(musb_regdump);

static int musb_test_mode_show(struct seq_file *s, void *unused)
{
	struct musb		*musb = s->private;
	unsigned		test;

	pm_runtime_get_sync(musb->controller);
	test = musb_readb(musb->mregs, MUSB_TESTMODE);
	pm_runtime_mark_last_busy(musb->controller);
	pm_runtime_put_autosuspend(musb->controller);

	if (test == (MUSB_TEST_FORCE_HOST | MUSB_TEST_FORCE_FS))
		seq_printf(s, "force host full-speed\n");

	else if (test == (MUSB_TEST_FORCE_HOST | MUSB_TEST_FORCE_HS))
		seq_printf(s, "force host high-speed\n");

	else if (test == MUSB_TEST_FORCE_HOST)
		seq_printf(s, "force host\n");

	else if (test == MUSB_TEST_FIFO_ACCESS)
		seq_printf(s, "fifo access\n");

	else if (test == MUSB_TEST_FORCE_FS)
		seq_printf(s, "force full-speed\n");

	else if (test == MUSB_TEST_FORCE_HS)
		seq_printf(s, "force high-speed\n");

	else if (test == MUSB_TEST_PACKET)
		seq_printf(s, "test packet\n");

	else if (test == MUSB_TEST_K)
		seq_printf(s, "test K\n");

	else if (test == MUSB_TEST_J)
		seq_printf(s, "test J\n");

	else if (test == MUSB_TEST_SE0_NAK)
		seq_printf(s, "test SE0 NAK\n");

	return 0;
}

static int musb_test_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, musb_test_mode_show, inode->i_private);
}

static ssize_t musb_test_mode_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct musb		*musb = s->private;
	u8			test;
	char			buf[24];

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	pm_runtime_get_sync(musb->controller);
	test = musb_readb(musb->mregs, MUSB_TESTMODE);
	if (test) {
		dev_err(musb->controller, "Error: test mode is already set. "
			"Please do USB Bus Reset to start a new test.\n");
		goto ret;
	}

	if (strstarts(buf, "force host full-speed"))
		test = MUSB_TEST_FORCE_HOST | MUSB_TEST_FORCE_FS;

	else if (strstarts(buf, "force host high-speed"))
		test = MUSB_TEST_FORCE_HOST | MUSB_TEST_FORCE_HS;

	else if (strstarts(buf, "force host"))
		test = MUSB_TEST_FORCE_HOST;

	else if (strstarts(buf, "fifo access"))
		test = MUSB_TEST_FIFO_ACCESS;

	else if (strstarts(buf, "force full-speed"))
		test = MUSB_TEST_FORCE_FS;

	else if (strstarts(buf, "force high-speed"))
		test = MUSB_TEST_FORCE_HS;

	else if (strstarts(buf, "test packet")) {
		test = MUSB_TEST_PACKET;
		musb_load_testpacket(musb);
	}

	else if (strstarts(buf, "test K"))
		test = MUSB_TEST_K;

	else if (strstarts(buf, "test J"))
		test = MUSB_TEST_J;

	else if (strstarts(buf, "test SE0 NAK"))
		test = MUSB_TEST_SE0_NAK;

	musb_writeb(musb->mregs, MUSB_TESTMODE, test);

ret:
	pm_runtime_mark_last_busy(musb->controller);
	pm_runtime_put_autosuspend(musb->controller);
	return count;
}

static const struct file_operations musb_test_mode_fops = {
	.open			= musb_test_mode_open,
	.write			= musb_test_mode_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

static int musb_softconnect_show(struct seq_file *s, void *unused)
{
	struct musb	*musb = s->private;
	u8		reg;
	int		connect;

	switch (musb->xceiv->otg->state) {
	case OTG_STATE_A_HOST:
	case OTG_STATE_A_WAIT_BCON:
		pm_runtime_get_sync(musb->controller);

		reg = musb_readb(musb->mregs, MUSB_DEVCTL);
		connect = reg & MUSB_DEVCTL_SESSION ? 1 : 0;

		pm_runtime_mark_last_busy(musb->controller);
		pm_runtime_put_autosuspend(musb->controller);
		break;
	default:
		connect = -1;
	}

	seq_printf(s, "%d\n", connect);

	return 0;
}

static int musb_softconnect_open(struct inode *inode, struct file *file)
{
	return single_open(file, musb_softconnect_show, inode->i_private);
}

static ssize_t musb_softconnect_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file		*s = file->private_data;
	struct musb		*musb = s->private;
	char			buf[2];
	u8			reg;

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(&buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	pm_runtime_get_sync(musb->controller);
	if (!strncmp(buf, "0", 1)) {
		switch (musb->xceiv->otg->state) {
		case OTG_STATE_A_HOST:
			musb_root_disconnect(musb);
			reg = musb_readb(musb->mregs, MUSB_DEVCTL);
			reg &= ~MUSB_DEVCTL_SESSION;
			musb_writeb(musb->mregs, MUSB_DEVCTL, reg);
			break;
		default:
			break;
		}
	} else if (!strncmp(buf, "1", 1)) {
		switch (musb->xceiv->otg->state) {
		case OTG_STATE_A_WAIT_BCON:
			/*
			 * musb_save_context() called in musb_runtime_suspend()
			 * might cache devctl with SESSION bit cleared during
			 * soft-disconnect, so specifically set SESSION bit
			 * here to preserve it for musb_runtime_resume().
			 */
			musb->context.devctl |= MUSB_DEVCTL_SESSION;
			reg = musb_readb(musb->mregs, MUSB_DEVCTL);
			reg |= MUSB_DEVCTL_SESSION;
			musb_writeb(musb->mregs, MUSB_DEVCTL, reg);
			break;
		default:
			break;
		}
	}

	pm_runtime_mark_last_busy(musb->controller);
	pm_runtime_put_autosuspend(musb->controller);
	return count;
}

/*
 * In host mode, connect/disconnect the bus without physically
 * remove the devices.
 */
static const struct file_operations musb_softconnect_fops = {
	.open			= musb_softconnect_open,
	.write			= musb_softconnect_write,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

void musb_init_debugfs(struct musb *musb)
{
	struct dentry *root;

	root = debugfs_create_dir(dev_name(musb->controller), usb_debug_root);
	musb->debugfs_root = root;

	debugfs_create_file("regdump", S_IRUGO, root, musb, &musb_regdump_fops);
	debugfs_create_file("testmode", S_IRUGO | S_IWUSR, root, musb,
			    &musb_test_mode_fops);
	debugfs_create_file("softconnect", S_IRUGO | S_IWUSR, root, musb,
			    &musb_softconnect_fops);
}

void /* __init_or_exit */ musb_exit_debugfs(struct musb *musb)
{
	debugfs_remove_recursive(musb->debugfs_root);
}
