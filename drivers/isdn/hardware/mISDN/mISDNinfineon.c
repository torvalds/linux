/*
 * mISDNinfineon.c
 *		Support for cards based on following Infineon ISDN chipsets
 *		- ISAC + HSCX
 *		- IPAC and IPAC-X
 *		- ISAC-SX + HSCX
 *
 * Supported cards:
 *		- Dialogic Diva 2.0
 *		- Dialogic Diva 2.0U
 *		- Dialogic Diva 2.01
 *		- Dialogic Diva 2.02
 *		- Sedlbauer Speedwin
 *		- HST Saphir3
 *		- Develo (former ELSA) Microlink PCI (Quickstep 1000)
 *		- Develo (former ELSA) Quickstep 3000
 *		- Berkom Scitel BRIX Quadro
 *		- Dr.Neuhaus (Sagem) Niccy
 *
 *
 *
 * Author       Karsten Keil <keil@isdn4linux.de>
 *
 * Copyright 2009  by Karsten Keil <keil@isdn4linux.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/mISDNhw.h>
#include "ipac.h"

#define INFINEON_REV	"1.0"

static int inf_cnt;
static u32 debug;
static u32 irqloops = 4;

enum inf_types {
	INF_NONE,
	INF_DIVA20,
	INF_DIVA20U,
	INF_DIVA201,
	INF_DIVA202,
	INF_SPEEDWIN,
	INF_SAPHIR3,
	INF_QS1000,
	INF_QS3000,
	INF_NICCY,
	INF_SCT_1,
	INF_SCT_2,
	INF_SCT_3,
	INF_SCT_4,
	INF_GAZEL_R685,
	INF_GAZEL_R753
};

enum addr_mode {
	AM_NONE = 0,
	AM_IO,
	AM_MEMIO,
	AM_IND_IO,
};

struct inf_cinfo {
	enum inf_types	typ;
	const char	*full;
	const char	*name;
	enum addr_mode	cfg_mode;
	enum addr_mode	addr_mode;
	u8		cfg_bar;
	u8		addr_bar;
	void		*irqfunc;
};

struct _ioaddr {
	enum addr_mode	mode;
	union {
		void __iomem	*p;
		struct _ioport	io;
	} a;
};

struct _iohandle {
	enum addr_mode	mode;
	resource_size_t	size;
	resource_size_t	start;
	void __iomem	*p;
};

struct inf_hw {
	struct list_head	list;
	struct pci_dev		*pdev;
	const struct inf_cinfo	*ci;
	char			name[MISDN_MAX_IDLEN];
	u32			irq;
	u32			irqcnt;
	struct _iohandle	cfg;
	struct _iohandle	addr;
	struct _ioaddr		isac;
	struct _ioaddr		hscx;
	spinlock_t		lock;	/* HW access lock */
	struct ipac_hw		ipac;
	struct inf_hw		*sc[3];	/* slave cards */
};


#define PCI_SUBVENDOR_HST_SAPHIR3       0x52
#define PCI_SUBVENDOR_SEDLBAUER_PCI     0x53
#define PCI_SUB_ID_SEDLBAUER            0x01

static struct pci_device_id infineon_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_DIVA20,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_DIVA20},
	{ PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_DIVA20_U,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_DIVA20U},
	{ PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_DIVA201,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_DIVA201},
	{ PCI_VENDOR_ID_EICON, PCI_DEVICE_ID_EICON_DIVA202,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_DIVA202},
	{ PCI_VENDOR_ID_TIGERJET, PCI_DEVICE_ID_TIGERJET_100,
	  PCI_SUBVENDOR_SEDLBAUER_PCI, PCI_SUB_ID_SEDLBAUER, 0, 0,
	  INF_SPEEDWIN},
	{ PCI_VENDOR_ID_TIGERJET, PCI_DEVICE_ID_TIGERJET_100,
	  PCI_SUBVENDOR_HST_SAPHIR3, PCI_SUB_ID_SEDLBAUER, 0, 0, INF_SAPHIR3},
	{ PCI_VENDOR_ID_ELSA, PCI_DEVICE_ID_ELSA_MICROLINK,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_QS1000},
	{ PCI_VENDOR_ID_ELSA, PCI_DEVICE_ID_ELSA_QS3000,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_QS3000},
	{ PCI_VENDOR_ID_SATSAGEM, PCI_DEVICE_ID_SATSAGEM_NICCY,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_NICCY},
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_9050,
	  PCI_VENDOR_ID_BERKOM, PCI_DEVICE_ID_BERKOM_SCITEL_QUADRO, 0, 0,
	  INF_SCT_1},
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_R685,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_GAZEL_R685},
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_R753,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_GAZEL_R753},
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_DJINN_ITOO,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_GAZEL_R753},
	{ PCI_VENDOR_ID_PLX, PCI_DEVICE_ID_PLX_OLITEC,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, INF_GAZEL_R753},
	{ }
};
MODULE_DEVICE_TABLE(pci, infineon_ids);

/* PCI interface specific defines */
/* Diva 2.0/2.0U */
#define DIVA_HSCX_PORT		0x00
#define DIVA_HSCX_ALE		0x04
#define DIVA_ISAC_PORT		0x08
#define DIVA_ISAC_ALE		0x0C
#define DIVA_PCI_CTRL           0x10

/* DIVA_PCI_CTRL bits */
#define DIVA_IRQ_BIT		0x01
#define DIVA_RESET_BIT		0x08
#define DIVA_EEPROM_CLK		0x40
#define DIVA_LED_A		0x10
#define DIVA_LED_B		0x20
#define DIVA_IRQ_CLR		0x80

/* Diva 2.01/2.02 */
/* Siemens PITA */
#define PITA_ICR_REG		0x00
#define PITA_INT0_STATUS	0x02

#define PITA_MISC_REG		0x1c
#define PITA_PARA_SOFTRESET	0x01000000
#define PITA_SER_SOFTRESET	0x02000000
#define PITA_PARA_MPX_MODE	0x04000000
#define PITA_INT0_ENABLE	0x00020000

/* TIGER 100 Registers */
#define TIGER_RESET_ADDR	0x00
#define TIGER_EXTERN_RESET	0x01
#define TIGER_AUX_CTRL		0x02
#define TIGER_AUX_DATA		0x03
#define TIGER_AUX_IRQMASK	0x05
#define TIGER_AUX_STATUS	0x07

/* Tiger AUX BITs */
#define TIGER_IOMASK		0xdd	/* 1 and 5 are inputs */
#define TIGER_IRQ_BIT		0x02

#define TIGER_IPAC_ALE		0xC0
#define TIGER_IPAC_PORT		0xC8

/* ELSA (now Develo) PCI cards */
#define ELSA_IRQ_ADDR		0x4c
#define ELSA_IRQ_MASK		0x04
#define QS1000_IRQ_OFF		0x01
#define QS3000_IRQ_OFF		0x03
#define QS1000_IRQ_ON		0x41
#define QS3000_IRQ_ON		0x43

/* Dr Neuhaus/Sagem Niccy */
#define NICCY_ISAC_PORT		0x00
#define NICCY_HSCX_PORT		0x01
#define NICCY_ISAC_ALE		0x02
#define NICCY_HSCX_ALE		0x03

#define NICCY_IRQ_CTRL_REG	0x38
#define NICCY_IRQ_ENABLE	0x001f00
#define NICCY_IRQ_DISABLE	0xff0000
#define NICCY_IRQ_BIT		0x800000


/* Scitel PLX */
#define SCT_PLX_IRQ_ADDR	0x4c
#define SCT_PLX_RESET_ADDR	0x50
#define SCT_PLX_IRQ_ENABLE	0x41
#define SCT_PLX_RESET_BIT	0x04

/* Gazel */
#define	GAZEL_IPAC_DATA_PORT	0x04
/* Gazel PLX */
#define GAZEL_CNTRL		0x50
#define GAZEL_RESET		0x04
#define GAZEL_RESET_9050	0x40000000
#define GAZEL_INCSR		0x4C
#define GAZEL_ISAC_EN		0x08
#define GAZEL_INT_ISAC		0x20
#define GAZEL_HSCX_EN		0x01
#define GAZEL_INT_HSCX		0x04
#define GAZEL_PCI_EN		0x40
#define GAZEL_IPAC_EN		0x03


static LIST_HEAD(Cards);
static DEFINE_RWLOCK(card_lock); /* protect Cards */

static void
_set_debug(struct inf_hw *card)
{
	card->ipac.isac.dch.debug = debug;
	card->ipac.hscx[0].bch.debug = debug;
	card->ipac.hscx[1].bch.debug = debug;
}

static int
set_debug(const char *val, struct kernel_param *kp)
{
	int ret;
	struct inf_hw *card;

	ret = param_set_uint(val, kp);
	if (!ret) {
		read_lock(&card_lock);
		list_for_each_entry(card, &Cards, list)
			_set_debug(card);
		read_unlock(&card_lock);
	}
	return ret;
}

MODULE_AUTHOR("Karsten Keil");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(INFINEON_REV);
module_param_call(debug, set_debug, param_get_uint, &debug, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "infineon debug mask");
module_param(irqloops, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(irqloops, "infineon maximal irqloops (default 4)");

/* Interface functions */

IOFUNC_IO(ISAC, inf_hw, isac.a.io)
IOFUNC_IO(IPAC, inf_hw, hscx.a.io)
IOFUNC_IND(ISAC, inf_hw, isac.a.io)
IOFUNC_IND(IPAC, inf_hw, hscx.a.io)
IOFUNC_MEMIO(ISAC, inf_hw, u32, isac.a.p)
IOFUNC_MEMIO(IPAC, inf_hw, u32, hscx.a.p)

static irqreturn_t
diva_irq(int intno, void *dev_id)
{
	struct inf_hw *hw = dev_id;
	u8 val;

	spin_lock(&hw->lock);
	val = inb((u32)hw->cfg.start + DIVA_PCI_CTRL);
	if (!(val & DIVA_IRQ_BIT)) { /* for us or shared ? */
		spin_unlock(&hw->lock);
		return IRQ_NONE; /* shared */
	}
	hw->irqcnt++;
	mISDNipac_irq(&hw->ipac, irqloops);
	spin_unlock(&hw->lock);
	return IRQ_HANDLED;
}

static irqreturn_t
diva20x_irq(int intno, void *dev_id)
{
	struct inf_hw *hw = dev_id;
	u8 val;

	spin_lock(&hw->lock);
	val = readb(hw->cfg.p);
	if (!(val & PITA_INT0_STATUS)) { /* for us or shared ? */
		spin_unlock(&hw->lock);
		return IRQ_NONE; /* shared */
	}
	hw->irqcnt++;
	mISDNipac_irq(&hw->ipac, irqloops);
	writeb(PITA_INT0_STATUS, hw->cfg.p); /* ACK PITA INT0 */
	spin_unlock(&hw->lock);
	return IRQ_HANDLED;
}

static irqreturn_t
tiger_irq(int intno, void *dev_id)
{
	struct inf_hw *hw = dev_id;
	u8 val;

	spin_lock(&hw->lock);
	val = inb((u32)hw->cfg.start + TIGER_AUX_STATUS);
	if (val & TIGER_IRQ_BIT) { /* for us or shared ? */
		spin_unlock(&hw->lock);
		return IRQ_NONE; /* shared */
	}
	hw->irqcnt++;
	mISDNipac_irq(&hw->ipac, irqloops);
	spin_unlock(&hw->lock);
	return IRQ_HANDLED;
}

static irqreturn_t
elsa_irq(int intno, void *dev_id)
{
	struct inf_hw *hw = dev_id;
	u8 val;

	spin_lock(&hw->lock);
	val = inb((u32)hw->cfg.start + ELSA_IRQ_ADDR);
	if (!(val & ELSA_IRQ_MASK)) {
		spin_unlock(&hw->lock);
		return IRQ_NONE; /* shared */
	}
	hw->irqcnt++;
	mISDNipac_irq(&hw->ipac, irqloops);
	spin_unlock(&hw->lock);
	return IRQ_HANDLED;
}

static irqreturn_t
niccy_irq(int intno, void *dev_id)
{
	struct inf_hw *hw = dev_id;
	u32 val;

	spin_lock(&hw->lock);
	val = inl((u32)hw->cfg.start + NICCY_IRQ_CTRL_REG);
	if (!(val & NICCY_IRQ_BIT)) { /* for us or shared ? */
		spin_unlock(&hw->lock);
		return IRQ_NONE; /* shared */
	}
	outl(val, (u32)hw->cfg.start + NICCY_IRQ_CTRL_REG);
	hw->irqcnt++;
	mISDNipac_irq(&hw->ipac, irqloops);
	spin_unlock(&hw->lock);
	return IRQ_HANDLED;
}

static irqreturn_t
gazel_irq(int intno, void *dev_id)
{
	struct inf_hw *hw = dev_id;
	irqreturn_t ret;

	spin_lock(&hw->lock);
	ret = mISDNipac_irq(&hw->ipac, irqloops);
	spin_unlock(&hw->lock);
	return ret;
}

static irqreturn_t
ipac_irq(int intno, void *dev_id)
{
	struct inf_hw *hw = dev_id;
	u8 val;

	spin_lock(&hw->lock);
	val = hw->ipac.read_reg(hw, IPAC_ISTA);
	if (!(val & 0x3f)) {
		spin_unlock(&hw->lock);
		return IRQ_NONE; /* shared */
	}
	hw->irqcnt++;
	mISDNipac_irq(&hw->ipac, irqloops);
	spin_unlock(&hw->lock);
	return IRQ_HANDLED;
}

static void
enable_hwirq(struct inf_hw *hw)
{
	u16 w;
	u32 val;

	switch (hw->ci->typ) {
	case INF_DIVA201:
	case INF_DIVA202:
		writel(PITA_INT0_ENABLE, hw->cfg.p);
		break;
	case INF_SPEEDWIN:
	case INF_SAPHIR3:
		outb(TIGER_IRQ_BIT, (u32)hw->cfg.start + TIGER_AUX_IRQMASK);
		break;
	case INF_QS1000:
		outb(QS1000_IRQ_ON, (u32)hw->cfg.start + ELSA_IRQ_ADDR);
		break;
	case INF_QS3000:
		outb(QS3000_IRQ_ON, (u32)hw->cfg.start + ELSA_IRQ_ADDR);
		break;
	case INF_NICCY:
		val = inl((u32)hw->cfg.start + NICCY_IRQ_CTRL_REG);
		val |= NICCY_IRQ_ENABLE;;
		outl(val, (u32)hw->cfg.start + NICCY_IRQ_CTRL_REG);
		break;
	case INF_SCT_1:
		w = inw((u32)hw->cfg.start + SCT_PLX_IRQ_ADDR);
		w |= SCT_PLX_IRQ_ENABLE;
		outw(w, (u32)hw->cfg.start + SCT_PLX_IRQ_ADDR);
		break;
	case INF_GAZEL_R685:
		outb(GAZEL_ISAC_EN + GAZEL_HSCX_EN + GAZEL_PCI_EN,
			(u32)hw->cfg.start + GAZEL_INCSR);
		break;
	case INF_GAZEL_R753:
		outb(GAZEL_IPAC_EN + GAZEL_PCI_EN,
			(u32)hw->cfg.start + GAZEL_INCSR);
		break;
	default:
		break;
	}
}

static void
disable_hwirq(struct inf_hw *hw)
{
	u16 w;
	u32 val;

	switch (hw->ci->typ) {
	case INF_DIVA201:
	case INF_DIVA202:
		writel(0, hw->cfg.p);
		break;
	case INF_SPEEDWIN:
	case INF_SAPHIR3:
		outb(0, (u32)hw->cfg.start + TIGER_AUX_IRQMASK);
		break;
	case INF_QS1000:
		outb(QS1000_IRQ_OFF, (u32)hw->cfg.start + ELSA_IRQ_ADDR);
		break;
	case INF_QS3000:
		outb(QS3000_IRQ_OFF, (u32)hw->cfg.start + ELSA_IRQ_ADDR);
		break;
	case INF_NICCY:
		val = inl((u32)hw->cfg.start + NICCY_IRQ_CTRL_REG);
		val &= NICCY_IRQ_DISABLE;
		outl(val, (u32)hw->cfg.start + NICCY_IRQ_CTRL_REG);
		break;
	case INF_SCT_1:
		w = inw((u32)hw->cfg.start + SCT_PLX_IRQ_ADDR);
		w &= (~SCT_PLX_IRQ_ENABLE);
		outw(w, (u32)hw->cfg.start + SCT_PLX_IRQ_ADDR);
		break;
	case INF_GAZEL_R685:
	case INF_GAZEL_R753:
		outb(0, (u32)hw->cfg.start + GAZEL_INCSR);
		break;
	default:
		break;
	}
}

static void
ipac_chip_reset(struct inf_hw *hw)
{
	hw->ipac.write_reg(hw, IPAC_POTA2, 0x20);
	mdelay(5);
	hw->ipac.write_reg(hw, IPAC_POTA2, 0x00);
	mdelay(5);
	hw->ipac.write_reg(hw, IPAC_CONF, hw->ipac.conf);
	hw->ipac.write_reg(hw, IPAC_MASK, 0xc0);
}

static void
reset_inf(struct inf_hw *hw)
{
	u16 w;
	u32 val;

	if (debug & DEBUG_HW)
		pr_notice("%s: resetting card\n", hw->name);
	switch (hw->ci->typ) {
	case INF_DIVA20:
	case INF_DIVA20U:
		outb(0, (u32)hw->cfg.start + DIVA_PCI_CTRL);
		mdelay(10);
		outb(DIVA_RESET_BIT, (u32)hw->cfg.start + DIVA_PCI_CTRL);
		mdelay(10);
		/* Workaround PCI9060 */
		outb(9, (u32)hw->cfg.start + 0x69);
		outb(DIVA_RESET_BIT | DIVA_LED_A,
			(u32)hw->cfg.start + DIVA_PCI_CTRL);
		break;
	case INF_DIVA201:
		writel(PITA_PARA_SOFTRESET | PITA_PARA_MPX_MODE,
			hw->cfg.p + PITA_MISC_REG);
		mdelay(1);
		writel(PITA_PARA_MPX_MODE, hw->cfg.p + PITA_MISC_REG);
		mdelay(10);
		break;
	case INF_DIVA202:
		writel(PITA_PARA_SOFTRESET | PITA_PARA_MPX_MODE,
			hw->cfg.p + PITA_MISC_REG);
		mdelay(1);
		writel(PITA_PARA_MPX_MODE | PITA_SER_SOFTRESET,
			hw->cfg.p + PITA_MISC_REG);
		mdelay(10);
		break;
	case INF_SPEEDWIN:
	case INF_SAPHIR3:
		ipac_chip_reset(hw);
		hw->ipac.write_reg(hw, IPAC_ACFG, 0xff);
		hw->ipac.write_reg(hw, IPAC_AOE, 0x00);
		hw->ipac.write_reg(hw, IPAC_PCFG, 0x12);
		break;
	case INF_QS1000:
	case INF_QS3000:
		ipac_chip_reset(hw);
		hw->ipac.write_reg(hw, IPAC_ACFG, 0x00);
		hw->ipac.write_reg(hw, IPAC_AOE, 0x3c);
		hw->ipac.write_reg(hw, IPAC_ATX, 0xff);
		break;
	case INF_NICCY:
		break;
	case INF_SCT_1:
		w = inw((u32)hw->cfg.start + SCT_PLX_RESET_ADDR);
		w &= (~SCT_PLX_RESET_BIT);
		outw(w, (u32)hw->cfg.start + SCT_PLX_RESET_ADDR);
		mdelay(10);
		w = inw((u32)hw->cfg.start + SCT_PLX_RESET_ADDR);
		w |= SCT_PLX_RESET_BIT;
		outw(w, (u32)hw->cfg.start + SCT_PLX_RESET_ADDR);
		mdelay(10);
		break;
	case INF_GAZEL_R685:
		val = inl((u32)hw->cfg.start + GAZEL_CNTRL);
		val |= (GAZEL_RESET_9050 + GAZEL_RESET);
		outl(val, (u32)hw->cfg.start + GAZEL_CNTRL);
		val &= ~(GAZEL_RESET_9050 + GAZEL_RESET);
		mdelay(4);
		outl(val, (u32)hw->cfg.start + GAZEL_CNTRL);
		mdelay(10);
		hw->ipac.isac.adf2 = 0x87;
		hw->ipac.hscx[0].slot = 0x1f;
		hw->ipac.hscx[0].slot = 0x23;
		break;
	case INF_GAZEL_R753:
		val = inl((u32)hw->cfg.start + GAZEL_CNTRL);
		val |= (GAZEL_RESET_9050 + GAZEL_RESET);
		outl(val, (u32)hw->cfg.start + GAZEL_CNTRL);
		val &= ~(GAZEL_RESET_9050 + GAZEL_RESET);
		mdelay(4);
		outl(val, (u32)hw->cfg.start + GAZEL_CNTRL);
		mdelay(10);
		ipac_chip_reset(hw);
		hw->ipac.write_reg(hw, IPAC_ACFG, 0xff);
		hw->ipac.write_reg(hw, IPAC_AOE, 0x00);
		hw->ipac.conf = 0x01; /* IOM off */
		break;
	default:
		return;
	}
	enable_hwirq(hw);
}

static int
inf_ctrl(struct inf_hw *hw, u32 cmd, u_long arg)
{
	int ret = 0;

	switch (cmd) {
	case HW_RESET_REQ:
		reset_inf(hw);
		break;
	default:
		pr_info("%s: %s unknown command %x %lx\n",
			hw->name, __func__, cmd, arg);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int __devinit
init_irq(struct inf_hw *hw)
{
	int	ret, cnt = 3;
	u_long	flags;

	if (!hw->ci->irqfunc)
		return -EINVAL;
	ret = request_irq(hw->irq, hw->ci->irqfunc, IRQF_SHARED, hw->name, hw);
	if (ret) {
		pr_info("%s: couldn't get interrupt %d\n", hw->name, hw->irq);
		return ret;
	}
	while (cnt--) {
		spin_lock_irqsave(&hw->lock, flags);
		reset_inf(hw);
		ret = hw->ipac.init(&hw->ipac);
		if (ret) {
			spin_unlock_irqrestore(&hw->lock, flags);
			pr_info("%s: ISAC init failed with %d\n",
				hw->name, ret);
			break;
		}
		spin_unlock_irqrestore(&hw->lock, flags);
		msleep_interruptible(10);
		if (debug & DEBUG_HW)
			pr_notice("%s: IRQ %d count %d\n", hw->name,
				hw->irq, hw->irqcnt);
		if (!hw->irqcnt) {
			pr_info("%s: IRQ(%d) got no requests during init %d\n",
				hw->name, hw->irq, 3 - cnt);
		} else
			return 0;
	}
	free_irq(hw->irq, hw);
	return -EIO;
}

static void
release_io(struct inf_hw *hw)
{
	if (hw->cfg.mode) {
		if (hw->cfg.p) {
			release_mem_region(hw->cfg.start, hw->cfg.size);
			iounmap(hw->cfg.p);
		} else
			release_region(hw->cfg.start, hw->cfg.size);
		hw->cfg.mode = AM_NONE;
	}
	if (hw->addr.mode) {
		if (hw->addr.p) {
			release_mem_region(hw->addr.start, hw->addr.size);
			iounmap(hw->addr.p);
		} else
			release_region(hw->addr.start, hw->addr.size);
		hw->addr.mode = AM_NONE;
	}
}

static int __devinit
setup_io(struct inf_hw *hw)
{
	int err = 0;

	if (hw->ci->cfg_mode) {
		hw->cfg.start = pci_resource_start(hw->pdev, hw->ci->cfg_bar);
		hw->cfg.size = pci_resource_len(hw->pdev, hw->ci->cfg_bar);
		if (hw->ci->cfg_mode == AM_MEMIO) {
			if (!request_mem_region(hw->cfg.start, hw->cfg.size,
			    hw->name))
				err = -EBUSY;
		} else {
			if (!request_region(hw->cfg.start, hw->cfg.size,
			    hw->name))
				err = -EBUSY;
		}
		if (err) {
			pr_info("mISDN: %s config port %lx (%lu bytes)"
				"already in use\n", hw->name,
				(ulong)hw->cfg.start, (ulong)hw->cfg.size);
			return err;
		}
		if (hw->ci->cfg_mode == AM_MEMIO)
			hw->cfg.p = ioremap(hw->cfg.start, hw->cfg.size);
		hw->cfg.mode = hw->ci->cfg_mode;
		if (debug & DEBUG_HW)
			pr_notice("%s: IO cfg %lx (%lu bytes) mode%d\n",
				hw->name, (ulong)hw->cfg.start,
				(ulong)hw->cfg.size, hw->ci->cfg_mode);

	}
	if (hw->ci->addr_mode) {
		hw->addr.start = pci_resource_start(hw->pdev, hw->ci->addr_bar);
		hw->addr.size = pci_resource_len(hw->pdev, hw->ci->addr_bar);
		if (hw->ci->addr_mode == AM_MEMIO) {
			if (!request_mem_region(hw->addr.start, hw->addr.size,
			    hw->name))
				err = -EBUSY;
		} else {
			if (!request_region(hw->addr.start, hw->addr.size,
			    hw->name))
				err = -EBUSY;
		}
		if (err) {
			pr_info("mISDN: %s address port %lx (%lu bytes)"
				"already in use\n", hw->name,
				(ulong)hw->addr.start, (ulong)hw->addr.size);
			return err;
		}
		if (hw->ci->addr_mode == AM_MEMIO)
			hw->addr.p = ioremap(hw->addr.start, hw->addr.size);
		hw->addr.mode = hw->ci->addr_mode;
		if (debug & DEBUG_HW)
			pr_notice("%s: IO addr %lx (%lu bytes) mode%d\n",
				hw->name, (ulong)hw->addr.start,
				(ulong)hw->addr.size, hw->ci->addr_mode);

	}

	switch (hw->ci->typ) {
	case INF_DIVA20:
	case INF_DIVA20U:
		hw->ipac.type = IPAC_TYPE_ISAC | IPAC_TYPE_HSCX;
		hw->isac.mode = hw->cfg.mode;
		hw->isac.a.io.ale = (u32)hw->cfg.start + DIVA_ISAC_ALE;
		hw->isac.a.io.port = (u32)hw->cfg.start + DIVA_ISAC_PORT;
		hw->hscx.mode = hw->cfg.mode;
		hw->hscx.a.io.ale = (u32)hw->cfg.start + DIVA_HSCX_ALE;
		hw->hscx.a.io.port = (u32)hw->cfg.start + DIVA_HSCX_PORT;
		break;
	case INF_DIVA201:
		hw->ipac.type = IPAC_TYPE_IPAC;
		hw->ipac.isac.off = 0x80;
		hw->isac.mode = hw->addr.mode;
		hw->isac.a.p = hw->addr.p;
		hw->hscx.mode = hw->addr.mode;
		hw->hscx.a.p = hw->addr.p;
		break;
	case INF_DIVA202:
		hw->ipac.type = IPAC_TYPE_IPACX;
		hw->isac.mode = hw->addr.mode;
		hw->isac.a.p = hw->addr.p;
		hw->hscx.mode = hw->addr.mode;
		hw->hscx.a.p = hw->addr.p;
		break;
	case INF_SPEEDWIN:
	case INF_SAPHIR3:
		hw->ipac.type = IPAC_TYPE_IPAC;
		hw->ipac.isac.off = 0x80;
		hw->isac.mode = hw->cfg.mode;
		hw->isac.a.io.ale = (u32)hw->cfg.start + TIGER_IPAC_ALE;
		hw->isac.a.io.port = (u32)hw->cfg.start + TIGER_IPAC_PORT;
		hw->hscx.mode = hw->cfg.mode;
		hw->hscx.a.io.ale = (u32)hw->cfg.start + TIGER_IPAC_ALE;
		hw->hscx.a.io.port = (u32)hw->cfg.start + TIGER_IPAC_PORT;
		outb(0xff, (ulong)hw->cfg.start);
		mdelay(1);
		outb(0x00, (ulong)hw->cfg.start);
		mdelay(1);
		outb(TIGER_IOMASK, (ulong)hw->cfg.start + TIGER_AUX_CTRL);
		break;
	case INF_QS1000:
	case INF_QS3000:
		hw->ipac.type = IPAC_TYPE_IPAC;
		hw->ipac.isac.off = 0x80;
		hw->isac.a.io.ale = (u32)hw->addr.start;
		hw->isac.a.io.port = (u32)hw->addr.start + 1;
		hw->isac.mode = hw->addr.mode;
		hw->hscx.a.io.ale = (u32)hw->addr.start;
		hw->hscx.a.io.port = (u32)hw->addr.start + 1;
		hw->hscx.mode = hw->addr.mode;
		break;
	case INF_NICCY:
		hw->ipac.type = IPAC_TYPE_ISAC | IPAC_TYPE_HSCX;
		hw->isac.mode = hw->addr.mode;
		hw->isac.a.io.ale = (u32)hw->addr.start + NICCY_ISAC_ALE;
		hw->isac.a.io.port = (u32)hw->addr.start + NICCY_ISAC_PORT;
		hw->hscx.mode = hw->addr.mode;
		hw->hscx.a.io.ale = (u32)hw->addr.start + NICCY_HSCX_ALE;
		hw->hscx.a.io.port = (u32)hw->addr.start + NICCY_HSCX_PORT;
		break;
	case INF_SCT_1:
		hw->ipac.type = IPAC_TYPE_IPAC;
		hw->ipac.isac.off = 0x80;
		hw->isac.a.io.ale = (u32)hw->addr.start;
		hw->isac.a.io.port = hw->isac.a.io.ale + 4;
		hw->isac.mode = hw->addr.mode;
		hw->hscx.a.io.ale = hw->isac.a.io.ale;
		hw->hscx.a.io.port = hw->isac.a.io.port;
		hw->hscx.mode = hw->addr.mode;
		break;
	case INF_SCT_2:
		hw->ipac.type = IPAC_TYPE_IPAC;
		hw->ipac.isac.off = 0x80;
		hw->isac.a.io.ale = (u32)hw->addr.start + 0x08;
		hw->isac.a.io.port = hw->isac.a.io.ale + 4;
		hw->isac.mode = hw->addr.mode;
		hw->hscx.a.io.ale = hw->isac.a.io.ale;
		hw->hscx.a.io.port = hw->isac.a.io.port;
		hw->hscx.mode = hw->addr.mode;
		break;
	case INF_SCT_3:
		hw->ipac.type = IPAC_TYPE_IPAC;
		hw->ipac.isac.off = 0x80;
		hw->isac.a.io.ale = (u32)hw->addr.start + 0x10;
		hw->isac.a.io.port = hw->isac.a.io.ale + 4;
		hw->isac.mode = hw->addr.mode;
		hw->hscx.a.io.ale = hw->isac.a.io.ale;
		hw->hscx.a.io.port = hw->isac.a.io.port;
		hw->hscx.mode = hw->addr.mode;
		break;
	case INF_SCT_4:
		hw->ipac.type = IPAC_TYPE_IPAC;
		hw->ipac.isac.off = 0x80;
		hw->isac.a.io.ale = (u32)hw->addr.start + 0x20;
		hw->isac.a.io.port = hw->isac.a.io.ale + 4;
		hw->isac.mode = hw->addr.mode;
		hw->hscx.a.io.ale = hw->isac.a.io.ale;
		hw->hscx.a.io.port = hw->isac.a.io.port;
		hw->hscx.mode = hw->addr.mode;
		break;
	case INF_GAZEL_R685:
		hw->ipac.type = IPAC_TYPE_ISAC | IPAC_TYPE_HSCX;
		hw->ipac.isac.off = 0x80;
		hw->isac.mode = hw->addr.mode;
		hw->isac.a.io.port = (u32)hw->addr.start;
		hw->hscx.mode = hw->addr.mode;
		hw->hscx.a.io.port = hw->isac.a.io.port;
		break;
	case INF_GAZEL_R753:
		hw->ipac.type = IPAC_TYPE_IPAC;
		hw->ipac.isac.off = 0x80;
		hw->isac.mode = hw->addr.mode;
		hw->isac.a.io.ale = (u32)hw->addr.start;
		hw->isac.a.io.port = (u32)hw->addr.start + GAZEL_IPAC_DATA_PORT;
		hw->hscx.mode = hw->addr.mode;
		hw->hscx.a.io.ale = hw->isac.a.io.ale;
		hw->hscx.a.io.port = hw->isac.a.io.port;
		break;
	default:
		return -EINVAL;
	}
	switch (hw->isac.mode) {
	case AM_MEMIO:
		ASSIGN_FUNC_IPAC(MIO, hw->ipac);
		break;
	case AM_IND_IO:
		ASSIGN_FUNC_IPAC(IND, hw->ipac);
		break;
	case AM_IO:
		ASSIGN_FUNC_IPAC(IO, hw->ipac);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void
release_card(struct inf_hw *card) {
	ulong	flags;
	int	i;

	spin_lock_irqsave(&card->lock, flags);
	disable_hwirq(card);
	spin_unlock_irqrestore(&card->lock, flags);
	card->ipac.isac.release(&card->ipac.isac);
	free_irq(card->irq, card);
	mISDN_unregister_device(&card->ipac.isac.dch.dev);
	release_io(card);
	write_lock_irqsave(&card_lock, flags);
	list_del(&card->list);
	write_unlock_irqrestore(&card_lock, flags);
	switch (card->ci->typ) {
	case INF_SCT_2:
	case INF_SCT_3:
	case INF_SCT_4:
		break;
	case INF_SCT_1:
		for (i = 0; i < 3; i++) {
			if (card->sc[i])
				release_card(card->sc[i]);
			card->sc[i] = NULL;
		}
	default:
		pci_disable_device(card->pdev);
		pci_set_drvdata(card->pdev, NULL);
		break;
	}
	kfree(card);
	inf_cnt--;
}

static int __devinit
setup_instance(struct inf_hw *card)
{
	int err;
	ulong flags;

	snprintf(card->name, MISDN_MAX_IDLEN - 1, "%s.%d", card->ci->name,
		inf_cnt + 1);
	write_lock_irqsave(&card_lock, flags);
	list_add_tail(&card->list, &Cards);
	write_unlock_irqrestore(&card_lock, flags);

	_set_debug(card);
	card->ipac.isac.name = card->name;
	card->ipac.name = card->name;
	card->ipac.owner = THIS_MODULE;
	spin_lock_init(&card->lock);
	card->ipac.isac.hwlock = &card->lock;
	card->ipac.hwlock = &card->lock;
	card->ipac.ctrl = (void *)&inf_ctrl;

	err = setup_io(card);
	if (err)
		goto error_setup;

	card->ipac.isac.dch.dev.Bprotocols =
		mISDNipac_init(&card->ipac, card);

	if (card->ipac.isac.dch.dev.Bprotocols == 0)
		goto error_setup;;

	err = mISDN_register_device(&card->ipac.isac.dch.dev,
		&card->pdev->dev, card->name);
	if (err)
		goto error;

	err = init_irq(card);
	if (!err)  {
		inf_cnt++;
		pr_notice("Infineon %d cards installed\n", inf_cnt);
		return 0;
	}
	mISDN_unregister_device(&card->ipac.isac.dch.dev);
error:
	card->ipac.release(&card->ipac);
error_setup:
	release_io(card);
	write_lock_irqsave(&card_lock, flags);
	list_del(&card->list);
	write_unlock_irqrestore(&card_lock, flags);
	return err;
}

static const struct inf_cinfo inf_card_info[] = {
	{
		INF_DIVA20,
		"Dialogic Diva 2.0",
		"diva20",
		AM_IND_IO, AM_NONE, 2, 0,
		&diva_irq
	},
	{
		INF_DIVA20U,
		"Dialogic Diva 2.0U",
		"diva20U",
		AM_IND_IO, AM_NONE, 2, 0,
		&diva_irq
	},
	{
		INF_DIVA201,
		"Dialogic Diva 2.01",
		"diva201",
		AM_MEMIO, AM_MEMIO, 0, 1,
		&diva20x_irq
	},
	{
		INF_DIVA202,
		"Dialogic Diva 2.02",
		"diva202",
		AM_MEMIO, AM_MEMIO, 0, 1,
		&diva20x_irq
	},
	{
		INF_SPEEDWIN,
		"Sedlbauer SpeedWin PCI",
		"speedwin",
		AM_IND_IO, AM_NONE, 0, 0,
		&tiger_irq
	},
	{
		INF_SAPHIR3,
		"HST Saphir 3",
		"saphir",
		AM_IND_IO, AM_NONE, 0, 0,
		&tiger_irq
	},
	{
		INF_QS1000,
		"Develo Microlink PCI",
		"qs1000",
		AM_IO, AM_IND_IO, 1, 3,
		&elsa_irq
	},
	{
		INF_QS3000,
		"Develo QuickStep 3000",
		"qs3000",
		AM_IO, AM_IND_IO, 1, 3,
		&elsa_irq
	},
	{
		INF_NICCY,
		"Sagem NICCY",
		"niccy",
		AM_IO, AM_IND_IO, 0, 1,
		&niccy_irq
	},
	{
		INF_SCT_1,
		"SciTel Quadro",
		"p1_scitel",
		AM_IO, AM_IND_IO, 1, 5,
		&ipac_irq
	},
	{
		INF_SCT_2,
		"SciTel Quadro",
		"p2_scitel",
		AM_NONE, AM_IND_IO, 0, 4,
		&ipac_irq
	},
	{
		INF_SCT_3,
		"SciTel Quadro",
		"p3_scitel",
		AM_NONE, AM_IND_IO, 0, 3,
		&ipac_irq
	},
	{
		INF_SCT_4,
		"SciTel Quadro",
		"p4_scitel",
		AM_NONE, AM_IND_IO, 0, 2,
		&ipac_irq
	},
	{
		INF_GAZEL_R685,
		"Gazel R685",
		"gazel685",
		AM_IO, AM_IO, 1, 2,
		&gazel_irq
	},
	{
		INF_GAZEL_R753,
		"Gazel R753",
		"gazel753",
		AM_IO, AM_IND_IO, 1, 2,
		&ipac_irq
	},
	{
		INF_NONE,
	}
};

static const struct inf_cinfo * __devinit
get_card_info(enum inf_types typ)
{
	const struct inf_cinfo *ci = inf_card_info;

	while (ci->typ != INF_NONE) {
		if (ci->typ == typ)
			return ci;
		ci++;
	}
	return NULL;
}

static int __devinit
inf_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = -ENOMEM;
	struct inf_hw *card;

	card = kzalloc(sizeof(struct inf_hw), GFP_KERNEL);
	if (!card) {
		pr_info("No memory for Infineon ISDN card\n");
		return err;
	}
	card->pdev = pdev;
	err = pci_enable_device(pdev);
	if (err) {
		kfree(card);
		return err;
	}
	card->ci = get_card_info(ent->driver_data);
	if (!card->ci) {
		pr_info("mISDN: do not have informations about adapter at %s\n",
			pci_name(pdev));
		kfree(card);
		return -EINVAL;
	} else
		pr_notice("mISDN: found adapter %s at %s\n",
			card->ci->full, pci_name(pdev));

	card->irq = pdev->irq;
	pci_set_drvdata(pdev, card);
	err = setup_instance(card);
	if (err) {
		pci_disable_device(card->pdev);
		kfree(card);
		pci_set_drvdata(pdev, NULL);
	} else if (ent->driver_data == INF_SCT_1) {
		int i;
		struct inf_hw *sc;

		for (i = 1; i < 4; i++) {
			sc = kzalloc(sizeof(struct inf_hw), GFP_KERNEL);
			if (!sc) {
				release_card(card);
				return -ENOMEM;
			}
			sc->irq = card->irq;
			sc->pdev = card->pdev;
			sc->ci = card->ci + i;
			err = setup_instance(sc);
			if (err) {
				kfree(sc);
				release_card(card);
			} else
				card->sc[i - 1] = sc;
		}
	}
	return err;
}

static void __devexit
inf_remove(struct pci_dev *pdev)
{
	struct inf_hw	*card = pci_get_drvdata(pdev);

	if (card)
		release_card(card);
	else
		pr_debug("%s: drvdata allready removed\n", __func__);
}

static struct pci_driver infineon_driver = {
	.name = "ISDN Infineon pci",
	.probe = inf_probe,
	.remove = __devexit_p(inf_remove),
	.id_table = infineon_ids,
};

static int __init
infineon_init(void)
{
	int err;

	pr_notice("Infineon ISDN Driver Rev. %s\n", INFINEON_REV);
	err = pci_register_driver(&infineon_driver);
	return err;
}

static void __exit
infineon_cleanup(void)
{
	pci_unregister_driver(&infineon_driver);
}

module_init(infineon_init);
module_exit(infineon_cleanup);
