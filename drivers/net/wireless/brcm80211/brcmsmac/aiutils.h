/*
 * Copyright (c) 2011 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_BRCM_AIUTILS_H_
#define	_BRCM_AIUTILS_H_

#include <linux/bcma/bcma.h>

#include "types.h"

/*
 * SOC Interconnect Address Map.
 * All regions may not exist on all chips.
 */
/* each core gets 4Kbytes for registers */
#define SI_CORE_SIZE		0x1000
/*
 * Max cores (this is arbitrary, for software
 * convenience and could be changed if we
 * make any larger chips
 */
#define	SI_MAXCORES		16

/* Client Mode sb2pcitranslation2 size in bytes */
#define SI_PCI_DMA_SZ		0x40000000

/* PCIE Client Mode sb2pcitranslation2 (2 ZettaBytes), high 32 bits */
#define SI_PCIE_DMA_H32		0x80000000

/* chipcommon being the first core: */
#define	SI_CC_IDX		0

/* SOC Interconnect types (aka chip types) */
#define	SOCI_AI			1

/* A register that is common to all cores to
 * communicate w/PMU regarding clock control.
 */
#define SI_CLK_CTL_ST		0x1e0	/* clock control and status */

/* clk_ctl_st register */
#define	CCS_FORCEALP		0x00000001	/* force ALP request */
#define	CCS_FORCEHT		0x00000002	/* force HT request */
#define	CCS_FORCEILP		0x00000004	/* force ILP request */
#define	CCS_ALPAREQ		0x00000008	/* ALP Avail Request */
#define	CCS_HTAREQ		0x00000010	/* HT Avail Request */
#define	CCS_FORCEHWREQOFF	0x00000020	/* Force HW Clock Request Off */
#define CCS_ERSRC_REQ_MASK	0x00000700	/* external resource requests */
#define CCS_ERSRC_REQ_SHIFT	8
#define	CCS_ALPAVAIL		0x00010000	/* ALP is available */
#define	CCS_HTAVAIL		0x00020000	/* HT is available */
#define CCS_BP_ON_APL		0x00040000	/* RO: running on ALP clock */
#define CCS_BP_ON_HT		0x00080000	/* RO: running on HT clock */
#define CCS_ERSRC_STS_MASK	0x07000000	/* external resource status */
#define CCS_ERSRC_STS_SHIFT	24

/* HT avail in chipc and pcmcia on 4328a0 */
#define	CCS0_HTAVAIL		0x00010000
/* ALP avail in chipc and pcmcia on 4328a0 */
#define	CCS0_ALPAVAIL		0x00020000

/* Not really related to SOC Interconnect, but a couple of software
 * conventions for the use the flash space:
 */

/* Minumum amount of flash we support */
#define FLASH_MIN		0x00020000	/* Minimum flash size */

#define	CC_SROM_OTP		0x800	/* SROM/OTP address space */

/* gpiotimerval */
#define GPIO_ONTIME_SHIFT	16

/* Fields in clkdiv */
#define	CLKD_OTP		0x000f0000
#define	CLKD_OTP_SHIFT		16

/* dynamic clock control defines */
#define	LPOMINFREQ		25000	/* low power oscillator min */
#define	LPOMAXFREQ		43000	/* low power oscillator max */
#define	XTALMINFREQ		19800000	/* 20 MHz - 1% */
#define	XTALMAXFREQ		20200000	/* 20 MHz + 1% */
#define	PCIMINFREQ		25000000	/* 25 MHz */
#define	PCIMAXFREQ		34000000	/* 33 MHz + fudge */

#define	ILP_DIV_5MHZ		0	/* ILP = 5 MHz */
#define	ILP_DIV_1MHZ		4	/* ILP = 1 MHz */

/* clkctl xtal what flags */
#define	XTAL			0x1	/* primary crystal oscillator (2050) */
#define	PLL			0x2	/* main chip pll */

/* GPIO usage priorities */
#define GPIO_DRV_PRIORITY	0	/* Driver */
#define GPIO_APP_PRIORITY	1	/* Application */
#define GPIO_HI_PRIORITY	2	/* Highest priority. Ignore GPIO
					 * reservation
					 */

/* GPIO pull up/down */
#define GPIO_PULLUP		0
#define GPIO_PULLDN		1

/* GPIO event regtype */
#define GPIO_REGEVT		0	/* GPIO register event */
#define GPIO_REGEVT_INTMSK	1	/* GPIO register event int mask */
#define GPIO_REGEVT_INTPOL	2	/* GPIO register event int polarity */

/* device path */
#define SI_DEVPATH_BUFSZ	16	/* min buffer size in bytes */

/* SI routine enumeration: to be used by update function with multiple hooks */
#define	SI_DOATTACH	1
#define SI_PCIDOWN	2
#define SI_PCIUP	3

/*
 * Data structure to export all chip specific common variables
 *   public (read-only) portion of aiutils handle returned by si_attach()
 */
struct si_pub {
	int ccrev;		/* chip common core rev */
	u32 cccaps;		/* chip common capabilities */
	int pmurev;		/* pmu core rev */
	u32 pmucaps;		/* pmu capabilities */
	uint boardtype;		/* board type */
	uint boardvendor;	/* board vendor */
	uint chip;		/* chip number */
	uint chiprev;		/* chip revision */
	uint chippkg;		/* chip package option */
};

struct pci_dev;

struct gpioh_item {
	void *arg;
	bool level;
	void (*handler) (u32 stat, void *arg);
	u32 event;
	struct gpioh_item *next;
};

/* misc si info needed by some of the routines */
struct si_info {
	struct si_pub pub;	/* back plane public state (must be first) */
	struct bcma_bus *icbus;	/* handle to soc interconnect bus */
	struct pci_dev *pcibus;	/* handle to pci bus */

	u32 chipst;		/* chip status */
};

/*
 * Many of the routines below take an 'sih' handle as their first arg.
 * Allocate this by calling si_attach().  Free it by calling si_detach().
 * At any one time, the sih is logically focused on one particular si core
 * (the "current core").
 * Use si_setcore() or si_setcoreidx() to change the association to another core
 */


/* AMBA Interconnect exported externs */
u32 ai_core_cflags(struct bcma_device *core, u32 mask, u32 val);

/* === exported functions === */
struct si_pub *ai_attach(struct bcma_bus *pbus);
void ai_detach(struct si_pub *sih);
uint ai_cc_reg(struct si_pub *sih, uint regoff, u32 mask, u32 val);
void ai_clkctl_init(struct si_pub *sih);
u16 ai_clkctl_fast_pwrup_delay(struct si_pub *sih);
bool ai_clkctl_cc(struct si_pub *sih, enum bcma_clkmode mode);
bool ai_deviceremoved(struct si_pub *sih);

/* Enable Ex-PA for 4313 */
void ai_epa_4313war(struct si_pub *sih);

static inline u32 ai_get_cccaps(struct si_pub *sih)
{
	return sih->cccaps;
}

static inline int ai_get_pmurev(struct si_pub *sih)
{
	return sih->pmurev;
}

static inline u32 ai_get_pmucaps(struct si_pub *sih)
{
	return sih->pmucaps;
}

static inline uint ai_get_boardtype(struct si_pub *sih)
{
	return sih->boardtype;
}

static inline uint ai_get_boardvendor(struct si_pub *sih)
{
	return sih->boardvendor;
}

static inline uint ai_get_chip_id(struct si_pub *sih)
{
	return sih->chip;
}

static inline uint ai_get_chiprev(struct si_pub *sih)
{
	return sih->chiprev;
}

static inline uint ai_get_chippkg(struct si_pub *sih)
{
	return sih->chippkg;
}

#endif				/* _BRCM_AIUTILS_H_ */
