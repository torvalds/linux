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
 *
 * File contents: support functions for PCI/PCIe
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/pci.h>

#include <defs.h>
#include <chipcommon.h>
#include <brcmu_utils.h>
#include <brcm_hw_ids.h>
#include <soc.h>
#include "types.h"
#include "pub.h"
#include "pmu.h"
#include "srom.h"
#include "nicpci.h"
#include "aiutils.h"

/* slow_clk_ctl */
 /* slow clock source mask */
#define SCC_SS_MASK		0x00000007
 /* source of slow clock is LPO */
#define	SCC_SS_LPO		0x00000000
 /* source of slow clock is crystal */
#define	SCC_SS_XTAL		0x00000001
 /* source of slow clock is PCI */
#define	SCC_SS_PCI		0x00000002
 /* LPOFreqSel, 1: 160Khz, 0: 32KHz */
#define SCC_LF			0x00000200
 /* LPOPowerDown, 1: LPO is disabled, 0: LPO is enabled */
#define SCC_LP			0x00000400
 /* ForceSlowClk, 1: sb/cores running on slow clock, 0: power logic control */
#define SCC_FS			0x00000800
 /* IgnorePllOffReq, 1/0:
  *  power logic ignores/honors PLL clock disable requests from core
  */
#define SCC_IP			0x00001000
 /* XtalControlEn, 1/0:
  *  power logic does/doesn't disable crystal when appropriate
  */
#define SCC_XC			0x00002000
 /* XtalPU (RO), 1/0: crystal running/disabled */
#define SCC_XP			0x00004000
 /* ClockDivider (SlowClk = 1/(4+divisor)) */
#define SCC_CD_MASK		0xffff0000
#define SCC_CD_SHIFT		16

/* system_clk_ctl */
 /* ILPen: Enable Idle Low Power */
#define	SYCC_IE			0x00000001
 /* ALPen: Enable Active Low Power */
#define	SYCC_AE			0x00000002
 /* ForcePLLOn */
#define	SYCC_FP			0x00000004
 /* Force ALP (or HT if ALPen is not set */
#define	SYCC_AR			0x00000008
 /* Force HT */
#define	SYCC_HR			0x00000010
 /* ClkDiv  (ILP = 1/(4 * (divisor + 1)) */
#define SYCC_CD_MASK		0xffff0000
#define SYCC_CD_SHIFT		16

#define CST4329_SPROM_OTP_SEL_MASK	0x00000003
 /* OTP is powered up, use def. CIS, no SPROM */
#define CST4329_DEFCIS_SEL		0
 /* OTP is powered up, SPROM is present */
#define CST4329_SPROM_SEL		1
 /* OTP is powered up, no SPROM */
#define CST4329_OTP_SEL			2
 /* OTP is powered down, SPROM is present */
#define CST4329_OTP_PWRDN		3

#define CST4329_SPI_SDIO_MODE_MASK	0x00000004
#define CST4329_SPI_SDIO_MODE_SHIFT	2

/* 43224 chip-specific ChipControl register bits */
#define CCTRL43224_GPIO_TOGGLE          0x8000
 /* 12 mA drive strength */
#define CCTRL_43224A0_12MA_LED_DRIVE    0x00F000F0
 /* 12 mA drive strength for later 43224s */
#define CCTRL_43224B0_12MA_LED_DRIVE    0xF0

/* 43236 Chip specific ChipStatus register bits */
#define CST43236_SFLASH_MASK		0x00000040
#define CST43236_OTP_MASK		0x00000080
#define CST43236_HSIC_MASK		0x00000100	/* USB/HSIC */
#define CST43236_BP_CLK			0x00000200	/* 120/96Mbps */
#define CST43236_BOOT_MASK		0x00001800
#define CST43236_BOOT_SHIFT		11
#define CST43236_BOOT_FROM_SRAM		0 /* boot from SRAM, ARM in reset */
#define CST43236_BOOT_FROM_ROM		1 /* boot from ROM */
#define CST43236_BOOT_FROM_FLASH	2 /* boot from FLASH */
#define CST43236_BOOT_FROM_INVALID	3

/* 4331 chip-specific ChipControl register bits */
 /* 0 disable */
#define CCTRL4331_BT_COEXIST		(1<<0)
 /* 0 SECI is disabled (JTAG functional) */
#define CCTRL4331_SECI			(1<<1)
 /* 0 disable */
#define CCTRL4331_EXT_LNA		(1<<2)
 /* sprom/gpio13-15 mux */
#define CCTRL4331_SPROM_GPIO13_15       (1<<3)
 /* 0 ext pa disable, 1 ext pa enabled */
#define CCTRL4331_EXTPA_EN		(1<<4)
 /* set drive out GPIO_CLK on sprom_cs pin */
#define CCTRL4331_GPIOCLK_ON_SPROMCS	(1<<5)
 /* use sprom_cs pin as PCIE mdio interface */
#define CCTRL4331_PCIE_MDIO_ON_SPROMCS	(1<<6)
 /* aband extpa will be at gpio2/5 and sprom_dout */
#define CCTRL4331_EXTPA_ON_GPIO2_5	(1<<7)
 /* override core control on pipe_AuxClkEnable */
#define CCTRL4331_OVR_PIPEAUXCLKEN	(1<<8)
 /* override core control on pipe_AuxPowerDown */
#define CCTRL4331_OVR_PIPEAUXPWRDOWN	(1<<9)
 /* pcie_auxclkenable */
#define CCTRL4331_PCIE_AUXCLKEN		(1<<10)
 /* pcie_pipe_pllpowerdown */
#define CCTRL4331_PCIE_PIPE_PLLDOWN	(1<<11)
 /* enable bt_shd0 at gpio4 */
#define CCTRL4331_BT_SHD0_ON_GPIO4	(1<<16)
 /* enable bt_shd1 at gpio5 */
#define CCTRL4331_BT_SHD1_ON_GPIO5	(1<<17)

/* 4331 Chip specific ChipStatus register bits */
 /* crystal frequency 20/40Mhz */
#define	CST4331_XTAL_FREQ		0x00000001
#define	CST4331_SPROM_PRESENT		0x00000002
#define	CST4331_OTP_PRESENT		0x00000004
#define	CST4331_LDO_RF			0x00000008
#define	CST4331_LDO_PAR			0x00000010

/* 4319 chip-specific ChipStatus register bits */
#define	CST4319_SPI_CPULESSUSB		0x00000001
#define	CST4319_SPI_CLK_POL		0x00000002
#define	CST4319_SPI_CLK_PH		0x00000008
 /* gpio [7:6], SDIO CIS selection */
#define	CST4319_SPROM_OTP_SEL_MASK	0x000000c0
#define	CST4319_SPROM_OTP_SEL_SHIFT	6
 /* use default CIS, OTP is powered up */
#define	CST4319_DEFCIS_SEL		0x00000000
 /* use SPROM, OTP is powered up */
#define	CST4319_SPROM_SEL		0x00000040
 /* use OTP, OTP is powered up */
#define	CST4319_OTP_SEL			0x00000080
 /* use SPROM, OTP is powered down */
#define	CST4319_OTP_PWRDN		0x000000c0
 /* gpio [8], sdio/usb mode */
#define	CST4319_SDIO_USB_MODE		0x00000100
#define	CST4319_REMAP_SEL_MASK		0x00000600
#define	CST4319_ILPDIV_EN		0x00000800
#define	CST4319_XTAL_PD_POL		0x00001000
#define	CST4319_LPO_SEL			0x00002000
#define	CST4319_RES_INIT_MODE		0x0000c000
 /* PALDO is configured with external PNP */
#define	CST4319_PALDO_EXTPNP		0x00010000
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
 /* 12 mA drive strengh for later 4313 */
#define CCTRL_4313_12MA_LED_DRIVE    0x00000007

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

/* PCI config space bit 4 for 4306c0 slow clock source */
#define	PCI_CFG_GPIO_SCS	0x10
/* PCI config space GPIO 14 for Xtal power-up */
#define PCI_CFG_GPIO_XTAL	0x40
/* PCI config space GPIO 15 for PLL power-down */
#define PCI_CFG_GPIO_PLL	0x80

/* power control defines */
#define PLL_DELAY		150	/* us pll on delay */
#define FREF_DELAY		200	/* us fref change delay */
#define	XTAL_ON_DELAY		1000	/* us crystal power-on delay */

/* resetctrl */
#define	AIRC_RESET		1

#define	NOREV		-1	/* Invalid rev */

/* GPIO Based LED powersave defines */
#define DEFAULT_GPIO_ONTIME	10	/* Default: 10% on */
#define DEFAULT_GPIO_OFFTIME	90	/* Default: 10% on */

/* When Srom support present, fields in sromcontrol */
#define	SRC_START		0x80000000
#define	SRC_BUSY		0x80000000
#define	SRC_OPCODE		0x60000000
#define	SRC_OP_READ		0x00000000
#define	SRC_OP_WRITE		0x20000000
#define	SRC_OP_WRDIS		0x40000000
#define	SRC_OP_WREN		0x60000000
#define	SRC_OTPSEL		0x00000010
#define	SRC_LOCK		0x00000008
#define	SRC_SIZE_MASK		0x00000006
#define	SRC_SIZE_1K		0x00000000
#define	SRC_SIZE_4K		0x00000002
#define	SRC_SIZE_16K		0x00000004
#define	SRC_SIZE_SHIFT		1
#define	SRC_PRESENT		0x00000001

/* External PA enable mask */
#define GPIO_CTRL_EPA_EN_MASK 0x40

#define DEFAULT_GPIOTIMERVAL \
	((DEFAULT_GPIO_ONTIME << GPIO_ONTIME_SHIFT) | DEFAULT_GPIO_OFFTIME)

#define	BADIDX		(SI_MAXCORES + 1)

#define	IS_SIM(chippkg)	\
	((chippkg == HDLSIM_PKG_ID) || (chippkg == HWSIM_PKG_ID))

/*
 * Macros to disable/restore function core(D11, ENET, ILINE20, etc) interrupts
 * before after core switching to avoid invalid register accesss inside ISR.
 */
#define INTR_OFF(si, intr_val) \
	if ((si)->intrsoff_fn && \
	    (si)->coreid[(si)->curidx] == (si)->dev_coreid) \
		intr_val = (*(si)->intrsoff_fn)((si)->intr_arg)

#define INTR_RESTORE(si, intr_val) \
	if ((si)->intrsrestore_fn && \
	    (si)->coreid[(si)->curidx] == (si)->dev_coreid) \
		(*(si)->intrsrestore_fn)((si)->intr_arg, intr_val)

#define PCI(sih)	(ai_get_buscoretype(sih) == PCI_CORE_ID)
#define PCIE(sih)	(ai_get_buscoretype(sih) == PCIE_CORE_ID)

#define PCI_FORCEHT(sih) (PCIE(sih) && (ai_get_chip_id(sih) == BCM4716_CHIP_ID))

#ifdef BCMDBG
#define	SI_MSG(fmt, ...)	pr_debug(fmt, ##__VA_ARGS__)
#else
#define	SI_MSG(fmt, ...)	no_printk(fmt, ##__VA_ARGS__)
#endif				/* BCMDBG */

#define	GOODCOREADDR(x, b) \
	(((x) >= (b)) && ((x) < ((b) + SI_MAXCORES * SI_CORE_SIZE)) && \
		IS_ALIGNED((x), SI_CORE_SIZE))

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

/* parse the enumeration rom to identify all cores */
static void ai_scan(struct si_pub *sih, struct bcma_bus *bus)
{
	struct si_info *sii = (struct si_info *)sih;
	struct bcma_device *core;
	uint idx;

	list_for_each_entry(core, &bus->cores, list) {
		idx = core->core_index;
		sii->cia[idx] = core->id.manuf << CIA_MFG_SHIFT;
		sii->cia[idx] |= core->id.id << CIA_CID_SHIFT;
		sii->cia[idx] |= core->id.class << CIA_CCL_SHIFT;
		sii->cib[idx] = core->id.rev << CIB_REV_SHIFT;
		sii->coreid[idx] = core->id.id;
		sii->coresba[idx] = core->addr;
		sii->coresba_size[idx] = 0x1000;
		sii->coresba2[idx] = 0;
		sii->coresba2_size[idx] = 0;
		sii->wrapba[idx] = core->wrap;
		sii->numcores++;
	}
}

static struct bcma_device *ai_find_bcma_core(struct si_pub *sih, uint coreidx)
{
	struct si_info *sii = (struct si_info *)sih;
	struct bcma_device *core;

	list_for_each_entry(core, &sii->icbus->cores, list) {
		if (core->core_index == coreidx)
			return core;
	}
	return NULL;
}
/*
 * This function changes the logical "focus" to the indicated core.
 * Return the current core's virtual address. Since each core starts with the
 * same set of registers (BIST, clock control, etc), the returned address
 * contains the first register of this 'common' register block (not to be
 * confused with 'common core').
 */
void __iomem *ai_setcoreidx(struct si_pub *sih, uint coreidx)
{
	struct si_info *sii = (struct si_info *)sih;
	struct bcma_device *core;

	if (sii->curidx != coreidx) {
		core = ai_find_bcma_core(sih, coreidx);
		if (core == NULL)
			return NULL;

		(void)bcma_aread32(core, BCMA_IOST);
		sii->curidx = coreidx;
	}
	return sii->curmap;
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

	sii = (struct si_info *)sih;
	cidx = sii->curidx;

	if (asidx == 0)
		return sii->coresba[cidx];
	else if (asidx == 1)
		return sii->coresba2[cidx];
	else {
		/* Need to parse the erom again to find addr space */
		return 0;
	}
}

/* Return the size of the nth address space in the current core */
u32 ai_addrspacesize(struct si_pub *sih, uint asidx)
{
	struct si_info *sii;
	uint cidx;

	sii = (struct si_info *)sih;
	cidx = sii->curidx;

	if (asidx == 0)
		return sii->coresba_size[cidx];
	else if (asidx == 1)
		return sii->coresba2_size[cidx];
	else {
		/* Need to parse the erom again to find addr */
		return 0;
	}
}

uint ai_flag(struct si_pub *sih)
{
	struct si_info *sii;
	struct aidmp *ai;

	sii = (struct si_info *)sih;
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

	sii = (struct si_info *)sih;
	cia = sii->cia[sii->curidx];
	return (cia & CIA_MFG_MASK) >> CIA_MFG_SHIFT;
}

uint ai_corerev(struct si_pub *sih)
{
	struct si_info *sii;
	u32 cib;

	sii = (struct si_info *)sih;
	cib = sii->cib[sii->curidx];
	return (cib & CIB_REV_MASK) >> CIB_REV_SHIFT;
}

bool ai_iscoreup(struct si_pub *sih)
{
	struct si_info *sii;
	struct aidmp *ai;

	sii = (struct si_info *)sih;
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

	sii = (struct si_info *)sih;

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

	sii = (struct si_info *)sih;
	ai = sii->curwrap;

	if (mask || val) {
		w = ((R_REG(&ai->ioctrl) & ~mask) | val);
		W_REG(&ai->ioctrl, w);
	}

	return R_REG(&ai->ioctrl);
}

/* return true if PCIE capability exists in the pci config space */
static bool ai_ispcie(struct si_info *sii)
{
	u8 cap_ptr;

	cap_ptr =
	    pcicore_find_pci_capability(sii->pcibus, PCI_CAP_ID_EXP, NULL,
					NULL);
	if (!cap_ptr)
		return false;

	return true;
}

static bool ai_buscore_prep(struct si_info *sii)
{
	/* kludge to enable the clock on the 4306 which lacks a slowclock */
	if (!ai_ispcie(sii))
		ai_clkctl_xtal(&sii->pub, XTAL | PLL, ON);
	return true;
}

u32 ai_core_sflags(struct si_pub *sih, u32 mask, u32 val)
{
	struct si_info *sii;
	struct aidmp *ai;
	u32 w;

	sii = (struct si_info *)sih;
	ai = sii->curwrap;

	if (mask || val) {
		w = ((R_REG(&ai->iostatus) & ~mask) | val);
		W_REG(&ai->iostatus, w);
	}

	return R_REG(&ai->iostatus);
}

static bool
ai_buscore_setup(struct si_info *sii, u32 savewin, uint *origidx)
{
	bool pci, pcie;
	uint i;
	uint pciidx, pcieidx, pcirev, pcierev;
	struct chipcregs __iomem *cc;

	cc = ai_setcoreidx(&sii->pub, SI_CC_IDX);

	/* get chipcommon rev */
	sii->pub.ccrev = (int)ai_corerev(&sii->pub);

	/* get chipcommon chipstatus */
	if (ai_get_ccrev(&sii->pub) >= 11)
		sii->chipst = R_REG(&cc->chipstatus);

	/* get chipcommon capabilites */
	sii->pub.cccaps = R_REG(&cc->capabilities);

	/* get pmu rev and caps */
	if (ai_get_cccaps(&sii->pub) & CC_CAP_PMU) {
		sii->pub.pmucaps = R_REG(&cc->pmucapabilities);
		sii->pub.pmurev = sii->pub.pmucaps & PCAP_REV_MASK;
	}

	/* figure out bus/orignal core idx */
	sii->pub.buscoretype = NODEV_CORE_ID;
	sii->pub.buscorerev = NOREV;
	sii->buscoreidx = BADIDX;

	pci = pcie = false;
	pcirev = pcierev = NOREV;
	pciidx = pcieidx = BADIDX;

	for (i = 0; i < sii->numcores; i++) {
		uint cid, crev;

		ai_setcoreidx(&sii->pub, i);
		cid = ai_coreid(&sii->pub);
		crev = ai_corerev(&sii->pub);

		if (cid == PCI_CORE_ID) {
			pciidx = i;
			pcirev = crev;
			pci = true;
		} else if (cid == PCIE_CORE_ID) {
			pcieidx = i;
			pcierev = crev;
			pcie = true;
		}

		/* find the core idx before entering this func. */
		if ((savewin && (savewin == sii->coresba[i])) ||
		    (cc == sii->regs[i]))
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
		sii->buscoreidx = pciidx;
	} else if (pcie) {
		sii->pub.buscoretype = PCIE_CORE_ID;
		sii->pub.buscorerev = pcierev;
		sii->buscoreidx = pcieidx;
	}

	/* fixup necessary chip/core configurations */
	if (!sii->pch) {
		sii->pch = pcicore_init(&sii->pub, sii->pcibus,
					sii->curmap + PCI_16KB0_PCIREGS_OFFSET);
		if (sii->pch == NULL)
			return false;
	}
	if (ai_pci_fixcfg(&sii->pub)) {
		/* si_doattach: si_pci_fixcfg failed */
		return false;
	}

	/* return to the original core */
	ai_setcoreidx(&sii->pub, *origidx);

	return true;
}

/*
 * get boardtype and boardrev
 */
static __used void ai_nvram_process(struct si_info *sii)
{
	uint w = 0;

	/* do a pci config read to get subsystem id and subvendor id */
	pci_read_config_dword(sii->pcibus, PCI_SUBSYSTEM_VENDOR_ID, &w);

	sii->pub.boardvendor = w & 0xffff;
	sii->pub.boardtype = (w >> 16) & 0xffff;
}

static struct si_info *ai_doattach(struct si_info *sii,
				   struct bcma_bus *pbus)
{
	void __iomem *regs = pbus->mmio;
	struct si_pub *sih = &sii->pub;
	u32 w, savewin;
	struct chipcregs __iomem *cc;
	uint socitype;
	uint origidx;

	memset((unsigned char *) sii, 0, sizeof(struct si_info));

	savewin = 0;

	sii->icbus = pbus;
	sii->buscoreidx = BADIDX;
	sii->pcibus = pbus->host_pci;
	sii->curmap = regs;
	sii->curwrap = sii->curmap + SI_CORE_SIZE;

	/* switch to Chipcommon core */
	bcma_read32(pbus->drv_cc.core, 0);
	savewin = SI_ENUM_BASE;

	cc = (struct chipcregs __iomem *) regs;

	/* bus/core/clk setup for register access */
	if (!ai_buscore_prep(sii))
		return NULL;

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

	/* scan for cores */
	if (socitype == SOCI_AI) {
		SI_MSG("Found chip type AI (0x%08x)\n", w);
		/* pass chipc address instead of original core base */
		ai_scan(&sii->pub, pbus);
	} else {
		/* Found chip of unknown type */
		return NULL;
	}
	/* no cores found, bail out */
	if (sii->numcores == 0)
		return NULL;

	/* bus/core/clk setup */
	origidx = SI_CC_IDX;
	if (!ai_buscore_setup(sii, savewin, &origidx))
		goto exit;

	/* Init nvram from sprom/otp if they exist */
	if (srom_var_init(&sii->pub, cc))
		goto exit;

	ai_nvram_process(sii);

	/* === NVRAM, clock is ready === */
	cc = (struct chipcregs __iomem *) ai_setcore(sih, CC_CORE_ID, 0);
	W_REG(&cc->gpiopullup, 0);
	W_REG(&cc->gpiopulldown, 0);
	ai_setcoreidx(sih, origidx);

	/* PMU specific initializations */
	if (ai_get_cccaps(sih) & CC_CAP_PMU) {
		u32 xtalfreq;
		si_pmu_init(sih);
		si_pmu_chip_init(sih);

		xtalfreq = si_pmu_measure_alpclk(sih);
		si_pmu_pll_init(sih, xtalfreq);
		si_pmu_res_init(sih);
		si_pmu_swreg_init(sih);
	}

	/* setup the GPIO based LED powersave register */
	w = getintvar(sih, BRCMS_SROM_LEDDC);
	if (w == 0)
		w = DEFAULT_GPIOTIMERVAL;
	ai_cc_reg(sih, offsetof(struct chipcregs, gpiotimerval),
		  ~0, w);

	if (PCIE(sih))
		pcicore_attach(sii->pch, SI_DOATTACH);

	if (ai_get_chip_id(sih) == BCM43224_CHIP_ID) {
		/*
		 * enable 12 mA drive strenth for 43224 and
		 * set chipControl register bit 15
		 */
		if (ai_get_chiprev(sih) == 0) {
			SI_MSG("Applying 43224A0 WARs\n");
			ai_cc_reg(sih, offsetof(struct chipcregs, chipcontrol),
				  CCTRL43224_GPIO_TOGGLE,
				  CCTRL43224_GPIO_TOGGLE);
			si_pmu_chipcontrol(sih, 0, CCTRL_43224A0_12MA_LED_DRIVE,
					   CCTRL_43224A0_12MA_LED_DRIVE);
		}
		if (ai_get_chiprev(sih) >= 1) {
			SI_MSG("Applying 43224B0+ WARs\n");
			si_pmu_chipcontrol(sih, 0, CCTRL_43224B0_12MA_LED_DRIVE,
					   CCTRL_43224B0_12MA_LED_DRIVE);
		}
	}

	if (ai_get_chip_id(sih) == BCM4313_CHIP_ID) {
		/*
		 * enable 12 mA drive strenth for 4313 and
		 * set chipControl register bit 1
		 */
		SI_MSG("Applying 4313 WARs\n");
		si_pmu_chipcontrol(sih, 0, CCTRL_4313_12MA_LED_DRIVE,
				   CCTRL_4313_12MA_LED_DRIVE);
	}

	return sii;

 exit:
	if (sii->pch)
		pcicore_deinit(sii->pch);
	sii->pch = NULL;

	return NULL;
}

/*
 * Allocate a si handle and do the attach.
 */
struct si_pub *
ai_attach(struct bcma_bus *pbus)
{
	struct si_info *sii;

	/* alloc struct si_info */
	sii = kmalloc(sizeof(struct si_info), GFP_ATOMIC);
	if (sii == NULL)
		return NULL;

	if (ai_doattach(sii, pbus) == NULL) {
		kfree(sii);
		return NULL;
	}

	return (struct si_pub *) sii;
}

/* may be called with core in reset */
void ai_detach(struct si_pub *sih)
{
	struct si_info *sii;

	struct si_pub *si_local = NULL;
	memcpy(&si_local, &sih, sizeof(struct si_pub **));

	sii = (struct si_info *)sih;

	if (sii == NULL)
		return;

	if (sii->pch)
		pcicore_deinit(sii->pch);
	sii->pch = NULL;

	srom_free_vars(sih);
	kfree(sii);
}

/* register driver interrupt disabling and restoring callback functions */
void
ai_register_intr_callback(struct si_pub *sih, void *intrsoff_fn,
			  void *intrsrestore_fn,
			  void *intrsenabled_fn, void *intr_arg)
{
	struct si_info *sii;

	sii = (struct si_info *)sih;
	sii->intr_arg = intr_arg;
	sii->intrsoff_fn = (u32 (*)(void *)) intrsoff_fn;
	sii->intrsrestore_fn = (void (*) (void *, u32)) intrsrestore_fn;
	sii->intrsenabled_fn = (bool (*)(void *)) intrsenabled_fn;
	/* save current core id.  when this function called, the current core
	 * must be the core which provides driver functions(il, et, wl, etc.)
	 */
	sii->dev_coreid = sii->coreid[sii->curidx];
}

void ai_deregister_intr_callback(struct si_pub *sih)
{
	struct si_info *sii;

	sii = (struct si_info *)sih;
	sii->intrsoff_fn = NULL;
}

uint ai_coreid(struct si_pub *sih)
{
	struct si_info *sii;

	sii = (struct si_info *)sih;
	return sii->coreid[sii->curidx];
}

uint ai_coreidx(struct si_pub *sih)
{
	struct si_info *sii;

	sii = (struct si_info *)sih;
	return sii->curidx;
}

bool ai_backplane64(struct si_pub *sih)
{
	return (ai_get_cccaps(sih) & CC_CAP_BKPLN64) != 0;
}

/* return index of coreid or BADIDX if not found */
uint ai_findcoreidx(struct si_pub *sih, uint coreid, uint coreunit)
{
	struct bcma_device *core;
	struct si_info *sii;
	uint found;

	sii = (struct si_info *)sih;

	found = 0;

	list_for_each_entry(core, &sii->icbus->cores, list)
		if (core->id.id == coreid) {
			if (found == coreunit)
				return core->core_index;
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
void __iomem *ai_setcore(struct si_pub *sih, uint coreid, uint coreunit)
{
	uint idx;

	idx = ai_findcoreidx(sih, coreid, coreunit);
	if (idx >= SI_MAXCORES)
		return NULL;

	return ai_setcoreidx(sih, idx);
}

/* Turn off interrupt as required by ai_setcore, before switch core */
void __iomem *ai_switch_core(struct si_pub *sih, uint coreid, uint *origidx,
			     uint *intr_val)
{
	void __iomem *cc;
	struct si_info *sii;

	sii = (struct si_info *)sih;

	INTR_OFF(sii, *intr_val);
	*origidx = sii->curidx;
	cc = ai_setcore(sih, coreid, 0);
	return cc;
}

/* restore coreidx and restore interrupt */
void ai_restore_core(struct si_pub *sih, uint coreid, uint intr_val)
{
	struct si_info *sii;

	sii = (struct si_info *)sih;

	ai_setcoreidx(sih, coreid);
	INTR_RESTORE(sii, intr_val);
}

void ai_write_wrapperreg(struct si_pub *sih, u32 offset, u32 val)
{
	struct si_info *sii = (struct si_info *)sih;
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
uint ai_cc_reg(struct si_pub *sih, uint regoff, u32 mask, u32 val)
{
	struct bcma_device *cc;
	uint origidx = 0;
	u32 w;
	uint intr_val = 0;
	struct si_info *sii;

	sii = (struct si_info *)sih;
	cc = sii->icbus->drv_cc.core;

	INTR_OFF(sii, intr_val);

	/* save current core index */
	origidx = ai_coreidx(&sii->pub);

	/* mask and set */
	if (mask || val) {
		bcma_maskset32(cc, regoff, ~mask, val);
	}

	/* readback */
	w = bcma_read32(cc, regoff);

	/* restore core index */
	ai_setcoreidx(&sii->pub, origidx);

	INTR_RESTORE(sii, intr_val);

	return w;
}

void ai_core_disable(struct si_pub *sih, u32 bits)
{
	struct si_info *sii;
	u32 dummy;
	struct aidmp *ai;

	sii = (struct si_info *)sih;

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

	sii = (struct si_info *)sih;
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
	struct chipcregs __iomem *cc;
	u32 val;

	if (ai_get_ccrev(&sii->pub) < 6) {
		pci_read_config_dword(sii->pcibus, PCI_GPIO_OUT,
				      &val);
		if (val & PCI_CFG_GPIO_SCS)
			return SCC_SS_PCI;
		return SCC_SS_XTAL;
	} else if (ai_get_ccrev(&sii->pub) < 10) {
		cc = (struct chipcregs __iomem *)
			ai_setcoreidx(&sii->pub, sii->curidx);
		return R_REG(&cc->slow_clk_ctl) & SCC_SS_MASK;
	} else			/* Insta-clock */
		return SCC_SS_XTAL;
}

/*
* return the ILP (slowclock) min or max frequency
* precondition: we've established the chip has dynamic clk control
*/
static uint ai_slowclk_freq(struct si_info *sii, bool max_freq,
			    struct chipcregs __iomem *cc)
{
	u32 slowclk;
	uint div;

	slowclk = ai_slowclk_src(sii);
	if (ai_get_ccrev(&sii->pub) < 6) {
		if (slowclk == SCC_SS_PCI)
			return max_freq ? (PCIMAXFREQ / 64)
				: (PCIMINFREQ / 64);
		else
			return max_freq ? (XTALMAXFREQ / 32)
				: (XTALMINFREQ / 32);
	} else if (ai_get_ccrev(&sii->pub) < 10) {
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

static void
ai_clkctl_setdelay(struct si_info *sii, struct chipcregs __iomem *cc)
{
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
	    ai_slowclk_freq(sii,
			    (ai_get_ccrev(&sii->pub) >= 10) ? false : true, cc);

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
	struct chipcregs __iomem *cc;

	if (!(ai_get_cccaps(sih) & CC_CAP_PWR_CTL))
		return;

	sii = (struct si_info *)sih;
	origidx = sii->curidx;
	cc = (struct chipcregs __iomem *)
		ai_setcore(sih, CC_CORE_ID, 0);
	if (cc == NULL)
		return;

	/* set all Instaclk chip ILP to 1 MHz */
	if (ai_get_ccrev(sih) >= 10)
		SET_REG(&cc->system_clk_ctl, SYCC_CD_MASK,
			(ILP_DIV_1MHZ << SYCC_CD_SHIFT));

	ai_clkctl_setdelay(sii, cc);

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
	struct chipcregs __iomem *cc;
	uint slowminfreq;
	u16 fpdelay;
	uint intr_val = 0;

	sii = (struct si_info *)sih;
	if (ai_get_cccaps(sih) & CC_CAP_PMU) {
		INTR_OFF(sii, intr_val);
		fpdelay = si_pmu_fast_pwrup_delay(sih);
		INTR_RESTORE(sii, intr_val);
		return fpdelay;
	}

	if (!(ai_get_cccaps(sih) & CC_CAP_PWR_CTL))
		return 0;

	fpdelay = 0;
	origidx = sii->curidx;
	INTR_OFF(sii, intr_val);
	cc = (struct chipcregs __iomem *)
		ai_setcore(sih, CC_CORE_ID, 0);
	if (cc == NULL)
		goto done;

	slowminfreq = ai_slowclk_freq(sii, false, cc);
	fpdelay = (((R_REG(&cc->pll_on_delay) + 2) * 1000000) +
		   (slowminfreq - 1)) / slowminfreq;

 done:
	ai_setcoreidx(sih, origidx);
	INTR_RESTORE(sii, intr_val);
	return fpdelay;
}

/* turn primary xtal and/or pll off/on */
int ai_clkctl_xtal(struct si_pub *sih, uint what, bool on)
{
	struct si_info *sii;
	u32 in, out, outen;

	sii = (struct si_info *)sih;

	/* pcie core doesn't have any mapping to control the xtal pu */
	if (PCIE(sih))
		return -1;

	pci_read_config_dword(sii->pcibus, PCI_GPIO_IN, &in);
	pci_read_config_dword(sii->pcibus, PCI_GPIO_OUT, &out);
	pci_read_config_dword(sii->pcibus, PCI_GPIO_OUTEN, &outen);

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
			pci_write_config_dword(sii->pcibus,
					       PCI_GPIO_OUT, out);
			pci_write_config_dword(sii->pcibus,
					       PCI_GPIO_OUTEN, outen);
			udelay(XTAL_ON_DELAY);
		}

		/* turn pll on */
		if (what & PLL) {
			out &= ~PCI_CFG_GPIO_PLL;
			pci_write_config_dword(sii->pcibus,
					       PCI_GPIO_OUT, out);
			mdelay(2);
		}
	} else {
		if (what & XTAL)
			out &= ~PCI_CFG_GPIO_XTAL;
		if (what & PLL)
			out |= PCI_CFG_GPIO_PLL;
		pci_write_config_dword(sii->pcibus,
				       PCI_GPIO_OUT, out);
		pci_write_config_dword(sii->pcibus,
				       PCI_GPIO_OUTEN, outen);
	}

	return 0;
}

/* clk control mechanism through chipcommon, no policy checking */
static bool _ai_clkctl_cc(struct si_info *sii, uint mode)
{
	uint origidx = 0;
	struct chipcregs __iomem *cc;
	u32 scc;
	uint intr_val = 0;

	/* chipcommon cores prior to rev6 don't support dynamic clock control */
	if (ai_get_ccrev(&sii->pub) < 6)
		return false;

	INTR_OFF(sii, intr_val);
	origidx = sii->curidx;
	cc = (struct chipcregs __iomem *)
				ai_setcore(&sii->pub, CC_CORE_ID, 0);

	if (!(ai_get_cccaps(&sii->pub) & CC_CAP_PWR_CTL) &&
	    (ai_get_ccrev(&sii->pub) < 20))
		goto done;

	switch (mode) {
	case CLK_FAST:		/* FORCEHT, fast (pll) clock */
		if (ai_get_ccrev(&sii->pub) < 10) {
			/*
			 * don't forget to force xtal back
			 * on before we clear SCC_DYN_XTAL..
			 */
			ai_clkctl_xtal(&sii->pub, XTAL, ON);
			SET_REG(&cc->slow_clk_ctl,
				(SCC_XC | SCC_FS | SCC_IP), SCC_IP);
		} else if (ai_get_ccrev(&sii->pub) < 20) {
			OR_REG(&cc->system_clk_ctl, SYCC_HR);
		} else {
			OR_REG(&cc->clk_ctl_st, CCS_FORCEHT);
		}

		/* wait for the PLL */
		if (ai_get_cccaps(&sii->pub) & CC_CAP_PMU) {
			u32 htavail = CCS_HTAVAIL;
			SPINWAIT(((R_REG(&cc->clk_ctl_st) & htavail)
				  == 0), PMU_MAX_TRANSITION_DLY);
		} else {
			udelay(PLL_DELAY);
		}
		break;

	case CLK_DYNAMIC:	/* enable dynamic clock control */
		if (ai_get_ccrev(&sii->pub) < 10) {
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
		} else if (ai_get_ccrev(&sii->pub) < 20) {
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
	ai_setcoreidx(&sii->pub, origidx);
	INTR_RESTORE(sii, intr_val);
	return mode == CLK_FAST;
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

	sii = (struct si_info *)sih;

	/* chipcommon cores prior to rev6 don't support dynamic clock control */
	if (ai_get_ccrev(sih) < 6)
		return false;

	if (PCI_FORCEHT(sih))
		return mode == CLK_FAST;

	return _ai_clkctl_cc(sii, mode);
}

/* Build device path */
int ai_devpath(struct si_pub *sih, char *path, int size)
{
	int slen;

	if (!path || size <= 0)
		return -1;

	slen = snprintf(path, (size_t) size, "pci/%u/%u/",
		((struct si_info *)sih)->pcibus->bus->number,
		PCI_SLOT(((struct pci_dev *)
				(((struct si_info *)(sih))->pcibus))->devfn));

	if (slen < 0 || slen >= size) {
		path[0] = '\0';
		return -1;
	}

	return 0;
}

void ai_pci_up(struct si_pub *sih)
{
	struct si_info *sii;

	sii = (struct si_info *)sih;

	if (PCI_FORCEHT(sih))
		_ai_clkctl_cc(sii, CLK_FAST);

	if (PCIE(sih))
		pcicore_up(sii->pch, SI_PCIUP);

}

/* Unconfigure and/or apply various WARs when system is going to sleep mode */
void ai_pci_sleep(struct si_pub *sih)
{
	struct si_info *sii;

	sii = (struct si_info *)sih;

	pcicore_sleep(sii->pch);
}

/* Unconfigure and/or apply various WARs when going down */
void ai_pci_down(struct si_pub *sih)
{
	struct si_info *sii;

	sii = (struct si_info *)sih;

	/* release FORCEHT since chip is going to "down" state */
	if (PCI_FORCEHT(sih))
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
	struct sbpciregs __iomem *regs = NULL;
	u32 siflag = 0, w;
	uint idx = 0;

	sii = (struct si_info *)sih;

	if (PCI(sih)) {
		/* get current core index */
		idx = sii->curidx;

		/* we interrupt on this backplane flag number */
		siflag = ai_flag(sih);

		/* switch over to pci core */
		regs = ai_setcoreidx(sih, sii->buscoreidx);
	}

	/*
	 * Enable sb->pci interrupts.  Assume
	 * PCI rev 2.3 support was added in pci core rev 6 and things changed..
	 */
	if (PCIE(sih) || (PCI(sih) && (ai_get_buscorerev(sih) >= 6))) {
		/* pci config write to set this core bit in PCIIntMask */
		pci_read_config_dword(sii->pcibus, PCI_INT_MASK, &w);
		w |= (coremask << PCI_SBIM_SHIFT);
		pci_write_config_dword(sii->pcibus, PCI_INT_MASK, w);
	} else {
		/* set sbintvec bit for our flag number */
		ai_setint(sih, siflag);
	}

	if (PCI(sih)) {
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
	void __iomem *regs = NULL;
	struct si_info *sii = (struct si_info *)sih;

	/* Fixup PI in SROM shadow area to enable the correct PCI core access */
	/* save the current index */
	origidx = ai_coreidx(&sii->pub);

	/* check 'pi' is correct and fix it if not */
	regs = ai_setcore(&sii->pub, ai_get_buscoretype(sih), 0);
	if (ai_get_buscoretype(sih) == PCIE_CORE_ID)
		pcicore_fixcfg_pcie(sii->pch,
				    (struct sbpcieregs __iomem *)regs);
	else if (ai_get_buscoretype(sih) == PCI_CORE_ID)
		pcicore_fixcfg_pci(sii->pch, (struct sbpciregs __iomem *)regs);

	/* restore the original index */
	ai_setcoreidx(&sii->pub, origidx);

	pcicore_hwup(sii->pch);
	return 0;
}

/* mask&set gpiocontrol bits */
u32 ai_gpiocontrol(struct si_pub *sih, u32 mask, u32 val, u8 priority)
{
	uint regoff;

	regoff = offsetof(struct chipcregs, gpiocontrol);
	return ai_cc_reg(sih, regoff, mask, val);
}

void ai_chipcontrl_epa4331(struct si_pub *sih, bool on)
{
	struct si_info *sii;
	struct chipcregs __iomem *cc;
	uint origidx;
	u32 val;

	sii = (struct si_info *)sih;
	origidx = ai_coreidx(sih);

	cc = (struct chipcregs __iomem *) ai_setcore(sih, CC_CORE_ID, 0);

	val = R_REG(&cc->chipcontrol);

	if (on) {
		if (ai_get_chippkg(sih) == 9 || ai_get_chippkg(sih) == 0xb)
			/* Ext PA Controls for 4331 12x9 Package */
			W_REG(&cc->chipcontrol, val |
			      CCTRL4331_EXTPA_EN |
			      CCTRL4331_EXTPA_ON_GPIO2_5);
		else
			/* Ext PA Controls for 4331 12x12 Package */
			W_REG(&cc->chipcontrol,
			      val | CCTRL4331_EXTPA_EN);
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
	struct chipcregs __iomem *cc;
	uint origidx;

	sii = (struct si_info *)sih;
	origidx = ai_coreidx(sih);

	cc = ai_setcore(sih, CC_CORE_ID, 0);

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

	sii = (struct si_info *)sih;

	pci_read_config_dword(sii->pcibus, PCI_VENDOR_ID, &w);
	if ((w & 0xFFFF) != PCI_VENDOR_ID_BROADCOM)
		return true;

	return false;
}

bool ai_is_sprom_available(struct si_pub *sih)
{
	struct si_info *sii = (struct si_info *)sih;

	if (ai_get_ccrev(sih) >= 31) {
		uint origidx;
		struct chipcregs __iomem *cc;
		u32 sromctrl;

		if ((ai_get_cccaps(sih) & CC_CAP_SROM) == 0)
			return false;

		origidx = sii->curidx;
		cc = ai_setcoreidx(sih, SI_CC_IDX);
		sromctrl = R_REG(&cc->sromcontrol);
		ai_setcoreidx(sih, origidx);
		return sromctrl & SRC_PRESENT;
	}

	switch (ai_get_chip_id(sih)) {
	case BCM4313_CHIP_ID:
		return (sii->chipst & CST4313_SPROM_PRESENT) != 0;
	default:
		return true;
	}
}

bool ai_is_otp_disabled(struct si_pub *sih)
{
	struct si_info *sii = (struct si_info *)sih;

	switch (ai_get_chip_id(sih)) {
	case BCM4313_CHIP_ID:
		return (sii->chipst & CST4313_OTP_PRESENT) == 0;
		/* These chips always have their OTP on */
	case BCM43224_CHIP_ID:
	case BCM43225_CHIP_ID:
	default:
		return false;
	}
}
