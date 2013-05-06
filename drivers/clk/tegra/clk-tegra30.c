/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/io.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/tegra.h>

#include <mach/powergate.h>

#include "clk.h"

#define RST_DEVICES_L 0x004
#define RST_DEVICES_H 0x008
#define RST_DEVICES_U 0x00c
#define RST_DEVICES_V 0x358
#define RST_DEVICES_W 0x35c
#define RST_DEVICES_SET_L 0x300
#define RST_DEVICES_CLR_L 0x304
#define RST_DEVICES_SET_H 0x308
#define RST_DEVICES_CLR_H 0x30c
#define RST_DEVICES_SET_U 0x310
#define RST_DEVICES_CLR_U 0x314
#define RST_DEVICES_SET_V 0x430
#define RST_DEVICES_CLR_V 0x434
#define RST_DEVICES_SET_W 0x438
#define RST_DEVICES_CLR_W 0x43c
#define RST_DEVICES_NUM 5

#define CLK_OUT_ENB_L 0x010
#define CLK_OUT_ENB_H 0x014
#define CLK_OUT_ENB_U 0x018
#define CLK_OUT_ENB_V 0x360
#define CLK_OUT_ENB_W 0x364
#define CLK_OUT_ENB_SET_L 0x320
#define CLK_OUT_ENB_CLR_L 0x324
#define CLK_OUT_ENB_SET_H 0x328
#define CLK_OUT_ENB_CLR_H 0x32c
#define CLK_OUT_ENB_SET_U 0x330
#define CLK_OUT_ENB_CLR_U 0x334
#define CLK_OUT_ENB_SET_V 0x440
#define CLK_OUT_ENB_CLR_V 0x444
#define CLK_OUT_ENB_SET_W 0x448
#define CLK_OUT_ENB_CLR_W 0x44c
#define CLK_OUT_ENB_NUM 5

#define OSC_CTRL			0x50
#define OSC_CTRL_OSC_FREQ_MASK		(0xF<<28)
#define OSC_CTRL_OSC_FREQ_13MHZ		(0X0<<28)
#define OSC_CTRL_OSC_FREQ_19_2MHZ	(0X4<<28)
#define OSC_CTRL_OSC_FREQ_12MHZ		(0X8<<28)
#define OSC_CTRL_OSC_FREQ_26MHZ		(0XC<<28)
#define OSC_CTRL_OSC_FREQ_16_8MHZ	(0X1<<28)
#define OSC_CTRL_OSC_FREQ_38_4MHZ	(0X5<<28)
#define OSC_CTRL_OSC_FREQ_48MHZ		(0X9<<28)
#define OSC_CTRL_MASK			(0x3f2 | OSC_CTRL_OSC_FREQ_MASK)

#define OSC_CTRL_PLL_REF_DIV_MASK	(3<<26)
#define OSC_CTRL_PLL_REF_DIV_1		(0<<26)
#define OSC_CTRL_PLL_REF_DIV_2		(1<<26)
#define OSC_CTRL_PLL_REF_DIV_4		(2<<26)

#define OSC_FREQ_DET			0x58
#define OSC_FREQ_DET_TRIG		BIT(31)

#define OSC_FREQ_DET_STATUS		0x5c
#define OSC_FREQ_DET_BUSY		BIT(31)
#define OSC_FREQ_DET_CNT_MASK		0xffff

#define CCLKG_BURST_POLICY 0x368
#define SUPER_CCLKG_DIVIDER 0x36c
#define CCLKLP_BURST_POLICY 0x370
#define SUPER_CCLKLP_DIVIDER 0x374
#define SCLK_BURST_POLICY 0x028
#define SUPER_SCLK_DIVIDER 0x02c

#define SYSTEM_CLK_RATE 0x030

#define PLLC_BASE 0x80
#define PLLC_MISC 0x8c
#define PLLM_BASE 0x90
#define PLLM_MISC 0x9c
#define PLLP_BASE 0xa0
#define PLLP_MISC 0xac
#define PLLX_BASE 0xe0
#define PLLX_MISC 0xe4
#define PLLD_BASE 0xd0
#define PLLD_MISC 0xdc
#define PLLD2_BASE 0x4b8
#define PLLD2_MISC 0x4bc
#define PLLE_BASE 0xe8
#define PLLE_MISC 0xec
#define PLLA_BASE 0xb0
#define PLLA_MISC 0xbc
#define PLLU_BASE 0xc0
#define PLLU_MISC 0xcc

#define PLL_MISC_LOCK_ENABLE 18
#define PLLDU_MISC_LOCK_ENABLE 22
#define PLLE_MISC_LOCK_ENABLE 9

#define PLL_BASE_LOCK BIT(27)
#define PLLE_MISC_LOCK BIT(11)

#define PLLE_AUX 0x48c
#define PLLC_OUT 0x84
#define PLLM_OUT 0x94
#define PLLP_OUTA 0xa4
#define PLLP_OUTB 0xa8
#define PLLA_OUT 0xb4

#define AUDIO_SYNC_CLK_I2S0 0x4a0
#define AUDIO_SYNC_CLK_I2S1 0x4a4
#define AUDIO_SYNC_CLK_I2S2 0x4a8
#define AUDIO_SYNC_CLK_I2S3 0x4ac
#define AUDIO_SYNC_CLK_I2S4 0x4b0
#define AUDIO_SYNC_CLK_SPDIF 0x4b4

#define PMC_CLK_OUT_CNTRL 0x1a8

#define CLK_SOURCE_I2S0 0x1d8
#define CLK_SOURCE_I2S1 0x100
#define CLK_SOURCE_I2S2 0x104
#define CLK_SOURCE_I2S3 0x3bc
#define CLK_SOURCE_I2S4 0x3c0
#define CLK_SOURCE_SPDIF_OUT 0x108
#define CLK_SOURCE_SPDIF_IN 0x10c
#define CLK_SOURCE_PWM 0x110
#define CLK_SOURCE_D_AUDIO 0x3d0
#define CLK_SOURCE_DAM0 0x3d8
#define CLK_SOURCE_DAM1 0x3dc
#define CLK_SOURCE_DAM2 0x3e0
#define CLK_SOURCE_HDA 0x428
#define CLK_SOURCE_HDA2CODEC_2X 0x3e4
#define CLK_SOURCE_SBC1 0x134
#define CLK_SOURCE_SBC2 0x118
#define CLK_SOURCE_SBC3 0x11c
#define CLK_SOURCE_SBC4 0x1b4
#define CLK_SOURCE_SBC5 0x3c8
#define CLK_SOURCE_SBC6 0x3cc
#define CLK_SOURCE_SATA_OOB 0x420
#define CLK_SOURCE_SATA 0x424
#define CLK_SOURCE_NDFLASH 0x160
#define CLK_SOURCE_NDSPEED 0x3f8
#define CLK_SOURCE_VFIR 0x168
#define CLK_SOURCE_SDMMC1 0x150
#define CLK_SOURCE_SDMMC2 0x154
#define CLK_SOURCE_SDMMC3 0x1bc
#define CLK_SOURCE_SDMMC4 0x164
#define CLK_SOURCE_VDE 0x1c8
#define CLK_SOURCE_CSITE 0x1d4
#define CLK_SOURCE_LA 0x1f8
#define CLK_SOURCE_OWR 0x1cc
#define CLK_SOURCE_NOR 0x1d0
#define CLK_SOURCE_MIPI 0x174
#define CLK_SOURCE_I2C1 0x124
#define CLK_SOURCE_I2C2 0x198
#define CLK_SOURCE_I2C3 0x1b8
#define CLK_SOURCE_I2C4 0x3c4
#define CLK_SOURCE_I2C5 0x128
#define CLK_SOURCE_UARTA 0x178
#define CLK_SOURCE_UARTB 0x17c
#define CLK_SOURCE_UARTC 0x1a0
#define CLK_SOURCE_UARTD 0x1c0
#define CLK_SOURCE_UARTE 0x1c4
#define CLK_SOURCE_VI 0x148
#define CLK_SOURCE_VI_SENSOR 0x1a8
#define CLK_SOURCE_3D 0x158
#define CLK_SOURCE_3D2 0x3b0
#define CLK_SOURCE_2D 0x15c
#define CLK_SOURCE_EPP 0x16c
#define CLK_SOURCE_MPE 0x170
#define CLK_SOURCE_HOST1X 0x180
#define CLK_SOURCE_CVE 0x140
#define CLK_SOURCE_TVO 0x188
#define CLK_SOURCE_DTV 0x1dc
#define CLK_SOURCE_HDMI 0x18c
#define CLK_SOURCE_TVDAC 0x194
#define CLK_SOURCE_DISP1 0x138
#define CLK_SOURCE_DISP2 0x13c
#define CLK_SOURCE_DSIB 0xd0
#define CLK_SOURCE_TSENSOR 0x3b8
#define CLK_SOURCE_ACTMON 0x3e8
#define CLK_SOURCE_EXTERN1 0x3ec
#define CLK_SOURCE_EXTERN2 0x3f0
#define CLK_SOURCE_EXTERN3 0x3f4
#define CLK_SOURCE_I2CSLOW 0x3fc
#define CLK_SOURCE_SE 0x42c
#define CLK_SOURCE_MSELECT 0x3b4
#define CLK_SOURCE_EMC 0x19c

#define AUDIO_SYNC_DOUBLER 0x49c

#define PMC_CTRL 0
#define PMC_CTRL_BLINK_ENB 7

#define PMC_DPD_PADS_ORIDE 0x1c
#define PMC_DPD_PADS_ORIDE_BLINK_ENB 20
#define PMC_BLINK_TIMER 0x40

#define UTMIP_PLL_CFG2 0x488
#define UTMIP_PLL_CFG2_STABLE_COUNT(x) (((x) & 0xffff) << 6)
#define UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(x) (((x) & 0x3f) << 18)
#define UTMIP_PLL_CFG2_FORCE_PD_SAMP_A_POWERDOWN BIT(0)
#define UTMIP_PLL_CFG2_FORCE_PD_SAMP_B_POWERDOWN BIT(2)
#define UTMIP_PLL_CFG2_FORCE_PD_SAMP_C_POWERDOWN BIT(4)

#define UTMIP_PLL_CFG1 0x484
#define UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(x) (((x) & 0x1f) << 6)
#define UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(x) (((x) & 0xfff) << 0)
#define UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERDOWN BIT(14)
#define UTMIP_PLL_CFG1_FORCE_PLL_ACTIVE_POWERDOWN BIT(12)
#define UTMIP_PLL_CFG1_FORCE_PLLU_POWERDOWN BIT(16)

/* Tegra CPU clock and reset control regs */
#define TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX		0x4c
#define TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET	0x340
#define TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_CLR	0x344
#define TEGRA30_CLK_RST_CONTROLLER_CLK_CPU_CMPLX_CLR	0x34c
#define TEGRA30_CLK_RST_CONTROLLER_CPU_CMPLX_STATUS	0x470

#define CPU_CLOCK(cpu)	(0x1 << (8 + cpu))
#define CPU_RESET(cpu)	(0x1111ul << (cpu))

#define CLK_RESET_CCLK_BURST	0x20
#define CLK_RESET_CCLK_DIVIDER	0x24
#define CLK_RESET_PLLX_BASE	0xe0
#define CLK_RESET_PLLX_MISC	0xe4

#define CLK_RESET_SOURCE_CSITE	0x1d4

#define CLK_RESET_CCLK_BURST_POLICY_SHIFT	28
#define CLK_RESET_CCLK_RUN_POLICY_SHIFT		4
#define CLK_RESET_CCLK_IDLE_POLICY_SHIFT	0
#define CLK_RESET_CCLK_IDLE_POLICY		1
#define CLK_RESET_CCLK_RUN_POLICY		2
#define CLK_RESET_CCLK_BURST_POLICY_PLLX	8

#ifdef CONFIG_PM_SLEEP
static struct cpu_clk_suspend_context {
	u32 pllx_misc;
	u32 pllx_base;

	u32 cpu_burst;
	u32 clk_csite_src;
	u32 cclk_divider;
} tegra30_cpu_clk_sctx;
#endif

static int periph_clk_enb_refcnt[CLK_OUT_ENB_NUM * 32];

static void __iomem *clk_base;
static void __iomem *pmc_base;
static unsigned long input_freq;

static DEFINE_SPINLOCK(clk_doubler_lock);
static DEFINE_SPINLOCK(clk_out_lock);
static DEFINE_SPINLOCK(pll_div_lock);
static DEFINE_SPINLOCK(cml_lock);
static DEFINE_SPINLOCK(pll_d_lock);
static DEFINE_SPINLOCK(sysrate_lock);

#define TEGRA_INIT_DATA_MUX(_name, _con_id, _dev_id, _parents, _offset,	\
			    _clk_num, _regs, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA(_name, _con_id, _dev_id, _parents, _offset,	\
			30, 2, 0, 0, 8, 1, 0, _regs, _clk_num,		\
			periph_clk_enb_refcnt, _gate_flags, _clk_id)

#define TEGRA_INIT_DATA_DIV16(_name, _con_id, _dev_id, _parents, _offset, \
			    _clk_num, _regs, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA(_name, _con_id, _dev_id, _parents, _offset,	\
			30, 2, 0, 0, 16, 0, TEGRA_DIVIDER_ROUND_UP,	\
			_regs, _clk_num, periph_clk_enb_refcnt,		\
			_gate_flags, _clk_id)

#define TEGRA_INIT_DATA_MUX8(_name, _con_id, _dev_id, _parents, _offset, \
			     _clk_num, _regs, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA(_name, _con_id, _dev_id, _parents, _offset,	\
			29, 3, 0, 0, 8, 1, 0, _regs, _clk_num,		\
			periph_clk_enb_refcnt, _gate_flags, _clk_id)

#define TEGRA_INIT_DATA_INT(_name, _con_id, _dev_id, _parents, _offset,	\
			    _clk_num, _regs, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA(_name, _con_id, _dev_id, _parents, _offset,	\
			30, 2, 0, 0, 8, 1, TEGRA_DIVIDER_INT, _regs,	\
			_clk_num, periph_clk_enb_refcnt, _gate_flags,	\
			_clk_id)

#define TEGRA_INIT_DATA_UART(_name, _con_id, _dev_id, _parents, _offset,\
			     _clk_num, _regs, _clk_id)			\
	TEGRA_INIT_DATA(_name, _con_id, _dev_id, _parents, _offset,	\
			30, 2, 0, 0, 16, 1, TEGRA_DIVIDER_UART, _regs,	\
			_clk_num, periph_clk_enb_refcnt, 0, _clk_id)

#define TEGRA_INIT_DATA_NODIV(_name, _con_id, _dev_id, _parents, _offset, \
			      _mux_shift, _mux_width, _clk_num, _regs,	\
			      _gate_flags, _clk_id)			\
	TEGRA_INIT_DATA(_name, _con_id, _dev_id, _parents, _offset,	\
			_mux_shift, _mux_width, 0, 0, 0, 0, 0, _regs,	\
			_clk_num, periph_clk_enb_refcnt, _gate_flags,	\
			_clk_id)

/*
 * IDs assigned here must be in sync with DT bindings definition
 * for Tegra30 clocks.
 */
enum tegra30_clk {
	cpu, rtc = 4, timer, uarta, gpio = 8, sdmmc2, i2s1 = 11, i2c1, ndflash,
	sdmmc1, sdmmc4, pwm = 17, i2s2, epp, gr2d = 21, usbd, isp, gr3d,
	disp2 = 26, disp1, host1x, vcp, i2s0, cop_cache, mc, ahbdma, apbdma,
	kbc = 36, statmon, pmc, kfuse = 40, sbc1, nor, sbc2 = 44, sbc3 = 46,
	i2c5, dsia, mipi = 50, hdmi, csi, tvdac, i2c2, uartc, emc = 57, usb2,
	usb3, mpe, vde, bsea, bsev, speedo, uartd, uarte, i2c3, sbc4, sdmmc3,
	pcie, owr, afi, csite, pciex, avpucq, la, dtv = 79, ndspeed, i2cslow,
	dsib, irama = 84, iramb, iramc, iramd, cram2, audio_2x = 90, csus = 92,
	cdev2, cdev1, cpu_g = 96, cpu_lp, gr3d2, mselect, tsensor, i2s3, i2s4,
	i2c4, sbc5, sbc6, d_audio, apbif, dam0, dam1, dam2, hda2codec_2x,
	atomics, audio0_2x, audio1_2x, audio2_2x, audio3_2x, audio4_2x,
	spdif_2x, actmon, extern1, extern2, extern3, sata_oob, sata, hda,
	se = 127, hda2hdmi, sata_cold, uartb = 160, vfir, spdif_in, spdif_out,
	vi, vi_sensor, fuse, fuse_burn, cve, tvo, clk_32k, clk_m, clk_m_div2,
	clk_m_div4, pll_ref, pll_c, pll_c_out1, pll_m, pll_m_out1, pll_p,
	pll_p_out1, pll_p_out2, pll_p_out3, pll_p_out4, pll_a, pll_a_out0,
	pll_d, pll_d_out0, pll_d2, pll_d2_out0, pll_u, pll_x, pll_x_out0, pll_e,
	spdif_in_sync, i2s0_sync, i2s1_sync, i2s2_sync, i2s3_sync, i2s4_sync,
	vimclk_sync, audio0, audio1, audio2, audio3, audio4, spdif, clk_out_1,
	clk_out_2, clk_out_3, sclk, blink, cclk_g, cclk_lp, twd, cml0, cml1,
	hclk, pclk, clk_out_1_mux = 300, clk_max
};

static struct clk *clks[clk_max];
static struct clk_onecell_data clk_data;

/*
 * Structure defining the fields for USB UTMI clocks Parameters.
 */
struct utmi_clk_param {
	/* Oscillator Frequency in KHz */
	u32 osc_frequency;
	/* UTMIP PLL Enable Delay Count  */
	u8 enable_delay_count;
	/* UTMIP PLL Stable count */
	u8 stable_count;
	/*  UTMIP PLL Active delay count */
	u8 active_delay_count;
	/* UTMIP PLL Xtal frequency count */
	u8 xtal_freq_count;
};

static const struct utmi_clk_param utmi_parameters[] = {
/*	OSC_FREQUENCY, ENABLE_DLY, STABLE_CNT, ACTIVE_DLY, XTAL_FREQ_CNT */
	{13000000,     0x02,       0x33,       0x05,       0x7F},
	{19200000,     0x03,       0x4B,       0x06,       0xBB},
	{12000000,     0x02,       0x2F,       0x04,       0x76},
	{26000000,     0x04,       0x66,       0x09,       0xFE},
	{16800000,     0x03,       0x41,       0x0A,       0xA4},
};

static struct tegra_clk_pll_freq_table pll_c_freq_table[] = {
	{ 12000000, 1040000000, 520,  6, 0, 8},
	{ 13000000, 1040000000, 480,  6, 0, 8},
	{ 16800000, 1040000000, 495,  8, 0, 8},	/* actual: 1039.5 MHz */
	{ 19200000, 1040000000, 325,  6, 0, 6},
	{ 26000000, 1040000000, 520, 13, 0, 8},

	{ 12000000, 832000000, 416,  6, 0, 8},
	{ 13000000, 832000000, 832, 13, 0, 8},
	{ 16800000, 832000000, 396,  8, 0, 8},	/* actual: 831.6 MHz */
	{ 19200000, 832000000, 260,  6, 0, 8},
	{ 26000000, 832000000, 416, 13, 0, 8},

	{ 12000000, 624000000, 624, 12, 0, 8},
	{ 13000000, 624000000, 624, 13, 0, 8},
	{ 16800000, 600000000, 520, 14, 0, 8},
	{ 19200000, 624000000, 520, 16, 0, 8},
	{ 26000000, 624000000, 624, 26, 0, 8},

	{ 12000000, 600000000, 600, 12, 0, 8},
	{ 13000000, 600000000, 600, 13, 0, 8},
	{ 16800000, 600000000, 500, 14, 0, 8},
	{ 19200000, 600000000, 375, 12, 0, 6},
	{ 26000000, 600000000, 600, 26, 0, 8},

	{ 12000000, 520000000, 520, 12, 0, 8},
	{ 13000000, 520000000, 520, 13, 0, 8},
	{ 16800000, 520000000, 495, 16, 0, 8},	/* actual: 519.75 MHz */
	{ 19200000, 520000000, 325, 12, 0, 6},
	{ 26000000, 520000000, 520, 26, 0, 8},

	{ 12000000, 416000000, 416, 12, 0, 8},
	{ 13000000, 416000000, 416, 13, 0, 8},
	{ 16800000, 416000000, 396, 16, 0, 8},	/* actual: 415.8 MHz */
	{ 19200000, 416000000, 260, 12, 0, 6},
	{ 26000000, 416000000, 416, 26, 0, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_m_freq_table[] = {
	{ 12000000, 666000000, 666, 12, 0, 8},
	{ 13000000, 666000000, 666, 13, 0, 8},
	{ 16800000, 666000000, 555, 14, 0, 8},
	{ 19200000, 666000000, 555, 16, 0, 8},
	{ 26000000, 666000000, 666, 26, 0, 8},
	{ 12000000, 600000000, 600, 12, 0, 8},
	{ 13000000, 600000000, 600, 13, 0, 8},
	{ 16800000, 600000000, 500, 14, 0, 8},
	{ 19200000, 600000000, 375, 12, 0, 6},
	{ 26000000, 600000000, 600, 26, 0, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_p_freq_table[] = {
	{ 12000000, 216000000, 432, 12, 1, 8},
	{ 13000000, 216000000, 432, 13, 1, 8},
	{ 16800000, 216000000, 360, 14, 1, 8},
	{ 19200000, 216000000, 360, 16, 1, 8},
	{ 26000000, 216000000, 432, 26, 1, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_a_freq_table[] = {
	{ 9600000, 564480000, 294, 5, 0, 4},
	{ 9600000, 552960000, 288, 5, 0, 4},
	{ 9600000, 24000000,  5,   2, 0, 1},

	{ 28800000, 56448000, 49, 25, 0, 1},
	{ 28800000, 73728000, 64, 25, 0, 1},
	{ 28800000, 24000000,  5,  6, 0, 1},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_d_freq_table[] = {
	{ 12000000, 216000000, 216, 12, 0, 4},
	{ 13000000, 216000000, 216, 13, 0, 4},
	{ 16800000, 216000000, 180, 14, 0, 4},
	{ 19200000, 216000000, 180, 16, 0, 4},
	{ 26000000, 216000000, 216, 26, 0, 4},

	{ 12000000, 594000000, 594, 12, 0, 8},
	{ 13000000, 594000000, 594, 13, 0, 8},
	{ 16800000, 594000000, 495, 14, 0, 8},
	{ 19200000, 594000000, 495, 16, 0, 8},
	{ 26000000, 594000000, 594, 26, 0, 8},

	{ 12000000, 1000000000, 1000, 12, 0, 12},
	{ 13000000, 1000000000, 1000, 13, 0, 12},
	{ 19200000, 1000000000, 625,  12, 0, 8},
	{ 26000000, 1000000000, 1000, 26, 0, 12},

	{ 0, 0, 0, 0, 0, 0 },
};

static struct pdiv_map pllu_p[] = {
	{ .pdiv = 1, .hw_val = 1 },
	{ .pdiv = 2, .hw_val = 0 },
	{ .pdiv = 0, .hw_val = 0 },
};

static struct tegra_clk_pll_freq_table pll_u_freq_table[] = {
	{ 12000000, 480000000, 960, 12, 0, 12},
	{ 13000000, 480000000, 960, 13, 0, 12},
	{ 16800000, 480000000, 400, 7,  0, 5},
	{ 19200000, 480000000, 200, 4,  0, 3},
	{ 26000000, 480000000, 960, 26, 0, 12},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_x_freq_table[] = {
	/* 1.7 GHz */
	{ 12000000, 1700000000, 850,  6,  0, 8},
	{ 13000000, 1700000000, 915,  7,  0, 8},	/* actual: 1699.2 MHz */
	{ 16800000, 1700000000, 708,  7,  0, 8},	/* actual: 1699.2 MHz */
	{ 19200000, 1700000000, 885,  10, 0, 8},	/* actual: 1699.2 MHz */
	{ 26000000, 1700000000, 850,  13, 0, 8},

	/* 1.6 GHz */
	{ 12000000, 1600000000, 800,  6,  0, 8},
	{ 13000000, 1600000000, 738,  6,  0, 8},	/* actual: 1599.0 MHz */
	{ 16800000, 1600000000, 857,  9,  0, 8},	/* actual: 1599.7 MHz */
	{ 19200000, 1600000000, 500,  6,  0, 8},
	{ 26000000, 1600000000, 800,  13, 0, 8},

	/* 1.5 GHz */
	{ 12000000, 1500000000, 750,  6,  0, 8},
	{ 13000000, 1500000000, 923,  8,  0, 8},	/* actual: 1499.8 MHz */
	{ 16800000, 1500000000, 625,  7,  0, 8},
	{ 19200000, 1500000000, 625,  8,  0, 8},
	{ 26000000, 1500000000, 750,  13, 0, 8},

	/* 1.4 GHz */
	{ 12000000, 1400000000, 700,  6,  0, 8},
	{ 13000000, 1400000000, 969,  9,  0, 8},	/* actual: 1399.7 MHz */
	{ 16800000, 1400000000, 1000, 12, 0, 8},
	{ 19200000, 1400000000, 875,  12, 0, 8},
	{ 26000000, 1400000000, 700,  13, 0, 8},

	/* 1.3 GHz */
	{ 12000000, 1300000000, 975,  9,  0, 8},
	{ 13000000, 1300000000, 1000, 10, 0, 8},
	{ 16800000, 1300000000, 928,  12, 0, 8},	/* actual: 1299.2 MHz */
	{ 19200000, 1300000000, 812,  12, 0, 8},	/* actual: 1299.2 MHz */
	{ 26000000, 1300000000, 650,  13, 0, 8},

	/* 1.2 GHz */
	{ 12000000, 1200000000, 1000, 10, 0, 8},
	{ 13000000, 1200000000, 923,  10, 0, 8},	/* actual: 1199.9 MHz */
	{ 16800000, 1200000000, 1000, 14, 0, 8},
	{ 19200000, 1200000000, 1000, 16, 0, 8},
	{ 26000000, 1200000000, 600,  13, 0, 8},

	/* 1.1 GHz */
	{ 12000000, 1100000000, 825,  9,  0, 8},
	{ 13000000, 1100000000, 846,  10, 0, 8},	/* actual: 1099.8 MHz */
	{ 16800000, 1100000000, 982,  15, 0, 8},	/* actual: 1099.8 MHz */
	{ 19200000, 1100000000, 859,  15, 0, 8},	/* actual: 1099.5 MHz */
	{ 26000000, 1100000000, 550,  13, 0, 8},

	/* 1 GHz */
	{ 12000000, 1000000000, 1000, 12, 0, 8},
	{ 13000000, 1000000000, 1000, 13, 0, 8},
	{ 16800000, 1000000000, 833,  14, 0, 8},	/* actual: 999.6 MHz */
	{ 19200000, 1000000000, 625,  12, 0, 8},
	{ 26000000, 1000000000, 1000, 26, 0, 8},

	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_e_freq_table[] = {
	/* PLLE special case: use cpcon field to store cml divider value */
	{ 12000000,  100000000, 150, 1,  18, 11},
	{ 216000000, 100000000, 200, 18, 24, 13},
	{ 0, 0, 0, 0, 0, 0 },
};

/* PLL parameters */
static struct tegra_clk_pll_params pll_c_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1400000000,
	.base_reg = PLLC_BASE,
	.misc_reg = PLLC_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
};

static struct tegra_clk_pll_params pll_m_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1200000000,
	.base_reg = PLLM_BASE,
	.misc_reg = PLLM_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
};

static struct tegra_clk_pll_params pll_p_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1400000000,
	.base_reg = PLLP_BASE,
	.misc_reg = PLLP_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
};

static struct tegra_clk_pll_params pll_a_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1400000000,
	.base_reg = PLLA_BASE,
	.misc_reg = PLLA_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
};

static struct tegra_clk_pll_params pll_d_params = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 40000000,
	.vco_max = 1000000000,
	.base_reg = PLLD_BASE,
	.misc_reg = PLLD_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
};

static struct tegra_clk_pll_params pll_d2_params = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 40000000,
	.vco_max = 1000000000,
	.base_reg = PLLD2_BASE,
	.misc_reg = PLLD2_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
};

static struct tegra_clk_pll_params pll_u_params = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 48000000,
	.vco_max = 960000000,
	.base_reg = PLLU_BASE,
	.misc_reg = PLLU_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
	.pdiv_tohw = pllu_p,
};

static struct tegra_clk_pll_params pll_x_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1700000000,
	.base_reg = PLLX_BASE,
	.misc_reg = PLLX_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
};

static struct tegra_clk_pll_params pll_e_params = {
	.input_min = 12000000,
	.input_max = 216000000,
	.cf_min = 12000000,
	.cf_max = 12000000,
	.vco_min = 1200000000,
	.vco_max = 2400000000U,
	.base_reg = PLLE_BASE,
	.misc_reg = PLLE_MISC,
	.lock_mask = PLLE_MISC_LOCK,
	.lock_enable_bit_idx = PLLE_MISC_LOCK_ENABLE,
	.lock_delay = 300,
};

/* Peripheral clock registers */
static struct tegra_clk_periph_regs periph_l_regs = {
	.enb_reg = CLK_OUT_ENB_L,
	.enb_set_reg = CLK_OUT_ENB_SET_L,
	.enb_clr_reg = CLK_OUT_ENB_CLR_L,
	.rst_reg = RST_DEVICES_L,
	.rst_set_reg = RST_DEVICES_SET_L,
	.rst_clr_reg = RST_DEVICES_CLR_L,
};

static struct tegra_clk_periph_regs periph_h_regs = {
	.enb_reg = CLK_OUT_ENB_H,
	.enb_set_reg = CLK_OUT_ENB_SET_H,
	.enb_clr_reg = CLK_OUT_ENB_CLR_H,
	.rst_reg = RST_DEVICES_H,
	.rst_set_reg = RST_DEVICES_SET_H,
	.rst_clr_reg = RST_DEVICES_CLR_H,
};

static struct tegra_clk_periph_regs periph_u_regs = {
	.enb_reg = CLK_OUT_ENB_U,
	.enb_set_reg = CLK_OUT_ENB_SET_U,
	.enb_clr_reg = CLK_OUT_ENB_CLR_U,
	.rst_reg = RST_DEVICES_U,
	.rst_set_reg = RST_DEVICES_SET_U,
	.rst_clr_reg = RST_DEVICES_CLR_U,
};

static struct tegra_clk_periph_regs periph_v_regs = {
	.enb_reg = CLK_OUT_ENB_V,
	.enb_set_reg = CLK_OUT_ENB_SET_V,
	.enb_clr_reg = CLK_OUT_ENB_CLR_V,
	.rst_reg = RST_DEVICES_V,
	.rst_set_reg = RST_DEVICES_SET_V,
	.rst_clr_reg = RST_DEVICES_CLR_V,
};

static struct tegra_clk_periph_regs periph_w_regs = {
	.enb_reg = CLK_OUT_ENB_W,
	.enb_set_reg = CLK_OUT_ENB_SET_W,
	.enb_clr_reg = CLK_OUT_ENB_CLR_W,
	.rst_reg = RST_DEVICES_W,
	.rst_set_reg = RST_DEVICES_SET_W,
	.rst_clr_reg = RST_DEVICES_CLR_W,
};

static void tegra30_clk_measure_input_freq(void)
{
	u32 osc_ctrl = readl_relaxed(clk_base + OSC_CTRL);
	u32 auto_clk_control = osc_ctrl & OSC_CTRL_OSC_FREQ_MASK;
	u32 pll_ref_div = osc_ctrl & OSC_CTRL_PLL_REF_DIV_MASK;

	switch (auto_clk_control) {
	case OSC_CTRL_OSC_FREQ_12MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		input_freq = 12000000;
		break;
	case OSC_CTRL_OSC_FREQ_13MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		input_freq = 13000000;
		break;
	case OSC_CTRL_OSC_FREQ_19_2MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		input_freq = 19200000;
		break;
	case OSC_CTRL_OSC_FREQ_26MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		input_freq = 26000000;
		break;
	case OSC_CTRL_OSC_FREQ_16_8MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		input_freq = 16800000;
		break;
	case OSC_CTRL_OSC_FREQ_38_4MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_2);
		input_freq = 38400000;
		break;
	case OSC_CTRL_OSC_FREQ_48MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_4);
		input_freq = 48000000;
		break;
	default:
		pr_err("Unexpected auto clock control value %d",
			auto_clk_control);
		BUG();
		return;
	}
}

static unsigned int tegra30_get_pll_ref_div(void)
{
	u32 pll_ref_div = readl_relaxed(clk_base + OSC_CTRL) &
					OSC_CTRL_PLL_REF_DIV_MASK;

	switch (pll_ref_div) {
	case OSC_CTRL_PLL_REF_DIV_1:
		return 1;
	case OSC_CTRL_PLL_REF_DIV_2:
		return 2;
	case OSC_CTRL_PLL_REF_DIV_4:
		return 4;
	default:
		pr_err("Invalid pll ref divider %d", pll_ref_div);
		BUG();
	}
	return 0;
}

static void tegra30_utmi_param_configure(void)
{
	u32 reg;
	int i;

	for (i = 0; i < ARRAY_SIZE(utmi_parameters); i++) {
		if (input_freq == utmi_parameters[i].osc_frequency)
			break;
	}

	if (i >= ARRAY_SIZE(utmi_parameters)) {
		pr_err("%s: Unexpected input rate %lu\n", __func__, input_freq);
		return;
	}

	reg = readl_relaxed(clk_base + UTMIP_PLL_CFG2);

	/* Program UTMIP PLL stable and active counts */
	reg &= ~UTMIP_PLL_CFG2_STABLE_COUNT(~0);
	reg |= UTMIP_PLL_CFG2_STABLE_COUNT(
			utmi_parameters[i].stable_count);

	reg &= ~UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(~0);

	reg |= UTMIP_PLL_CFG2_ACTIVE_DLY_COUNT(
			utmi_parameters[i].active_delay_count);

	/* Remove power downs from UTMIP PLL control bits */
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_A_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_B_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG2_FORCE_PD_SAMP_C_POWERDOWN;

	writel_relaxed(reg, clk_base + UTMIP_PLL_CFG2);

	/* Program UTMIP PLL delay and oscillator frequency counts */
	reg = readl_relaxed(clk_base + UTMIP_PLL_CFG1);
	reg &= ~UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(~0);

	reg |= UTMIP_PLL_CFG1_ENABLE_DLY_COUNT(
		utmi_parameters[i].enable_delay_count);

	reg &= ~UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(~0);
	reg |= UTMIP_PLL_CFG1_XTAL_FREQ_COUNT(
		utmi_parameters[i].xtal_freq_count);

	/* Remove power downs from UTMIP PLL control bits */
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ENABLE_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLL_ACTIVE_POWERDOWN;
	reg &= ~UTMIP_PLL_CFG1_FORCE_PLLU_POWERDOWN;

	writel_relaxed(reg, clk_base + UTMIP_PLL_CFG1);
}

static const char *pll_e_parents[] = {"pll_ref", "pll_p"};

static void __init tegra30_pll_init(void)
{
	struct clk *clk;

	/* PLLC */
	clk = tegra_clk_register_pll("pll_c", "pll_ref", clk_base, pmc_base, 0,
			    0, &pll_c_params,
			    TEGRA_PLL_HAS_CPCON | TEGRA_PLL_USE_LOCK,
			    pll_c_freq_table, NULL);
	clk_register_clkdev(clk, "pll_c", NULL);
	clks[pll_c] = clk;

	/* PLLC_OUT1 */
	clk = tegra_clk_register_divider("pll_c_out1_div", "pll_c",
				clk_base + PLLC_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_c_out1", "pll_c_out1_div",
				clk_base + PLLC_OUT, 1, 0, CLK_SET_RATE_PARENT,
				0, NULL);
	clk_register_clkdev(clk, "pll_c_out1", NULL);
	clks[pll_c_out1] = clk;

	/* PLLP */
	clk = tegra_clk_register_pll("pll_p", "pll_ref", clk_base, pmc_base, 0,
			    408000000, &pll_p_params,
			    TEGRA_PLL_FIXED | TEGRA_PLL_HAS_CPCON |
			    TEGRA_PLL_USE_LOCK, pll_p_freq_table, NULL);
	clk_register_clkdev(clk, "pll_p", NULL);
	clks[pll_p] = clk;

	/* PLLP_OUT1 */
	clk = tegra_clk_register_divider("pll_p_out1_div", "pll_p",
				clk_base + PLLP_OUTA, 0, TEGRA_DIVIDER_FIXED |
				TEGRA_DIVIDER_ROUND_UP, 8, 8, 1,
				&pll_div_lock);
	clk = tegra_clk_register_pll_out("pll_p_out1", "pll_p_out1_div",
				clk_base + PLLP_OUTA, 1, 0,
				CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0,
				&pll_div_lock);
	clk_register_clkdev(clk, "pll_p_out1", NULL);
	clks[pll_p_out1] = clk;

	/* PLLP_OUT2 */
	clk = tegra_clk_register_divider("pll_p_out2_div", "pll_p",
				clk_base + PLLP_OUTA, 0, TEGRA_DIVIDER_FIXED |
				TEGRA_DIVIDER_ROUND_UP, 24, 8, 1,
				&pll_div_lock);
	clk = tegra_clk_register_pll_out("pll_p_out2", "pll_p_out2_div",
				clk_base + PLLP_OUTA, 17, 16,
				CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0,
				&pll_div_lock);
	clk_register_clkdev(clk, "pll_p_out2", NULL);
	clks[pll_p_out2] = clk;

	/* PLLP_OUT3 */
	clk = tegra_clk_register_divider("pll_p_out3_div", "pll_p",
				clk_base + PLLP_OUTB, 0, TEGRA_DIVIDER_FIXED |
				TEGRA_DIVIDER_ROUND_UP, 8, 8, 1,
				&pll_div_lock);
	clk = tegra_clk_register_pll_out("pll_p_out3", "pll_p_out3_div",
				clk_base + PLLP_OUTB, 1, 0,
				CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0,
				&pll_div_lock);
	clk_register_clkdev(clk, "pll_p_out3", NULL);
	clks[pll_p_out3] = clk;

	/* PLLP_OUT4 */
	clk = tegra_clk_register_divider("pll_p_out4_div", "pll_p",
				clk_base + PLLP_OUTB, 0, TEGRA_DIVIDER_FIXED |
				TEGRA_DIVIDER_ROUND_UP, 24, 8, 1,
				&pll_div_lock);
	clk = tegra_clk_register_pll_out("pll_p_out4", "pll_p_out4_div",
				clk_base + PLLP_OUTB, 17, 16,
				CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0,
				&pll_div_lock);
	clk_register_clkdev(clk, "pll_p_out4", NULL);
	clks[pll_p_out4] = clk;

	/* PLLM */
	clk = tegra_clk_register_pll("pll_m", "pll_ref", clk_base, pmc_base,
			    CLK_IGNORE_UNUSED | CLK_SET_RATE_GATE, 0,
			    &pll_m_params, TEGRA_PLLM | TEGRA_PLL_HAS_CPCON |
			    TEGRA_PLL_SET_DCCON | TEGRA_PLL_USE_LOCK,
			    pll_m_freq_table, NULL);
	clk_register_clkdev(clk, "pll_m", NULL);
	clks[pll_m] = clk;

	/* PLLM_OUT1 */
	clk = tegra_clk_register_divider("pll_m_out1_div", "pll_m",
				clk_base + PLLM_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_m_out1", "pll_m_out1_div",
				clk_base + PLLM_OUT, 1, 0, CLK_IGNORE_UNUSED |
				CLK_SET_RATE_PARENT, 0, NULL);
	clk_register_clkdev(clk, "pll_m_out1", NULL);
	clks[pll_m_out1] = clk;

	/* PLLX */
	clk = tegra_clk_register_pll("pll_x", "pll_ref", clk_base, pmc_base, 0,
			    0, &pll_x_params, TEGRA_PLL_HAS_CPCON |
			    TEGRA_PLL_SET_DCCON | TEGRA_PLL_USE_LOCK,
			    pll_x_freq_table, NULL);
	clk_register_clkdev(clk, "pll_x", NULL);
	clks[pll_x] = clk;

	/* PLLX_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_x_out0", "pll_x",
					CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll_x_out0", NULL);
	clks[pll_x_out0] = clk;

	/* PLLU */
	clk = tegra_clk_register_pll("pll_u", "pll_ref", clk_base, pmc_base, 0,
			    0, &pll_u_params, TEGRA_PLLU | TEGRA_PLL_HAS_CPCON |
			    TEGRA_PLL_SET_LFCON | TEGRA_PLL_USE_LOCK,
			    pll_u_freq_table,
			    NULL);
	clk_register_clkdev(clk, "pll_u", NULL);
	clks[pll_u] = clk;

	tegra30_utmi_param_configure();

	/* PLLD */
	clk = tegra_clk_register_pll("pll_d", "pll_ref", clk_base, pmc_base, 0,
			    0, &pll_d_params, TEGRA_PLL_HAS_CPCON |
			    TEGRA_PLL_SET_LFCON | TEGRA_PLL_USE_LOCK,
			    pll_d_freq_table, &pll_d_lock);
	clk_register_clkdev(clk, "pll_d", NULL);
	clks[pll_d] = clk;

	/* PLLD_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_d_out0", "pll_d",
					CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll_d_out0", NULL);
	clks[pll_d_out0] = clk;

	/* PLLD2 */
	clk = tegra_clk_register_pll("pll_d2", "pll_ref", clk_base, pmc_base, 0,
			    0, &pll_d2_params, TEGRA_PLL_HAS_CPCON |
			    TEGRA_PLL_SET_LFCON | TEGRA_PLL_USE_LOCK,
			    pll_d_freq_table, NULL);
	clk_register_clkdev(clk, "pll_d2", NULL);
	clks[pll_d2] = clk;

	/* PLLD2_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_d2_out0", "pll_d2",
					CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "pll_d2_out0", NULL);
	clks[pll_d2_out0] = clk;

	/* PLLA */
	clk = tegra_clk_register_pll("pll_a", "pll_p_out1", clk_base, pmc_base,
			    0, 0, &pll_a_params, TEGRA_PLL_HAS_CPCON |
			    TEGRA_PLL_USE_LOCK, pll_a_freq_table, NULL);
	clk_register_clkdev(clk, "pll_a", NULL);
	clks[pll_a] = clk;

	/* PLLA_OUT0 */
	clk = tegra_clk_register_divider("pll_a_out0_div", "pll_a",
				clk_base + PLLA_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_a_out0", "pll_a_out0_div",
				clk_base + PLLA_OUT, 1, 0, CLK_IGNORE_UNUSED |
				CLK_SET_RATE_PARENT, 0, NULL);
	clk_register_clkdev(clk, "pll_a_out0", NULL);
	clks[pll_a_out0] = clk;

	/* PLLE */
	clk = clk_register_mux(NULL, "pll_e_mux", pll_e_parents,
			       ARRAY_SIZE(pll_e_parents), 0,
			       clk_base + PLLE_AUX, 2, 1, 0, NULL);
	clk = tegra_clk_register_plle("pll_e", "pll_e_mux", clk_base, pmc_base,
			     CLK_GET_RATE_NOCACHE, 100000000, &pll_e_params,
			     TEGRA_PLLE_CONFIGURE, pll_e_freq_table, NULL);
	clk_register_clkdev(clk, "pll_e", NULL);
	clks[pll_e] = clk;
}

static const char *mux_audio_sync_clk[] = { "spdif_in_sync", "i2s0_sync",
	"i2s1_sync", "i2s2_sync", "i2s3_sync", "i2s4_sync", "vimclk_sync",};
static const char *clk_out1_parents[] = { "clk_m", "clk_m_div2",
					  "clk_m_div4", "extern1", };
static const char *clk_out2_parents[] = { "clk_m", "clk_m_div2",
					  "clk_m_div4", "extern2", };
static const char *clk_out3_parents[] = { "clk_m", "clk_m_div2",
					  "clk_m_div4", "extern3", };

static void __init tegra30_audio_clk_init(void)
{
	struct clk *clk;

	/* spdif_in_sync */
	clk = tegra_clk_register_sync_source("spdif_in_sync", 24000000,
					     24000000);
	clk_register_clkdev(clk, "spdif_in_sync", NULL);
	clks[spdif_in_sync] = clk;

	/* i2s0_sync */
	clk = tegra_clk_register_sync_source("i2s0_sync", 24000000, 24000000);
	clk_register_clkdev(clk, "i2s0_sync", NULL);
	clks[i2s0_sync] = clk;

	/* i2s1_sync */
	clk = tegra_clk_register_sync_source("i2s1_sync", 24000000, 24000000);
	clk_register_clkdev(clk, "i2s1_sync", NULL);
	clks[i2s1_sync] = clk;

	/* i2s2_sync */
	clk = tegra_clk_register_sync_source("i2s2_sync", 24000000, 24000000);
	clk_register_clkdev(clk, "i2s2_sync", NULL);
	clks[i2s2_sync] = clk;

	/* i2s3_sync */
	clk = tegra_clk_register_sync_source("i2s3_sync", 24000000, 24000000);
	clk_register_clkdev(clk, "i2s3_sync", NULL);
	clks[i2s3_sync] = clk;

	/* i2s4_sync */
	clk = tegra_clk_register_sync_source("i2s4_sync", 24000000, 24000000);
	clk_register_clkdev(clk, "i2s4_sync", NULL);
	clks[i2s4_sync] = clk;

	/* vimclk_sync */
	clk = tegra_clk_register_sync_source("vimclk_sync", 24000000, 24000000);
	clk_register_clkdev(clk, "vimclk_sync", NULL);
	clks[vimclk_sync] = clk;

	/* audio0 */
	clk = clk_register_mux(NULL, "audio0_mux", mux_audio_sync_clk,
				ARRAY_SIZE(mux_audio_sync_clk), 0,
				clk_base + AUDIO_SYNC_CLK_I2S0, 0, 3, 0, NULL);
	clk = clk_register_gate(NULL, "audio0", "audio0_mux", 0,
				clk_base + AUDIO_SYNC_CLK_I2S0, 4,
				CLK_GATE_SET_TO_DISABLE, NULL);
	clk_register_clkdev(clk, "audio0", NULL);
	clks[audio0] = clk;

	/* audio1 */
	clk = clk_register_mux(NULL, "audio1_mux", mux_audio_sync_clk,
				ARRAY_SIZE(mux_audio_sync_clk), 0,
				clk_base + AUDIO_SYNC_CLK_I2S1, 0, 3, 0, NULL);
	clk = clk_register_gate(NULL, "audio1", "audio1_mux", 0,
				clk_base + AUDIO_SYNC_CLK_I2S1, 4,
				CLK_GATE_SET_TO_DISABLE, NULL);
	clk_register_clkdev(clk, "audio1", NULL);
	clks[audio1] = clk;

	/* audio2 */
	clk = clk_register_mux(NULL, "audio2_mux", mux_audio_sync_clk,
				ARRAY_SIZE(mux_audio_sync_clk), 0,
				clk_base + AUDIO_SYNC_CLK_I2S2, 0, 3, 0, NULL);
	clk = clk_register_gate(NULL, "audio2", "audio2_mux", 0,
				clk_base + AUDIO_SYNC_CLK_I2S2, 4,
				CLK_GATE_SET_TO_DISABLE, NULL);
	clk_register_clkdev(clk, "audio2", NULL);
	clks[audio2] = clk;

	/* audio3 */
	clk = clk_register_mux(NULL, "audio3_mux", mux_audio_sync_clk,
				ARRAY_SIZE(mux_audio_sync_clk), 0,
				clk_base + AUDIO_SYNC_CLK_I2S3, 0, 3, 0, NULL);
	clk = clk_register_gate(NULL, "audio3", "audio3_mux", 0,
				clk_base + AUDIO_SYNC_CLK_I2S3, 4,
				CLK_GATE_SET_TO_DISABLE, NULL);
	clk_register_clkdev(clk, "audio3", NULL);
	clks[audio3] = clk;

	/* audio4 */
	clk = clk_register_mux(NULL, "audio4_mux", mux_audio_sync_clk,
				ARRAY_SIZE(mux_audio_sync_clk), 0,
				clk_base + AUDIO_SYNC_CLK_I2S4, 0, 3, 0, NULL);
	clk = clk_register_gate(NULL, "audio4", "audio4_mux", 0,
				clk_base + AUDIO_SYNC_CLK_I2S4, 4,
				CLK_GATE_SET_TO_DISABLE, NULL);
	clk_register_clkdev(clk, "audio4", NULL);
	clks[audio4] = clk;

	/* spdif */
	clk = clk_register_mux(NULL, "spdif_mux", mux_audio_sync_clk,
				ARRAY_SIZE(mux_audio_sync_clk), 0,
				clk_base + AUDIO_SYNC_CLK_SPDIF, 0, 3, 0, NULL);
	clk = clk_register_gate(NULL, "spdif", "spdif_mux", 0,
				clk_base + AUDIO_SYNC_CLK_SPDIF, 4,
				CLK_GATE_SET_TO_DISABLE, NULL);
	clk_register_clkdev(clk, "spdif", NULL);
	clks[spdif] = clk;

	/* audio0_2x */
	clk = clk_register_fixed_factor(NULL, "audio0_doubler", "audio0",
					CLK_SET_RATE_PARENT, 2, 1);
	clk = tegra_clk_register_divider("audio0_div", "audio0_doubler",
				clk_base + AUDIO_SYNC_DOUBLER, 0, 0, 24, 1, 0,
				&clk_doubler_lock);
	clk = tegra_clk_register_periph_gate("audio0_2x", "audio0_div",
				    TEGRA_PERIPH_NO_RESET, clk_base,
				    CLK_SET_RATE_PARENT, 113, &periph_v_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "audio0_2x", NULL);
	clks[audio0_2x] = clk;

	/* audio1_2x */
	clk = clk_register_fixed_factor(NULL, "audio1_doubler", "audio1",
					CLK_SET_RATE_PARENT, 2, 1);
	clk = tegra_clk_register_divider("audio1_div", "audio1_doubler",
				clk_base + AUDIO_SYNC_DOUBLER, 0, 0, 25, 1, 0,
				&clk_doubler_lock);
	clk = tegra_clk_register_periph_gate("audio1_2x", "audio1_div",
				    TEGRA_PERIPH_NO_RESET, clk_base,
				    CLK_SET_RATE_PARENT, 114, &periph_v_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "audio1_2x", NULL);
	clks[audio1_2x] = clk;

	/* audio2_2x */
	clk = clk_register_fixed_factor(NULL, "audio2_doubler", "audio2",
					CLK_SET_RATE_PARENT, 2, 1);
	clk = tegra_clk_register_divider("audio2_div", "audio2_doubler",
				clk_base + AUDIO_SYNC_DOUBLER, 0, 0, 26, 1, 0,
				&clk_doubler_lock);
	clk = tegra_clk_register_periph_gate("audio2_2x", "audio2_div",
				    TEGRA_PERIPH_NO_RESET, clk_base,
				    CLK_SET_RATE_PARENT, 115, &periph_v_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "audio2_2x", NULL);
	clks[audio2_2x] = clk;

	/* audio3_2x */
	clk = clk_register_fixed_factor(NULL, "audio3_doubler", "audio3",
					CLK_SET_RATE_PARENT, 2, 1);
	clk = tegra_clk_register_divider("audio3_div", "audio3_doubler",
				clk_base + AUDIO_SYNC_DOUBLER, 0, 0, 27, 1, 0,
				&clk_doubler_lock);
	clk = tegra_clk_register_periph_gate("audio3_2x", "audio3_div",
				    TEGRA_PERIPH_NO_RESET, clk_base,
				    CLK_SET_RATE_PARENT, 116, &periph_v_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "audio3_2x", NULL);
	clks[audio3_2x] = clk;

	/* audio4_2x */
	clk = clk_register_fixed_factor(NULL, "audio4_doubler", "audio4",
					CLK_SET_RATE_PARENT, 2, 1);
	clk = tegra_clk_register_divider("audio4_div", "audio4_doubler",
				clk_base + AUDIO_SYNC_DOUBLER, 0, 0, 28, 1, 0,
				&clk_doubler_lock);
	clk = tegra_clk_register_periph_gate("audio4_2x", "audio4_div",
				    TEGRA_PERIPH_NO_RESET, clk_base,
				    CLK_SET_RATE_PARENT, 117, &periph_v_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "audio4_2x", NULL);
	clks[audio4_2x] = clk;

	/* spdif_2x */
	clk = clk_register_fixed_factor(NULL, "spdif_doubler", "spdif",
					CLK_SET_RATE_PARENT, 2, 1);
	clk = tegra_clk_register_divider("spdif_div", "spdif_doubler",
				clk_base + AUDIO_SYNC_DOUBLER, 0, 0, 29, 1, 0,
				&clk_doubler_lock);
	clk = tegra_clk_register_periph_gate("spdif_2x", "spdif_div",
				    TEGRA_PERIPH_NO_RESET, clk_base,
				    CLK_SET_RATE_PARENT, 118, &periph_v_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "spdif_2x", NULL);
	clks[spdif_2x] = clk;
}

static void __init tegra30_pmc_clk_init(void)
{
	struct clk *clk;

	/* clk_out_1 */
	clk = clk_register_mux(NULL, "clk_out_1_mux", clk_out1_parents,
			       ARRAY_SIZE(clk_out1_parents), 0,
			       pmc_base + PMC_CLK_OUT_CNTRL, 6, 3, 0,
			       &clk_out_lock);
	clks[clk_out_1_mux] = clk;
	clk = clk_register_gate(NULL, "clk_out_1", "clk_out_1_mux", 0,
				pmc_base + PMC_CLK_OUT_CNTRL, 2, 0,
				&clk_out_lock);
	clk_register_clkdev(clk, "extern1", "clk_out_1");
	clks[clk_out_1] = clk;

	/* clk_out_2 */
	clk = clk_register_mux(NULL, "clk_out_2_mux", clk_out2_parents,
			       ARRAY_SIZE(clk_out1_parents), 0,
			       pmc_base + PMC_CLK_OUT_CNTRL, 14, 3, 0,
			       &clk_out_lock);
	clk = clk_register_gate(NULL, "clk_out_2", "clk_out_2_mux", 0,
				pmc_base + PMC_CLK_OUT_CNTRL, 10, 0,
				&clk_out_lock);
	clk_register_clkdev(clk, "extern2", "clk_out_2");
	clks[clk_out_2] = clk;

	/* clk_out_3 */
	clk = clk_register_mux(NULL, "clk_out_3_mux", clk_out3_parents,
			       ARRAY_SIZE(clk_out1_parents), 0,
			       pmc_base + PMC_CLK_OUT_CNTRL, 22, 3, 0,
			       &clk_out_lock);
	clk = clk_register_gate(NULL, "clk_out_3", "clk_out_3_mux", 0,
				pmc_base + PMC_CLK_OUT_CNTRL, 18, 0,
				&clk_out_lock);
	clk_register_clkdev(clk, "extern3", "clk_out_3");
	clks[clk_out_3] = clk;

	/* blink */
	writel_relaxed(0, pmc_base + PMC_BLINK_TIMER);
	clk = clk_register_gate(NULL, "blink_override", "clk_32k", 0,
				pmc_base + PMC_DPD_PADS_ORIDE,
				PMC_DPD_PADS_ORIDE_BLINK_ENB, 0, NULL);
	clk = clk_register_gate(NULL, "blink", "blink_override", 0,
				pmc_base + PMC_CTRL,
				PMC_CTRL_BLINK_ENB, 0, NULL);
	clk_register_clkdev(clk, "blink", NULL);
	clks[blink] = clk;

}

static const char *cclk_g_parents[] = { "clk_m", "pll_c", "clk_32k", "pll_m",
					"pll_p_cclkg", "pll_p_out4_cclkg",
					"pll_p_out3_cclkg", "unused", "pll_x" };
static const char *cclk_lp_parents[] = { "clk_m", "pll_c", "clk_32k", "pll_m",
					 "pll_p_cclklp", "pll_p_out4_cclklp",
					 "pll_p_out3_cclklp", "unused", "pll_x",
					 "pll_x_out0" };
static const char *sclk_parents[] = { "clk_m", "pll_c_out1", "pll_p_out4",
				      "pll_p_out3", "pll_p_out2", "unused",
				      "clk_32k", "pll_m_out1" };

static void __init tegra30_super_clk_init(void)
{
	struct clk *clk;

	/*
	 * Clock input to cclk_g divided from pll_p using
	 * U71 divider of cclk_g.
	 */
	clk = tegra_clk_register_divider("pll_p_cclkg", "pll_p",
				clk_base + SUPER_CCLKG_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_cclkg", NULL);

	/*
	 * Clock input to cclk_g divided from pll_p_out3 using
	 * U71 divider of cclk_g.
	 */
	clk = tegra_clk_register_divider("pll_p_out3_cclkg", "pll_p_out3",
				clk_base + SUPER_CCLKG_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_out3_cclkg", NULL);

	/*
	 * Clock input to cclk_g divided from pll_p_out4 using
	 * U71 divider of cclk_g.
	 */
	clk = tegra_clk_register_divider("pll_p_out4_cclkg", "pll_p_out4",
				clk_base + SUPER_CCLKG_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_out4_cclkg", NULL);

	/* CCLKG */
	clk = tegra_clk_register_super_mux("cclk_g", cclk_g_parents,
				  ARRAY_SIZE(cclk_g_parents),
				  CLK_SET_RATE_PARENT,
				  clk_base + CCLKG_BURST_POLICY,
				  0, 4, 0, 0, NULL);
	clk_register_clkdev(clk, "cclk_g", NULL);
	clks[cclk_g] = clk;

	/*
	 * Clock input to cclk_lp divided from pll_p using
	 * U71 divider of cclk_lp.
	 */
	clk = tegra_clk_register_divider("pll_p_cclklp", "pll_p",
				clk_base + SUPER_CCLKLP_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_cclklp", NULL);

	/*
	 * Clock input to cclk_lp divided from pll_p_out3 using
	 * U71 divider of cclk_lp.
	 */
	clk = tegra_clk_register_divider("pll_p_out3_cclklp", "pll_p_out3",
				clk_base + SUPER_CCLKG_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_out3_cclklp", NULL);

	/*
	 * Clock input to cclk_lp divided from pll_p_out4 using
	 * U71 divider of cclk_lp.
	 */
	clk = tegra_clk_register_divider("pll_p_out4_cclklp", "pll_p_out4",
				clk_base + SUPER_CCLKLP_DIVIDER, 0,
				TEGRA_DIVIDER_INT, 16, 8, 1, NULL);
	clk_register_clkdev(clk, "pll_p_out4_cclklp", NULL);

	/* CCLKLP */
	clk = tegra_clk_register_super_mux("cclk_lp", cclk_lp_parents,
				  ARRAY_SIZE(cclk_lp_parents),
				  CLK_SET_RATE_PARENT,
				  clk_base + CCLKLP_BURST_POLICY,
				  TEGRA_DIVIDER_2, 4, 8, 9,
			      NULL);
	clk_register_clkdev(clk, "cclk_lp", NULL);
	clks[cclk_lp] = clk;

	/* SCLK */
	clk = tegra_clk_register_super_mux("sclk", sclk_parents,
				  ARRAY_SIZE(sclk_parents),
				  CLK_SET_RATE_PARENT,
				  clk_base + SCLK_BURST_POLICY,
				  0, 4, 0, 0, NULL);
	clk_register_clkdev(clk, "sclk", NULL);
	clks[sclk] = clk;

	/* HCLK */
	clk = clk_register_divider(NULL, "hclk_div", "sclk", 0,
				   clk_base + SYSTEM_CLK_RATE, 4, 2, 0,
				   &sysrate_lock);
	clk = clk_register_gate(NULL, "hclk", "hclk_div", CLK_SET_RATE_PARENT,
				clk_base + SYSTEM_CLK_RATE, 7,
				CLK_GATE_SET_TO_DISABLE, &sysrate_lock);
	clk_register_clkdev(clk, "hclk", NULL);
	clks[hclk] = clk;

	/* PCLK */
	clk = clk_register_divider(NULL, "pclk_div", "hclk", 0,
				   clk_base + SYSTEM_CLK_RATE, 0, 2, 0,
				   &sysrate_lock);
	clk = clk_register_gate(NULL, "pclk", "pclk_div", CLK_SET_RATE_PARENT,
				clk_base + SYSTEM_CLK_RATE, 3,
				CLK_GATE_SET_TO_DISABLE, &sysrate_lock);
	clk_register_clkdev(clk, "pclk", NULL);
	clks[pclk] = clk;

	/* twd */
	clk = clk_register_fixed_factor(NULL, "twd", "cclk_g",
					CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "twd", NULL);
	clks[twd] = clk;
}

static const char *mux_pllacp_clkm[] = { "pll_a_out0", "unused", "pll_p",
					 "clk_m" };
static const char *mux_pllpcm_clkm[] = { "pll_p", "pll_c", "pll_m", "clk_m" };
static const char *mux_pllmcp_clkm[] = { "pll_m", "pll_c", "pll_p", "clk_m" };
static const char *i2s0_parents[] = { "pll_a_out0", "audio0_2x", "pll_p",
				      "clk_m" };
static const char *i2s1_parents[] = { "pll_a_out0", "audio1_2x", "pll_p",
				      "clk_m" };
static const char *i2s2_parents[] = { "pll_a_out0", "audio2_2x", "pll_p",
				      "clk_m" };
static const char *i2s3_parents[] = { "pll_a_out0", "audio3_2x", "pll_p",
				      "clk_m" };
static const char *i2s4_parents[] = { "pll_a_out0", "audio4_2x", "pll_p",
				      "clk_m" };
static const char *spdif_out_parents[] = { "pll_a_out0", "spdif_2x", "pll_p",
					   "clk_m" };
static const char *spdif_in_parents[] = { "pll_p", "pll_c", "pll_m" };
static const char *mux_pllpc_clk32k_clkm[] = { "pll_p", "pll_c", "clk_32k",
					       "clk_m" };
static const char *mux_pllpc_clkm_clk32k[] = { "pll_p", "pll_c", "clk_m",
					       "clk_32k" };
static const char *mux_pllmcpa[] = { "pll_m", "pll_c", "pll_p", "pll_a_out0" };
static const char *mux_pllpdc_clkm[] = { "pll_p", "pll_d_out0", "pll_c",
					 "clk_m" };
static const char *mux_pllp_clkm[] = { "pll_p", "unused", "unused", "clk_m" };
static const char *mux_pllpmdacd2_clkm[] = { "pll_p", "pll_m", "pll_d_out0",
					     "pll_a_out0", "pll_c",
					     "pll_d2_out0", "clk_m" };
static const char *mux_plla_clk32k_pllp_clkm_plle[] = { "pll_a_out0",
							"clk_32k", "pll_p",
							"clk_m", "pll_e" };
static const char *mux_plld_out0_plld2_out0[] = { "pll_d_out0",
						  "pll_d2_out0" };

static struct tegra_periph_init_data tegra_periph_clk_list[] = {
	TEGRA_INIT_DATA_MUX("i2s0",	NULL,		"tegra30-i2s.0",	i2s0_parents,		CLK_SOURCE_I2S0,	30,	&periph_l_regs, TEGRA_PERIPH_ON_APB, i2s0),
	TEGRA_INIT_DATA_MUX("i2s1",	NULL,		"tegra30-i2s.1",	i2s1_parents,		CLK_SOURCE_I2S1,	11,	&periph_l_regs, TEGRA_PERIPH_ON_APB, i2s1),
	TEGRA_INIT_DATA_MUX("i2s2",	NULL,		"tegra30-i2s.2",	i2s2_parents,		CLK_SOURCE_I2S2,	18,	&periph_l_regs, TEGRA_PERIPH_ON_APB, i2s2),
	TEGRA_INIT_DATA_MUX("i2s3",	NULL,		"tegra30-i2s.3",	i2s3_parents,		CLK_SOURCE_I2S3,	101,	&periph_v_regs, TEGRA_PERIPH_ON_APB, i2s3),
	TEGRA_INIT_DATA_MUX("i2s4",	NULL,		"tegra30-i2s.4",	i2s4_parents,		CLK_SOURCE_I2S4,	102,	&periph_v_regs, TEGRA_PERIPH_ON_APB, i2s4),
	TEGRA_INIT_DATA_MUX("spdif_out", "spdif_out",	"tegra30-spdif",	spdif_out_parents,	CLK_SOURCE_SPDIF_OUT,	10,	&periph_l_regs, TEGRA_PERIPH_ON_APB, spdif_out),
	TEGRA_INIT_DATA_MUX("spdif_in",	"spdif_in",	"tegra30-spdif",	spdif_in_parents,	CLK_SOURCE_SPDIF_IN,	10,	&periph_l_regs, TEGRA_PERIPH_ON_APB, spdif_in),
	TEGRA_INIT_DATA_MUX("d_audio",	"d_audio",	"tegra30-ahub",		mux_pllacp_clkm,	CLK_SOURCE_D_AUDIO,	106,	&periph_v_regs, 0, d_audio),
	TEGRA_INIT_DATA_MUX("dam0",	NULL,		"tegra30-dam.0",	mux_pllacp_clkm,	CLK_SOURCE_DAM0,	108,	&periph_v_regs, 0, dam0),
	TEGRA_INIT_DATA_MUX("dam1",	NULL,		"tegra30-dam.1",	mux_pllacp_clkm,	CLK_SOURCE_DAM1,	109,	&periph_v_regs, 0, dam1),
	TEGRA_INIT_DATA_MUX("dam2",	NULL,		"tegra30-dam.2",	mux_pllacp_clkm,	CLK_SOURCE_DAM2,	110,	&periph_v_regs, 0, dam2),
	TEGRA_INIT_DATA_MUX("hda",	"hda",		"tegra30-hda",		mux_pllpcm_clkm,	CLK_SOURCE_HDA,		125,	&periph_v_regs, 0, hda),
	TEGRA_INIT_DATA_MUX("hda2codec_2x", "hda2codec", "tegra30-hda",		mux_pllpcm_clkm,	CLK_SOURCE_HDA2CODEC_2X, 111,	&periph_v_regs, 0, hda2codec_2x),
	TEGRA_INIT_DATA_MUX("sbc1",	NULL,		"spi_tegra.0",		mux_pllpcm_clkm,	CLK_SOURCE_SBC1,	41,	&periph_h_regs, TEGRA_PERIPH_ON_APB, sbc1),
	TEGRA_INIT_DATA_MUX("sbc2",	NULL,		"spi_tegra.1",		mux_pllpcm_clkm,	CLK_SOURCE_SBC2,	44,	&periph_h_regs, TEGRA_PERIPH_ON_APB, sbc2),
	TEGRA_INIT_DATA_MUX("sbc3",	NULL,		"spi_tegra.2",		mux_pllpcm_clkm,	CLK_SOURCE_SBC3,	46,	&periph_h_regs, TEGRA_PERIPH_ON_APB, sbc3),
	TEGRA_INIT_DATA_MUX("sbc4",	NULL,		"spi_tegra.3",		mux_pllpcm_clkm,	CLK_SOURCE_SBC4,	68,	&periph_u_regs, TEGRA_PERIPH_ON_APB, sbc4),
	TEGRA_INIT_DATA_MUX("sbc5",	NULL,		"spi_tegra.4",		mux_pllpcm_clkm,	CLK_SOURCE_SBC5,	104,	&periph_v_regs, TEGRA_PERIPH_ON_APB, sbc5),
	TEGRA_INIT_DATA_MUX("sbc6",	NULL,		"spi_tegra.5",		mux_pllpcm_clkm,	CLK_SOURCE_SBC6,	105,	&periph_v_regs, TEGRA_PERIPH_ON_APB, sbc6),
	TEGRA_INIT_DATA_MUX("sata_oob",	NULL,		"tegra_sata_oob",	mux_pllpcm_clkm,	CLK_SOURCE_SATA_OOB,	123,	&periph_v_regs, TEGRA_PERIPH_ON_APB, sata_oob),
	TEGRA_INIT_DATA_MUX("sata",	NULL,		"tegra_sata",		mux_pllpcm_clkm,	CLK_SOURCE_SATA,	124,	&periph_v_regs, TEGRA_PERIPH_ON_APB, sata),
	TEGRA_INIT_DATA_MUX("ndflash",	NULL,		"tegra_nand",		mux_pllpcm_clkm,	CLK_SOURCE_NDFLASH,	13,	&periph_l_regs, TEGRA_PERIPH_ON_APB, ndflash),
	TEGRA_INIT_DATA_MUX("ndspeed",	NULL,		"tegra_nand_speed",	mux_pllpcm_clkm,	CLK_SOURCE_NDSPEED,	80,	&periph_u_regs, TEGRA_PERIPH_ON_APB, ndspeed),
	TEGRA_INIT_DATA_MUX("vfir",	NULL,		"vfir",			mux_pllpcm_clkm,	CLK_SOURCE_VFIR,	7,	&periph_l_regs, TEGRA_PERIPH_ON_APB, vfir),
	TEGRA_INIT_DATA_MUX("csite",	NULL,		"csite",		mux_pllpcm_clkm,	CLK_SOURCE_CSITE,	73,	&periph_u_regs, TEGRA_PERIPH_ON_APB, csite),
	TEGRA_INIT_DATA_MUX("la",	NULL,		"la",			mux_pllpcm_clkm,	CLK_SOURCE_LA,		76,	&periph_u_regs, TEGRA_PERIPH_ON_APB, la),
	TEGRA_INIT_DATA_MUX("owr",	NULL,		"tegra_w1",		mux_pllpcm_clkm,	CLK_SOURCE_OWR,		71,	&periph_u_regs, TEGRA_PERIPH_ON_APB, owr),
	TEGRA_INIT_DATA_MUX("mipi",	NULL,		"mipi",			mux_pllpcm_clkm,	CLK_SOURCE_MIPI,	50,	&periph_h_regs, TEGRA_PERIPH_ON_APB, mipi),
	TEGRA_INIT_DATA_MUX("tsensor",	NULL,		"tegra-tsensor",	mux_pllpc_clkm_clk32k,	CLK_SOURCE_TSENSOR,	100,	&periph_v_regs, TEGRA_PERIPH_ON_APB, tsensor),
	TEGRA_INIT_DATA_MUX("i2cslow",	NULL,		"i2cslow",		mux_pllpc_clk32k_clkm,	CLK_SOURCE_I2CSLOW,	81,	&periph_u_regs, TEGRA_PERIPH_ON_APB, i2cslow),
	TEGRA_INIT_DATA_INT("vde",	NULL,		"vde",			mux_pllpcm_clkm,	CLK_SOURCE_VDE,		61,	&periph_h_regs, 0, vde),
	TEGRA_INIT_DATA_INT("vi",	"vi",		"tegra_camera",		mux_pllmcpa,		CLK_SOURCE_VI,		20,	&periph_l_regs, 0, vi),
	TEGRA_INIT_DATA_INT("epp",	NULL,		"epp",			mux_pllmcpa,		CLK_SOURCE_EPP,		19,	&periph_l_regs, 0, epp),
	TEGRA_INIT_DATA_INT("mpe",	NULL,		"mpe",			mux_pllmcpa,		CLK_SOURCE_MPE,		60,	&periph_h_regs, 0, mpe),
	TEGRA_INIT_DATA_INT("host1x",	NULL,		"host1x",		mux_pllmcpa,		CLK_SOURCE_HOST1X,	28,	&periph_l_regs, 0, host1x),
	TEGRA_INIT_DATA_INT("3d",	NULL,		"3d",			mux_pllmcpa,		CLK_SOURCE_3D,		24,	&periph_l_regs, TEGRA_PERIPH_MANUAL_RESET, gr3d),
	TEGRA_INIT_DATA_INT("3d2",	NULL,		"3d2",			mux_pllmcpa,		CLK_SOURCE_3D2,		98,	&periph_v_regs, TEGRA_PERIPH_MANUAL_RESET, gr3d2),
	TEGRA_INIT_DATA_INT("2d",	NULL,		"2d",			mux_pllmcpa,		CLK_SOURCE_2D,		21,	&periph_l_regs, 0, gr2d),
	TEGRA_INIT_DATA_INT("se",	NULL,		"se",			mux_pllpcm_clkm,	CLK_SOURCE_SE,		127,	&periph_v_regs, 0, se),
	TEGRA_INIT_DATA_MUX("mselect",	NULL,		"mselect",		mux_pllp_clkm,		CLK_SOURCE_MSELECT,	99,	&periph_v_regs, 0, mselect),
	TEGRA_INIT_DATA_MUX("nor",	NULL,		"tegra-nor",		mux_pllpcm_clkm,	CLK_SOURCE_NOR,		42,	&periph_h_regs, 0, nor),
	TEGRA_INIT_DATA_MUX("sdmmc1",	NULL,		"sdhci-tegra.0",	mux_pllpcm_clkm,	CLK_SOURCE_SDMMC1,	14,	&periph_l_regs, 0, sdmmc1),
	TEGRA_INIT_DATA_MUX("sdmmc2",	NULL,		"sdhci-tegra.1",	mux_pllpcm_clkm,	CLK_SOURCE_SDMMC2,	9,	&periph_l_regs, 0, sdmmc2),
	TEGRA_INIT_DATA_MUX("sdmmc3",	NULL,		"sdhci-tegra.2",	mux_pllpcm_clkm,	CLK_SOURCE_SDMMC3,	69,	&periph_u_regs, 0, sdmmc3),
	TEGRA_INIT_DATA_MUX("sdmmc4",	NULL,		"sdhci-tegra.3",	mux_pllpcm_clkm,	CLK_SOURCE_SDMMC4,	15,	&periph_l_regs, 0, sdmmc4),
	TEGRA_INIT_DATA_MUX("cve",	NULL,		"cve",			mux_pllpdc_clkm,	CLK_SOURCE_CVE,		49,	&periph_h_regs, 0, cve),
	TEGRA_INIT_DATA_MUX("tvo",	NULL,		"tvo",			mux_pllpdc_clkm,	CLK_SOURCE_TVO,		49,	&periph_h_regs, 0, tvo),
	TEGRA_INIT_DATA_MUX("tvdac",	NULL,		"tvdac",		mux_pllpdc_clkm,	CLK_SOURCE_TVDAC,	53,	&periph_h_regs, 0, tvdac),
	TEGRA_INIT_DATA_MUX("actmon",	NULL,		"actmon",		mux_pllpc_clk32k_clkm,	CLK_SOURCE_ACTMON,	119,	&periph_v_regs, 0, actmon),
	TEGRA_INIT_DATA_MUX("vi_sensor", "vi_sensor",	"tegra_camera",		mux_pllmcpa,		CLK_SOURCE_VI_SENSOR,	20,	&periph_l_regs, TEGRA_PERIPH_NO_RESET, vi_sensor),
	TEGRA_INIT_DATA_DIV16("i2c1",	"div-clk",	"tegra-i2c.0",		mux_pllp_clkm,		CLK_SOURCE_I2C1,	12,	&periph_l_regs, TEGRA_PERIPH_ON_APB, i2c1),
	TEGRA_INIT_DATA_DIV16("i2c2",	"div-clk",	"tegra-i2c.1",		mux_pllp_clkm,		CLK_SOURCE_I2C2,	54,	&periph_h_regs, TEGRA_PERIPH_ON_APB, i2c2),
	TEGRA_INIT_DATA_DIV16("i2c3",	"div-clk",	"tegra-i2c.2",		mux_pllp_clkm,		CLK_SOURCE_I2C3,	67,	&periph_u_regs,	TEGRA_PERIPH_ON_APB, i2c3),
	TEGRA_INIT_DATA_DIV16("i2c4",	"div-clk",	"tegra-i2c.3",		mux_pllp_clkm,		CLK_SOURCE_I2C4,	103,	&periph_v_regs,	TEGRA_PERIPH_ON_APB, i2c4),
	TEGRA_INIT_DATA_DIV16("i2c5",	"div-clk",	"tegra-i2c.4",		mux_pllp_clkm,		CLK_SOURCE_I2C5,	47,	&periph_h_regs,	TEGRA_PERIPH_ON_APB, i2c5),
	TEGRA_INIT_DATA_UART("uarta",	NULL,		"tegra_uart.0",		mux_pllpcm_clkm,	CLK_SOURCE_UARTA,	6,	&periph_l_regs, uarta),
	TEGRA_INIT_DATA_UART("uartb",	NULL,		"tegra_uart.1",		mux_pllpcm_clkm,	CLK_SOURCE_UARTB,	7,	&periph_l_regs, uartb),
	TEGRA_INIT_DATA_UART("uartc",	NULL,		"tegra_uart.2",		mux_pllpcm_clkm,	CLK_SOURCE_UARTC,	55,	&periph_h_regs, uartc),
	TEGRA_INIT_DATA_UART("uartd",	NULL,		"tegra_uart.3",		mux_pllpcm_clkm,	CLK_SOURCE_UARTD,	65,	&periph_u_regs, uartd),
	TEGRA_INIT_DATA_UART("uarte",	NULL,		"tegra_uart.4",		mux_pllpcm_clkm,	CLK_SOURCE_UARTE,	66,	&periph_u_regs, uarte),
	TEGRA_INIT_DATA_MUX8("hdmi",	NULL,		"hdmi",			mux_pllpmdacd2_clkm,	CLK_SOURCE_HDMI,	51,	&periph_h_regs,	0, hdmi),
	TEGRA_INIT_DATA_MUX8("extern1",	NULL,		"extern1",		mux_plla_clk32k_pllp_clkm_plle,	CLK_SOURCE_EXTERN1,	120,	&periph_v_regs,	0, extern1),
	TEGRA_INIT_DATA_MUX8("extern2",	NULL,		"extern2",		mux_plla_clk32k_pllp_clkm_plle,	CLK_SOURCE_EXTERN2,	121,	&periph_v_regs,	0, extern2),
	TEGRA_INIT_DATA_MUX8("extern3",	NULL,		"extern3",		mux_plla_clk32k_pllp_clkm_plle,	CLK_SOURCE_EXTERN3,	122,	&periph_v_regs,	0, extern3),
	TEGRA_INIT_DATA("pwm",		NULL,		"pwm",			mux_pllpc_clk32k_clkm,	CLK_SOURCE_PWM,		28, 2, 0, 0, 8, 1, 0, &periph_l_regs, 17, periph_clk_enb_refcnt, 0, pwm),
};

static struct tegra_periph_init_data tegra_periph_nodiv_clk_list[] = {
	TEGRA_INIT_DATA_NODIV("disp1",	NULL, "tegradc.0", mux_pllpmdacd2_clkm,	     CLK_SOURCE_DISP1,	29, 3, 27, &periph_l_regs, 0, disp1),
	TEGRA_INIT_DATA_NODIV("disp2",	NULL, "tegradc.1", mux_pllpmdacd2_clkm,      CLK_SOURCE_DISP2,	29, 3, 26, &periph_l_regs, 0, disp2),
	TEGRA_INIT_DATA_NODIV("dsib",	NULL, "tegradc.1", mux_plld_out0_plld2_out0, CLK_SOURCE_DSIB,	25, 1, 82, &periph_u_regs, 0, dsib),
};

static void __init tegra30_periph_clk_init(void)
{
	struct tegra_periph_init_data *data;
	struct clk *clk;
	int i;

	/* apbdma */
	clk = tegra_clk_register_periph_gate("apbdma", "clk_m", 0, clk_base, 0, 34,
				    &periph_h_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "tegra-apbdma");
	clks[apbdma] = clk;

	/* rtc */
	clk = tegra_clk_register_periph_gate("rtc", "clk_32k",
				    TEGRA_PERIPH_NO_RESET | TEGRA_PERIPH_ON_APB,
				    clk_base, 0, 4, &periph_l_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "rtc-tegra");
	clks[rtc] = clk;

	/* timer */
	clk = tegra_clk_register_periph_gate("timer", "clk_m", 0, clk_base, 0,
				    5, &periph_l_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "timer");
	clks[timer] = clk;

	/* kbc */
	clk = tegra_clk_register_periph_gate("kbc", "clk_32k",
				    TEGRA_PERIPH_NO_RESET | TEGRA_PERIPH_ON_APB,
				    clk_base, 0, 36, &periph_h_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "tegra-kbc");
	clks[kbc] = clk;

	/* csus */
	clk = tegra_clk_register_periph_gate("csus", "clk_m",
				    TEGRA_PERIPH_NO_RESET | TEGRA_PERIPH_ON_APB,
				    clk_base, 0, 92, &periph_u_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "csus", "tengra_camera");
	clks[csus] = clk;

	/* vcp */
	clk = tegra_clk_register_periph_gate("vcp", "clk_m", 0, clk_base, 0, 29,
				    &periph_l_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "vcp", "tegra-avp");
	clks[vcp] = clk;

	/* bsea */
	clk = tegra_clk_register_periph_gate("bsea", "clk_m", 0, clk_base, 0,
				    62, &periph_h_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "bsea", "tegra-avp");
	clks[bsea] = clk;

	/* bsev */
	clk = tegra_clk_register_periph_gate("bsev", "clk_m", 0, clk_base, 0,
				    63, &periph_h_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "bsev", "tegra-aes");
	clks[bsev] = clk;

	/* usbd */
	clk = tegra_clk_register_periph_gate("usbd", "clk_m", 0, clk_base, 0,
				    22, &periph_l_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "fsl-tegra-udc");
	clks[usbd] = clk;

	/* usb2 */
	clk = tegra_clk_register_periph_gate("usb2", "clk_m", 0, clk_base, 0,
				    58, &periph_h_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "tegra-ehci.1");
	clks[usb2] = clk;

	/* usb3 */
	clk = tegra_clk_register_periph_gate("usb3", "clk_m", 0, clk_base, 0,
				    59, &periph_h_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "tegra-ehci.2");
	clks[usb3] = clk;

	/* dsia */
	clk = tegra_clk_register_periph_gate("dsia", "pll_d_out0", 0, clk_base,
				    0, 48, &periph_h_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "dsia", "tegradc.0");
	clks[dsia] = clk;

	/* csi */
	clk = tegra_clk_register_periph_gate("csi", "pll_p_out3", 0, clk_base,
				    0, 52, &periph_h_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "csi", "tegra_camera");
	clks[csi] = clk;

	/* isp */
	clk = tegra_clk_register_periph_gate("isp", "clk_m", 0, clk_base, 0, 23,
				    &periph_l_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "isp", "tegra_camera");
	clks[isp] = clk;

	/* pcie */
	clk = tegra_clk_register_periph_gate("pcie", "clk_m", 0, clk_base, 0,
				    70, &periph_u_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "pcie", "tegra-pcie");
	clks[pcie] = clk;

	/* afi */
	clk = tegra_clk_register_periph_gate("afi", "clk_m", 0, clk_base, 0, 72,
				    &periph_u_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "afi", "tegra-pcie");
	clks[afi] = clk;

	/* kfuse */
	clk = tegra_clk_register_periph_gate("kfuse", "clk_m",
				    TEGRA_PERIPH_ON_APB,
				    clk_base, 0, 40, &periph_h_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "kfuse-tegra");
	clks[kfuse] = clk;

	/* fuse */
	clk = tegra_clk_register_periph_gate("fuse", "clk_m",
				    TEGRA_PERIPH_ON_APB,
				    clk_base, 0, 39, &periph_h_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "fuse", "fuse-tegra");
	clks[fuse] = clk;

	/* fuse_burn */
	clk = tegra_clk_register_periph_gate("fuse_burn", "clk_m",
				    TEGRA_PERIPH_ON_APB,
				    clk_base, 0, 39, &periph_h_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "fuse_burn", "fuse-tegra");
	clks[fuse_burn] = clk;

	/* apbif */
	clk = tegra_clk_register_periph_gate("apbif", "clk_m", 0,
				    clk_base, 0, 107, &periph_v_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "apbif", "tegra30-ahub");
	clks[apbif] = clk;

	/* hda2hdmi */
	clk = tegra_clk_register_periph_gate("hda2hdmi", "clk_m",
				    TEGRA_PERIPH_ON_APB,
				    clk_base, 0, 128, &periph_w_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "hda2hdmi", "tegra30-hda");
	clks[hda2hdmi] = clk;

	/* sata_cold */
	clk = tegra_clk_register_periph_gate("sata_cold", "clk_m",
				    TEGRA_PERIPH_ON_APB,
				    clk_base, 0, 129, &periph_w_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "tegra_sata_cold");
	clks[sata_cold] = clk;

	/* dtv */
	clk = tegra_clk_register_periph_gate("dtv", "clk_m",
				    TEGRA_PERIPH_ON_APB,
				    clk_base, 0, 79, &periph_u_regs,
				    periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "dtv");
	clks[dtv] = clk;

	/* emc */
	clk = clk_register_mux(NULL, "emc_mux", mux_pllmcp_clkm,
			       ARRAY_SIZE(mux_pllmcp_clkm), 0,
			       clk_base + CLK_SOURCE_EMC,
			       30, 2, 0, NULL);
	clk = tegra_clk_register_periph_gate("emc", "emc_mux", 0, clk_base, 0,
				    57, &periph_h_regs, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, "emc", NULL);
	clks[emc] = clk;

	for (i = 0; i < ARRAY_SIZE(tegra_periph_clk_list); i++) {
		data = &tegra_periph_clk_list[i];
		clk = tegra_clk_register_periph(data->name, data->parent_names,
				data->num_parents, &data->periph,
				clk_base, data->offset, data->flags);
		clk_register_clkdev(clk, data->con_id, data->dev_id);
		clks[data->clk_id] = clk;
	}

	for (i = 0; i < ARRAY_SIZE(tegra_periph_nodiv_clk_list); i++) {
		data = &tegra_periph_nodiv_clk_list[i];
		clk = tegra_clk_register_periph_nodiv(data->name,
					data->parent_names,
					data->num_parents, &data->periph,
					clk_base, data->offset);
		clk_register_clkdev(clk, data->con_id, data->dev_id);
		clks[data->clk_id] = clk;
	}
}

static void __init tegra30_fixed_clk_init(void)
{
	struct clk *clk;

	/* clk_32k */
	clk = clk_register_fixed_rate(NULL, "clk_32k", NULL, CLK_IS_ROOT,
				32768);
	clk_register_clkdev(clk, "clk_32k", NULL);
	clks[clk_32k] = clk;

	/* clk_m_div2 */
	clk = clk_register_fixed_factor(NULL, "clk_m_div2", "clk_m",
				CLK_SET_RATE_PARENT, 1, 2);
	clk_register_clkdev(clk, "clk_m_div2", NULL);
	clks[clk_m_div2] = clk;

	/* clk_m_div4 */
	clk = clk_register_fixed_factor(NULL, "clk_m_div4", "clk_m",
				CLK_SET_RATE_PARENT, 1, 4);
	clk_register_clkdev(clk, "clk_m_div4", NULL);
	clks[clk_m_div4] = clk;

	/* cml0 */
	clk = clk_register_gate(NULL, "cml0", "pll_e", 0, clk_base + PLLE_AUX,
				0, 0, &cml_lock);
	clk_register_clkdev(clk, "cml0", NULL);
	clks[cml0] = clk;

	/* cml1 */
	clk = clk_register_gate(NULL, "cml1", "pll_e", 0, clk_base + PLLE_AUX,
				1, 0, &cml_lock);
	clk_register_clkdev(clk, "cml1", NULL);
	clks[cml1] = clk;

	/* pciex */
	clk = clk_register_fixed_rate(NULL, "pciex", "pll_e", 0, 100000000);
	clk_register_clkdev(clk, "pciex", NULL);
	clks[pciex] = clk;
}

static void __init tegra30_osc_clk_init(void)
{
	struct clk *clk;
	unsigned int pll_ref_div;

	tegra30_clk_measure_input_freq();

	/* clk_m */
	clk = clk_register_fixed_rate(NULL, "clk_m", NULL, CLK_IS_ROOT,
				input_freq);
	clk_register_clkdev(clk, "clk_m", NULL);
	clks[clk_m] = clk;

	/* pll_ref */
	pll_ref_div = tegra30_get_pll_ref_div();
	clk = clk_register_fixed_factor(NULL, "pll_ref", "clk_m",
				CLK_SET_RATE_PARENT, 1, pll_ref_div);
	clk_register_clkdev(clk, "pll_ref", NULL);
	clks[pll_ref] = clk;
}

/* Tegra30 CPU clock and reset control functions */
static void tegra30_wait_cpu_in_reset(u32 cpu)
{
	unsigned int reg;

	do {
		reg = readl(clk_base +
			    TEGRA30_CLK_RST_CONTROLLER_CPU_CMPLX_STATUS);
		cpu_relax();
	} while (!(reg & (1 << cpu)));	/* check CPU been reset or not */

	return;
}

static void tegra30_put_cpu_in_reset(u32 cpu)
{
	writel(CPU_RESET(cpu),
	       clk_base + TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET);
	dmb();
}

static void tegra30_cpu_out_of_reset(u32 cpu)
{
	writel(CPU_RESET(cpu),
	       clk_base + TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_CLR);
	wmb();
}


static void tegra30_enable_cpu_clock(u32 cpu)
{
	unsigned int reg;

	writel(CPU_CLOCK(cpu),
	       clk_base + TEGRA30_CLK_RST_CONTROLLER_CLK_CPU_CMPLX_CLR);
	reg = readl(clk_base +
		    TEGRA30_CLK_RST_CONTROLLER_CLK_CPU_CMPLX_CLR);
}

static void tegra30_disable_cpu_clock(u32 cpu)
{

	unsigned int reg;

	reg = readl(clk_base + TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
	writel(reg | CPU_CLOCK(cpu),
	       clk_base + TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
}

#ifdef CONFIG_PM_SLEEP
static bool tegra30_cpu_rail_off_ready(void)
{
	unsigned int cpu_rst_status;
	int cpu_pwr_status;

	cpu_rst_status = readl(clk_base +
				TEGRA30_CLK_RST_CONTROLLER_CPU_CMPLX_STATUS);
	cpu_pwr_status = tegra_powergate_is_powered(TEGRA_POWERGATE_CPU1) ||
			 tegra_powergate_is_powered(TEGRA_POWERGATE_CPU2) ||
			 tegra_powergate_is_powered(TEGRA_POWERGATE_CPU3);

	if (((cpu_rst_status & 0xE) != 0xE) || cpu_pwr_status)
		return false;

	return true;
}

static void tegra30_cpu_clock_suspend(void)
{
	/* switch coresite to clk_m, save off original source */
	tegra30_cpu_clk_sctx.clk_csite_src =
				readl(clk_base + CLK_RESET_SOURCE_CSITE);
	writel(3<<30, clk_base + CLK_RESET_SOURCE_CSITE);

	tegra30_cpu_clk_sctx.cpu_burst =
				readl(clk_base + CLK_RESET_CCLK_BURST);
	tegra30_cpu_clk_sctx.pllx_base =
				readl(clk_base + CLK_RESET_PLLX_BASE);
	tegra30_cpu_clk_sctx.pllx_misc =
				readl(clk_base + CLK_RESET_PLLX_MISC);
	tegra30_cpu_clk_sctx.cclk_divider =
				readl(clk_base + CLK_RESET_CCLK_DIVIDER);
}

static void tegra30_cpu_clock_resume(void)
{
	unsigned int reg, policy;

	/* Is CPU complex already running on PLLX? */
	reg = readl(clk_base + CLK_RESET_CCLK_BURST);
	policy = (reg >> CLK_RESET_CCLK_BURST_POLICY_SHIFT) & 0xF;

	if (policy == CLK_RESET_CCLK_IDLE_POLICY)
		reg = (reg >> CLK_RESET_CCLK_IDLE_POLICY_SHIFT) & 0xF;
	else if (policy == CLK_RESET_CCLK_RUN_POLICY)
		reg = (reg >> CLK_RESET_CCLK_RUN_POLICY_SHIFT) & 0xF;
	else
		BUG();

	if (reg != CLK_RESET_CCLK_BURST_POLICY_PLLX) {
		/* restore PLLX settings if CPU is on different PLL */
		writel(tegra30_cpu_clk_sctx.pllx_misc,
					clk_base + CLK_RESET_PLLX_MISC);
		writel(tegra30_cpu_clk_sctx.pllx_base,
					clk_base + CLK_RESET_PLLX_BASE);

		/* wait for PLL stabilization if PLLX was enabled */
		if (tegra30_cpu_clk_sctx.pllx_base & (1 << 30))
			udelay(300);
	}

	/*
	 * Restore original burst policy setting for calls resulting from CPU
	 * LP2 in idle or system suspend.
	 */
	writel(tegra30_cpu_clk_sctx.cclk_divider,
					clk_base + CLK_RESET_CCLK_DIVIDER);
	writel(tegra30_cpu_clk_sctx.cpu_burst,
					clk_base + CLK_RESET_CCLK_BURST);

	writel(tegra30_cpu_clk_sctx.clk_csite_src,
					clk_base + CLK_RESET_SOURCE_CSITE);
}
#endif

static struct tegra_cpu_car_ops tegra30_cpu_car_ops = {
	.wait_for_reset	= tegra30_wait_cpu_in_reset,
	.put_in_reset	= tegra30_put_cpu_in_reset,
	.out_of_reset	= tegra30_cpu_out_of_reset,
	.enable_clock	= tegra30_enable_cpu_clock,
	.disable_clock	= tegra30_disable_cpu_clock,
#ifdef CONFIG_PM_SLEEP
	.rail_off_ready	= tegra30_cpu_rail_off_ready,
	.suspend	= tegra30_cpu_clock_suspend,
	.resume		= tegra30_cpu_clock_resume,
#endif
};

static __initdata struct tegra_clk_init_table init_table[] = {
	{uarta, pll_p, 408000000, 0},
	{uartb, pll_p, 408000000, 0},
	{uartc, pll_p, 408000000, 0},
	{uartd, pll_p, 408000000, 0},
	{uarte, pll_p, 408000000, 0},
	{pll_a, clk_max, 564480000, 1},
	{pll_a_out0, clk_max, 11289600, 1},
	{extern1, pll_a_out0, 0, 1},
	{clk_out_1_mux, extern1, 0, 0},
	{clk_out_1, clk_max, 0, 1},
	{blink, clk_max, 0, 1},
	{i2s0, pll_a_out0, 11289600, 0},
	{i2s1, pll_a_out0, 11289600, 0},
	{i2s2, pll_a_out0, 11289600, 0},
	{i2s3, pll_a_out0, 11289600, 0},
	{i2s4, pll_a_out0, 11289600, 0},
	{sdmmc1, pll_p, 48000000, 0},
	{sdmmc2, pll_p, 48000000, 0},
	{sdmmc3, pll_p, 48000000, 0},
	{pll_m, clk_max, 0, 1},
	{pclk, clk_max, 0, 1},
	{csite, clk_max, 0, 1},
	{emc, clk_max, 0, 1},
	{mselect, clk_max, 0, 1},
	{sbc1, pll_p, 100000000, 0},
	{sbc2, pll_p, 100000000, 0},
	{sbc3, pll_p, 100000000, 0},
	{sbc4, pll_p, 100000000, 0},
	{sbc5, pll_p, 100000000, 0},
	{sbc6, pll_p, 100000000, 0},
	{host1x, pll_c, 150000000, 0},
	{disp1, pll_p, 600000000, 0},
	{disp2, pll_p, 600000000, 0},
	{twd, clk_max, 0, 1},
	{gr2d, pll_c, 300000000, 0},
	{gr3d, pll_c, 300000000, 0},
	{clk_max, clk_max, 0, 0}, /* This MUST be the last entry. */
};

static void __init tegra30_clock_apply_init_table(void)
{
	tegra_init_from_table(init_table, clks, clk_max);
}

/*
 * Some clocks may be used by different drivers depending on the board
 * configuration.  List those here to register them twice in the clock lookup
 * table under two names.
 */
static struct tegra_clk_duplicate tegra_clk_duplicates[] = {
	TEGRA_CLK_DUPLICATE(usbd, "utmip-pad", NULL),
	TEGRA_CLK_DUPLICATE(usbd, "tegra-ehci.0", NULL),
	TEGRA_CLK_DUPLICATE(usbd, "tegra-otg", NULL),
	TEGRA_CLK_DUPLICATE(bsev, "tegra-avp", "bsev"),
	TEGRA_CLK_DUPLICATE(bsev, "nvavp", "bsev"),
	TEGRA_CLK_DUPLICATE(vde, "tegra-aes", "vde"),
	TEGRA_CLK_DUPLICATE(bsea, "tegra-aes", "bsea"),
	TEGRA_CLK_DUPLICATE(bsea, "nvavp", "bsea"),
	TEGRA_CLK_DUPLICATE(cml1, "tegra_sata_cml", NULL),
	TEGRA_CLK_DUPLICATE(cml0, "tegra_pcie", "cml"),
	TEGRA_CLK_DUPLICATE(pciex, "tegra_pcie", "pciex"),
	TEGRA_CLK_DUPLICATE(vcp, "nvavp", "vcp"),
	TEGRA_CLK_DUPLICATE(clk_max, NULL, NULL), /* MUST be the last entry */
};

static const struct of_device_id pmc_match[] __initconst = {
	{ .compatible = "nvidia,tegra30-pmc" },
	{},
};

void __init tegra30_clock_init(struct device_node *np)
{
	struct device_node *node;
	int i;

	clk_base = of_iomap(np, 0);
	if (!clk_base) {
		pr_err("ioremap tegra30 CAR failed\n");
		return;
	}

	node = of_find_matching_node(NULL, pmc_match);
	if (!node) {
		pr_err("Failed to find pmc node\n");
		BUG();
	}

	pmc_base = of_iomap(node, 0);
	if (!pmc_base) {
		pr_err("Can't map pmc registers\n");
		BUG();
	}

	tegra30_osc_clk_init();
	tegra30_fixed_clk_init();
	tegra30_pll_init();
	tegra30_super_clk_init();
	tegra30_periph_clk_init();
	tegra30_audio_clk_init();
	tegra30_pmc_clk_init();

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		if (IS_ERR(clks[i])) {
			pr_err("Tegra30 clk %d: register failed with %ld\n",
			       i, PTR_ERR(clks[i]));
			BUG();
		}
		if (!clks[i])
			clks[i] = ERR_PTR(-EINVAL);
	}

	tegra_init_dup_clks(tegra_clk_duplicates, clks, clk_max);

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	tegra_clk_apply_init_table = tegra30_clock_apply_init_table;

	tegra_cpu_car_ops = &tegra30_cpu_car_ops;
}
