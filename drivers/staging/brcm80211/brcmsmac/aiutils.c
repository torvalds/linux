/*
 * Copyright (c) 2010 Broadcom Corporation
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
#include <linux/delay.h>
#include <linux/pci.h>

#include <defs.h>
#include <chipcommon.h>
#include <brcmu_utils.h>
#include <brcm_hw_ids.h>
#include "types.h"
#include "pub.h"
#include "pmu.h"
#include "srom.h"
#include "nicpci.h"
#include "aiutils.h"

/* slow_clk_ctl */
#define SCC_SS_MASK		0x00000007	/* slow clock source mask */
#define	SCC_SS_LPO		0x00000000	/* source of slow clock is LPO */
#define	SCC_SS_XTAL		0x00000001	/* source of slow clock is crystal */
#define	SCC_SS_PCI		0x00000002	/* source of slow clock is PCI */
#define SCC_LF			0x00000200	/* LPOFreqSel, 1: 160Khz, 0: 32KHz */
#define SCC_LP			0x00000400	/* LPOPowerDown, 1: LPO is disabled,
						 * 0: LPO is enabled
						 */
#define SCC_FS			0x00000800	/* ForceSlowClk, 1: sb/cores running on slow clock,
						 * 0: power logic control
						 */
#define SCC_IP			0x00001000	/* IgnorePllOffReq, 1/0: power logic ignores/honors
						 * PLL clock disable requests from core
						 */
#define SCC_XC			0x00002000	/* XtalControlEn, 1/0: power logic does/doesn't
						 * disable crystal when appropriate
						 */
#define SCC_XP			0x00004000	/* XtalPU (RO), 1/0: crystal running/disabled */
#define SCC_CD_MASK		0xffff0000	/* ClockDivider (SlowClk = 1/(4+divisor)) */
#define SCC_CD_SHIFT		16

/* system_clk_ctl */
#define	SYCC_IE			0x00000001	/* ILPen: Enable Idle Low Power */
#define	SYCC_AE			0x00000002	/* ALPen: Enable Active Low Power */
#define	SYCC_FP			0x00000004	/* ForcePLLOn */
#define	SYCC_AR			0x00000008	/* Force ALP (or HT if ALPen is not set */
#define	SYCC_HR			0x00000010	/* Force HT */
#define SYCC_CD_MASK		0xffff0000	/* ClkDiv  (ILP = 1/(4 * (divisor + 1)) */
#define SYCC_CD_SHIFT		16

#define CST4329_SPROM_OTP_SEL_MASK	0x00000003
#define CST4329_DEFCIS_SEL		0	/* OTP is powered up, use def. CIS, no SPROM */
#define CST4329_SPROM_SEL		1	/* OTP is powered up, SPROM is present */
#define CST4329_OTP_SEL			2	/* OTP is powered up, no SPROM */
#define CST4329_OTP_PWRDN		3	/* OTP is powered down, SPROM is present */
#define CST4329_SPI_SDIO_MODE_MASK	0x00000004
#define CST4329_SPI_SDIO_MODE_SHIFT	2

/* 43224 chip-specific ChipControl register bits */
#define CCTRL43224_GPIO_TOGGLE          0x8000
#define CCTRL_43224A0_12MA_LED_DRIVE    0x00F000F0	/* 12 mA drive strength */
#define CCTRL_43224B0_12MA_LED_DRIVE    0xF0	/* 12 mA drive strength for later 43224s */

/* 43236 Chip specific ChipStatus register bits */
#define CST43236_SFLASH_MASK		0x00000040
#define CST43236_OTP_MASK		0x00000080
#define CST43236_HSIC_MASK		0x00000100	/* USB/HSIC */
#define CST43236_BP_CLK			0x00000200	/* 120/96Mbps */
#define CST43236_BOOT_MASK		0x00001800
#define CST43236_BOOT_SHIFT		11
#define CST43236_BOOT_FROM_SRAM		0	/* boot from SRAM, ARM in reset */
#define CST43236_BOOT_FROM_ROM		1	/* boot from ROM */
#define CST43236_BOOT_FROM_FLASH	2	/* boot from FLASH */
#define CST43236_BOOT_FROM_INVALID	3

/* 4331 chip-specific ChipControl register bits */
#define CCTRL4331_BT_COEXIST		(1<<0)	/* 0 disable */
#define CCTRL4331_SECI			(1<<1)	/* 0 SECI is disabled (JATG functional) */
#define CCTRL4331_EXT_LNA		(1<<2)	/* 0 disable */
#define CCTRL4331_SPROM_GPIO13_15       (1<<3)	/* sprom/gpio13-15 mux */
#define CCTRL4331_EXTPA_EN		(1<<4)	/* 0 ext pa disable, 1 ext pa enabled */
#define CCTRL4331_GPIOCLK_ON_SPROMCS	(1<<5)	/* set drive out GPIO_CLK on sprom_cs pin */
#define CCTRL4331_PCIE_MDIO_ON_SPROMCS	(1<<6)	/* use sprom_cs pin as PCIE mdio interface */
#define CCTRL4331_EXTPA_ON_GPIO2_5	(1<<7)	/* aband extpa will be at gpio2/5 and sprom_dout */
#define CCTRL4331_OVR_PIPEAUXCLKEN	(1<<8)	/* override core control on pipe_AuxClkEnable */
#define CCTRL4331_OVR_PIPEAUXPWRDOWN	(1<<9)	/* override core control on pipe_AuxPowerDown */
#define CCTRL4331_PCIE_AUXCLKEN		(1<<10)	/* pcie_auxclkenable */
#define CCTRL4331_PCIE_PIPE_PLLDOWN	(1<<11)	/* pcie_pipe_pllpowerdown */
#define CCTRL4331_BT_SHD0_ON_GPIO4	(1<<16)	/* enable bt_shd0 at gpio4 */
#define CCTRL4331_BT_SHD1_ON_GPIO5	(1<<17)	/* enable bt_shd1 at gpio5 */

/* 4331 Chip specific ChipStatus register bits */
#define	CST4331_XTAL_FREQ		0x00000001	/* crystal frequency 20/40Mhz */
#define	CST4331_SPROM_PRESENT		0x00000002
#define	CST4331_OTP_PRESENT		0x00000004
#define	CST4331_LDO_RF			0x00000008
#define	CST4331_LDO_PAR			0x00000010

/* 4319 chip-specific ChipStatus register bits */
#define	CST4319_SPI_CPULESSUSB		0x00000001
#define	CST4319_SPI_CLK_POL		0x00000002
#define	CST4319_SPI_CLK_PH		0x00000008
#define	CST4319_SPROM_OTP_SEL_MASK	0x000000c0	/* gpio [7:6], SDIO CIS selection */
#define	CST4319_SPROM_OTP_SEL_SHIFT	6
#define	CST4319_DEFCIS_SEL		0x00000000	/* use default CIS, OTP is powered up */
#define	CST4319_SPROM_SEL		0x00000040	/* use SPROM, OTP is powered up */
#define	CST4319_OTP_SEL			0x00000080	/* use OTP, OTP is powered up */
#define	CST4319_OTP_PWRDN		0x000000c0	/* use SPROM, OTP is powered down */
#define	CST4319_SDIO_USB_MODE		0x00000100	/* gpio [8], sdio/usb mode */
#define	CST4319_REMAP_SEL_MASK		0x00000600
#define	CST4319_ILPDIV_EN		0x00000800
#define	CST4319_XTAL_PD_POL		0x00001000
#define	CST4319_LPO_SEL			0x00002000
#define	CST4319_RES_INIT_MODE		0x0000c000
#define	CST4319_PALDO_EXTPNP		0x00010000	/* PALDO is configured with external PNP */
#define	CST4319_CBUCK_MODE_MASK		0x00060000
#define CST4319_CBUCK_MODE_BURST	0x00020000
#define CST4319_CBUCK_MODE_LPBURST	0x00060000
#define	CST4319_RCAL_VALID		0x01000000
#define	CST4319_RCAL_VALUE_MASK		0x3e000000
#define	CST4319_RCAL_VALUE_SHIFT	25

/* 4336 chip-specific ChipStatus register bits */
#define	CST4336_SPI_MODE_MASK		0x00000001
#define	CST4336_SPROM_PRESENT		0x00000002
#define	CST4336_OTP_PRESENT		0x00000004
#define	CST4336_ARMREMAP_0		0x00000008
#define	CST4336_ILPDIV_EN_MASK		0x00000010
#define	CST4336_ILPDIV_EN_SHIFT		4
#define	CST4336_XTAL_PD_POL_MASK	0x00000020
#define	CST4336_XTAL_PD_POL_SHIFT	5
#define	CST4336_LPO_SEL_MASK		0x00000040
#define	CST4336_LPO_SEL_SHIFT		6
#define	CST4336_RES_INIT_MODE_MASK	0x00000180
#define	CST4336_RES_INIT_MODE_SHIFT	7
#define	CST4336_CBUCK_MODE_MASK		0x00000600
#define	CST4336_CBUCK_MODE_SHIFT	9

/* 4313 chip-specific ChipStatus register bits */
#define	CST4313_SPROM_PRESENT			1
#define	CST4313_OTP_PRESENT			2
#define	CST4313_SPROM_OTP_SEL_MASK		0x00000002
#define	CST4313_SPROM_OTP_SEL_SHIFT		0

/* 4313 Chip specific ChipControl register bits */
#define CCTRL_4313_12MA_LED_DRIVE    0x00000007	/* 12 mA drive strengh for later 4313 */

#define BCM47162_DMP() ((sih->chip == BCM47162_CHIP_ID) && \
		(sih->chiprev == 0) && \
		(sii->coreid[sii->curidx] == MIPS74K_CORE_ID))

/* Manufacturer Ids */
#define	MFGID_ARM		0x43b
#define	MFGID_BRCM		0x4bf
#define	MFGID_MIPS		0x4a7

/* Enumeration ROM registers */
#define	ER_EROMENTRY		0x000
#define	ER_REMAPCONTROL		0xe00
#define	ER_REMAPSELECT		0xe04
#define	ER_MASTERSELECT		0xe10
#define	ER_ITCR			0xf00
#define	ER_ITIP			0xf04

/* Erom entries */
#define	ER_TAG			0xe
#define	ER_TAG1			0x6
#define	ER_VALID		1
#define	ER_CI			0
#define	ER_MP			2
#define	ER_ADD			4
#define	ER_END			0xe
#define	ER_BAD			0xffffffff

/* EROM CompIdentA */
#define	CIA_MFG_MASK		0xfff00000
#define	CIA_MFG_SHIFT		20
#define	CIA_CID_MASK		0x000fff00
#define	CIA_CID_SHIFT		8
#define	CIA_CCL_MASK		0x000000f0
#define	CIA_CCL_SHIFT		4

/* EROM CompIdentB */
#define	CIB_REV_MASK		0xff000000
#define	CIB_REV_SHIFT		24
#define	CIB_NSW_MASK		0x00f80000
#define	CIB_NSW_SHIFT		19
#define	CIB_NMW_MASK		0x0007c000
#define	CIB_NMW_SHIFT		14
#define	CIB_NSP_MASK		0x00003e00
#define	CIB_NSP_SHIFT		9
#define	CIB_NMP_MASK		0x000001f0
#define	CIB_NMP_SHIFT		4

/* EROM AddrDesc */
#define	AD_ADDR_MASK		0xfffff000
#define	AD_SP_MASK		0x00000f00
#define	AD_SP_SHIFT		8
#define	AD_ST_MASK		0x000000c0
#define	AD_ST_SHIFT		6
#define	AD_ST_SLAVE		0x00000000
#define	AD_ST_BRIDGE		0x00000040
#define	AD_ST_SWRAP		0x00000080
#define	AD_ST_MWRAP		0x000000c0
#define	AD_SZ_MASK		0x00000030
#define	AD_SZ_SHIFT		4
#define	AD_SZ_4K		0x00000000
#define	AD_SZ_8K		0x00000010
#define	AD_SZ_16K		0x00000020
#define	AD_SZ_SZD		0x00000030
#define	AD_AG32			0x00000008
#define	AD_ADDR_ALIGN		0x00000fff
#define	AD_SZ_BASE		0x00001000	/* 4KB */

/* EROM SizeDesc */
#define	SD_SZ_MASK		0xfffff000
#define	SD_SG32			0x00000008
#define	SD_SZ_ALIGN		0x00000fff

#define	PCI_CFG_GPIO_SCS	0x10	/* PCI config space bit 4 for 4306c0 slow clock source */
#define PCI_CFG_GPIO_XTAL	0x40	/* PCI config space GPIO 14 for Xtal power-up */
#define PCI_CFG_GPIO_PLL	0x80	/* PCI config space GPIO 15 for PLL power-down */

/* power control defines */
#define PLL_DELAY		150	/* us pll on delay */
#define FREF_DELAY		200	/* us fref change delay */
#define	XTAL_ON_DELAY		1000	/* us crystal power-on delay */

/* resetctrl */
#define	AIRC_RESET		1

struct aidmp {
	u32 oobselina30;	/* 0x000 */
	u32 oobselina74;	/* 0x004 */
	u32 PAD[6];
	u32 oobselinb30;	/* 0x020 */
	u32 oobselinb74;	/* 0x024 */
	u32 PAD[6];
	u32 oobselinc30;	/* 0x040 */
	u32 oobselinc74;	/* 0x044 */
	u32 PAD[6];
	u32 oobselind30;	/* 0x060 */
	u32 oobselind74;	/* 0x064 */
	u32 PAD[38];
	u32 oobselouta30;	/* 0x100 */
	u32 oobselouta74;	/* 0x104 */
	u32 PAD[6];
	u32 oobseloutb30;	/* 0x120 */
	u32 oobseloutb74;	/* 0x124 */
	u32 PAD[6];
	u32 oobseloutc30;	/* 0x140 */
	u32 oobseloutc74;	/* 0x144 */
	u32 PAD[6];
	u32 oobseloutd30;	/* 0x160 */
	u32 oobseloutd74;	/* 0x164 */
	u32 PAD[38];
	u32 oobsynca;	/* 0x200 */
	u32 oobseloutaen;	/* 0x204 */
	u32 PAD[6];
	u32 oobsyncb;	/* 0x220 */
	u32 oobseloutben;	/* 0x224 */
	u32 PAD[6];
	u32 oobsyncc;	/* 0x240 */
	u32 oobseloutcen;	/* 0x244 */
	u32 PAD[6];
	u32 oobsyncd;	/* 0x260 */
	u32 oobseloutden;	/* 0x264 */
	u32 PAD[38];
	u32 oobaextwidth;	/* 0x300 */
	u32 oobainwidth;	/* 0x304 */
	u32 oobaoutwidth;	/* 0x308 */
	u32 PAD[5];
	u32 oobbextwidth;	/* 0x320 */
	u32 oobbinwidth;	/* 0x324 */
	u32 oobboutwidth;	/* 0x328 */
	u32 PAD[5];
	u32 oobcextwidth;	/* 0x340 */
	u32 oobcinwidth;	/* 0x344 */
	u32 oobcoutwidth;	/* 0x348 */
	u32 PAD[5];
	u32 oobdextwidth;	/* 0x360 */
	u32 oobdinwidth;	/* 0x364 */
	u32 oobdoutwidth;	/* 0x368 */
	u32 PAD[37];
	u32 ioctrlset;	/* 0x400 */
	u32 ioctrlclear;	/* 0x404 */
	u32 ioctrl;		/* 0x408 */
	u32 PAD[61];
	u32 iostatus;	/* 0x500 */
	u32 PAD[127];
	u32 ioctrlwidth;	/* 0x700 */
	u32 iostatuswidth;	/* 0x704 */
	u32 PAD[62];
	u32 resetctrl;	/* 0x800 */
	u32 resetstatus;	/* 0x804 */
	u32 resetreadid;	/* 0x808 */
	u32 resetwriteid;	/* 0x80c */
	u32 PAD[60];
	u32 errlogctrl;	/* 0x900 */
	u32 errlogdone;	/* 0x904 */
	u32 errlogstatus;	/* 0x908 */
	u32 errlogaddrlo;	/* 0x90c */
	u32 errlogaddrhi;	/* 0x910 */
	u32 errlogid;	/* 0x914 */
	u32 errloguser;	/* 0x918 */
	u32 errlogflags;	/* 0x91c */
	u32 PAD[56];
	u32 intstatus;	/* 0xa00 */
	u32 PAD[127];
	u32 config;		/* 0xe00 */
	u32 PAD[63];
	u32 itcr;		/* 0xf00 */
	u32 PAD[3];
	u32 itipooba;	/* 0xf10 */
	u32 itipoobb;	/* 0xf14 */
	u32 itipoobc;	/* 0xf18 */
	u32 itipoobd;	/* 0xf1c */
	u32 PAD[4];
	u32 itipoobaout;	/* 0xf30 */
	u32 itipoobbout;	/* 0xf34 */
	u32 itipoobcout;	/* 0xf38 */
	u32 itipoobdout;	/* 0xf3c */
	u32 PAD[4];
	u32 itopooba;	/* 0xf50 */
	u32 itopoobb;	/* 0xf54 */
	u32 itopoobc;	/* 0xf58 */
	u32 itopoobd;	/* 0xf5c */
	u32 PAD[4];
	u32 itopoobain;	/* 0xf70 */
	u32 itopoobbin;	/* 0xf74 */
	u32 itopoobcin;	/* 0xf78 */
	u32 itopoobdin;	/* 0xf7c */
	u32 PAD[4];
	u32 itopreset;	/* 0xf90 */
	u32 PAD[15];
	u32 peripherialid4;	/* 0xfd0 */
	u32 peripherialid5;	/* 0xfd4 */
	u32 peripherialid6;	/* 0xfd8 */
	u32 peripherialid7;	/* 0xfdc */
	u32 peripherialid0;	/* 0xfe0 */
	u32 peripherialid1;	/* 0xfe4 */
	u32 peripherialid2;	/* 0xfe8 */
	u32 peripherialid3;	/* 0xfec */
	u32 componentid0;	/* 0xff0 */
	u32 componentid1;	/* 0xff4 */
	u32 componentid2;	/* 0xff8 */
	u32 componentid3;	/* 0xffc */
};

/* EROM parsing */

static u32
get_erom_ent(struct si_pub *sih, u32 **eromptr, u32 mask, u32 match)
{
	u32 ent;
	uint inv = 0, nom = 0;

	while (true) {
		ent = R_REG(*eromptr);
		(*eromptr)++;

		if (mask == 0)
			break;

		if ((ent & ER_VALID) == 0) {
			inv++;
			continue;
		}

		if (ent == (ER_END | ER_VALID))
			break;

		if ((ent & mask) == match)
			break;

		nom++;
	}

	SI_VMSG(("%s: Returning ent 0x%08x\n", __func__, ent));
	if (inv + nom) {
		SI_VMSG(("  after %d invalid and %d non-matching entries\n",
			 inv, nom));
	}
	return ent;
}

static u32
get_asd(struct si_pub *sih, u32 **eromptr, uint sp, uint ad, uint st,
	u32 *addrl, u32 *addrh, u32 *sizel, u32 *sizeh)
{
	u32 asd, sz, szd;

	asd = get_erom_ent(sih, eromptr, ER_VALID, ER_VALID);
	if (((asd & ER_TAG1) != ER_ADD) ||
	    (((asd & AD_SP_MASK) >> AD_SP_SHIFT) != sp) ||
	    ((asd & AD_ST_MASK) != st)) {
		/* This is not what we want, "push" it back */
		(*eromptr)--;
		return 0;
	}
	*addrl = asd & AD_ADDR_MASK;
	if (asd & AD_AG32)
		*addrh = get_erom_ent(sih, eromptr, 0, 0);
	else
		*addrh = 0;
	*sizeh = 0;
	sz = asd & AD_SZ_MASK;
	if (sz == AD_SZ_SZD) {
		szd = get_erom_ent(sih, eromptr, 0, 0);
		*sizel = szd & SD_SZ_MASK;
		if (szd & SD_SG32)
			*sizeh = get_erom_ent(sih, eromptr, 0, 0);
	} else
		*sizel = AD_SZ_BASE << (sz >> AD_SZ_SHIFT);

	SI_VMSG(("  SP %d, ad %d: st = %d, 0x%08x_0x%08x @ 0x%08x_0x%08x\n",
		 sp, ad, st, *sizeh, *sizel, *addrh, *addrl));

	return asd;
}

static void ai_hwfixup(struct si_info *sii)
{
}

/* parse the enumeration rom to identify all cores */
void ai_scan(struct si_pub *sih, void *regs)
{
	struct si_info *sii = SI_INFO(sih);
	chipcregs_t *cc = (chipcregs_t *) regs;
	u32 erombase, *eromptr, *eromlim;

	erombase = R_REG(&cc->eromptr);

	switch (sih->bustype) {
	case SI_BUS:
		eromptr = (u32 *) REG_MAP(erombase, SI_CORE_SIZE);
		break;

	case PCI_BUS:
		/* Set wrappers address */
		sii->curwrap = (void *)((unsigned long)regs + SI_CORE_SIZE);

		/* Now point the window at the erom */
		pci_write_config_dword(sii->pbus, PCI_BAR0_WIN, erombase);
		eromptr = regs;
		break;

	case SPI_BUS:
	case SDIO_BUS:
		eromptr = (u32 *)(unsigned long)erombase;
		break;

	default:
		SI_ERROR(("Don't know how to do AXI enumertion on bus %d\n",
			  sih->bustype));
		return;
	}
	eromlim = eromptr + (ER_REMAPCONTROL / sizeof(u32));

	SI_VMSG(("ai_scan: regs = 0x%p, erombase = 0x%08x, eromptr = 0x%p, eromlim = 0x%p\n", regs, erombase, eromptr, eromlim));
	while (eromptr < eromlim) {
		u32 cia, cib, cid, mfg, crev, nmw, nsw, nmp, nsp;
		u32 mpd, asd, addrl, addrh, sizel, sizeh;
		u32 *base;
		uint i, j, idx;
		bool br;

		br = false;

		/* Grok a component */
		cia = get_erom_ent(sih, &eromptr, ER_TAG, ER_CI);
		if (cia == (ER_END | ER_VALID)) {
			SI_VMSG(("Found END of erom after %d cores\n",
				 sii->numcores));
			ai_hwfixup(sii);
			return;
		}
		base = eromptr - 1;
		cib = get_erom_ent(sih, &eromptr, 0, 0);

		if ((cib & ER_TAG) != ER_CI) {
			SI_ERROR(("CIA not followed by CIB\n"));
			goto error;
		}

		cid = (cia & CIA_CID_MASK) >> CIA_CID_SHIFT;
		mfg = (cia & CIA_MFG_MASK) >> CIA_MFG_SHIFT;
		crev = (cib & CIB_REV_MASK) >> CIB_REV_SHIFT;
		nmw = (cib & CIB_NMW_MASK) >> CIB_NMW_SHIFT;
		nsw = (cib & CIB_NSW_MASK) >> CIB_NSW_SHIFT;
		nmp = (cib & CIB_NMP_MASK) >> CIB_NMP_SHIFT;
		nsp = (cib & CIB_NSP_MASK) >> CIB_NSP_SHIFT;

		SI_VMSG(("Found component 0x%04x/0x%04x rev %d at erom addr 0x%p, with nmw = %d, " "nsw = %d, nmp = %d & nsp = %d\n", mfg, cid, crev, base, nmw, nsw, nmp, nsp));

		if (((mfg == MFGID_ARM) && (cid == DEF_AI_COMP)) || (nsp == 0))
			continue;
		if ((nmw + nsw == 0)) {
			/* A component which is not a core */
			if (cid == OOB_ROUTER_CORE_ID) {
				asd = get_asd(sih, &eromptr, 0, 0, AD_ST_SLAVE,
					      &addrl, &addrh, &sizel, &sizeh);
				if (asd != 0) {
					sii->oob_router = addrl;
				}
			}
			continue;
		}

		idx = sii->numcores;
/*		sii->eromptr[idx] = base; */
		sii->cia[idx] = cia;
		sii->cib[idx] = cib;
		sii->coreid[idx] = cid;

		for (i = 0; i < nmp; i++) {
			mpd = get_erom_ent(sih, &eromptr, ER_VALID, ER_VALID);
			if ((mpd & ER_TAG) != ER_MP) {
				SI_ERROR(("Not enough MP entries for component 0x%x\n", cid));
				goto error;
			}
			SI_VMSG(("  Master port %d, mp: %d id: %d\n", i,
				 (mpd & MPD_MP_MASK) >> MPD_MP_SHIFT,
				 (mpd & MPD_MUI_MASK) >> MPD_MUI_SHIFT));
		}

		/* First Slave Address Descriptor should be port 0:
		 * the main register space for the core
		 */
		asd =
		    get_asd(sih, &eromptr, 0, 0, AD_ST_SLAVE, &addrl, &addrh,
			    &sizel, &sizeh);
		if (asd == 0) {
			/* Try again to see if it is a bridge */
			asd =
			    get_asd(sih, &eromptr, 0, 0, AD_ST_BRIDGE, &addrl,
				    &addrh, &sizel, &sizeh);
			if (asd != 0)
				br = true;
			else if ((addrh != 0) || (sizeh != 0)
				 || (sizel != SI_CORE_SIZE)) {
				SI_ERROR(("First Slave ASD for core 0x%04x malformed " "(0x%08x)\n", cid, asd));
				goto error;
			}
		}
		sii->coresba[idx] = addrl;
		sii->coresba_size[idx] = sizel;
		/* Get any more ASDs in port 0 */
		j = 1;
		do {
			asd =
			    get_asd(sih, &eromptr, 0, j, AD_ST_SLAVE, &addrl,
				    &addrh, &sizel, &sizeh);
			if ((asd != 0) && (j == 1) && (sizel == SI_CORE_SIZE)) {
				sii->coresba2[idx] = addrl;
				sii->coresba2_size[idx] = sizel;
			}
			j++;
		} while (asd != 0);

		/* Go through the ASDs for other slave ports */
		for (i = 1; i < nsp; i++) {
			j = 0;
			do {
				asd =
				    get_asd(sih, &eromptr, i, j++, AD_ST_SLAVE,
					    &addrl, &addrh, &sizel, &sizeh);
			} while (asd != 0);
			if (j == 0) {
				SI_ERROR((" SP %d has no address descriptors\n",
					  i));
				goto error;
			}
		}

		/* Now get master wrappers */
		for (i = 0; i < nmw; i++) {
			asd =
			    get_asd(sih, &eromptr, i, 0, AD_ST_MWRAP, &addrl,
				    &addrh, &sizel, &sizeh);
			if (asd == 0) {
				SI_ERROR(("Missing descriptor for MW %d\n", i));
				goto error;
			}
			if ((sizeh != 0) || (sizel != SI_CORE_SIZE)) {
				SI_ERROR(("Master wrapper %d is not 4KB\n", i));
				goto error;
			}
			if (i == 0)
				sii->wrapba[idx] = addrl;
		}

		/* And finally slave wrappers */
		for (i = 0; i < nsw; i++) {
			uint fwp = (nsp == 1) ? 0 : 1;
			asd =
			    get_asd(sih, &eromptr, fwp + i, 0, AD_ST_SWRAP,
				    &addrl, &addrh, &sizel, &sizeh);
			if (asd == 0) {
				SI_ERROR(("Missing descriptor for SW %d\n", i));
				goto error;
			}
			if ((sizeh != 0) || (sizel != SI_CORE_SIZE)) {
				SI_ERROR(("Slave wrapper %d is not 4KB\n", i));
				goto error;
			}
			if ((nmw == 0) && (i == 0))
				sii->wrapba[idx] = addrl;
		}

		/* Don't record bridges */
		if (br)
			continue;

		/* Done with core */
		sii->numcores++;
	}

	SI_ERROR(("Reached end of erom without finding END"));

 error:
	sii->numcores = 0;
	return;
}

/* This function changes the logical "focus" to the indicated core.
 * Return the current core's virtual address.
 */
void *ai_setcoreidx(struct si_pub *sih, uint coreidx)
{
	struct si_info *sii = SI_INFO(sih);
	u32 addr = sii->coresba[coreidx];
	u32 wrap = sii->wrapba[coreidx];
	void *regs;

	if (coreidx >= sii->numcores)
		return NULL;

	switch (sih->bustype) {
	case SI_BUS:
		/* map new one */
		if (!sii->regs[coreidx]) {
			sii->regs[coreidx] = REG_MAP(addr, SI_CORE_SIZE);
		}
		sii->curmap = regs = sii->regs[coreidx];
		if (!sii->wrappers[coreidx]) {
			sii->wrappers[coreidx] = REG_MAP(wrap, SI_CORE_SIZE);
		}
		sii->curwrap = sii->wrappers[coreidx];
		break;

	case PCI_BUS:
		/* point bar0 window */
		pci_write_config_dword(sii->pbus, PCI_BAR0_WIN, addr);
		regs = sii->curmap;
		/* point bar0 2nd 4KB window */
		pci_write_config_dword(sii->pbus, PCI_BAR0_WIN2, wrap);
		break;

	case SPI_BUS:
	case SDIO_BUS:
		sii->curmap = regs = (void *)(unsigned long)addr;
		sii->curwrap = (void *)(unsigned long)wrap;
		break;

	default:
		regs = NULL;
		break;
	}

	sii->curmap = regs;
	sii->curidx = coreidx;

	return regs;
}

/* Return the number of address spaces in current core */
int ai_numaddrspaces(struct si_pub *sih)
{
	return 2;
}

/* Return the address of the nth address space in the current core */
u32 ai_addrspace(struct si_pub *sih, uint asidx)
{
	struct si_info *sii;
	uint cidx;

	sii = SI_INFO(sih);
	cidx = sii->curidx;

	if (asidx == 0)
		return sii->coresba[cidx];
	else if (asidx == 1)
		return sii->coresba2[cidx];
	else {
		SI_ERROR(("%s: Need to parse the erom again to find addr space %d\n", __func__, asidx));
		return 0;
	}
}

/* Return the size of the nth address space in the current core */
u32 ai_addrspacesize(struct si_pub *sih, uint asidx)
{
	struct si_info *sii;
	uint cidx;

	sii = SI_INFO(sih);
	cidx = sii->curidx;

	if (asidx == 0)
		return sii->coresba_size[cidx];
	else if (asidx == 1)
		return sii->coresba2_size[cidx];
	else {
		SI_ERROR(("%s: Need to parse the erom again to find addr space %d\n", __func__, asidx));
		return 0;
	}
}

uint ai_flag(struct si_pub *sih)
{
	struct si_info *sii;
	struct aidmp *ai;

	sii = SI_INFO(sih);
	if (BCM47162_DMP()) {
		SI_ERROR(("%s: Attempting to read MIPS DMP registers on 47162a0", __func__));
		return sii->curidx;
	}
	ai = sii->curwrap;

	return R_REG(&ai->oobselouta30) & 0x1f;
}

void ai_setint(struct si_pub *sih, int siflag)
{
}

uint ai_corevendor(struct si_pub *sih)
{
	struct si_info *sii;
	u32 cia;

	sii = SI_INFO(sih);
	cia = sii->cia[sii->curidx];
	return (cia & CIA_MFG_MASK) >> CIA_MFG_SHIFT;
}

uint ai_corerev(struct si_pub *sih)
{
	struct si_info *sii;
	u32 cib;

	sii = SI_INFO(sih);
	cib = sii->cib[sii->curidx];
	return (cib & CIB_REV_MASK) >> CIB_REV_SHIFT;
}

bool ai_iscoreup(struct si_pub *sih)
{
	struct si_info *sii;
	struct aidmp *ai;

	sii = SI_INFO(sih);
	ai = sii->curwrap;

	return (((R_REG(&ai->ioctrl) & (SICF_FGC | SICF_CLOCK_EN)) ==
		 SICF_CLOCK_EN)
		&& ((R_REG(&ai->resetctrl) & AIRC_RESET) == 0));
}

void ai_core_cflags_wo(struct si_pub *sih, u32 mask, u32 val)
{
	struct si_info *sii;
	struct aidmp *ai;
	u32 w;

	sii = SI_INFO(sih);

	if (BCM47162_DMP()) {
		SI_ERROR(("%s: Accessing MIPS DMP register (ioctrl) on 47162a0",
			  __func__));
		return;
	}

	ai = sii->curwrap;

	if (mask || val) {
		w = ((R_REG(&ai->ioctrl) & ~mask) | val);
		W_REG(&ai->ioctrl, w);
	}
}

u32 ai_core_cflags(struct si_pub *sih, u32 mask, u32 val)
{
	struct si_info *sii;
	struct aidmp *ai;
	u32 w;

	sii = SI_INFO(sih);
	if (BCM47162_DMP()) {
		SI_ERROR(("%s: Accessing MIPS DMP register (ioctrl) on 47162a0",
			  __func__));
		return 0;
	}

	ai = sii->curwrap;

	if (mask || val) {
		w = ((R_REG(&ai->ioctrl) & ~mask) | val);
		W_REG(&ai->ioctrl, w);
	}

	return R_REG(&ai->ioctrl);
}

u32 ai_core_sflags(struct si_pub *sih, u32 mask, u32 val)
{
	struct si_info *sii;
	struct aidmp *ai;
	u32 w;

	sii = SI_INFO(sih);
	if (BCM47162_DMP()) {
		SI_ERROR(("%s: Accessing MIPS DMP register (iostatus) on 47162a0", __func__));
		return 0;
	}

	ai = sii->curwrap;

	if (mask || val) {
		w = ((R_REG(&ai->iostatus) & ~mask) | val);
		W_REG(&ai->iostatus, w);
	}

	return R_REG(&ai->iostatus);
}

/* *************** from siutils.c ************** */
/* local prototypes */
static struct si_info *ai_doattach(struct si_info *sii, void *regs,
			      uint bustype, void *sdh, char **vars,
			      uint *varsz);
static bool ai_buscore_prep(struct si_info *sii, uint bustype);
static bool ai_buscore_setup(struct si_info *sii, chipcregs_t *cc, uint bustype,
			     u32 savewin, uint *origidx, void *regs);
static void ai_nvram_process(struct si_info *sii, char *pvars);

/* dev path concatenation util */
static char *ai_devpathvar(struct si_pub *sih, char *var, int len,
			   const char *name);
static bool _ai_clkctl_cc(struct si_info *sii, uint mode);
static bool ai_ispcie(struct si_info *sii);

/* global variable to indicate reservation/release of gpio's */
static u32 ai_gpioreservation;

/*
 * Allocate a si handle.
 * devid - pci device id (used to determine chip#)
 * osh - opaque OS handle
 * regs - virtual address of initial core registers
 * bustype - pci/sb/sdio/etc
 * vars - pointer to a pointer area for "environment" variables
 * varsz - pointer to int to return the size of the vars
 */
struct si_pub *ai_attach(void *regs, uint bustype,
		void *sdh, char **vars, uint *varsz)
{
	struct si_info *sii;

	/* alloc struct si_info */
	sii = kmalloc(sizeof(struct si_info), GFP_ATOMIC);
	if (sii == NULL) {
		SI_ERROR(("si_attach: malloc failed!\n"));
		return NULL;
	}

	if (ai_doattach(sii, regs, bustype, sdh, vars, varsz) ==
	    NULL) {
		kfree(sii);
		return NULL;
	}
	sii->vars = vars ? *vars : NULL;
	sii->varsz = varsz ? *varsz : 0;

	return (struct si_pub *) sii;
}

/* global kernel resource */
static struct si_info ksii;

static bool ai_buscore_prep(struct si_info *sii, uint bustype)
{
	/* kludge to enable the clock on the 4306 which lacks a slowclock */
	if (bustype == PCI_BUS && !ai_ispcie(sii))
		ai_clkctl_xtal(&sii->pub, XTAL | PLL, ON);
	return true;
}

static bool ai_buscore_setup(struct si_info *sii, chipcregs_t *cc, uint bustype,
			     u32 savewin, uint *origidx, void *regs)
{
	bool pci, pcie;
	uint i;
	uint pciidx, pcieidx, pcirev, pcierev;

	cc = ai_setcoreidx(&sii->pub, SI_CC_IDX);

	/* get chipcommon rev */
	sii->pub.ccrev = (int)ai_corerev(&sii->pub);

	/* get chipcommon chipstatus */
	if (sii->pub.ccrev >= 11)
		sii->pub.chipst = R_REG(&cc->chipstatus);

	/* get chipcommon capabilites */
	sii->pub.cccaps = R_REG(&cc->capabilities);
	/* get chipcommon extended capabilities */

	if (sii->pub.ccrev >= 35)
		sii->pub.cccaps_ext = R_REG(&cc->capabilities_ext);

	/* get pmu rev and caps */
	if (sii->pub.cccaps & CC_CAP_PMU) {
		sii->pub.pmucaps = R_REG(&cc->pmucapabilities);
		sii->pub.pmurev = sii->pub.pmucaps & PCAP_REV_MASK;
	}

	/* figure out bus/orignal core idx */
	sii->pub.buscoretype = NODEV_CORE_ID;
	sii->pub.buscorerev = NOREV;
	sii->pub.buscoreidx = BADIDX;

	pci = pcie = false;
	pcirev = pcierev = NOREV;
	pciidx = pcieidx = BADIDX;

	for (i = 0; i < sii->numcores; i++) {
		uint cid, crev;

		ai_setcoreidx(&sii->pub, i);
		cid = ai_coreid(&sii->pub);
		crev = ai_corerev(&sii->pub);

		/* Display cores found */
		SI_VMSG(("CORE[%d]: id 0x%x rev %d base 0x%x regs 0x%p\n",
			 i, cid, crev, sii->coresba[i], sii->regs[i]));

		if (bustype == PCI_BUS) {
			if (cid == PCI_CORE_ID) {
				pciidx = i;
				pcirev = crev;
				pci = true;
			} else if (cid == PCIE_CORE_ID) {
				pcieidx = i;
				pcierev = crev;
				pcie = true;
			}
		}

		/* find the core idx before entering this func. */
		if ((savewin && (savewin == sii->coresba[i])) ||
		    (regs == sii->regs[i]))
			*origidx = i;
	}

	if (pci && pcie) {
		if (ai_ispcie(sii))
			pci = false;
		else
			pcie = false;
	}
	if (pci) {
		sii->pub.buscoretype = PCI_CORE_ID;
		sii->pub.buscorerev = pcirev;
		sii->pub.buscoreidx = pciidx;
	} else if (pcie) {
		sii->pub.buscoretype = PCIE_CORE_ID;
		sii->pub.buscorerev = pcierev;
		sii->pub.buscoreidx = pcieidx;
	}

	SI_VMSG(("Buscore id/type/rev %d/0x%x/%d\n", sii->pub.buscoreidx,
		 sii->pub.buscoretype, sii->pub.buscorerev));

	/* fixup necessary chip/core configurations */
	if (sii->pub.bustype == PCI_BUS) {
		if (SI_FAST(sii)) {
			if (!sii->pch) {
				sii->pch = (void *)pcicore_init(
					&sii->pub, sii->pbus,
					(void *)PCIEREGS(sii));
				if (sii->pch == NULL)
					return false;
			}
		}
		if (ai_pci_fixcfg(&sii->pub)) {
			SI_ERROR(("si_doattach: si_pci_fixcfg failed\n"));
			return false;
		}
	}

	/* return to the original core */
	ai_setcoreidx(&sii->pub, *origidx);

	return true;
}

static __used void ai_nvram_process(struct si_info *sii, char *pvars)
{
	uint w = 0;

	/* get boardtype and boardrev */
	switch (sii->pub.bustype) {
	case PCI_BUS:
		/* do a pci config read to get subsystem id and subvendor id */
		pci_read_config_dword(sii->pbus, PCI_SUBSYSTEM_VENDOR_ID, &w);
		/* Let nvram variables override subsystem Vend/ID */
		sii->pub.boardvendor = (u16)ai_getdevpathintvar(&sii->pub,
			"boardvendor");
		if (sii->pub.boardvendor == 0)
			sii->pub.boardvendor = w & 0xffff;
		else
			SI_ERROR(("Overriding boardvendor: 0x%x instead of "
				  "0x%x\n", sii->pub.boardvendor, w & 0xffff));
		sii->pub.boardtype = (u16)ai_getdevpathintvar(&sii->pub,
			"boardtype");
		if (sii->pub.boardtype == 0)
			sii->pub.boardtype = (w >> 16) & 0xffff;
		else
			SI_ERROR(("Overriding boardtype: 0x%x instead of 0x%x\n"
				  , sii->pub.boardtype, (w >> 16) & 0xffff));
		break;

		sii->pub.boardvendor = getintvar(pvars, "manfid");
		sii->pub.boardtype = getintvar(pvars, "prodid");
		break;

	case SI_BUS:
	case JTAG_BUS:
		sii->pub.boardvendor = PCI_VENDOR_ID_BROADCOM;
		sii->pub.boardtype = getintvar(pvars, "prodid");
		if (pvars == NULL || (sii->pub.boardtype == 0)) {
			sii->pub.boardtype = getintvar(NULL, "boardtype");
			if (sii->pub.boardtype == 0)
				sii->pub.boardtype = 0xffff;
		}
		break;
	}

	if (sii->pub.boardtype == 0) {
		SI_ERROR(("si_doattach: unknown board type\n"));
	}

	sii->pub.boardflags = getintvar(pvars, "boardflags");
}

static struct si_info *ai_doattach(struct si_info *sii,
			      void *regs, uint bustype, void *pbus,
			      char **vars, uint *varsz)
{
	struct si_pub *sih = &sii->pub;
	u32 w, savewin;
	chipcregs_t *cc;
	char *pvars = NULL;
	uint socitype;
	uint origidx;

	memset((unsigned char *) sii, 0, sizeof(struct si_info));

	savewin = 0;

	sih->buscoreidx = BADIDX;

	sii->curmap = regs;
	sii->pbus = pbus;

	/* check to see if we are a si core mimic'ing a pci core */
	if (bustype == PCI_BUS) {
		pci_read_config_dword(sii->pbus, PCI_SPROM_CONTROL,  &w);
		if (w == 0xffffffff) {
			SI_ERROR(("%s: incoming bus is PCI but it's a lie, "
				" switching to SI devid:0x%x\n",
				__func__, devid));
			bustype = SI_BUS;
		}
	}

	/* find Chipcommon address */
	if (bustype == PCI_BUS) {
		pci_read_config_dword(sii->pbus, PCI_BAR0_WIN, &savewin);
		if (!GOODCOREADDR(savewin, SI_ENUM_BASE))
			savewin = SI_ENUM_BASE;
		pci_write_config_dword(sii->pbus, PCI_BAR0_WIN,
				       SI_ENUM_BASE);
		cc = (chipcregs_t *) regs;
	} else {
		cc = (chipcregs_t *) REG_MAP(SI_ENUM_BASE, SI_CORE_SIZE);
	}

	sih->bustype = bustype;

	/* bus/core/clk setup for register access */
	if (!ai_buscore_prep(sii, bustype)) {
		SI_ERROR(("si_doattach: si_core_clk_prep failed %d\n",
			  bustype));
		return NULL;
	}

	/*
	 * ChipID recognition.
	 *   We assume we can read chipid at offset 0 from the regs arg.
	 *   If we add other chiptypes (or if we need to support old sdio
	 *   hosts w/o chipcommon), some way of recognizing them needs to
	 *   be added here.
	 */
	w = R_REG(&cc->chipid);
	socitype = (w & CID_TYPE_MASK) >> CID_TYPE_SHIFT;
	/* Might as wll fill in chip id rev & pkg */
	sih->chip = w & CID_ID_MASK;
	sih->chiprev = (w & CID_REV_MASK) >> CID_REV_SHIFT;
	sih->chippkg = (w & CID_PKG_MASK) >> CID_PKG_SHIFT;

	sih->issim = IS_SIM(sih->chippkg);

	/* scan for cores */
	if (socitype == SOCI_AI) {
		SI_MSG(("Found chip type AI (0x%08x)\n", w));
		/* pass chipc address instead of original core base */
		ai_scan(&sii->pub, (void *)cc);
	} else {
		SI_ERROR(("Found chip of unknown type (0x%08x)\n", w));
		return NULL;
	}
	/* no cores found, bail out */
	if (sii->numcores == 0) {
		SI_ERROR(("si_doattach: could not find any cores\n"));
		return NULL;
	}
	/* bus/core/clk setup */
	origidx = SI_CC_IDX;
	if (!ai_buscore_setup(sii, cc, bustype, savewin, &origidx, regs)) {
		SI_ERROR(("si_doattach: si_buscore_setup failed\n"));
		goto exit;
	}

	/* Init nvram from sprom/otp if they exist */
	if (srom_var_init
	    (&sii->pub, bustype, regs, vars, varsz)) {
		SI_ERROR(("si_doattach: srom_var_init failed: bad srom\n"));
		goto exit;
	}
	pvars = vars ? *vars : NULL;
	ai_nvram_process(sii, pvars);

	/* === NVRAM, clock is ready === */
	cc = (chipcregs_t *) ai_setcore(sih, CC_CORE_ID, 0);
	W_REG(&cc->gpiopullup, 0);
	W_REG(&cc->gpiopulldown, 0);
	ai_setcoreidx(sih, origidx);

	/* PMU specific initializations */
	if (PMUCTL_ENAB(sih)) {
		u32 xtalfreq;
		si_pmu_init(sih);
		si_pmu_chip_init(sih);
		xtalfreq = getintvar(pvars, "xtalfreq");
		/* If xtalfreq var not available, try to measure it */
		if (xtalfreq == 0)
			xtalfreq = si_pmu_measure_alpclk(sih);
		si_pmu_pll_init(sih, xtalfreq);
		si_pmu_res_init(sih);
		si_pmu_swreg_init(sih);
	}

	/* setup the GPIO based LED powersave register */
	w = getintvar(pvars, "leddc");
	if (w == 0)
		w = DEFAULT_GPIOTIMERVAL;
	ai_corereg(sih, SI_CC_IDX, offsetof(chipcregs_t, gpiotimerval), ~0, w);

	if (PCIE(sii)) {
		pcicore_attach(sii->pch, pvars, SI_DOATTACH);
	}

	if (sih->chip == BCM43224_CHIP_ID) {
		/*
		 * enable 12 mA drive strenth for 43224 and
		 * set chipControl register bit 15
		 */
		if (sih->chiprev == 0) {
			SI_MSG(("Applying 43224A0 WARs\n"));
			ai_corereg(sih, SI_CC_IDX,
				   offsetof(chipcregs_t, chipcontrol),
				   CCTRL43224_GPIO_TOGGLE,
				   CCTRL43224_GPIO_TOGGLE);
			si_pmu_chipcontrol(sih, 0, CCTRL_43224A0_12MA_LED_DRIVE,
					   CCTRL_43224A0_12MA_LED_DRIVE);
		}
		if (sih->chiprev >= 1) {
			SI_MSG(("Applying 43224B0+ WARs\n"));
			si_pmu_chipcontrol(sih, 0, CCTRL_43224B0_12MA_LED_DRIVE,
					   CCTRL_43224B0_12MA_LED_DRIVE);
		}
	}

	if (sih->chip == BCM4313_CHIP_ID) {
		/*
		 * enable 12 mA drive strenth for 4313 and
		 * set chipControl register bit 1
		 */
		SI_MSG(("Applying 4313 WARs\n"));
		si_pmu_chipcontrol(sih, 0, CCTRL_4313_12MA_LED_DRIVE,
				   CCTRL_4313_12MA_LED_DRIVE);
	}

	return sii;
 exit:
	if (sih->bustype == PCI_BUS) {
		if (sii->pch)
			pcicore_deinit(sii->pch);
		sii->pch = NULL;
	}

	return NULL;
}

/* may be called with core in reset */
void ai_detach(struct si_pub *sih)
{
	struct si_info *sii;
	uint idx;

	struct si_pub *si_local = NULL;
	memcpy(&si_local, &sih, sizeof(struct si_pub **));

	sii = SI_INFO(sih);

	if (sii == NULL)
		return;

	if (sih->bustype == SI_BUS)
		for (idx = 0; idx < SI_MAXCORES; idx++)
			if (sii->regs[idx]) {
				iounmap(sii->regs[idx]);
				sii->regs[idx] = NULL;
			}

	if (sih->bustype == PCI_BUS) {
		if (sii->pch)
			pcicore_deinit(sii->pch);
		sii->pch = NULL;
	}

	if (sii != &ksii)
		kfree(sii);
}

/* register driver interrupt disabling and restoring callback functions */
void
ai_register_intr_callback(struct si_pub *sih, void *intrsoff_fn,
			  void *intrsrestore_fn,
			  void *intrsenabled_fn, void *intr_arg)
{
	struct si_info *sii;

	sii = SI_INFO(sih);
	sii->intr_arg = intr_arg;
	sii->intrsoff_fn = (si_intrsoff_t) intrsoff_fn;
	sii->intrsrestore_fn = (si_intrsrestore_t) intrsrestore_fn;
	sii->intrsenabled_fn = (si_intrsenabled_t) intrsenabled_fn;
	/* save current core id.  when this function called, the current core
	 * must be the core which provides driver functions(il, et, wl, etc.)
	 */
	sii->dev_coreid = sii->coreid[sii->curidx];
}

void ai_deregister_intr_callback(struct si_pub *sih)
{
	struct si_info *sii;

	sii = SI_INFO(sih);
	sii->intrsoff_fn = NULL;
}

uint ai_coreid(struct si_pub *sih)
{
	struct si_info *sii;

	sii = SI_INFO(sih);
	return sii->coreid[sii->curidx];
}

uint ai_coreidx(struct si_pub *sih)
{
	struct si_info *sii;

	sii = SI_INFO(sih);
	return sii->curidx;
}

bool ai_backplane64(struct si_pub *sih)
{
	return (sih->cccaps & CC_CAP_BKPLN64) != 0;
}

/* return index of coreid or BADIDX if not found */
uint ai_findcoreidx(struct si_pub *sih, uint coreid, uint coreunit)
{
	struct si_info *sii;
	uint found;
	uint i;

	sii = SI_INFO(sih);

	found = 0;

	for (i = 0; i < sii->numcores; i++)
		if (sii->coreid[i] == coreid) {
			if (found == coreunit)
				return i;
			found++;
		}

	return BADIDX;
}

/*
 * This function changes logical "focus" to the indicated core;
 * must be called with interrupts off.
 * Moreover, callers should keep interrupts off during switching
 * out of and back to d11 core.
 */
void *ai_setcore(struct si_pub *sih, uint coreid, uint coreunit)
{
	uint idx;

	idx = ai_findcoreidx(sih, coreid, coreunit);
	if (!GOODIDX(idx))
		return NULL;

	return ai_setcoreidx(sih, idx);
}

/* Turn off interrupt as required by ai_setcore, before switch core */
void *ai_switch_core(struct si_pub *sih, uint coreid, uint *origidx,
		     uint *intr_val)
{
	void *cc;
	struct si_info *sii;

	sii = SI_INFO(sih);

	if (SI_FAST(sii)) {
		/* Overloading the origidx variable to remember the coreid,
		 * this works because the core ids cannot be confused with
		 * core indices.
		 */
		*origidx = coreid;
		if (coreid == CC_CORE_ID)
			return (void *)CCREGS_FAST(sii);
		else if (coreid == sih->buscoretype)
			return (void *)PCIEREGS(sii);
	}
	INTR_OFF(sii, *intr_val);
	*origidx = sii->curidx;
	cc = ai_setcore(sih, coreid, 0);
	return cc;
}

/* restore coreidx and restore interrupt */
void ai_restore_core(struct si_pub *sih, uint coreid, uint intr_val)
{
	struct si_info *sii;

	sii = SI_INFO(sih);
	if (SI_FAST(sii)
	    && ((coreid == CC_CORE_ID) || (coreid == sih->buscoretype)))
		return;

	ai_setcoreidx(sih, coreid);
	INTR_RESTORE(sii, intr_val);
}

void ai_write_wrapperreg(struct si_pub *sih, u32 offset, u32 val)
{
	struct si_info *sii = SI_INFO(sih);
	u32 *w = (u32 *) sii->curwrap;
	W_REG(w + (offset / 4), val);
	return;
}

/*
 * Switch to 'coreidx', issue a single arbitrary 32bit register mask&set
 * operation, switch back to the original core, and return the new value.
 *
 * When using the silicon backplane, no fiddling with interrupts or core
 * switches is needed.
 *
 * Also, when using pci/pcie, we can optimize away the core switching for pci
 * registers and (on newer pci cores) chipcommon registers.
 */
uint ai_corereg(struct si_pub *sih, uint coreidx, uint regoff, uint mask,
		uint val)
{
	uint origidx = 0;
	u32 *r = NULL;
	uint w;
	uint intr_val = 0;
	bool fast = false;
	struct si_info *sii;

	sii = SI_INFO(sih);

	if (coreidx >= SI_MAXCORES)
		return 0;

	if (sih->bustype == SI_BUS) {
		/* If internal bus, we can always get at everything */
		fast = true;
		/* map if does not exist */
		if (!sii->regs[coreidx]) {
			sii->regs[coreidx] = REG_MAP(sii->coresba[coreidx],
						     SI_CORE_SIZE);
		}
		r = (u32 *) ((unsigned char *) sii->regs[coreidx] + regoff);
	} else if (sih->bustype == PCI_BUS) {
		/*
		 * If pci/pcie, we can get at pci/pcie regs
		 * and on newer cores to chipc
		 */
		if ((sii->coreid[coreidx] == CC_CORE_ID) && SI_FAST(sii)) {
			/* Chipc registers are mapped at 12KB */

			fast = true;
			r = (u32 *) ((char *)sii->curmap +
					PCI_16KB0_CCREGS_OFFSET + regoff);
		} else if (sii->pub.buscoreidx == coreidx) {
			/*
			 * pci registers are at either in the last 2KB of
			 * an 8KB window or, in pcie and pci rev 13 at 8KB
			 */
			fast = true;
			if (SI_FAST(sii))
				r = (u32 *) ((char *)sii->curmap +
						PCI_16KB0_PCIREGS_OFFSET +
						regoff);
			else
				r = (u32 *) ((char *)sii->curmap +
						((regoff >= SBCONFIGOFF) ?
						 PCI_BAR0_PCISBR_OFFSET :
						 PCI_BAR0_PCIREGS_OFFSET) +
						regoff);
		}
	}

	if (!fast) {
		INTR_OFF(sii, intr_val);

		/* save current core index */
		origidx = ai_coreidx(&sii->pub);

		/* switch core */
		r = (u32 *) ((unsigned char *) ai_setcoreidx(&sii->pub, coreidx)
				+ regoff);
	}

	/* mask and set */
	if (mask || val) {
		w = (R_REG(r) & ~mask) | val;
		W_REG(r, w);
	}

	/* readback */
	w = R_REG(r);

	if (!fast) {
		/* restore core index */
		if (origidx != coreidx)
			ai_setcoreidx(&sii->pub, origidx);

		INTR_RESTORE(sii, intr_val);
	}

	return w;
}

void ai_core_disable(struct si_pub *sih, u32 bits)
{
	struct si_info *sii;
	u32 dummy;
	struct aidmp *ai;

	sii = SI_INFO(sih);

	ai = sii->curwrap;

	/* if core is already in reset, just return */
	if (R_REG(&ai->resetctrl) & AIRC_RESET)
		return;

	W_REG(&ai->ioctrl, bits);
	dummy = R_REG(&ai->ioctrl);
	udelay(10);

	W_REG(&ai->resetctrl, AIRC_RESET);
	udelay(1);
}

/* reset and re-enable a core
 * inputs:
 * bits - core specific bits that are set during and after reset sequence
 * resetbits - core specific bits that are set only during reset sequence
 */
void ai_core_reset(struct si_pub *sih, u32 bits, u32 resetbits)
{
	struct si_info *sii;
	struct aidmp *ai;
	u32 dummy;

	sii = SI_INFO(sih);
	ai = sii->curwrap;

	/*
	 * Must do the disable sequence first to work
	 * for arbitrary current core state.
	 */
	ai_core_disable(sih, (bits | resetbits));

	/*
	 * Now do the initialization sequence.
	 */
	W_REG(&ai->ioctrl, (bits | SICF_FGC | SICF_CLOCK_EN));
	dummy = R_REG(&ai->ioctrl);
	W_REG(&ai->resetctrl, 0);
	udelay(1);

	W_REG(&ai->ioctrl, (bits | SICF_CLOCK_EN));
	dummy = R_REG(&ai->ioctrl);
	udelay(1);
}

/* return the slow clock source - LPO, XTAL, or PCI */
static uint ai_slowclk_src(struct si_info *sii)
{
	chipcregs_t *cc;
	u32 val;

	if (sii->pub.ccrev < 6) {
		if (sii->pub.bustype == PCI_BUS) {
			pci_read_config_dword(sii->pbus, PCI_GPIO_OUT,
					      &val);
			if (val & PCI_CFG_GPIO_SCS)
				return SCC_SS_PCI;
		}
		return SCC_SS_XTAL;
	} else if (sii->pub.ccrev < 10) {
		cc = (chipcregs_t *) ai_setcoreidx(&sii->pub, sii->curidx);
		return R_REG(&cc->slow_clk_ctl) & SCC_SS_MASK;
	} else			/* Insta-clock */
		return SCC_SS_XTAL;
}

/*
* return the ILP (slowclock) min or max frequency
* precondition: we've established the chip has dynamic clk control
*/
static uint ai_slowclk_freq(struct si_info *sii, bool max_freq, chipcregs_t *cc)
{
	u32 slowclk;
	uint div;

	slowclk = ai_slowclk_src(sii);
	if (sii->pub.ccrev < 6) {
		if (slowclk == SCC_SS_PCI)
			return max_freq ? (PCIMAXFREQ / 64)
				: (PCIMINFREQ / 64);
		else
			return max_freq ? (XTALMAXFREQ / 32)
				: (XTALMINFREQ / 32);
	} else if (sii->pub.ccrev < 10) {
		div = 4 *
		    (((R_REG(&cc->slow_clk_ctl) & SCC_CD_MASK) >>
		      SCC_CD_SHIFT) + 1);
		if (slowclk == SCC_SS_LPO)
			return max_freq ? LPOMAXFREQ : LPOMINFREQ;
		else if (slowclk == SCC_SS_XTAL)
			return max_freq ? (XTALMAXFREQ / div)
				: (XTALMINFREQ / div);
		else if (slowclk == SCC_SS_PCI)
			return max_freq ? (PCIMAXFREQ / div)
				: (PCIMINFREQ / div);
	} else {
		/* Chipc rev 10 is InstaClock */
		div = R_REG(&cc->system_clk_ctl) >> SYCC_CD_SHIFT;
		div = 4 * (div + 1);
		return max_freq ? XTALMAXFREQ : (XTALMINFREQ / div);
	}
	return 0;
}

static void ai_clkctl_setdelay(struct si_info *sii, void *chipcregs)
{
	chipcregs_t *cc = (chipcregs_t *) chipcregs;
	uint slowmaxfreq, pll_delay, slowclk;
	uint pll_on_delay, fref_sel_delay;

	pll_delay = PLL_DELAY;

	/*
	 * If the slow clock is not sourced by the xtal then
	 * add the xtal_on_delay since the xtal will also be
	 * powered down by dynamic clk control logic.
	 */

	slowclk = ai_slowclk_src(sii);
	if (slowclk != SCC_SS_XTAL)
		pll_delay += XTAL_ON_DELAY;

	/* Starting with 4318 it is ILP that is used for the delays */
	slowmaxfreq =
	    ai_slowclk_freq(sii, (sii->pub.ccrev >= 10) ? false : true, cc);

	pll_on_delay = ((slowmaxfreq * pll_delay) + 999999) / 1000000;
	fref_sel_delay = ((slowmaxfreq * FREF_DELAY) + 999999) / 1000000;

	W_REG(&cc->pll_on_delay, pll_on_delay);
	W_REG(&cc->fref_sel_delay, fref_sel_delay);
}

/* initialize power control delay registers */
void ai_clkctl_init(struct si_pub *sih)
{
	struct si_info *sii;
	uint origidx = 0;
	chipcregs_t *cc;
	bool fast;

	if (!CCCTL_ENAB(sih))
		return;

	sii = SI_INFO(sih);
	fast = SI_FAST(sii);
	if (!fast) {
		origidx = sii->curidx;
		cc = (chipcregs_t *) ai_setcore(sih, CC_CORE_ID, 0);
		if (cc == NULL)
			return;
	} else {
		cc = (chipcregs_t *) CCREGS_FAST(sii);
		if (cc == NULL)
			return;
	}

	/* set all Instaclk chip ILP to 1 MHz */
	if (sih->ccrev >= 10)
		SET_REG(&cc->system_clk_ctl, SYCC_CD_MASK,
			(ILP_DIV_1MHZ << SYCC_CD_SHIFT));

	ai_clkctl_setdelay(sii, (void *)cc);

	if (!fast)
		ai_setcoreidx(sih, origidx);
}

/*
 * return the value suitable for writing to the
 * dot11 core FAST_PWRUP_DELAY register
 */
u16 ai_clkctl_fast_pwrup_delay(struct si_pub *sih)
{
	struct si_info *sii;
	uint origidx = 0;
	chipcregs_t *cc;
	uint slowminfreq;
	u16 fpdelay;
	uint intr_val = 0;
	bool fast;

	sii = SI_INFO(sih);
	if (PMUCTL_ENAB(sih)) {
		INTR_OFF(sii, intr_val);
		fpdelay = si_pmu_fast_pwrup_delay(sih);
		INTR_RESTORE(sii, intr_val);
		return fpdelay;
	}

	if (!CCCTL_ENAB(sih))
		return 0;

	fast = SI_FAST(sii);
	fpdelay = 0;
	if (!fast) {
		origidx = sii->curidx;
		INTR_OFF(sii, intr_val);
		cc = (chipcregs_t *) ai_setcore(sih, CC_CORE_ID, 0);
		if (cc == NULL)
			goto done;
	} else {
		cc = (chipcregs_t *) CCREGS_FAST(sii);
		if (cc == NULL)
			goto done;
	}

	slowminfreq = ai_slowclk_freq(sii, false, cc);
	fpdelay = (((R_REG(&cc->pll_on_delay) + 2) * 1000000) +
		   (slowminfreq - 1)) / slowminfreq;

 done:
	if (!fast) {
		ai_setcoreidx(sih, origidx);
		INTR_RESTORE(sii, intr_val);
	}
	return fpdelay;
}

/* turn primary xtal and/or pll off/on */
int ai_clkctl_xtal(struct si_pub *sih, uint what, bool on)
{
	struct si_info *sii;
	u32 in, out, outen;

	sii = SI_INFO(sih);

	switch (sih->bustype) {

	case PCI_BUS:
		/* pcie core doesn't have any mapping to control the xtal pu */
		if (PCIE(sii))
			return -1;

		pci_read_config_dword(sii->pbus, PCI_GPIO_IN, &in);
		pci_read_config_dword(sii->pbus, PCI_GPIO_OUT, &out);
		pci_read_config_dword(sii->pbus, PCI_GPIO_OUTEN, &outen);

		/*
		 * Avoid glitching the clock if GPRS is already using it.
		 * We can't actually read the state of the PLLPD so we infer it
		 * by the value of XTAL_PU which *is* readable via gpioin.
		 */
		if (on && (in & PCI_CFG_GPIO_XTAL))
			return 0;

		if (what & XTAL)
			outen |= PCI_CFG_GPIO_XTAL;
		if (what & PLL)
			outen |= PCI_CFG_GPIO_PLL;

		if (on) {
			/* turn primary xtal on */
			if (what & XTAL) {
				out |= PCI_CFG_GPIO_XTAL;
				if (what & PLL)
					out |= PCI_CFG_GPIO_PLL;
				pci_write_config_dword(sii->pbus,
						       PCI_GPIO_OUT, out);
				pci_write_config_dword(sii->pbus,
						       PCI_GPIO_OUTEN, outen);
				udelay(XTAL_ON_DELAY);
			}

			/* turn pll on */
			if (what & PLL) {
				out &= ~PCI_CFG_GPIO_PLL;
				pci_write_config_dword(sii->pbus,
						       PCI_GPIO_OUT, out);
				mdelay(2);
			}
		} else {
			if (what & XTAL)
				out &= ~PCI_CFG_GPIO_XTAL;
			if (what & PLL)
				out |= PCI_CFG_GPIO_PLL;
			pci_write_config_dword(sii->pbus,
					       PCI_GPIO_OUT, out);
			pci_write_config_dword(sii->pbus,
					       PCI_GPIO_OUTEN, outen);
		}

	default:
		return -1;
	}

	return 0;
}

/*
 *  clock control policy function throught chipcommon
 *
 *    set dynamic clk control mode (forceslow, forcefast, dynamic)
 *    returns true if we are forcing fast clock
 *    this is a wrapper over the next internal function
 *      to allow flexible policy settings for outside caller
 */
bool ai_clkctl_cc(struct si_pub *sih, uint mode)
{
	struct si_info *sii;

	sii = SI_INFO(sih);

	/* chipcommon cores prior to rev6 don't support dynamic clock control */
	if (sih->ccrev < 6)
		return false;

	if (PCI_FORCEHT(sii))
		return mode == CLK_FAST;

	return _ai_clkctl_cc(sii, mode);
}

/* clk control mechanism through chipcommon, no policy checking */
static bool _ai_clkctl_cc(struct si_info *sii, uint mode)
{
	uint origidx = 0;
	chipcregs_t *cc;
	u32 scc;
	uint intr_val = 0;
	bool fast = SI_FAST(sii);

	/* chipcommon cores prior to rev6 don't support dynamic clock control */
	if (sii->pub.ccrev < 6)
		return false;

	if (!fast) {
		INTR_OFF(sii, intr_val);
		origidx = sii->curidx;

		if ((sii->pub.bustype == SI_BUS) &&
		    ai_setcore(&sii->pub, MIPS33_CORE_ID, 0) &&
		    (ai_corerev(&sii->pub) <= 7) && (sii->pub.ccrev >= 10))
			goto done;

		cc = (chipcregs_t *) ai_setcore(&sii->pub, CC_CORE_ID, 0);
	} else {
		cc = (chipcregs_t *) CCREGS_FAST(sii);
		if (cc == NULL)
			goto done;
	}

	if (!CCCTL_ENAB(&sii->pub) && (sii->pub.ccrev < 20))
		goto done;

	switch (mode) {
	case CLK_FAST:		/* FORCEHT, fast (pll) clock */
		if (sii->pub.ccrev < 10) {
			/*
			 * don't forget to force xtal back
			 * on before we clear SCC_DYN_XTAL..
			 */
			ai_clkctl_xtal(&sii->pub, XTAL, ON);
			SET_REG(&cc->slow_clk_ctl,
				(SCC_XC | SCC_FS | SCC_IP), SCC_IP);
		} else if (sii->pub.ccrev < 20) {
			OR_REG(&cc->system_clk_ctl, SYCC_HR);
		} else {
			OR_REG(&cc->clk_ctl_st, CCS_FORCEHT);
		}

		/* wait for the PLL */
		if (PMUCTL_ENAB(&sii->pub)) {
			u32 htavail = CCS_HTAVAIL;
			SPINWAIT(((R_REG(&cc->clk_ctl_st) & htavail)
				  == 0), PMU_MAX_TRANSITION_DLY);
		} else {
			udelay(PLL_DELAY);
		}
		break;

	case CLK_DYNAMIC:	/* enable dynamic clock control */
		if (sii->pub.ccrev < 10) {
			scc = R_REG(&cc->slow_clk_ctl);
			scc &= ~(SCC_FS | SCC_IP | SCC_XC);
			if ((scc & SCC_SS_MASK) != SCC_SS_XTAL)
				scc |= SCC_XC;
			W_REG(&cc->slow_clk_ctl, scc);

			/*
			 * for dynamic control, we have to
			 * release our xtal_pu "force on"
			 */
			if (scc & SCC_XC)
				ai_clkctl_xtal(&sii->pub, XTAL, OFF);
		} else if (sii->pub.ccrev < 20) {
			/* Instaclock */
			AND_REG(&cc->system_clk_ctl, ~SYCC_HR);
		} else {
			AND_REG(&cc->clk_ctl_st, ~CCS_FORCEHT);
		}
		break;

	default:
		break;
	}

 done:
	if (!fast) {
		ai_setcoreidx(&sii->pub, origidx);
		INTR_RESTORE(sii, intr_val);
	}
	return mode == CLK_FAST;
}

/* Build device path. Support SI, PCI, and JTAG for now. */
int ai_devpath(struct si_pub *sih, char *path, int size)
{
	int slen;

	if (!path || size <= 0)
		return -1;

	switch (sih->bustype) {
	case SI_BUS:
	case JTAG_BUS:
		slen = snprintf(path, (size_t) size, "sb/%u/", ai_coreidx(sih));
		break;
	case PCI_BUS:
		slen = snprintf(path, (size_t) size, "pci/%u/%u/",
			((struct pci_dev *)((SI_INFO(sih))->pbus))->bus->number,
			PCI_SLOT(
			    ((struct pci_dev *)((SI_INFO(sih))->pbus))->devfn));
		break;

	default:
		slen = -1;
		break;
	}

	if (slen < 0 || slen >= size) {
		path[0] = '\0';
		return -1;
	}

	return 0;
}

/* Get a variable, but only if it has a devpath prefix */
char *ai_getdevpathvar(struct si_pub *sih, const char *name)
{
	char varname[SI_DEVPATH_BUFSZ + 32];

	ai_devpathvar(sih, varname, sizeof(varname), name);

	return getvar(NULL, varname);
}

/* Get a variable, but only if it has a devpath prefix */
int ai_getdevpathintvar(struct si_pub *sih, const char *name)
{
#if defined(BCMBUSTYPE) && (BCMBUSTYPE == SI_BUS)
	return getintvar(NULL, name);
#else
	char varname[SI_DEVPATH_BUFSZ + 32];

	ai_devpathvar(sih, varname, sizeof(varname), name);

	return getintvar(NULL, varname);
#endif
}

char *ai_getnvramflvar(struct si_pub *sih, const char *name)
{
	return getvar(NULL, name);
}

/* Concatenate the dev path with a varname into the given 'var' buffer
 * and return the 'var' pointer. Nothing is done to the arguments if
 * len == 0 or var is NULL, var is still returned. On overflow, the
 * first char will be set to '\0'.
 */
static char *ai_devpathvar(struct si_pub *sih, char *var, int len,
			   const char *name)
{
	uint path_len;

	if (!var || len <= 0)
		return var;

	if (ai_devpath(sih, var, len) == 0) {
		path_len = strlen(var);

		if (strlen(name) + 1 > (uint) (len - path_len))
			var[0] = '\0';
		else
			strncpy(var + path_len, name, len - path_len - 1);
	}

	return var;
}

/* return true if PCIE capability exists in the pci config space */
static bool ai_ispcie(struct si_info *sii)
{
	u8 cap_ptr;

	if (sii->pub.bustype != PCI_BUS)
		return false;

	cap_ptr =
	    pcicore_find_pci_capability(sii->pbus, PCI_CAP_ID_EXP, NULL,
					NULL);
	if (!cap_ptr)
		return false;

	return true;
}

bool ai_pci_war16165(struct si_pub *sih)
{
	struct si_info *sii;

	sii = SI_INFO(sih);

	return PCI(sii) && (sih->buscorerev <= 10);
}

void ai_pci_up(struct si_pub *sih)
{
	struct si_info *sii;

	sii = SI_INFO(sih);

	/* if not pci bus, we're done */
	if (sih->bustype != PCI_BUS)
		return;

	if (PCI_FORCEHT(sii))
		_ai_clkctl_cc(sii, CLK_FAST);

	if (PCIE(sii))
		pcicore_up(sii->pch, SI_PCIUP);

}

/* Unconfigure and/or apply various WARs when system is going to sleep mode */
void ai_pci_sleep(struct si_pub *sih)
{
	struct si_info *sii;

	sii = SI_INFO(sih);

	pcicore_sleep(sii->pch);
}

/* Unconfigure and/or apply various WARs when going down */
void ai_pci_down(struct si_pub *sih)
{
	struct si_info *sii;

	sii = SI_INFO(sih);

	/* if not pci bus, we're done */
	if (sih->bustype != PCI_BUS)
		return;

	/* release FORCEHT since chip is going to "down" state */
	if (PCI_FORCEHT(sii))
		_ai_clkctl_cc(sii, CLK_DYNAMIC);

	pcicore_down(sii->pch, SI_PCIDOWN);
}

/*
 * Configure the pci core for pci client (NIC) action
 * coremask is the bitvec of cores by index to be enabled.
 */
void ai_pci_setup(struct si_pub *sih, uint coremask)
{
	struct si_info *sii;
	void *regs = NULL;
	u32 siflag = 0, w;
	uint idx = 0;

	sii = SI_INFO(sih);

	if (sii->pub.bustype != PCI_BUS)
		return;

	if (PCI(sii)) {
		/* get current core index */
		idx = sii->curidx;

		/* we interrupt on this backplane flag number */
		siflag = ai_flag(sih);

		/* switch over to pci core */
		regs = ai_setcoreidx(sih, sii->pub.buscoreidx);
	}

	/*
	 * Enable sb->pci interrupts.  Assume
	 * PCI rev 2.3 support was added in pci core rev 6 and things changed..
	 */
	if (PCIE(sii) || (PCI(sii) && ((sii->pub.buscorerev) >= 6))) {
		/* pci config write to set this core bit in PCIIntMask */
		pci_read_config_dword(sii->pbus, PCI_INT_MASK, &w);
		w |= (coremask << PCI_SBIM_SHIFT);
		pci_write_config_dword(sii->pbus, PCI_INT_MASK, w);
	} else {
		/* set sbintvec bit for our flag number */
		ai_setint(sih, siflag);
	}

	if (PCI(sii)) {
		pcicore_pci_setup(sii->pch, regs);

		/* switch back to previous core */
		ai_setcoreidx(sih, idx);
	}
}

/*
 * Fixup SROMless PCI device's configuration.
 * The current core may be changed upon return.
 */
int ai_pci_fixcfg(struct si_pub *sih)
{
	uint origidx;
	void *regs = NULL;

	struct si_info *sii = SI_INFO(sih);

	/* Fixup PI in SROM shadow area to enable the correct PCI core access */
	/* save the current index */
	origidx = ai_coreidx(&sii->pub);

	/* check 'pi' is correct and fix it if not */
	regs = ai_setcore(&sii->pub, sii->pub.buscoretype, 0);
	pcicore_fixcfg(sii->pch, regs);

	/* restore the original index */
	ai_setcoreidx(&sii->pub, origidx);

	pcicore_hwup(sii->pch);
	return 0;
}

/* mask&set gpiocontrol bits */
u32 ai_gpiocontrol(struct si_pub *sih, u32 mask, u32 val, u8 priority)
{
	uint regoff;

	regoff = 0;

	/* gpios could be shared on router platforms
	 * ignore reservation if it's high priority (e.g., test apps)
	 */
	if ((priority != GPIO_HI_PRIORITY) &&
	    (sih->bustype == SI_BUS) && (val || mask)) {
		mask = priority ? (ai_gpioreservation & mask) :
		    ((ai_gpioreservation | mask) & ~(ai_gpioreservation));
		val &= mask;
	}

	regoff = offsetof(chipcregs_t, gpiocontrol);
	return ai_corereg(sih, SI_CC_IDX, regoff, mask, val);
}

void ai_chipcontrl_epa4331(struct si_pub *sih, bool on)
{
	struct si_info *sii;
	chipcregs_t *cc;
	uint origidx;
	u32 val;

	sii = SI_INFO(sih);
	origidx = ai_coreidx(sih);

	cc = (chipcregs_t *) ai_setcore(sih, CC_CORE_ID, 0);

	val = R_REG(&cc->chipcontrol);

	if (on) {
		if (sih->chippkg == 9 || sih->chippkg == 0xb) {
			/* Ext PA Controls for 4331 12x9 Package */
			W_REG(&cc->chipcontrol, val |
			      (CCTRL4331_EXTPA_EN |
			       CCTRL4331_EXTPA_ON_GPIO2_5));
		} else {
			/* Ext PA Controls for 4331 12x12 Package */
			W_REG(&cc->chipcontrol,
			      val | (CCTRL4331_EXTPA_EN));
		}
	} else {
		val &= ~(CCTRL4331_EXTPA_EN | CCTRL4331_EXTPA_ON_GPIO2_5);
		W_REG(&cc->chipcontrol, val);
	}

	ai_setcoreidx(sih, origidx);
}

/* Enable BT-COEX & Ex-PA for 4313 */
void ai_epa_4313war(struct si_pub *sih)
{
	struct si_info *sii;
	chipcregs_t *cc;
	uint origidx;

	sii = SI_INFO(sih);
	origidx = ai_coreidx(sih);

	cc = (chipcregs_t *) ai_setcore(sih, CC_CORE_ID, 0);

	/* EPA Fix */
	W_REG(&cc->gpiocontrol,
	      R_REG(&cc->gpiocontrol) | GPIO_CTRL_EPA_EN_MASK);

	ai_setcoreidx(sih, origidx);
}

/* check if the device is removed */
bool ai_deviceremoved(struct si_pub *sih)
{
	u32 w;
	struct si_info *sii;

	sii = SI_INFO(sih);

	switch (sih->bustype) {
	case PCI_BUS:
		pci_read_config_dword(sii->pbus, PCI_VENDOR_ID, &w);
		if ((w & 0xFFFF) != PCI_VENDOR_ID_BROADCOM)
			return true;
		break;
	}
	return false;
}

bool ai_is_sprom_available(struct si_pub *sih)
{
	if (sih->ccrev >= 31) {
		struct si_info *sii;
		uint origidx;
		chipcregs_t *cc;
		u32 sromctrl;

		if ((sih->cccaps & CC_CAP_SROM) == 0)
			return false;

		sii = SI_INFO(sih);
		origidx = sii->curidx;
		cc = ai_setcoreidx(sih, SI_CC_IDX);
		sromctrl = R_REG(&cc->sromcontrol);
		ai_setcoreidx(sih, origidx);
		return sromctrl & SRC_PRESENT;
	}

	switch (sih->chip) {
	case BCM4313_CHIP_ID:
		return (sih->chipst & CST4313_SPROM_PRESENT) != 0;
	default:
		return true;
	}
}

bool ai_is_otp_disabled(struct si_pub *sih)
{
	switch (sih->chip) {
	case BCM4313_CHIP_ID:
		return (sih->chipst & CST4313_OTP_PRESENT) == 0;
		/* These chips always have their OTP on */
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	default:
		return false;
	}
}
