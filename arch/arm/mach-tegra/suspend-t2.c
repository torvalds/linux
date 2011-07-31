/*
 * arch/arm/mach-tegra/suspend-t2.c
 *
 * BootROM LP0 scratch register preservation for Tegra 2
 *
 * Copyright (c) 2009-2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/io.h>

#include <mach/gpio.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/suspend.h>

#include "gpio-names.h"

#define PMC_SCRATCH3	0x5c
#define PMC_SCRATCH5	0x64
#define PMC_SCRATCH6	0x68
#define PMC_SCRATCH7	0x6c
#define PMC_SCRATCH8	0x70
#define PMC_SCRATCH9	0x74
#define PMC_SCRATCH10	0x78
#define PMC_SCRATCH11	0x7c
#define PMC_SCRATCH12	0x80
#define PMC_SCRATCH13	0x84
#define PMC_SCRATCH14	0x88
#define PMC_SCRATCH15	0x8c
#define PMC_SCRATCH16	0x90
#define PMC_SCRATCH17	0x94
#define PMC_SCRATCH18	0x98
#define PMC_SCRATCH19	0x9c
#define PMC_SCRATCH20	0xa0
#define PMC_SCRATCH21	0xa4
#define PMC_SCRATCH22	0xa8
#define PMC_SCRATCH23	0xac
#define PMC_SCRATCH25	0x100
#define PMC_SCRATCH35	0x128
#define PMC_SCRATCH36	0x12c
#define PMC_SCRATCH40	0x13c

struct pmc_scratch_field {
	void __iomem *addr;
	unsigned int mask;
	int shift_src;
	int shift_dst;
};

#define field(module, offs, field, dst)					\
	{								\
		.addr = IO_ADDRESS(TEGRA_##module##_BASE) + offs,	\
		.mask = 0xfffffffful >> (31 - ((1?field) - (0?field))), 	\
		.shift_src = 0?field,					\
		.shift_dst = 0?dst,					\
	}

static const struct pmc_scratch_field pllx[] __initdata = {
	field(CLK_RESET, 0xe0, 22:20, 17:15), /* PLLX_DIVP */
	field(CLK_RESET, 0xe0, 17:8, 14:5), /* PLLX_DIVN */
	field(CLK_RESET, 0xe0, 4:0, 4:0), /* PLLX_DIVM */
	field(CLK_RESET, 0xe4, 11:8, 25:22), /* PLLX_CPCON */
	field(CLK_RESET, 0xe4, 7:4, 21:18), /* PLLX_LFCON */
	field(APB_MISC, 0x8e4, 27:24, 30:27), /* XM2CFGC_VREF_DQ */
	field(APB_MISC, 0x8c8, 3:3, 26:26), /* XM2CFGC_SCHMT_EN */
	field(APB_MISC, 0x8d0, 2:2, 31:31), /* XM2CLKCFG_PREEMP_EN */
};

static const struct pmc_scratch_field emc_0[] __initdata = {
	field(EMC, 0x3c, 4:0, 31:27), /* R2W */
	field(EMC, 0x34, 5:0, 20:15), /* RAS */
	field(EMC, 0x2c, 5:0, 5:0), /* RC */
	field(EMC, 0x30, 8:0, 14:6), /* RFC */
	field(EMC, 0x38, 5:0, 26:21), /* RP */
};

static const struct pmc_scratch_field emc_1[] __initdata = {
	field(EMC, 0x44, 4:0, 9:5), /* R2P */
	field(EMC, 0x4c, 5:0, 20:15), /* RD_RCD */
	field(EMC, 0x54, 3:0, 30:27), /* RRD */
	field(EMC, 0x48, 4:0, 14:10), /* W2P */
	field(EMC, 0x40, 4:0, 4:0), /* W2R */
	field(EMC, 0x50, 5:0, 26:21), /* WR_RCD */
};

static const struct pmc_scratch_field emc_2[] __initdata = {
	field(EMC, 0x2b8, 2:2, 31:31), /* CLKCHANGE_SR_ENABLE */
	field(EMC, 0x2b8, 10:10, 30:30), /* USE_ADDR_CLK */
	field(EMC, 0x80, 4:0, 29:25), /* PCHG2PDEN */
	field(EMC, 0x64, 3:0, 15:12), /* QRST */
	field(EMC, 0x68, 3:0, 19:16), /* QSAFE */
	field(EMC, 0x60, 3:0, 11:8), /* QUSE */
	field(EMC, 0x6c, 4:0, 24:20), /* RDV */
	field(EMC, 0x58, 3:0, 3:0), /* REXT */
	field(EMC, 0x5c, 3:0, 7:4), /* WDV */
};

static const struct pmc_scratch_field emc_3[] __initdata = {
	field(EMC, 0x74, 3:0, 19:16), /* BURST_REFRESH_NUM */
	field(EMC, 0x7c, 3:0, 27:24), /* PDEX2RD */
	field(EMC, 0x78, 3:0, 23:20), /* PDEX2WR */
	field(EMC, 0x70, 4:0, 4:0), /* REFRESH_LO */
	field(EMC, 0x70, 15:5, 15:5), /* REFRESH */
	field(EMC, 0xa0, 3:0, 31:28), /* TCLKSTABLE */
};

static const struct pmc_scratch_field emc_4[] __initdata = {
	field(EMC, 0x84, 4:0, 4:0), /* ACT2PDEN */
	field(EMC, 0x88, 4:0, 9:5), /* AR2PDEN */
	field(EMC, 0x8c, 5:0, 15:10), /* RW2PDEN */
	field(EMC, 0x94, 3:0, 31:28), /* TCKE */
	field(EMC, 0x90, 11:0, 27:16), /* TXSR */
};

static const struct pmc_scratch_field emc_5[] __initdata = {
	field(EMC, 0x8, 10:10, 30:30), /* AP_REQ_BUSY_CTRL */
	field(EMC, 0x8, 24:24, 31:31), /* CFG_PRIORITY */
	field(EMC, 0x8, 2:2, 26:26), /* FORCE_UPDATE */
	field(EMC, 0x8, 4:4, 27:27), /* MRS_WAIT */
	field(EMC, 0x8, 5:5, 28:28), /* PERIODIC_QRST */
	field(EMC, 0x8, 9:9, 29:29), /* READ_DQM_CTRL */
	field(EMC, 0x8, 0:0, 24:24), /* READ_MUX */
	field(EMC, 0x8, 1:1, 25:25), /* WRITE_MUX */
	field(EMC, 0xa4, 3:0, 9:6), /* TCLKSTOP */
	field(EMC, 0xa8, 13:0, 23:10), /* TREFBW */
	field(EMC, 0x9c, 5:0, 5:0), /* TRPAB */
};

static const struct pmc_scratch_field emc_6[] __initdata = {
	field(EMC, 0xfc, 1:0, 1:0), /* DQSIB_DLY_MSB_BYTE_0 */
	field(EMC, 0xfc, 9:8, 3:2), /* DQSIB_DLY_MSB_BYTE_1 */
	field(EMC, 0xfc, 17:16, 5:4), /* DQSIB_DLY_MSB_BYTE_2 */
	field(EMC, 0xfc, 25:24, 7:6), /* DQSIB_DLY_MSB_BYTE_3 */
	field(EMC, 0x110, 1:0, 9:8), /* QUSE_DLY_MSB_BYTE_0 */
	field(EMC, 0x110, 9:8, 11:10), /* QUSE_DLY_MSB_BYTE_1 */
	field(EMC, 0x110, 17:16, 13:12), /* QUSE_DLY_MSB_BYTE_2 */
	field(EMC, 0x110, 25:24, 15:14), /* QUSE_DLY_MSB_BYTE_3 */
	field(EMC, 0xac, 3:0, 25:22), /* QUSE_EXTRA */
	field(EMC, 0x98, 5:0, 21:16), /* TFAW */
	field(APB_MISC, 0x8e4, 5:5, 30:30), /* XM2CFGC_VREF_DQ_EN */
	field(APB_MISC, 0x8e4, 19:16, 29:26), /* XM2CFGC_VREF_DQS */
};

static const struct pmc_scratch_field emc_dqsib_dly[] __initdata = {
	field(EMC, 0xf8, 31:0, 31:0), /* DQSIB_DLY_BYTE_0 - DQSIB_DLY_BYTE_3*/
};

static const struct pmc_scratch_field emc_quse_dly[] __initdata = {
	field(EMC, 0x10c, 31:0, 31:0), /* QUSE_DLY_BYTE_0 - QUSE_DLY_BYTE_3*/
};

static const struct pmc_scratch_field emc_clktrim[] __initdata = {
	field(EMC, 0x2d0, 29:0, 29:0), /* DATA0_CLKTRIM - DATA3_CLKTRIM +
					* MCLK_ADDR_CLKTRIM */
};

static const struct pmc_scratch_field emc_autocal_fbio[] __initdata = {
	field(EMC, 0x2a4, 29:29, 29:29), /* AUTO_CAL_ENABLE */
	field(EMC, 0x2a4, 30:30, 30:30), /* AUTO_CAL_OVERRIDE */
	field(EMC, 0x2a4, 12:8, 18:14), /* AUTO_CAL_PD_OFFSET */
	field(EMC, 0x2a4, 4:0, 13:9), /* AUTO_CAL_PU_OFFSET */
	field(EMC, 0x2a4, 25:16, 28:19), /* AUTO_CAL_STEP */
	field(EMC, 0xf4, 16:16, 0:0), /* CFG_DEN_EARLY */
	field(EMC, 0x104, 8:8, 8:8), /* CTT_TERMINATION */
	field(EMC, 0x104, 7:7, 7:7), /* DIFFERENTIAL_DQS */
	field(EMC, 0x104, 9:9, 31:31), /* DQS_PULLD */
	field(EMC, 0x104, 1:0, 5:4), /* DRAM_TYPE */
	field(EMC, 0x104, 4:4, 6:6), /* DRAM_WIDTH */
	field(EMC, 0x114, 2:0, 3:1), /* CFG_QUSE_LATE */
};

static const struct pmc_scratch_field emc_autocal_interval[] __initdata = {
	field(EMC, 0x2a8, 27:0, 27:0), /* AUTOCAL_INTERVAL */
	field(EMC, 0x2b8, 1:1, 29:29), /* CLKCHANGE_PD_ENABLE */
	field(EMC, 0x2b8, 0:0, 28:28), /* CLKCHANGE_REQ_ENABLE */
	field(EMC, 0x2b8, 9:8, 31:30), /* PIN_CONFIG */
};

static const struct pmc_scratch_field emc_cfgs[] __initdata = {
	field(EMC, 0x10, 9:8, 4:3), /* EMEM_BANKWIDTH */
	field(EMC, 0x10, 2:0, 2:0), /* EMEM_COLWIDTH */
	field(EMC, 0x10, 19:16, 8:5), /* EMEM_DEVSIZE */
	field(EMC, 0x10, 25:24, 10:9), /* EMEM_NUMDEV */
	field(EMC, 0xc, 24:24, 21:21), /* AUTO_PRE_RD */
	field(EMC, 0xc, 25:25, 22:22), /* AUTO_PRE_WR */
	field(EMC, 0xc, 16:16, 20:20), /* CLEAR_AP_PREV_SPREQ */
	field(EMC, 0xc, 29:29, 23:23), /* DRAM_ACPD */
	field(EMC, 0xc, 30:30, 24:24), /* DRAM_CLKSTOP_PDSR_ONLY */
	field(EMC, 0xc, 31:31, 25:25), /* DRAM_CLKSTOP */
	field(EMC, 0xc, 15:8, 19:12), /* PRE_IDLE_CYCLES */
	field(EMC, 0xc, 0:0, 11:11), /* PRE_IDLE_EN */
	field(EMC, 0x2bc, 29:28, 29:28), /* CFG_DLL_LOCK_LIMIT */
	field(EMC, 0x2bc, 7:6, 31:30), /* CFG_DLL_MODE */
	field(MC, 0x10c, 0:0, 26:26), /* LL_CTRL */
	field(MC, 0x10c, 1:1, 27:27), /* LL_SEND_BOTH */
};

static const struct pmc_scratch_field emc_adr_cfg1[] __initdata = {
	field(EMC, 0x14, 9:8, 9:8), /* EMEM1_BANKWIDTH */
	field(EMC, 0x14, 2:0, 7:5), /* EMEM1_COLWIDTH */
	field(EMC, 0x14, 19:16, 13:10), /* EMEM1_DEVSIZE */
	field(EMC, 0x2dc, 28:24, 4:0), /* TERM_DRVUP */
	field(APB_MISC, 0x8d4, 3:0, 17:14), /* XM2COMP_VREF_SEL */
	field(APB_MISC, 0x8d8, 18:16, 23:21), /* XM2VTTGEN_CAL_DRVDN */
	field(APB_MISC, 0x8d8, 26:24, 20:18), /* XM2VTTGEN_CAL_DRVUP */
	field(APB_MISC, 0x8d8, 1:1, 30:30), /* XM2VTTGEN_SHORT_PWRGND */
	field(APB_MISC, 0x8d8, 0:0, 31:31), /* XM2VTTGEN_SHORT */
	field(APB_MISC, 0x8d8, 14:12, 26:24), /* XM2VTTGEN_VAUXP_LEVEL */
	field(APB_MISC, 0x8d8, 10:8, 29:27), /* XM2VTTGEN_VCLAMP_LEVEL */
};

static const struct pmc_scratch_field emc_digital_dll[] __initdata = {
	field(EMC, 0x2bc, 1:1, 23:23), /* DLI_TRIMMER_EN */
	field(EMC, 0x2bc, 0:0, 22:22), /* DLL_EN */
	field(EMC, 0x2bc, 5:5, 27:27), /* DLL_LOWSPEED */
	field(EMC, 0x2bc, 2:2, 24:24), /* DLL_OVERRIDE_EN */
	field(EMC, 0x2bc, 11:8, 31:28), /* DLL_UDSET */
	field(EMC, 0x2bc, 4:4, 26:26), /* PERBYTE_TRIMMER_OVERRIDE */
	field(EMC, 0x2bc, 3:3, 25:25), /* USE_SINGLE_DLL */
	field(MC, 0xc, 21:0, 21:0), /* EMEM_SIZE_KB */
};

static const struct pmc_scratch_field emc_dqs_clktrim[] __initdata = {
	field(EMC, 0x2d4, 29:0, 29:0), /* DQS0_CLKTRIM - DQS3 + MCLK*/
	field(APB_MISC, 0x8e4, 3:3, 31:31), /* XM2CFGC_CTT_HIZ_EN */
	field(APB_MISC, 0x8e4, 4:4, 30:30), /* XM2CFGC_VREF_DQS_EN */
};

static const struct pmc_scratch_field emc_dq_clktrim[] __initdata = {
	field(EMC, 0x2d8, 29:0, 29:0),
	field(APB_MISC, 0x8e4, 2:2, 30:30), /* XM2CFGC_PREEMP_EN */
	field(APB_MISC, 0x8e4, 0:0, 31:31), /* XM2CFGC_RX_FT_REC_EN */
};

static const struct pmc_scratch_field emc_dll_xform_dqs[] __initdata = {
	field(EMC, 0x2bc, 25:16, 29:20), /* CFG_DLL_OVERRIDE_VAL */
	field(EMC, 0x2c0, 4:0, 4:0), /* DQS_MULT */
	field(EMC, 0x2c0, 22:8, 19:5), /* DQS_OFFS */
	field(MC, 0x10c, 31:31, 30:30), /* LL_DRAM_INTERLEAVE */
};

static const struct pmc_scratch_field emc_odt_rw[] __initdata = {
	field(EMC, 0x2c4, 4:0, 4:0), /* QUSE_MULT */
	field(EMC, 0x2c4, 22:8, 19:5), /* QUSE_OFF */
	field(EMC, 0xb4, 31:31, 29:29), /* DISABLE_ODT_DURING_READ */
	field(EMC, 0xb4, 30:30, 28:28), /* B4_READ */
	field(EMC, 0xb4, 2:0, 27:25), /* RD_DELAY */
	field(EMC, 0xb0, 31:31, 24:24), /* ENABLE_ODT_DURING_WRITE */
	field(EMC, 0xb0, 30:30, 23:23), /* B4_WRITE */
	field(EMC, 0xb0, 2:0, 22:20), /* WR_DELAY */
};

static const struct pmc_scratch_field arbitration_xbar[] __initdata = {
	field(AHB_GIZMO, 0xdc, 31:0, 31:0),
};

static const struct pmc_scratch_field emc_zcal[] __initdata = {
	field(EMC, 0x2e0, 23:0, 23:0), /* ZCAL_REF_INTERVAL */
	field(EMC, 0x2e4, 7:0, 31:24), /* ZCAL_WAIT_CNT */
};

static const struct pmc_scratch_field emc_ctt_term[] __initdata = {
	field(EMC, 0x2dc, 19:15, 30:26), /* TERM_DRVDN */
	field(EMC, 0x2dc, 12:8, 25:21), /* TERM_OFFSET */
	field(EMC, 0x2dc, 31:31, 31:31), /* TERM_OVERRIDE */
	field(EMC, 0x2dc, 2:0, 20:18), /* TERM_SLOPE */
	field(EMC, 0x2e8, 23:16, 15:8), /* ZQ_MRW_MA */
	field(EMC, 0x2e8, 7:0, 7:0), /* ZQ_MRW_OP */
};

static const struct pmc_scratch_field xm2_cfgd[] __initdata = {
	field(APB_MISC, 0x8e8, 18:16, 11:9), /* CFGD0_DLYIN_TRM */
	field(APB_MISC, 0x8e8, 22:20, 8:6), /* CFGD1_DLYIN_TRM */
	field(APB_MISC, 0x8e8, 26:24, 5:3), /* CFGD2_DLYIN_TRM */
	field(APB_MISC, 0x8e8, 30:28, 2:0), /* CFGD3_DLYIN_TRM */
	field(APB_MISC, 0x8e8, 3:3, 12:12), /* XM2CFGD_CTT_HIZ_EN */
	field(APB_MISC, 0x8e8, 2:2, 13:13), /* XM2CFGD_PREEMP_EN */
	field(APB_MISC, 0x8e8, 0:0, 14:14), /* CM2CFGD_RX_FT_REC_EN */
};

struct pmc_scratch_reg {
	const struct pmc_scratch_field *fields;
	void __iomem *scratch_addr;
	int num_fields;
};

#define scratch(offs, field_list)					\
	{								\
		.scratch_addr = IO_ADDRESS(TEGRA_PMC_BASE) + offs,	\
		.fields = field_list,					\
		.num_fields = ARRAY_SIZE(field_list),			\
	}

static const struct pmc_scratch_reg scratch[] __initdata = {
	scratch(PMC_SCRATCH3, pllx),
	scratch(PMC_SCRATCH5, emc_0),
	scratch(PMC_SCRATCH6, emc_1),
	scratch(PMC_SCRATCH7, emc_2),
	scratch(PMC_SCRATCH8, emc_3),
	scratch(PMC_SCRATCH9, emc_4),
	scratch(PMC_SCRATCH10, emc_5),
	scratch(PMC_SCRATCH11, emc_6),
	scratch(PMC_SCRATCH12, emc_dqsib_dly),
	scratch(PMC_SCRATCH13, emc_quse_dly),
	scratch(PMC_SCRATCH14, emc_clktrim),
	scratch(PMC_SCRATCH15, emc_autocal_fbio),
	scratch(PMC_SCRATCH16, emc_autocal_interval),
	scratch(PMC_SCRATCH17, emc_cfgs),
	scratch(PMC_SCRATCH18, emc_adr_cfg1),
	scratch(PMC_SCRATCH19, emc_digital_dll),
	scratch(PMC_SCRATCH20, emc_dqs_clktrim),
	scratch(PMC_SCRATCH21, emc_dq_clktrim),
	scratch(PMC_SCRATCH22, emc_dll_xform_dqs),
	scratch(PMC_SCRATCH23, emc_odt_rw),
	scratch(PMC_SCRATCH25, arbitration_xbar),
	scratch(PMC_SCRATCH35, emc_zcal),
	scratch(PMC_SCRATCH36, emc_ctt_term),
	scratch(PMC_SCRATCH40, xm2_cfgd),
};

void __init lp0_suspend_init(void)
{
	int i;

	for (i=0; i<ARRAY_SIZE(scratch); i++) {
		unsigned int r = 0;
		int j;

		for (j=0; j<scratch[i].num_fields; j++) {
			unsigned int v = readl(scratch[i].fields[j].addr);
			v >>= scratch[i].fields[j].shift_src;
			v &= scratch[i].fields[j].mask;
			v <<= scratch[i].fields[j].shift_dst;
			r |= v;
		}

		writel(r, scratch[i].scratch_addr);
	}
}

#define NUM_WAKE_EVENTS 31

static int tegra_wake_event_irq[NUM_WAKE_EVENTS] = {
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PO5),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV3),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PL1),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PB6),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PN7),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PA0),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PU5),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PU6),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PC7),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PS2),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PAA1),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PW3),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PW2),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PY6),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV6),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PJ7),
	INT_RTC,
	INT_KBC,
	INT_EXTERNAL_PMU,
	-EINVAL, /* TEGRA_USB1_VBUS, */
	-EINVAL, /* TEGRA_USB3_VBUS, */
	-EINVAL, /* TEGRA_USB1_ID, */
	-EINVAL, /* TEGRA_USB3_ID, */
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PI5),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PV2),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PS4),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PS5),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PS0),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PQ6),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PQ7),
	TEGRA_GPIO_TO_IRQ(TEGRA_GPIO_PN2),
};

int tegra_irq_to_wake(int irq)
{
	int i;
	for (i = 0; i < NUM_WAKE_EVENTS; i++)
		if (tegra_wake_event_irq[i] == irq)
			return i;

	return -EINVAL;
}

int tegra_wake_to_irq(int wake)
{
	if (wake < 0)
		return -EINVAL;

	if (wake >= NUM_WAKE_EVENTS)
		return -EINVAL;

	return tegra_wake_event_irq[wake];
}
