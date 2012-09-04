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

#include <defs.h>
#include <chipcommon.h>
#include <brcmu_utils.h>
#include <brcm_hw_ids.h>
#include <soc.h>
#include "types.h"
#include "pub.h"
#include "pmu.h"
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

#ifdef DEBUG
#define	SI_MSG(fmt, ...)	pr_debug(fmt, ##__VA_ARGS__)
#else
#define	SI_MSG(fmt, ...)	no_printk(fmt, ##__VA_ARGS__)
#endif				/* DEBUG */

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

static bool
ai_buscore_setup(struct si_info *sii, struct bcma_device *cc)
{
	/* no cores found, bail out */
	if (cc->bus->nr_cores == 0)
		return false;

	/* get chipcommon rev */
	sii->pub.ccrev = cc->id.rev;

	/* get chipcommon chipstatus */
	sii->chipst = bcma_read32(cc, CHIPCREGOFFS(chipstatus));

	/* get chipcommon capabilites */
	sii->pub.cccaps = bcma_read32(cc, CHIPCREGOFFS(capabilities));

	/* get pmu rev and caps */
	if (ai_get_cccaps(&sii->pub) & CC_CAP_PMU) {
		sii->pub.pmucaps = bcma_read32(cc,
					       CHIPCREGOFFS(pmucapabilities));
		sii->pub.pmurev = sii->pub.pmucaps & PCAP_REV_MASK;
	}

	return true;
}

static struct si_info *ai_doattach(struct si_info *sii,
				   struct bcma_bus *pbus)
{
	struct si_pub *sih = &sii->pub;
	struct bcma_device *cc;

	sii->icbus = pbus;
	sii->pcibus = pbus->host_pci;

	/* switch to Chipcommon core */
	cc = pbus->drv_cc.core;

	sih->chip = pbus->chipinfo.id;
	sih->chiprev = pbus->chipinfo.rev;
	sih->chippkg = pbus->chipinfo.pkg;
	sih->boardvendor = pbus->boardinfo.vendor;
	sih->boardtype = pbus->boardinfo.type;

	if (!ai_buscore_setup(sii, cc))
		goto exit;

	/* === NVRAM, clock is ready === */
	bcma_write32(cc, CHIPCREGOFFS(gpiopullup), 0);
	bcma_write32(cc, CHIPCREGOFFS(gpiopulldown), 0);

	/* PMU specific initializations */
	if (ai_get_cccaps(sih) & CC_CAP_PMU) {
		(void)si_pmu_measure_alpclk(sih);
	}

	return sii;

 exit:

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
	sii = kzalloc(sizeof(struct si_info), GFP_ATOMIC);
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

	sii = container_of(sih, struct si_info, pub);

	if (sii == NULL)
		return;

	kfree(sii);
}

/*
 * read/modify chipcommon core register.
 */
uint ai_cc_reg(struct si_pub *sih, uint regoff, u32 mask, u32 val)
{
	struct bcma_device *cc;
	u32 w;
	struct si_info *sii;

	sii = container_of(sih, struct si_info, pub);
	cc = sii->icbus->drv_cc.core;

	/* mask and set */
	if (mask || val)
		bcma_maskset32(cc, regoff, ~mask, val);

	/* readback */
	w = bcma_read32(cc, regoff);

	return w;
}

/* return the slow clock source - LPO, XTAL, or PCI */
static uint ai_slowclk_src(struct si_pub *sih, struct bcma_device *cc)
{
	return SCC_SS_XTAL;
}

/*
* return the ILP (slowclock) min or max frequency
* precondition: we've established the chip has dynamic clk control
*/
static uint ai_slowclk_freq(struct si_pub *sih, bool max_freq,
			    struct bcma_device *cc)
{
	uint div;

	/* Chipc rev 10 is InstaClock */
	div = bcma_read32(cc, CHIPCREGOFFS(system_clk_ctl));
	div = 4 * ((div >> SYCC_CD_SHIFT) + 1);
	return max_freq ? XTALMAXFREQ : (XTALMINFREQ / div);
}

static void
ai_clkctl_setdelay(struct si_pub *sih, struct bcma_device *cc)
{
	uint slowmaxfreq, pll_delay, slowclk;
	uint pll_on_delay, fref_sel_delay;

	pll_delay = PLL_DELAY;

	/*
	 * If the slow clock is not sourced by the xtal then
	 * add the xtal_on_delay since the xtal will also be
	 * powered down by dynamic clk control logic.
	 */

	slowclk = ai_slowclk_src(sih, cc);
	if (slowclk != SCC_SS_XTAL)
		pll_delay += XTAL_ON_DELAY;

	/* Starting with 4318 it is ILP that is used for the delays */
	slowmaxfreq =
	    ai_slowclk_freq(sih, false, cc);

	pll_on_delay = ((slowmaxfreq * pll_delay) + 999999) / 1000000;
	fref_sel_delay = ((slowmaxfreq * FREF_DELAY) + 999999) / 1000000;

	bcma_write32(cc, CHIPCREGOFFS(pll_on_delay), pll_on_delay);
	bcma_write32(cc, CHIPCREGOFFS(fref_sel_delay), fref_sel_delay);
}

/* initialize power control delay registers */
void ai_clkctl_init(struct si_pub *sih)
{
	struct si_info *sii = container_of(sih, struct si_info, pub);
	struct bcma_device *cc;

	if (!(ai_get_cccaps(sih) & CC_CAP_PWR_CTL))
		return;

	cc = sii->icbus->drv_cc.core;
	if (cc == NULL)
		return;

	/* set all Instaclk chip ILP to 1 MHz */
	bcma_maskset32(cc, CHIPCREGOFFS(system_clk_ctl), SYCC_CD_MASK,
		       (ILP_DIV_1MHZ << SYCC_CD_SHIFT));

	ai_clkctl_setdelay(sih, cc);
}

/*
 * return the value suitable for writing to the
 * dot11 core FAST_PWRUP_DELAY register
 */
u16 ai_clkctl_fast_pwrup_delay(struct si_pub *sih)
{
	struct si_info *sii;
	struct bcma_device *cc;
	uint slowminfreq;
	u16 fpdelay;

	sii = container_of(sih, struct si_info, pub);
	if (ai_get_cccaps(sih) & CC_CAP_PMU) {
		fpdelay = si_pmu_fast_pwrup_delay(sih);
		return fpdelay;
	}

	if (!(ai_get_cccaps(sih) & CC_CAP_PWR_CTL))
		return 0;

	fpdelay = 0;
	cc = sii->icbus->drv_cc.core;
	if (cc) {
		slowminfreq = ai_slowclk_freq(sih, false, cc);
		fpdelay = (((bcma_read32(cc, CHIPCREGOFFS(pll_on_delay)) + 2)
			    * 1000000) + (slowminfreq - 1)) / slowminfreq;
	}
	return fpdelay;
}

/*
 *  clock control policy function throught chipcommon
 *
 *    set dynamic clk control mode (forceslow, forcefast, dynamic)
 *    returns true if we are forcing fast clock
 *    this is a wrapper over the next internal function
 *      to allow flexible policy settings for outside caller
 */
bool ai_clkctl_cc(struct si_pub *sih, enum bcma_clkmode mode)
{
	struct si_info *sii;
	struct bcma_device *cc;

	sii = container_of(sih, struct si_info, pub);

	cc = sii->icbus->drv_cc.core;
	bcma_core_set_clockmode(cc, mode);
	return mode == BCMA_CLKMODE_FAST;
}

void ai_pci_up(struct si_pub *sih)
{
	struct si_info *sii;

	sii = container_of(sih, struct si_info, pub);

	if (sii->icbus->hosttype == BCMA_HOSTTYPE_PCI)
		bcma_core_pci_extend_L1timer(&sii->icbus->drv_pci, true);
}

/* Unconfigure and/or apply various WARs when going down */
void ai_pci_down(struct si_pub *sih)
{
	struct si_info *sii;

	sii = container_of(sih, struct si_info, pub);

	if (sii->icbus->hosttype == BCMA_HOSTTYPE_PCI)
		bcma_core_pci_extend_L1timer(&sii->icbus->drv_pci, false);
}

/* Enable BT-COEX & Ex-PA for 4313 */
void ai_epa_4313war(struct si_pub *sih)
{
	struct si_info *sii = container_of(sih, struct si_info, pub);
	struct bcma_device *cc;

	cc = sii->icbus->drv_cc.core;

	/* EPA Fix */
	bcma_set32(cc, CHIPCREGOFFS(gpiocontrol), GPIO_CTRL_EPA_EN_MASK);
}

/* check if the device is removed */
bool ai_deviceremoved(struct si_pub *sih)
{
	u32 w;
	struct si_info *sii;

	sii = container_of(sih, struct si_info, pub);

	if (sii->icbus->hosttype != BCMA_HOSTTYPE_PCI)
		return false;

	pci_read_config_dword(sii->pcibus, PCI_VENDOR_ID, &w);
	if ((w & 0xFFFF) != PCI_VENDOR_ID_BROADCOM)
		return true;

	return false;
}
