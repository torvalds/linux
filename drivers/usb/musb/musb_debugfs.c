/*
 * MUSB OTG driver debugfs support
 *
 * Copyright 2010 Nokia Corporation
 * Contact: Felipe Balbi <felipe.balbi@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#include <asm/uaccess.h>

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
#ifndef CONFIG_BLACKFIN
	{ "ConfigData",	MUSB_CONFIGDATA,8 },
	{ "BabbleCtl",	MUSB_BABBLE_CTL,8 },
	{ "TxFIFOsz",	MUSB_TXFIFOSZ,	8 },
	{ "RxFIFOsz",	MUSB_RXFIFOSZ,	8 },
	{ "TxFIFOadd",	MUSB_TXFIFOADD,	16 },
	{ "RxFIFOadd",	MUSB_RXFIFOADD,	16 },
	{ "EPInfo",	MUSB_EPINFO,	8 },
	{ "RAMInfo",	MUSB_RAMINFO,	8 },
#endif
	{  }	/* Terminating Entry */
};

static int musb_regdump_show(struct seq_file *s, void *unused)
{
	struct musb		*musb = s->private;
	unsigned		i;

	seq_printf(s, "MUSB (M)HDRC Register Dump\n");

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

	return 0;
}

static int musb_regdump_open(struct inode *inode, struct file *file)
{
	return single_open(file, musb_regdump_show, inode->i_private);
}

static int musb_test_mode_show(struct seq_file *s, void *unused)
{
	struct musb		*musb = s->private;
	unsigned		test;

	test = musb_readb(musb->mregs, MUSB_TESTMODE);

	if (test & MUSB_TEST_FORCE_HOST)
		seq_printf(s, "force host\n");

	if (test & MUSB_TEST_FIFO_ACCESS)
		seq_printf(s, "fifo access\n");

	if (test & MUSB_TEST_FORCE_FS)
		seq_printf(s, "force full-speed\n");

	if (test & MUSB_TEST_FORCE_HS)
		seq_printf(s, "force high-speed\n");

	if (test & MUSB_TEST_PACKET)
		seq_printf(s, "test packet\n");

	if (test & MUSB_TEST_K)
		seq_printf(s, "test K\n");

	if (test & MUSB_TEST_J)
		seq_printf(s, "test J\n");

	if (test & MUSB_TEST_SE0_NAK)
		seq_printf(s, "test SE0 NAK\n");

	return 0;
}

static const struct file_operations musb_regdump_fops = {
	.open			= musb_regdump_open,
	.read			= seq_read,
	.llseek			= seq_lseek,
	.release		= single_release,
};

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
	char			buf[18];

	test = musb_readb(musb->mregs, MUSB_TESTMODE);
	if (test) {
		dev_err(musb->controller, "Error: test mode is already set. "
			"Please do USB Bus Reset to start a new test.\n");
		return count;
	}

	memset(buf, 0x00, sizeof(buf));

	if (copy_from_user(buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (strstarts(buf, "force host"))
		test = MUSB_TEST_FORCE_HOST;

	if (strstarts(buf, "fifo access"))
		test = MUSB_TEST_FIFO_ACCESS;

	if (strstarts(buf, "force full-speed"))
		test = MUSB_TEST_FORCE_FS;

	if (strstarts(buf, "force high-speed"))
		test = MUSB_TEST_FORCE_HS;

	if (strstarts(buf, "test packet")) {
		test = MUSB_TEST_PACKET;
		musb_load_testpacket(musb);
	}

	if (strstarts(buf, "test K"))
		test = MUSB_TEST_K;

	if (strstarts(buf, "test J"))
		test = MUSB_TEST_J;

	if (strstarts(buf, "test SE0 NAK"))
		test = MUSB_TEST_SE0_NAK;

	musb_writeb(musb->mregs, MUSB_TESTMODE, test);

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
		reg = musb_readb(musb->mregs, MUSB_DEVCTL);
		connect = reg & MUSB_DEVCTL_SESSION ? 1 : 0;
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

int musb_init_debugfs(struct musb *musb)
{
	struct dentry		*root;
	struct dentry		*file;
	int			ret;

	root = debugfs_create_dir(dev_name(musb->controller), NULL);
	if (!root) {
		ret = -ENOMEM;
		goto err0;
	}

	file = debugfs_create_file("regdump", S_IRUGO, root, musb,
			&musb_regdump_fops);
	if (!file) {
		ret = -ENOMEM;
		goto err1;
	}

	file = debugfs_create_file("testmode", S_IRUGO | S_IWUSR,
			root, musb, &musb_test_mode_fops);
	if (!file) {
		ret = -ENOMEM;
		goto err1;
	}

	file = debugfs_create_file("softconnect", S_IRUGO | S_IWUSR,
			root, musb, &musb_softconnect_fops);
	if (!file) {
		ret = -ENOMEM;
		goto err1;
	}

	musb->debugfs_root = root;

	return 0;

err1:
	debugfs_remove_recursive(root);

err0:
	return ret;
}

void /* __init_or_exit */ musb_exit_debugfs(struct musb *musb)
{
	debugfs_remove_recursive(musb->debugfs_root);
}
