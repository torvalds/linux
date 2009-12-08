/*
 * cpc925_edac.c, EDAC driver for IBM CPC925 Bridge and Memory Controller.
 *
 * Copyright (c) 2008 Wind River Systems, Inc.
 *
 * Authors:	Cao Qingtao <qingtao.cao@windriver.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/edac.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include "edac_core.h"
#include "edac_module.h"

#define CPC925_EDAC_REVISION	" Ver: 1.0.0 " __DATE__
#define CPC925_EDAC_MOD_STR	"cpc925_edac"

#define cpc925_printk(level, fmt, arg...) \
	edac_printk(level, "CPC925", fmt, ##arg)

#define cpc925_mc_printk(mci, level, fmt, arg...) \
	edac_mc_chipset_printk(mci, level, "CPC925", fmt, ##arg)

/*
 * CPC925 registers are of 32 bits with bit0 defined at the
 * most significant bit and bit31 at that of least significant.
 */
#define CPC925_BITS_PER_REG	32
#define CPC925_BIT(nr)		(1UL << (CPC925_BITS_PER_REG - 1 - nr))

/*
 * EDAC device names for the error detections of
 * CPU Interface and Hypertransport Link.
 */
#define CPC925_CPU_ERR_DEV	"cpu"
#define CPC925_HT_LINK_DEV	"htlink"

/* Suppose DDR Refresh cycle is 15.6 microsecond */
#define CPC925_REF_FREQ		0xFA69
#define CPC925_SCRUB_BLOCK_SIZE 64	/* bytes */
#define CPC925_NR_CSROWS	8

/*
 * All registers and bits definitions are taken from
 * "CPC925 Bridge and Memory Controller User Manual, SA14-2761-02".
 */

/*
 * CPU and Memory Controller Registers
 */
/************************************************************
 *	Processor Interface Exception Mask Register (APIMASK)
 ************************************************************/
#define REG_APIMASK_OFFSET	0x30070
enum apimask_bits {
	APIMASK_DART	= CPC925_BIT(0), /* DART Exception */
	APIMASK_ADI0	= CPC925_BIT(1), /* Handshake Error on PI0_ADI */
	APIMASK_ADI1	= CPC925_BIT(2), /* Handshake Error on PI1_ADI */
	APIMASK_STAT	= CPC925_BIT(3), /* Status Exception */
	APIMASK_DERR	= CPC925_BIT(4), /* Data Error Exception */
	APIMASK_ADRS0	= CPC925_BIT(5), /* Addressing Exception on PI0 */
	APIMASK_ADRS1	= CPC925_BIT(6), /* Addressing Exception on PI1 */
					 /* BIT(7) Reserved */
	APIMASK_ECC_UE_H = CPC925_BIT(8), /* UECC upper */
	APIMASK_ECC_CE_H = CPC925_BIT(9), /* CECC upper */
	APIMASK_ECC_UE_L = CPC925_BIT(10), /* UECC lower */
	APIMASK_ECC_CE_L = CPC925_BIT(11), /* CECC lower */

	CPU_MASK_ENABLE = (APIMASK_DART | APIMASK_ADI0 | APIMASK_ADI1 |
			   APIMASK_STAT | APIMASK_DERR | APIMASK_ADRS0 |
			   APIMASK_ADRS1),
	ECC_MASK_ENABLE = (APIMASK_ECC_UE_H | APIMASK_ECC_CE_H |
			   APIMASK_ECC_UE_L | APIMASK_ECC_CE_L),
};

/************************************************************
 *	Processor Interface Exception Register (APIEXCP)
 ************************************************************/
#define REG_APIEXCP_OFFSET	0x30060
enum apiexcp_bits {
	APIEXCP_DART	= CPC925_BIT(0), /* DART Exception */
	APIEXCP_ADI0	= CPC925_BIT(1), /* Handshake Error on PI0_ADI */
	APIEXCP_ADI1	= CPC925_BIT(2), /* Handshake Error on PI1_ADI */
	APIEXCP_STAT	= CPC925_BIT(3), /* Status Exception */
	APIEXCP_DERR	= CPC925_BIT(4), /* Data Error Exception */
	APIEXCP_ADRS0	= CPC925_BIT(5), /* Addressing Exception on PI0 */
	APIEXCP_ADRS1	= CPC925_BIT(6), /* Addressing Exception on PI1 */
					 /* BIT(7) Reserved */
	APIEXCP_ECC_UE_H = CPC925_BIT(8), /* UECC upper */
	APIEXCP_ECC_CE_H = CPC925_BIT(9), /* CECC upper */
	APIEXCP_ECC_UE_L = CPC925_BIT(10), /* UECC lower */
	APIEXCP_ECC_CE_L = CPC925_BIT(11), /* CECC lower */

	CPU_EXCP_DETECTED = (APIEXCP_DART | APIEXCP_ADI0 | APIEXCP_ADI1 |
			     APIEXCP_STAT | APIEXCP_DERR | APIEXCP_ADRS0 |
			     APIEXCP_ADRS1),
	UECC_EXCP_DETECTED = (APIEXCP_ECC_UE_H | APIEXCP_ECC_UE_L),
	CECC_EXCP_DETECTED = (APIEXCP_ECC_CE_H | APIEXCP_ECC_CE_L),
	ECC_EXCP_DETECTED = (UECC_EXCP_DETECTED | CECC_EXCP_DETECTED),
};

/************************************************************
 *	Memory Bus Configuration Register (MBCR)
************************************************************/
#define REG_MBCR_OFFSET		0x2190
#define MBCR_64BITCFG_SHIFT	23
#define MBCR_64BITCFG_MASK	(1UL << MBCR_64BITCFG_SHIFT)
#define MBCR_64BITBUS_SHIFT	22
#define MBCR_64BITBUS_MASK	(1UL << MBCR_64BITBUS_SHIFT)

/************************************************************
 *	Memory Bank Mode Register (MBMR)
************************************************************/
#define REG_MBMR_OFFSET		0x21C0
#define MBMR_MODE_MAX_VALUE	0xF
#define MBMR_MODE_SHIFT		25
#define MBMR_MODE_MASK		(MBMR_MODE_MAX_VALUE << MBMR_MODE_SHIFT)
#define MBMR_BBA_SHIFT		24
#define MBMR_BBA_MASK		(1UL << MBMR_BBA_SHIFT)

/************************************************************
 *	Memory Bank Boundary Address Register (MBBAR)
 ************************************************************/
#define REG_MBBAR_OFFSET	0x21D0
#define MBBAR_BBA_MAX_VALUE	0xFF
#define MBBAR_BBA_SHIFT		24
#define MBBAR_BBA_MASK		(MBBAR_BBA_MAX_VALUE << MBBAR_BBA_SHIFT)

/************************************************************
 *	Memory Scrub Control Register (MSCR)
 ************************************************************/
#define REG_MSCR_OFFSET		0x2400
#define MSCR_SCRUB_MOD_MASK	0xC0000000 /* scrub_mod - bit0:1*/
#define MSCR_BACKGR_SCRUB	0x40000000 /* 01 */
#define MSCR_SI_SHIFT		16 	/* si - bit8:15*/
#define MSCR_SI_MAX_VALUE	0xFF
#define MSCR_SI_MASK		(MSCR_SI_MAX_VALUE << MSCR_SI_SHIFT)

/************************************************************
 *	Memory Scrub Range Start Register (MSRSR)
 ************************************************************/
#define REG_MSRSR_OFFSET	0x2410

/************************************************************
 *	Memory Scrub Range End Register (MSRER)
 ************************************************************/
#define REG_MSRER_OFFSET	0x2420

/************************************************************
 *	Memory Scrub Pattern Register (MSPR)
 ************************************************************/
#define REG_MSPR_OFFSET		0x2430

/************************************************************
 *	Memory Check Control Register (MCCR)
 ************************************************************/
#define REG_MCCR_OFFSET		0x2440
enum mccr_bits {
	MCCR_ECC_EN	= CPC925_BIT(0), /* ECC high and low check */
};

/************************************************************
 *	Memory Check Range End Register (MCRER)
 ************************************************************/
#define REG_MCRER_OFFSET	0x2450

/************************************************************
 *	Memory Error Address Register (MEAR)
 ************************************************************/
#define REG_MEAR_OFFSET		0x2460
#define MEAR_BCNT_MAX_VALUE	0x3
#define MEAR_BCNT_SHIFT		30
#define MEAR_BCNT_MASK		(MEAR_BCNT_MAX_VALUE << MEAR_BCNT_SHIFT)
#define MEAR_RANK_MAX_VALUE	0x7
#define MEAR_RANK_SHIFT		27
#define MEAR_RANK_MASK		(MEAR_RANK_MAX_VALUE << MEAR_RANK_SHIFT)
#define MEAR_COL_MAX_VALUE	0x7FF
#define MEAR_COL_SHIFT		16
#define MEAR_COL_MASK		(MEAR_COL_MAX_VALUE << MEAR_COL_SHIFT)
#define MEAR_BANK_MAX_VALUE	0x3
#define MEAR_BANK_SHIFT		14
#define MEAR_BANK_MASK		(MEAR_BANK_MAX_VALUE << MEAR_BANK_SHIFT)
#define MEAR_ROW_MASK		0x00003FFF

/************************************************************
 *	Memory Error Syndrome Register (MESR)
 ************************************************************/
#define REG_MESR_OFFSET		0x2470
#define MESR_ECC_SYN_H_MASK	0xFF00
#define MESR_ECC_SYN_L_MASK	0x00FF

/************************************************************
 *	Memory Mode Control Register (MMCR)
 ************************************************************/
#define REG_MMCR_OFFSET		0x2500
enum mmcr_bits {
	MMCR_REG_DIMM_MODE = CPC925_BIT(3),
};

/*
 * HyperTransport Link Registers
 */
/************************************************************
 *  Error Handling/Enumeration Scratch Pad Register (ERRCTRL)
 ************************************************************/
#define REG_ERRCTRL_OFFSET	0x70140
enum errctrl_bits {			 /* nonfatal interrupts for */
	ERRCTRL_SERR_NF	= CPC925_BIT(0), /* system error */
	ERRCTRL_CRC_NF	= CPC925_BIT(1), /* CRC error */
	ERRCTRL_RSP_NF	= CPC925_BIT(2), /* Response error */
	ERRCTRL_EOC_NF	= CPC925_BIT(3), /* End-Of-Chain error */
	ERRCTRL_OVF_NF	= CPC925_BIT(4), /* Overflow error */
	ERRCTRL_PROT_NF	= CPC925_BIT(5), /* Protocol error */

	ERRCTRL_RSP_ERR	= CPC925_BIT(6), /* Response error received */
	ERRCTRL_CHN_FAL = CPC925_BIT(7), /* Sync flooding detected */

	HT_ERRCTRL_ENABLE = (ERRCTRL_SERR_NF | ERRCTRL_CRC_NF |
			     ERRCTRL_RSP_NF | ERRCTRL_EOC_NF |
			     ERRCTRL_OVF_NF | ERRCTRL_PROT_NF),
	HT_ERRCTRL_DETECTED = (ERRCTRL_RSP_ERR | ERRCTRL_CHN_FAL),
};

/************************************************************
 *  Link Configuration and Link Control Register (LINKCTRL)
 ************************************************************/
#define REG_LINKCTRL_OFFSET	0x70110
enum linkctrl_bits {
	LINKCTRL_CRC_ERR	= (CPC925_BIT(22) | CPC925_BIT(23)),
	LINKCTRL_LINK_FAIL	= CPC925_BIT(27),

	HT_LINKCTRL_DETECTED	= (LINKCTRL_CRC_ERR | LINKCTRL_LINK_FAIL),
};

/************************************************************
 *  Link FreqCap/Error/Freq/Revision ID Register (LINKERR)
 ************************************************************/
#define REG_LINKERR_OFFSET	0x70120
enum linkerr_bits {
	LINKERR_EOC_ERR		= CPC925_BIT(17), /* End-Of-Chain error */
	LINKERR_OVF_ERR		= CPC925_BIT(18), /* Receive Buffer Overflow */
	LINKERR_PROT_ERR	= CPC925_BIT(19), /* Protocol error */

	HT_LINKERR_DETECTED	= (LINKERR_EOC_ERR | LINKERR_OVF_ERR |
				   LINKERR_PROT_ERR),
};

/************************************************************
 *	Bridge Control Register (BRGCTRL)
 ************************************************************/
#define REG_BRGCTRL_OFFSET	0x70300
enum brgctrl_bits {
	BRGCTRL_DETSERR = CPC925_BIT(0), /* SERR on Secondary Bus */
	BRGCTRL_SECBUSRESET = CPC925_BIT(9), /* Secondary Bus Reset */
};

/* Private structure for edac memory controller */
struct cpc925_mc_pdata {
	void __iomem *vbase;
	unsigned long total_mem;
	const char *name;
	int edac_idx;
};

/* Private structure for common edac device */
struct cpc925_dev_info {
	void __iomem *vbase;
	struct platform_device *pdev;
	char *ctl_name;
	int edac_idx;
	struct edac_device_ctl_info *edac_dev;
	void (*init)(struct cpc925_dev_info *dev_info);
	void (*exit)(struct cpc925_dev_info *dev_info);
	void (*check)(struct edac_device_ctl_info *edac_dev);
};

/* Get total memory size from Open Firmware DTB */
static void get_total_mem(struct cpc925_mc_pdata *pdata)
{
	struct device_node *np = NULL;
	const unsigned int *reg, *reg_end;
	int len, sw, aw;
	unsigned long start, size;

	np = of_find_node_by_type(NULL, "memory");
	if (!np)
		return;

	aw = of_n_addr_cells(np);
	sw = of_n_size_cells(np);
	reg = (const unsigned int *)of_get_property(np, "reg", &len);
	reg_end = reg + len/4;

	pdata->total_mem = 0;
	do {
		start = of_read_number(reg, aw);
		reg += aw;
		size = of_read_number(reg, sw);
		reg += sw;
		debugf1("%s: start 0x%lx, size 0x%lx\n", __func__,
			start, size);
		pdata->total_mem += size;
	} while (reg < reg_end);

	of_node_put(np);
	debugf0("%s: total_mem 0x%lx\n", __func__, pdata->total_mem);
}

static void cpc925_init_csrows(struct mem_ctl_info *mci)
{
	struct cpc925_mc_pdata *pdata = mci->pvt_info;
	struct csrow_info *csrow;
	int index;
	u32 mbmr, mbbar, bba;
	unsigned long row_size, last_nr_pages = 0;

	get_total_mem(pdata);

	for (index = 0; index < mci->nr_csrows; index++) {
		mbmr = __raw_readl(pdata->vbase + REG_MBMR_OFFSET +
				   0x20 * index);
		mbbar = __raw_readl(pdata->vbase + REG_MBBAR_OFFSET +
				   0x20 + index);
		bba = (((mbmr & MBMR_BBA_MASK) >> MBMR_BBA_SHIFT) << 8) |
		       ((mbbar & MBBAR_BBA_MASK) >> MBBAR_BBA_SHIFT);

		if (bba == 0)
			continue; /* not populated */

		csrow = &mci->csrows[index];

		row_size = bba * (1UL << 28);	/* 256M */
		csrow->first_page = last_nr_pages;
		csrow->nr_pages = row_size >> PAGE_SHIFT;
		csrow->last_page = csrow->first_page + csrow->nr_pages - 1;
		last_nr_pages = csrow->last_page + 1;

		csrow->mtype = MEM_RDDR;
		csrow->edac_mode = EDAC_SECDED;

		switch (csrow->nr_channels) {
		case 1: /* Single channel */
			csrow->grain = 32; /* four-beat burst of 32 bytes */
			break;
		case 2: /* Dual channel */
		default:
			csrow->grain = 64; /* four-beat burst of 64 bytes */
			break;
		}

		switch ((mbmr & MBMR_MODE_MASK) >> MBMR_MODE_SHIFT) {
		case 6: /* 0110, no way to differentiate X8 VS X16 */
		case 5:	/* 0101 */
		case 8: /* 1000 */
			csrow->dtype = DEV_X16;
			break;
		case 7: /* 0111 */
		case 9: /* 1001 */
			csrow->dtype = DEV_X8;
			break;
		default:
			csrow->dtype = DEV_UNKNOWN;
			break;
		}
	}
}

/* Enable memory controller ECC detection */
static void cpc925_mc_init(struct mem_ctl_info *mci)
{
	struct cpc925_mc_pdata *pdata = mci->pvt_info;
	u32 apimask;
	u32 mccr;

	/* Enable various ECC error exceptions */
	apimask = __raw_readl(pdata->vbase + REG_APIMASK_OFFSET);
	if ((apimask & ECC_MASK_ENABLE) == 0) {
		apimask |= ECC_MASK_ENABLE;
		__raw_writel(apimask, pdata->vbase + REG_APIMASK_OFFSET);
	}

	/* Enable ECC detection */
	mccr = __raw_readl(pdata->vbase + REG_MCCR_OFFSET);
	if ((mccr & MCCR_ECC_EN) == 0) {
		mccr |= MCCR_ECC_EN;
		__raw_writel(mccr, pdata->vbase + REG_MCCR_OFFSET);
	}
}

/* Disable memory controller ECC detection */
static void cpc925_mc_exit(struct mem_ctl_info *mci)
{
	/*
	 * WARNING:
	 * We are supposed to clear the ECC error detection bits,
	 * and it will be no problem to do so. However, once they
	 * are cleared here if we want to re-install CPC925 EDAC
	 * module later, setting them up in cpc925_mc_init() will
	 * trigger machine check exception.
	 * Also, it's ok to leave ECC error detection bits enabled,
	 * since they are reset to 1 by default or by boot loader.
	 */

	return;
}

/*
 * Revert DDR column/row/bank addresses into page frame number and
 * offset in page.
 *
 * Suppose memory mode is 0x0111(128-bit mode, identical DIMM pairs),
 * physical address(PA) bits to column address(CA) bits mappings are:
 * CA	0   1   2   3   4   5   6   7   8   9   10
 * PA	59  58  57  56  55  54  53  52  51  50  49
 *
 * physical address(PA) bits to bank address(BA) bits mappings are:
 * BA	0   1
 * PA	43  44
 *
 * physical address(PA) bits to row address(RA) bits mappings are:
 * RA	0   1   2   3   4   5   6   7   8   9   10   11   12
 * PA	36  35  34  48  47  46  45  40  41  42  39   38   37
 */
static void cpc925_mc_get_pfn(struct mem_ctl_info *mci, u32 mear,
		unsigned long *pfn, unsigned long *offset, int *csrow)
{
	u32 bcnt, rank, col, bank, row;
	u32 c;
	unsigned long pa;
	int i;

	bcnt = (mear & MEAR_BCNT_MASK) >> MEAR_BCNT_SHIFT;
	rank = (mear & MEAR_RANK_MASK) >> MEAR_RANK_SHIFT;
	col = (mear & MEAR_COL_MASK) >> MEAR_COL_SHIFT;
	bank = (mear & MEAR_BANK_MASK) >> MEAR_BANK_SHIFT;
	row = mear & MEAR_ROW_MASK;

	*csrow = rank;

#ifdef CONFIG_EDAC_DEBUG
	if (mci->csrows[rank].first_page == 0) {
		cpc925_mc_printk(mci, KERN_ERR, "ECC occurs in a "
			"non-populated csrow, broken hardware?\n");
		return;
	}
#endif

	/* Revert csrow number */
	pa = mci->csrows[rank].first_page << PAGE_SHIFT;

	/* Revert column address */
	col += bcnt;
	for (i = 0; i < 11; i++) {
		c = col & 0x1;
		col >>= 1;
		pa |= c << (14 - i);
	}

	/* Revert bank address */
	pa |= bank << 19;

	/* Revert row address, in 4 steps */
	for (i = 0; i < 3; i++) {
		c = row & 0x1;
		row >>= 1;
		pa |= c << (26 - i);
	}

	for (i = 0; i < 3; i++) {
		c = row & 0x1;
		row >>= 1;
		pa |= c << (21 + i);
	}

	for (i = 0; i < 4; i++) {
		c = row & 0x1;
		row >>= 1;
		pa |= c << (18 - i);
	}

	for (i = 0; i < 3; i++) {
		c = row & 0x1;
		row >>= 1;
		pa |= c << (29 - i);
	}

	*offset = pa & (PAGE_SIZE - 1);
	*pfn = pa >> PAGE_SHIFT;

	debugf0("%s: ECC physical address 0x%lx\n", __func__, pa);
}

static int cpc925_mc_find_channel(struct mem_ctl_info *mci, u16 syndrome)
{
	if ((syndrome & MESR_ECC_SYN_H_MASK) == 0)
		return 0;

	if ((syndrome & MESR_ECC_SYN_L_MASK) == 0)
		return 1;

	cpc925_mc_printk(mci, KERN_INFO, "Unexpected syndrome value: 0x%x\n",
			 syndrome);
	return 1;
}

/* Check memory controller registers for ECC errors */
static void cpc925_mc_check(struct mem_ctl_info *mci)
{
	struct cpc925_mc_pdata *pdata = mci->pvt_info;
	u32 apiexcp;
	u32 mear;
	u32 mesr;
	u16 syndrome;
	unsigned long pfn = 0, offset = 0;
	int csrow = 0, channel = 0;

	/* APIEXCP is cleared when read */
	apiexcp = __raw_readl(pdata->vbase + REG_APIEXCP_OFFSET);
	if ((apiexcp & ECC_EXCP_DETECTED) == 0)
		return;

	mesr = __raw_readl(pdata->vbase + REG_MESR_OFFSET);
	syndrome = mesr | (MESR_ECC_SYN_H_MASK | MESR_ECC_SYN_L_MASK);

	mear = __raw_readl(pdata->vbase + REG_MEAR_OFFSET);

	/* Revert column/row addresses into page frame number, etc */
	cpc925_mc_get_pfn(mci, mear, &pfn, &offset, &csrow);

	if (apiexcp & CECC_EXCP_DETECTED) {
		cpc925_mc_printk(mci, KERN_INFO, "DRAM CECC Fault\n");
		channel = cpc925_mc_find_channel(mci, syndrome);
		edac_mc_handle_ce(mci, pfn, offset, syndrome,
				  csrow, channel, mci->ctl_name);
	}

	if (apiexcp & UECC_EXCP_DETECTED) {
		cpc925_mc_printk(mci, KERN_INFO, "DRAM UECC Fault\n");
		edac_mc_handle_ue(mci, pfn, offset, csrow, mci->ctl_name);
	}

	cpc925_mc_printk(mci, KERN_INFO, "Dump registers:\n");
	cpc925_mc_printk(mci, KERN_INFO, "APIMASK		0x%08x\n",
		__raw_readl(pdata->vbase + REG_APIMASK_OFFSET));
	cpc925_mc_printk(mci, KERN_INFO, "APIEXCP		0x%08x\n",
		apiexcp);
	cpc925_mc_printk(mci, KERN_INFO, "Mem Scrub Ctrl	0x%08x\n",
		__raw_readl(pdata->vbase + REG_MSCR_OFFSET));
	cpc925_mc_printk(mci, KERN_INFO, "Mem Scrub Rge Start	0x%08x\n",
		__raw_readl(pdata->vbase + REG_MSRSR_OFFSET));
	cpc925_mc_printk(mci, KERN_INFO, "Mem Scrub Rge End	0x%08x\n",
		__raw_readl(pdata->vbase + REG_MSRER_OFFSET));
	cpc925_mc_printk(mci, KERN_INFO, "Mem Scrub Pattern	0x%08x\n",
		__raw_readl(pdata->vbase + REG_MSPR_OFFSET));
	cpc925_mc_printk(mci, KERN_INFO, "Mem Chk Ctrl		0x%08x\n",
		__raw_readl(pdata->vbase + REG_MCCR_OFFSET));
	cpc925_mc_printk(mci, KERN_INFO, "Mem Chk Rge End	0x%08x\n",
		__raw_readl(pdata->vbase + REG_MCRER_OFFSET));
	cpc925_mc_printk(mci, KERN_INFO, "Mem Err Address	0x%08x\n",
		mesr);
	cpc925_mc_printk(mci, KERN_INFO, "Mem Err Syndrome	0x%08x\n",
		syndrome);
}

/******************** CPU err device********************************/
/* Enable CPU Errors detection */
static void cpc925_cpu_init(struct cpc925_dev_info *dev_info)
{
	u32 apimask;

	apimask = __raw_readl(dev_info->vbase + REG_APIMASK_OFFSET);
	if ((apimask & CPU_MASK_ENABLE) == 0) {
		apimask |= CPU_MASK_ENABLE;
		__raw_writel(apimask, dev_info->vbase + REG_APIMASK_OFFSET);
	}
}

/* Disable CPU Errors detection */
static void cpc925_cpu_exit(struct cpc925_dev_info *dev_info)
{
	/*
	 * WARNING:
	 * We are supposed to clear the CPU error detection bits,
	 * and it will be no problem to do so. However, once they
	 * are cleared here if we want to re-install CPC925 EDAC
	 * module later, setting them up in cpc925_cpu_init() will
	 * trigger machine check exception.
	 * Also, it's ok to leave CPU error detection bits enabled,
	 * since they are reset to 1 by default.
	 */

	return;
}

/* Check for CPU Errors */
static void cpc925_cpu_check(struct edac_device_ctl_info *edac_dev)
{
	struct cpc925_dev_info *dev_info = edac_dev->pvt_info;
	u32 apiexcp;
	u32 apimask;

	/* APIEXCP is cleared when read */
	apiexcp = __raw_readl(dev_info->vbase + REG_APIEXCP_OFFSET);
	if ((apiexcp & CPU_EXCP_DETECTED) == 0)
		return;

	apimask = __raw_readl(dev_info->vbase + REG_APIMASK_OFFSET);
	cpc925_printk(KERN_INFO, "Processor Interface Fault\n"
				 "Processor Interface register dump:\n");
	cpc925_printk(KERN_INFO, "APIMASK		0x%08x\n", apimask);
	cpc925_printk(KERN_INFO, "APIEXCP		0x%08x\n", apiexcp);

	edac_device_handle_ue(edac_dev, 0, 0, edac_dev->ctl_name);
}

/******************** HT Link err device****************************/
/* Enable HyperTransport Link Error detection */
static void cpc925_htlink_init(struct cpc925_dev_info *dev_info)
{
	u32 ht_errctrl;

	ht_errctrl = __raw_readl(dev_info->vbase + REG_ERRCTRL_OFFSET);
	if ((ht_errctrl & HT_ERRCTRL_ENABLE) == 0) {
		ht_errctrl |= HT_ERRCTRL_ENABLE;
		__raw_writel(ht_errctrl, dev_info->vbase + REG_ERRCTRL_OFFSET);
	}
}

/* Disable HyperTransport Link Error detection */
static void cpc925_htlink_exit(struct cpc925_dev_info *dev_info)
{
	u32 ht_errctrl;

	ht_errctrl = __raw_readl(dev_info->vbase + REG_ERRCTRL_OFFSET);
	ht_errctrl &= ~HT_ERRCTRL_ENABLE;
	__raw_writel(ht_errctrl, dev_info->vbase + REG_ERRCTRL_OFFSET);
}

/* Check for HyperTransport Link errors */
static void cpc925_htlink_check(struct edac_device_ctl_info *edac_dev)
{
	struct cpc925_dev_info *dev_info = edac_dev->pvt_info;
	u32 brgctrl = __raw_readl(dev_info->vbase + REG_BRGCTRL_OFFSET);
	u32 linkctrl = __raw_readl(dev_info->vbase + REG_LINKCTRL_OFFSET);
	u32 errctrl = __raw_readl(dev_info->vbase + REG_ERRCTRL_OFFSET);
	u32 linkerr = __raw_readl(dev_info->vbase + REG_LINKERR_OFFSET);

	if (!((brgctrl & BRGCTRL_DETSERR) ||
	      (linkctrl & HT_LINKCTRL_DETECTED) ||
	      (errctrl & HT_ERRCTRL_DETECTED) ||
	      (linkerr & HT_LINKERR_DETECTED)))
		return;

	cpc925_printk(KERN_INFO, "HT Link Fault\n"
				 "HT register dump:\n");
	cpc925_printk(KERN_INFO, "Bridge Ctrl			0x%08x\n",
		      brgctrl);
	cpc925_printk(KERN_INFO, "Link Config Ctrl		0x%08x\n",
		      linkctrl);
	cpc925_printk(KERN_INFO, "Error Enum and Ctrl		0x%08x\n",
		      errctrl);
	cpc925_printk(KERN_INFO, "Link Error			0x%08x\n",
		      linkerr);

	/* Clear by write 1 */
	if (brgctrl & BRGCTRL_DETSERR)
		__raw_writel(BRGCTRL_DETSERR,
				dev_info->vbase + REG_BRGCTRL_OFFSET);

	if (linkctrl & HT_LINKCTRL_DETECTED)
		__raw_writel(HT_LINKCTRL_DETECTED,
				dev_info->vbase + REG_LINKCTRL_OFFSET);

	/* Initiate Secondary Bus Reset to clear the chain failure */
	if (errctrl & ERRCTRL_CHN_FAL)
		__raw_writel(BRGCTRL_SECBUSRESET,
				dev_info->vbase + REG_BRGCTRL_OFFSET);

	if (errctrl & ERRCTRL_RSP_ERR)
		__raw_writel(ERRCTRL_RSP_ERR,
				dev_info->vbase + REG_ERRCTRL_OFFSET);

	if (linkerr & HT_LINKERR_DETECTED)
		__raw_writel(HT_LINKERR_DETECTED,
				dev_info->vbase + REG_LINKERR_OFFSET);

	edac_device_handle_ce(edac_dev, 0, 0, edac_dev->ctl_name);
}

static struct cpc925_dev_info cpc925_devs[] = {
	{
	.ctl_name = CPC925_CPU_ERR_DEV,
	.init = cpc925_cpu_init,
	.exit = cpc925_cpu_exit,
	.check = cpc925_cpu_check,
	},
	{
	.ctl_name = CPC925_HT_LINK_DEV,
	.init = cpc925_htlink_init,
	.exit = cpc925_htlink_exit,
	.check = cpc925_htlink_check,
	},
	{0}, /* Terminated by NULL */
};

/*
 * Add CPU Err detection and HyperTransport Link Err detection
 * as common "edac_device", they have no corresponding device
 * nodes in the Open Firmware DTB and we have to add platform
 * devices for them. Also, they will share the MMIO with that
 * of memory controller.
 */
static void cpc925_add_edac_devices(void __iomem *vbase)
{
	struct cpc925_dev_info *dev_info;

	if (!vbase) {
		cpc925_printk(KERN_ERR, "MMIO not established yet\n");
		return;
	}

	for (dev_info = &cpc925_devs[0]; dev_info->init; dev_info++) {
		dev_info->vbase = vbase;
		dev_info->pdev = platform_device_register_simple(
					dev_info->ctl_name, 0, NULL, 0);
		if (IS_ERR(dev_info->pdev)) {
			cpc925_printk(KERN_ERR,
				"Can't register platform device for %s\n",
				dev_info->ctl_name);
			continue;
		}

		/*
		 * Don't have to allocate private structure but
		 * make use of cpc925_devs[] instead.
		 */
		dev_info->edac_idx = edac_device_alloc_index();
		dev_info->edac_dev =
			edac_device_alloc_ctl_info(0, dev_info->ctl_name,
				1, NULL, 0, 0, NULL, 0, dev_info->edac_idx);
		if (!dev_info->edac_dev) {
			cpc925_printk(KERN_ERR, "No memory for edac device\n");
			goto err1;
		}

		dev_info->edac_dev->pvt_info = dev_info;
		dev_info->edac_dev->dev = &dev_info->pdev->dev;
		dev_info->edac_dev->ctl_name = dev_info->ctl_name;
		dev_info->edac_dev->mod_name = CPC925_EDAC_MOD_STR;
		dev_info->edac_dev->dev_name = dev_name(&dev_info->pdev->dev);

		if (edac_op_state == EDAC_OPSTATE_POLL)
			dev_info->edac_dev->edac_check = dev_info->check;

		if (dev_info->init)
			dev_info->init(dev_info);

		if (edac_device_add_device(dev_info->edac_dev) > 0) {
			cpc925_printk(KERN_ERR,
				"Unable to add edac device for %s\n",
				dev_info->ctl_name);
			goto err2;
		}

		debugf0("%s: Successfully added edac device for %s\n",
			__func__, dev_info->ctl_name);

		continue;

err2:
		if (dev_info->exit)
			dev_info->exit(dev_info);
		edac_device_free_ctl_info(dev_info->edac_dev);
err1:
		platform_device_unregister(dev_info->pdev);
	}
}

/*
 * Delete the common "edac_device" for CPU Err Detection
 * and HyperTransport Link Err Detection
 */
static void cpc925_del_edac_devices(void)
{
	struct cpc925_dev_info *dev_info;

	for (dev_info = &cpc925_devs[0]; dev_info->init; dev_info++) {
		if (dev_info->edac_dev) {
			edac_device_del_device(dev_info->edac_dev->dev);
			edac_device_free_ctl_info(dev_info->edac_dev);
			platform_device_unregister(dev_info->pdev);
		}

		if (dev_info->exit)
			dev_info->exit(dev_info);

		debugf0("%s: Successfully deleted edac device for %s\n",
			__func__, dev_info->ctl_name);
	}
}

/* Convert current back-ground scrub rate into byte/sec bandwith */
static int cpc925_get_sdram_scrub_rate(struct mem_ctl_info *mci, u32 *bw)
{
	struct cpc925_mc_pdata *pdata = mci->pvt_info;
	u32 mscr;
	u8 si;

	mscr = __raw_readl(pdata->vbase + REG_MSCR_OFFSET);
	si = (mscr & MSCR_SI_MASK) >> MSCR_SI_SHIFT;

	debugf0("%s, Mem Scrub Ctrl Register 0x%x\n", __func__, mscr);

	if (((mscr & MSCR_SCRUB_MOD_MASK) != MSCR_BACKGR_SCRUB) ||
	    (si == 0)) {
		cpc925_mc_printk(mci, KERN_INFO, "Scrub mode not enabled\n");
		*bw = 0;
	} else
		*bw = CPC925_SCRUB_BLOCK_SIZE * 0xFA67 / si;

	return 0;
}

/* Return 0 for single channel; 1 for dual channel */
static int cpc925_mc_get_channels(void __iomem *vbase)
{
	int dual = 0;
	u32 mbcr;

	mbcr = __raw_readl(vbase + REG_MBCR_OFFSET);

	/*
	 * Dual channel only when 128-bit wide physical bus
	 * and 128-bit configuration.
	 */
	if (((mbcr & MBCR_64BITCFG_MASK) == 0) &&
	    ((mbcr & MBCR_64BITBUS_MASK) == 0))
		dual = 1;

	debugf0("%s: %s channel\n", __func__,
		(dual > 0) ? "Dual" : "Single");

	return dual;
}

static int __devinit cpc925_probe(struct platform_device *pdev)
{
	static int edac_mc_idx;
	struct mem_ctl_info *mci;
	void __iomem *vbase;
	struct cpc925_mc_pdata *pdata;
	struct resource *r;
	int res = 0, nr_channels;

	debugf0("%s: %s platform device found!\n", __func__, pdev->name);

	if (!devres_open_group(&pdev->dev, cpc925_probe, GFP_KERNEL)) {
		res = -ENOMEM;
		goto out;
	}

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		cpc925_printk(KERN_ERR, "Unable to get resource\n");
		res = -ENOENT;
		goto err1;
	}

	if (!devm_request_mem_region(&pdev->dev,
				     r->start,
				     resource_size(r),
				     pdev->name)) {
		cpc925_printk(KERN_ERR, "Unable to request mem region\n");
		res = -EBUSY;
		goto err1;
	}

	vbase = devm_ioremap(&pdev->dev, r->start, resource_size(r));
	if (!vbase) {
		cpc925_printk(KERN_ERR, "Unable to ioremap device\n");
		res = -ENOMEM;
		goto err2;
	}

	nr_channels = cpc925_mc_get_channels(vbase);
	mci = edac_mc_alloc(sizeof(struct cpc925_mc_pdata),
			CPC925_NR_CSROWS, nr_channels + 1, edac_mc_idx);
	if (!mci) {
		cpc925_printk(KERN_ERR, "No memory for mem_ctl_info\n");
		res = -ENOMEM;
		goto err2;
	}

	pdata = mci->pvt_info;
	pdata->vbase = vbase;
	pdata->edac_idx = edac_mc_idx++;
	pdata->name = pdev->name;

	mci->dev = &pdev->dev;
	platform_set_drvdata(pdev, mci);
	mci->dev_name = dev_name(&pdev->dev);
	mci->mtype_cap = MEM_FLAG_RDDR | MEM_FLAG_DDR;
	mci->edac_ctl_cap = EDAC_FLAG_NONE | EDAC_FLAG_SECDED;
	mci->edac_cap = EDAC_FLAG_SECDED;
	mci->mod_name = CPC925_EDAC_MOD_STR;
	mci->mod_ver = CPC925_EDAC_REVISION;
	mci->ctl_name = pdev->name;

	if (edac_op_state == EDAC_OPSTATE_POLL)
		mci->edac_check = cpc925_mc_check;

	mci->ctl_page_to_phys = NULL;
	mci->scrub_mode = SCRUB_SW_SRC;
	mci->set_sdram_scrub_rate = NULL;
	mci->get_sdram_scrub_rate = cpc925_get_sdram_scrub_rate;

	cpc925_init_csrows(mci);

	/* Setup memory controller registers */
	cpc925_mc_init(mci);

	if (edac_mc_add_mc(mci) > 0) {
		cpc925_mc_printk(mci, KERN_ERR, "Failed edac_mc_add_mc()\n");
		goto err3;
	}

	cpc925_add_edac_devices(vbase);

	/* get this far and it's successful */
	debugf0("%s: success\n", __func__);

	res = 0;
	goto out;

err3:
	cpc925_mc_exit(mci);
	edac_mc_free(mci);
err2:
	devm_release_mem_region(&pdev->dev, r->start, resource_size(r));
err1:
	devres_release_group(&pdev->dev, cpc925_probe);
out:
	return res;
}

static int cpc925_remove(struct platform_device *pdev)
{
	struct mem_ctl_info *mci = platform_get_drvdata(pdev);

	/*
	 * Delete common edac devices before edac mc, because
	 * the former share the MMIO of the latter.
	 */
	cpc925_del_edac_devices();
	cpc925_mc_exit(mci);

	edac_mc_del_mc(&pdev->dev);
	edac_mc_free(mci);

	return 0;
}

static struct platform_driver cpc925_edac_driver = {
	.probe = cpc925_probe,
	.remove = cpc925_remove,
	.driver = {
		   .name = "cpc925_edac",
	}
};

static int __init cpc925_edac_init(void)
{
	int ret = 0;

	printk(KERN_INFO "IBM CPC925 EDAC driver " CPC925_EDAC_REVISION "\n");
	printk(KERN_INFO "\t(c) 2008 Wind River Systems, Inc\n");

	/* Only support POLL mode so far */
	edac_op_state = EDAC_OPSTATE_POLL;

	ret = platform_driver_register(&cpc925_edac_driver);
	if (ret) {
		printk(KERN_WARNING "Failed to register %s\n",
			CPC925_EDAC_MOD_STR);
	}

	return ret;
}

static void __exit cpc925_edac_exit(void)
{
	platform_driver_unregister(&cpc925_edac_driver);
}

module_init(cpc925_edac_init);
module_exit(cpc925_edac_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cao Qingtao <qingtao.cao@windriver.com>");
MODULE_DESCRIPTION("IBM CPC925 Bridge and MC EDAC kernel module");
