// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Stefan Dösinger
 *
 * There are no public datasheets for zx297520v3. The controls on this clock controller have been
 * extracted from ZTE's kernel and U-Boot sources and a LOT of manual testing. Some clocks can be
 * found in the power management (evb_cpurpm.img) and LTE (cpko.ko) blobs shipped with the routers.
 *
 * Because ZTE's sources are wrong or misleading in some parts and testing can be misinterpreted,
 * this file contains a LOT of comments on how particular clocks were tested and how the impact of
 * changes to them can be observed.
 */

#include <dt-bindings/clock/zte,zx297520v3-clk.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "clk-zx.h"

MODULE_IMPORT_NS("ZTE_CLK");

/*
 * AXI speed is either determined by a fixed selection in matrix+0x0, bits 0:2 or by a voting system
 * with ballots in matrix+0x120, 0x124, 0x128. The voting system is enabled by setting matrix 0x12c
 * to 1. Otherwise the selection in 0x0 applies.
 *
 * The fastest selected speed wins. This holds true for values 0 (26 MHz) and 7 (6.5 MHz) too, so it
 * isn't a case of lowest selector wins. I haven't tested what happens if mpll is changed to output
 * 5x the normal clock, so 7 beats 0. Presumably value 0 would still win the election.
 *
 * AXI speed 6.5 is a child of matrix-osc26m, not a PLL. This can be seen by gating it off in
 * topcrm+0x140.
 *
 * The system initializes with voting disabled and all preferences set to the lowest setting, 7.
 * The zx297520v3_matrix_init function transfers the global mux to our vote and enables voting.
 *
 * Because rprocs may change their selection without notice, all possible parents of the AXI mux are
 * marked critical. This can however obscure bugs in the clock tree. Define ZX297520V3_AXI_CANDIDATE
 * to 0 to disable this protection for testing and either not run anything on the rprocs or make
 * sure Linux votes for the fastest rate.
 */
#define ZX297520V3_AXI_CANDIDATE CLK_IS_CRITICAL

/*
 * Top and matrix clocks are chaotic - I haven't found a consistent pattern behind their register
 * and bit locations. Generally there are two gates (pclk, wclk), one mux, two resets and sometimes
 * one divider, but exceptions apply. For some devices there is only a reset and some general
 * (parent) clocks need setup. This structure plus macro handles the somewhat regular parts.
 *
 * There are some patterns that can be observed.
 * mux 0x3c, div 0x48, gate 0x54
 * mux 0x40, div 0x4c, gate 0x5c
 * mux 0x44, div 0x50, gate 0x60
 *
 * For a 0 - 0xc - 0x18 pattern. Muxes from 0x3c to 0x44, dividers from 0x48 to 0x50, gates 0x54 to
 * 0x60. The pattern is broken for timer t17 though.
 *
 * Gates have 4 bits per clock - usually, but not always, bit 0 for wclk, bit 1 for pclk, bit 2 for
 * something the ZTE kernel calls "gate" (the bits we use here are called "en"), which presumably
 * enables automatic clock gating. Bit 3, if settable, seems unused. E.g. offset 0x54 accepts all
 * bits in 0xF77F7F7F - suggesting RTC, I2C0 have an extra gate bit.
 *
 * The default mpll settings multiply the 26 MHz reference clock times 24. A mux selection of 26 MHz
 * could mean using the 26 MHz oscillator directly, or passing it through the PLL and divide by 24.
 *
 * If a UART is set to mpll-d6 (default 104 MHz), changing the mpll multipliers does affect UART
 * timing as it should. This does not happen when the UART is set to 26 MHz input or timers that
 * read 26 MHz input. This suggests 26 MHz clocks use the reference clock directly.
 *
 * The general clock flow on this board goes from osc26m into the PLLs. The PLL output is
 * branched into a small number of divided frequencies (mpll, mpll-d4, mpll-d5, mpll-d6, mpll-d8,
 * gpll, gpll-d2, gpll-d4) at a high level. These divided frequencies as well as osc26m go
 * through a series of gates at offset 0x140 in topcrm. The output of these gates provide wclks for
 * everything.
 *
 * At least 3 of those gate outputs (mpll, dpll, gpll-d2) are further divided behind those gates,
 * presumably in the matrix controller. There are multiple paths how 104 and 78 MHz are derived,
 * some specific to topcrm (0x140 bits 1 and 4), some specific to matrix (0x140 bit 9). There are
 * likely 3 "copies" of the 78 MHz clock and two 50 MHz ones.
 *
 * Pclk is not gated by the 0x140 gates at all. Likewise osc32k is fed to the timers without
 * passing through this gate, even for consumers on matrix/LSP. Topcrm osc26m wclks are not gated
 * either, but top-consumed PLL clocks are.
 */

enum top_clock_ids {
	ZX_CLK_MPLL,
	ZX_CLK_UPLL,
	ZX_CLK_DPLL2,
	ZX_CLK_DPLL2_D2,
	ZX_CLK_DPLL,
	ZX_CLK_GPLL,

	ZX_CLK_OSC26M_D2,
	ZX_CLK_TOP_MPLL_D2_PREGATE,
	ZX_CLK_TOP_MPLL_D4_PREGATE,
	ZX_CLK_TOP_MPLL_D5_PREGATE,
	ZX_CLK_TOP_MPLL_D6_PREGATE,
	ZX_CLK_TOP_MPLL_D8_PREGATE,
	ZX_CLK_TOP_UPLL_D12_PREGATE,
	ZX_CLK_TOP_GPLL_D2_PREGATE,
	ZX_CLK_TOP_GPLL_D4_PREGATE,

	ZX_CLK_TOP_MPLL_D2,
	ZX_CLK_TOP_MPLL_D8,
	ZX_CLK_TOP_MPLL_D6,
	ZX_CLK_TOP_MPLL_D4,
	ZX_CLK_TOMATRIX_MPLL_D5,
	ZX_CLK_TOMATRIX_MPLL,
	ZX_CLK_TOMATRIX_DPLL,
	ZX_CLK_TOP_UPLL_D12,
	ZX_CLK_TOP_UPLL,
	ZX_CLK_TOP_GPLL_D4,
	ZX_CLK_TOMATRIX_GPLL_D2,
	ZX_CLK_TOMATRIX_GPLL,
	ZX_CLK_TOMATRIX_OSC26M,

	ZX_CLK_TOP_MPLL_D2_D4,
	ZX_CLK_TOP_MPLL_D2_D13,
	ZX_CLK_TOP_UPLL_D12_D2,
	ZX_CLK_TOP_GPLL_D4_D2,

	ZX_CLK_M0_MUX,
	ZX_CLK_AHB_MUX,
	ZX_CLK_TIMER_T08_MUX,
	ZX_CLK_TIMER_T09_MUX,
	ZX_CLK_TIMER_T12_MUX,
	ZX_CLK_TIMER_T13_MUX,
	ZX_CLK_TIMER_T14_MUX,
	ZX_CLK_TIMER_T15_MUX,
	ZX_CLK_TIMER_T16_MUX,
	ZX_CLK_TIMER_T17_MUX,
	ZX_CLK_WDT_T18_MUX,
	ZX_CLK_I2C0_MUX,
	ZX_CLK_UART0_MUX,
	ZX_CLK_OUT0_MUX,
	ZX_CLK_OUT1_MUX,
	ZX_CLK_OUT2_MUX,
	ZX_CLK_RMIIPHY_MUX,

	ZX_CLK_TIMER_T08_DIV,
	ZX_CLK_TIMER_T09_DIV,
	ZX_CLK_TIMER_T12_DIV,
	ZX_CLK_TIMER_T13_DIV,
	ZX_CLK_TIMER_T14_DIV,
	ZX_CLK_TIMER_T15_DIV,
	ZX_CLK_TIMER_T16_DIV,
	ZX_CLK_TIMER_T17_DIV,
	ZX_CLK_WDT_T18_DIV,
	ZX_CLK_USIM1_DIV,

	ZX_CLK_AHB_PCLK,
	ZX_CLK_AHB_WCLK,
	ZX_CLK_SRAM1_PCLK,
	ZX_CLK_SRAM2_PCLK,
	ZX_CLK_PMM_WCLK,
	ZX_CLK_PMM_PCLK,
	ZX_CLK_TIMER_T08_WCLK,
	ZX_CLK_TIMER_T08_PCLK,
	ZX_CLK_TIMER_T09_WCLK,
	ZX_CLK_TIMER_T09_PCLK,
	ZX_CLK_TIMER_T12_WCLK,
	ZX_CLK_TIMER_T12_PCLK,
	ZX_CLK_TIMER_T13_WCLK,
	ZX_CLK_TIMER_T13_PCLK,
	ZX_CLK_TIMER_T14_WCLK,
	ZX_CLK_TIMER_T14_PCLK,
	ZX_CLK_TIMER_T15_WCLK,
	ZX_CLK_TIMER_T15_PCLK,
	ZX_CLK_TIMER_T16_WCLK,
	ZX_CLK_TIMER_T16_PCLK,
	ZX_CLK_TIMER_T17_WCLK,
	ZX_CLK_TIMER_T17_PCLK,
	ZX_CLK_WDT_T18_WCLK,
	ZX_CLK_WDT_T18_PCLK,
	ZX_CLK_I2C0_WCLK,
	ZX_CLK_I2C0_PCLK,
	ZX_CLK_UART0_WCLK,
	ZX_CLK_UART0_PCLK,
	ZX_CLK_USB_WCLK,
	ZX_CLK_USB_PCLK,
	ZX_CLK_HSIC_WCLK,
	ZX_CLK_HSIC_PCLK,
	ZX_CLK_RTC_WCLK,
	ZX_CLK_RTC_PCLK,
	ZX_CLK_USIM1_WCLK,
	ZX_CLK_USIM1_PCLK,
	ZX_CLK_LPM_GSM_WCLK,
	ZX_CLK_LPM_GSM_PCLK,
	ZX_CLK_LPM_LTE_WCLK,
	ZX_CLK_LPM_LTE_PCLK,
	ZX_CLK_LPM_TD_WCLK,
	ZX_CLK_LPM_TD_PCLK,
	ZX_CLK_LPM_W_WCLK,
	ZX_CLK_LPM_W_PCLK,
	ZX_CLK_OUT0_WCLK,
	ZX_CLK_OUT1_WCLK,
	ZX_CLK_OUT2_WCLK,
	ZX_CLK_OUT32K_WCLK,
	ZX_CLK_RMIIPHY_WCLK,
	ZX_CLK_GSM_32K,
	ZX_CLK_GSM_26M,
	ZX_CLK_GSM_48M,
	ZX_CLK_GSM_104M,
	ZX_CLK_GSM_156M,
	ZX_CLK_ABB_WCLK,
};

/* Used for gates where we don't know the parent input(s). Assume general oscillator */
static const struct zx_parent_desc clk_unknown = PARENT_FW("osc26m");

/* For clocks that are known to use the ungated osc26m input */
static const struct zx_parent_desc clk_main[] = {
	PARENT_FW("osc26m"),
};

static const struct zx_parent_desc dpll_parents[] = {
	PARENT_ID(ZX_CLK_DPLL2_D2),
	PARENT_FW("osc26m"),
};

/*
 * AHB: The clock mux works and impact can be tested e.g. with iperf speed testing of the USB
 * network connection. Values 2 and 3 give the same speed and depend on the same bit (3) in 0x140.
 * The mpll-d6 rate is gated off by the same bit in 0x140 as m0's mpll-d6, but the d8 is different.
 * This may indicate that one of the two uses a different clock rate than documented in ZTE's
 * kernel. The M0 speed is easy to measure, so I am confident in the M0's 78 MHz rate. AHB looks
 * like 78 rather than 52, but confidence is lower because RAM read tests are influenced by CPU
 * overhead too.
 */

static const struct zx_parent_desc ahb_sel[] = {
	PARENT_FW("osc26m"),
	PARENT_ID(ZX_CLK_TOP_MPLL_D6),	/* 104 MHz */
	PARENT_ID(ZX_CLK_TOP_MPLL_D8),	/* 78 MHz */
	PARENT_ID(ZX_CLK_TOP_MPLL_D8),	/* 78 MHz */
};

static const struct zx_parent_desc timer_top_sel[] = {
	PARENT_FW("osc32k"),
	PARENT_FW("osc26m"),
};

static const struct zx_parent_desc uart_top_sel[] = {
	PARENT_FW("osc26m"),
	PARENT_ID(ZX_CLK_TOP_MPLL_D6),	/* 104 MHz, sharing M0's and ahb's 0x140 gate */
};

/*
 * The Cortex M0 coprocessor. It is responsible for booting the board and runs some power management
 * helper code on the stock firmware. The M0 rproc itself is not critical, but most of topcrm's
 * registers become unreadable if this is gated off or become slow when clocked at 32 kHz, e.g. GPIO
 * becomes borderline unusable at 32 kHz.
 *
 * There is no dedicated M0 gate. Disabling ahb-wclk looks like the M0 stops from A53's point of
 * view, but it merely breaks the A53's ability to communicate with it, and M0's ability to
 * communicate with the standard UART on LSP. M0 keeps running with both ahb-wclk and ahb-pclk
 * gated off and can re-enable the gates.
 */
static const struct zx_parent_desc m0_sel[] = {
	PARENT_FW("osc26m"),
	PARENT_ID(ZX_CLK_TOP_MPLL_D6),		/* 104 MHz */
	PARENT_ID(ZX_CLK_TOP_MPLL_D2_D4),	/* 78 MHz, but different gate than the ahb 78 MHz */
	PARENT_FW("osc32k"),			/* Yes, tested. It is SLLLLOOOOOWWW. */
};

/*
 * Clk-out0/1/2/32k: These clocks are exposed on GPIOs 15, 16, 17 and 18 respectively. They are used
 * in ZTE's camera and sound code, by directly poking into the clock registers from the device
 * drivers. Until the respective devices are supported they can safely be switched off.
 *
 * For clk-out1 ZTE's camera code says the following:
 *
 * 0 -> 20 MHz
 * 1 -> 40 MHz
 * 2 -> 13 MHz
 * 3 -> 26 MHz
 *
 * 0 and 1 read from upll. I confirmed their rates (upll-d24 and upll-d12) by setting upll to a very
 * low frequency and sampling the clock by GPIO reads. The outputs of 2 and 3 are way too high to
 * test that way. Neither is reading from any PLL, they keep oscillating when all PLLs are disabled.
 * I am fairly sure that 3 is just direct osc26m because it perfectly matches what out2 is
 * showing in its 26 MHz setting.
 *
 * Clk-out2 is similar, but it has only one control bit in top 0x34 bit 8. Neither setting selects
 * a PLL output. When setting *0x34 = 0x080, clk-out1 and clk-out2 are in lockstep, presumably
 * running at 13 MHz. 0x1c0 also runs in lockstep, presumably 26 MHz.
 *
 * clk-out0 has a mux in bit 5. Value 0 most likely selects osc26m. I am not entirely sure about
 * value 1 (which is the default), but the m0 mux has an impact on it. It looks like a debug pin
 * that exposes some core clock.
 */
static const struct zx_parent_desc out0_sel[] = {
	PARENT_FW("osc26m"),
	PARENT_ID(ZX_CLK_M0_MUX),
};

static const struct zx_parent_desc out1_sel[] = {
	PARENT_ID(ZX_CLK_TOP_UPLL_D12_D2),	/* 20 MHz */
	PARENT_ID(ZX_CLK_TOP_UPLL_D12),		/* 40 MHz */
	PARENT_ID(ZX_CLK_OSC26M_D2),		/* 13 MHz */
	PARENT_FW("osc26m"),
};

static const struct zx_parent_desc out2_sel[] = {
	PARENT_ID(ZX_CLK_OSC26M_D2),		/* 13 MHz */
	PARENT_FW("osc26m"),
};

/*
 * Clk-o is similar to clk-out*, providing a clock on GPIO 20, presumably for driving a (R)MII phy.
 * The 50 MHz value is documented in a stray comment in ZTE's GMAC driver. I tested it similarly to
 * the above pins. Mux setting 0 gives half the count as setting 1 and setting 1 gives gpll divided
 * by 4 - matching the 50 MHz suggested by the ZTE comment at the default gpll settings. Gating gpll
 * stops the clock for values 0 and 1.
 *
 * Settings 2 and 3 are possible, but seem to return garbage. It is generally pulsing up and down,
 * except if both gpll and upll are stopped. I suspect it just reads random electrical fluctuation
 * from other places in the board. Yes, I had a pull-down enabled when testing this.
 *
 * I am not aware of any board that uses this though. The Ethernet-equipped ones I have all run the
 * PHY with its own oscillator.
 */
static const struct zx_parent_desc rmii_sel[] = {
	PARENT_ID(ZX_CLK_TOP_GPLL_D4_D2),	/* 25 MHz */
	PARENT_ID(ZX_CLK_TOP_GPLL_D4),		/* 50 MHz */
};

#define PLL(_name, _parents, _rate, _reg, _flags)		\
	{							\
		.type = ZX_CLOCK_PLL,				\
		.pll = {					\
			.name = _name,				\
			.parents = _parents,			\
			.num_parents = ARRAY_SIZE(_parents),	\
			.rate = _rate,				\
			.reg = _reg,				\
			.flags = _flags,			\
		},						\
	}

#define FIXED_DIV(_name, _parent, _div)				\
	{							\
		.type = ZX_CLOCK_FIXED_DIV,			\
		.fixed_div = {					\
			.name = _name,				\
			.parent = _parent,			\
			.div = _div,				\
		},						\
	}

#define MUX(_name, _parents, _reg, _shift, _size)		\
	{							\
		.type = ZX_CLOCK_MUX,				\
		.mux = {					\
			.name = _name,				\
			.parents = _parents,			\
			.num_parents = ARRAY_SIZE(_parents),	\
			.reg = _reg,				\
			.shift = _shift,			\
			.size = _size,				\
		},						\
	}

#define DIV(_name, _parent, _reg, _shift, _size)		\
	{							\
		.type = ZX_CLOCK_DIV,				\
		.div = {					\
			.name = _name,				\
			.parent = _parent,			\
			.reg = _reg,				\
			.shift = _shift,			\
			.size = _size,				\
		},						\
	}

#define GATE(_name, _parent, _reg, _shift, _flags)		\
	{							\
		.type = ZX_CLOCK_GATE,				\
		.gate = {					\
			.name = _name,				\
			.parent = _parent,			\
			.reg = _reg,				\
			.flags = _flags,			\
			.shift = _shift,			\
		},						\
	}

static const struct zx_clock zx297520v3_top_clocks[] = {
	/*
	 * Default setting: 0x48040c11. 624/312/156. Only a single possible parent. This is the
	 * PLL for pretty much everything, including CPU, RAM and USB.
	 *
	 * Changing this PLL makes it possible to overclock the CPU or do more fine grained
	 * underclocking than the CPU's mux allows. It does run into two problems though: The USB
	 * device uses this PLL's output directly and is *very* sensitive to differences. DRAM
	 * is also fed by this clock and needs to be re-trained on larger changes, which needs to
	 * be done by the stage 1 boot loader.
	 */
	[ZX_CLK_MPLL]              = PLL("mpll", clk_main, 0, 0x8, 0),

	/*
	 * ZTE's code calls this PLL "upll". The only possible consumers I found are clk-out1,
	 * which outputs this clock on GPIO 16 and HSIC. HSIC doesn't have anything connected to
	 * it on the devices I tested. The device that consumes clk-out1 seems to be an SPI
	 * camera, which I haven't seen in any device so far. ZTE manipulates this PLL directly out
	 * of the camera code, so it is unlikely to have another consumer.
	 *
	 * Long story short, shut it off.
	 */
	[ZX_CLK_UPLL]              = PLL("upll", clk_main, 0, 0x10, 0),

	/*
	 * Default value 0x4834902d. Feeds dpll. 46.08 MHz. Bit 25 can be set, so two parents are
	 * possible. It looks like both values select the 26 MHz oscillator though.
	 *
	 * Since dpll's prepare may depend on this clock generating a usable signal, it needs to be
	 * enabled when prepared.
	 *
	 * It isn't named anywhere in ZTE's code, but cpko.ko has the string "dpll2" somewhere in
	 * its clock tables.
	 */
	[ZX_CLK_DPLL2]             = PLL("dpll2", clk_main, 0, 0x100, CLK_ZX_PLL_PREPARE_IS_ENABLE),
	[ZX_CLK_DPLL2_D2]          = FIXED_DIV("dpll2-d2", PARENT_ID(ZX_CLK_DPLL2), 2),

	/*
	 * The documentation says 491.52 MHz and measurement with the LSP TDM device supports this.
	 * The default value is 0x480c2011, but not all boot loaders set it up. To get to 491.52
	 * with these settings it needs a 23.04 MHz reference clock, which matches dpll2-d2. If
	 * dpll2 is disabled, dpll loses its lock. We set the frequency on this PLL if we find it is
	 * not prepared by the boot loader.
	 *
	 * The proprietary LTE driver and/or rproc prepare and unprepare it. TDM and I2S can use it.
	 *
	 * It accepts parent values 0, 1, 2 and 3. Parent 0 is dpll2-d2. The others look like
	 * osc26m. With a parent != 0 dpll never loses its lock even when all other PLLs are off
	 * and the TDM counter register increases at a rate consistent with a 26.0/23.04 clock
	 * increase.
	 */
	[ZX_CLK_DPLL]              = PLL("dpll", dpll_parents, 491520000, 0x18, 0),

	/*
	 * "g" is either for "general" or "gigahertz". The VCO runs at 1 GHz. Output clocks are 200,
	 * 100, 50, 25, ... MHz. It is used optionally by SDIO and QSPI and can drive a GPIO clock
	 * output for RMII, so it doesn't seem very general.
	 */
	[ZX_CLK_GPLL]              = PLL("gpll", clk_main, 0, 0x110, 0),

	/* osc26m division for clk_outX */
	[ZX_CLK_OSC26M_D2]         = FIXED_DIV("osc26m-d2", clk_main[0], 2),

	/* Pre-0x140 distribution fixed divs */
	[ZX_CLK_TOP_MPLL_D2_PREGATE]  = FIXED_DIV("mpll-d2", PARENT_ID(ZX_CLK_MPLL), 2),
	[ZX_CLK_TOP_MPLL_D4_PREGATE]  = FIXED_DIV("mpll-d4", PARENT_ID(ZX_CLK_MPLL), 4),
	[ZX_CLK_TOP_MPLL_D5_PREGATE]  = FIXED_DIV("mpll-d5", PARENT_ID(ZX_CLK_MPLL), 5),
	[ZX_CLK_TOP_MPLL_D6_PREGATE]  = FIXED_DIV("mpll-d6", PARENT_ID(ZX_CLK_MPLL), 6),
	[ZX_CLK_TOP_MPLL_D8_PREGATE]  = FIXED_DIV("mpll-d8", PARENT_ID(ZX_CLK_MPLL), 8),
	[ZX_CLK_TOP_UPLL_D12_PREGATE] = FIXED_DIV("upll-d12", PARENT_ID(ZX_CLK_UPLL), 12),
	[ZX_CLK_TOP_GPLL_D2_PREGATE]  = FIXED_DIV("gpll-d2", PARENT_ID(ZX_CLK_GPLL), 2),
	[ZX_CLK_TOP_GPLL_D4_PREGATE]  = FIXED_DIV("gpll-d4", PARENT_ID(ZX_CLK_GPLL), 4),

	/*
	 * topcrm distribution gates. These are complicated, but shed a lot of light on the board's
	 * clock distribution.
	 *
	 * Settable mask: 0x017333FF. Generally one nibble per PLL, except for MPLL that has more.
	 * One for osc26m work clock. pclk and osc32k are not gated globally here or anywhere else
	 * as far as I can see.
	 *
	 * If you want to test your understanding of this board, try to manually configure it into a
	 * setup where bit 24 or bit 9 are off without crashing it.
	 */

	/* Bit 0: No known consumer */
	/* Bit 1: M0's 78 MHz selection, but somehow also involved in USB too */
	[ZX_CLK_TOP_MPLL_D2]       = GATE("top-mpll-d2", PARENT_ID(ZX_CLK_TOP_MPLL_D2_PREGATE),
					  0x140, 1, 0),
	/* Bit 2: No known consumer, named "mpll_48m_clk" in ZTE's cpko.ko blob */
	[ZX_CLK_TOP_MPLL_D8]       = GATE("top-mpll-d8", PARENT_ID(ZX_CLK_TOP_MPLL_D8_PREGATE),
					  0x140, 3, 0),
	[ZX_CLK_TOP_MPLL_D6]       = GATE("top-mpll-d6", PARENT_ID(ZX_CLK_TOP_MPLL_D6_PREGATE),
					  0x140, 4, 0),
	[ZX_CLK_TOP_MPLL_D4]       = GATE("top-mpll-d4", PARENT_ID(ZX_CLK_TOP_MPLL_D4_PREGATE),
					  0x140, 5, 0),
	[ZX_CLK_TOMATRIX_MPLL_D5]  = GATE("matrix-mpll-d5", PARENT_ID(ZX_CLK_TOP_MPLL_D5_PREGATE),
					  0x140, 6, ZX297520V3_AXI_CANDIDATE),
	/* Bit 7: No known consumer */
	/* Bit 8: Has a consumer, LTE depends on it. But unknown what and how. */
	/*
	 * Bit 9: Big MPLL provider for Matrix. It is an AXI voting candidate, but it is protected
	 * in Matrix's 0x118 gates.
	 */
	[ZX_CLK_TOMATRIX_MPLL]     = GATE("matrix-mpll", PARENT_ID(ZX_CLK_MPLL), 0x140, 9, 0),
	/* Bit 10: Always 0 */
	/* Bit 11: Always 0 */

	/* Bit 12: No known consumer. Existing patterns indicate a dpll consumer in top itself */
	[ZX_CLK_TOMATRIX_DPLL]     = GATE("matrix-dpll", PARENT_ID(ZX_CLK_DPLL), 0x140, 13, 0),
	/* Bit 14: Always 0 */
	/* Bit 15: Always 0 */

	[ZX_CLK_TOP_UPLL_D12]      = GATE("top-upll-d12", PARENT_ID(ZX_CLK_TOP_UPLL_D12_PREGATE),
					  0x140, 16, 0),
	[ZX_CLK_TOP_UPLL]          = GATE("top-upll", PARENT_ID(ZX_CLK_UPLL), 0x140, 17, 0),
	/* Bit 18: Always 0 */
	/* Bit 19: Always 0 */

	[ZX_CLK_TOP_GPLL_D4]       = GATE("top-gpll-d4", PARENT_ID(ZX_CLK_TOP_GPLL_D4_PREGATE),
					  0x140, 20, 0),
	[ZX_CLK_TOMATRIX_GPLL_D2]  = GATE("matrix-gpll-d2", PARENT_ID(ZX_CLK_TOP_GPLL_D2_PREGATE),
					  0x140, 21, 0),
	[ZX_CLK_TOMATRIX_GPLL]     = GATE("matrix-gpll", PARENT_ID(ZX_CLK_GPLL), 0x140, 22, 0),
	/* Bit 23: Always 0 */

	/*
	 * Bit 24 covers all 26 MHz on matrix, but not pclks. Osc26m consumers on topcrm do not
	 * use this, they get the ungated osc26m. It is an AXI voting candidate, but does not
	 * need protection because it is the slowest speed. It will only be used if every processor,
	 * including us, votes for it.
	 */
	[ZX_CLK_TOMATRIX_OSC26M]   = GATE("matrix-osc26m", clk_main[0], 0x140, 24, 0),

	/*
	 * Post-140 divisions. They enter through the same bit and can be selected by different mux
	 * values of their consumers or get piped to multiple consumers with different documented
	 * rates - in particular, USB wclk (24 MHz according to ZTE) shares bit 1 with M0's 78 MHz.
	 */
	[ZX_CLK_TOP_MPLL_D2_D4]    = FIXED_DIV("top-mpll-d2-d4", PARENT_ID(ZX_CLK_TOP_MPLL_D2), 4),
	[ZX_CLK_TOP_MPLL_D2_D13]   = FIXED_DIV("top-mpll-d2-d13", PARENT_ID(ZX_CLK_TOP_MPLL_D2),
					       13),
	[ZX_CLK_TOP_UPLL_D12_D2]   = FIXED_DIV("top-upll-d12-d2", PARENT_ID(ZX_CLK_TOP_UPLL_D12),
					       2),
	[ZX_CLK_TOP_GPLL_D4_D2]    = FIXED_DIV("top-gpll-d4-d2", PARENT_ID(ZX_CLK_TOP_GPLL_D4), 2),

	/* Device-specific Muxes */
	[ZX_CLK_M0_MUX]            = MUX("m0-mux",        m0_sel,            0x38,  0, 2),
	[ZX_CLK_AHB_MUX]           = MUX("ahb-mux",       ahb_sel,           0x3c,  4, 2),
	[ZX_CLK_TIMER_T08_MUX]     = MUX("timer-t08-mux", timer_top_sel,     0x40,  1, 1),
	[ZX_CLK_TIMER_T09_MUX]     = MUX("timer-t09-mux", timer_top_sel,     0x40,  0, 1),
	[ZX_CLK_TIMER_T12_MUX]     = MUX("timer-t12-mux", timer_top_sel,     0x3c,  0, 1),
	[ZX_CLK_TIMER_T13_MUX]     = MUX("timer-t13-mux", timer_top_sel,     0x44,  0, 1),
	[ZX_CLK_TIMER_T14_MUX]     = MUX("timer-t14-mux", timer_top_sel,     0x44,  1, 1),
	[ZX_CLK_TIMER_T15_MUX]     = MUX("timer-t15-mux", timer_top_sel,     0x3c,  3, 1),
	[ZX_CLK_TIMER_T16_MUX]     = MUX("timer-t16-mux", timer_top_sel,     0x44,  2, 1),
	[ZX_CLK_TIMER_T17_MUX]     = MUX("timer-t17-mux", timer_top_sel,    0x120,  0, 1),
	[ZX_CLK_WDT_T18_MUX]       = MUX("wdt-t18-mux",   timer_top_sel,     0x3c,  6, 1),
	[ZX_CLK_I2C0_MUX]          = MUX("i2c0-mux",      uart_top_sel,      0x3c,  1, 1),
	[ZX_CLK_UART0_MUX]         = MUX("uart0-mux",     uart_top_sel,      0x40,  2, 1),
	[ZX_CLK_OUT0_MUX]          = MUX("out0-mux",      out0_sel,          0x34,  5, 1),
	[ZX_CLK_OUT1_MUX]          = MUX("out1-mux",      out1_sel,          0x34,  6, 2),
	[ZX_CLK_OUT2_MUX]          = MUX("out2-mux",      out2_sel,          0x34,  8, 1),
	[ZX_CLK_RMIIPHY_MUX]       = MUX("rmiiphy-mux",   rmii_sel,         0x11c,  0, 2),

	/* Device-specific dividers */
	[ZX_CLK_TIMER_T08_DIV]     = DIV("timer-t08-div", PARENT_ID(ZX_CLK_TIMER_T08_MUX),
					 0x4c, 8, 4),
	[ZX_CLK_TIMER_T09_DIV]     = DIV("timer-t09-div", PARENT_ID(ZX_CLK_TIMER_T09_MUX),
					 0x4c, 0, 4),
	[ZX_CLK_TIMER_T12_DIV]     = DIV("timer-t12-div", PARENT_ID(ZX_CLK_TIMER_T12_MUX),
					 0x48, 0, 4),
	[ZX_CLK_TIMER_T13_DIV]     = DIV("timer-t13-div", PARENT_ID(ZX_CLK_TIMER_T13_MUX),
					 0x50, 0, 4),
	[ZX_CLK_TIMER_T14_DIV]     = DIV("timer-t14-div", PARENT_ID(ZX_CLK_TIMER_T14_MUX),
					 0x50, 4, 4),
	[ZX_CLK_TIMER_T15_DIV]     = DIV("timer-t15-div", PARENT_ID(ZX_CLK_TIMER_T15_MUX),
					 0x48, 4, 4),
	[ZX_CLK_TIMER_T16_DIV]     = DIV("timer-t16-div", PARENT_ID(ZX_CLK_TIMER_T16_MUX),
					 0x50, 8, 4),
	[ZX_CLK_TIMER_T17_DIV]     = DIV("timer-t17-div", PARENT_ID(ZX_CLK_TIMER_T17_MUX),
					 0x124, 0, 4),
	[ZX_CLK_WDT_T18_DIV]       = DIV("wdt-t18-div", PARENT_ID(ZX_CLK_WDT_T18_MUX),
					 0x48, 8, 4),
	[ZX_CLK_USIM1_DIV]         = DIV("usim1-div", clk_unknown,
					 0x48, 12, 1),

	/* Device specific gates */

	/*
	 * AHB clock gates: Disabling these cuts off the A53 from register access into 0x130000/
	 * 0x140000, including the topcrm region, so it can't be re-enabled. Mark them critical for
	 * that reason.
	 *
	 * Gating this off does not stop the M0 rproc, nor does it stop M0 from accessing topcrm. It
	 * does however gate off M0 from the AXI interconnect, so it can't read RAM, Matrix, etc. So
	 * these bits here merely gate the bus, not the actual devices.
	 */
	[ZX_CLK_AHB_PCLK]          = GATE("ahb-pclk", PARENT_ID(ZX_CLK_AHB_MUX), 0x54, 12,
					  CLK_IS_CRITICAL),
	[ZX_CLK_AHB_WCLK]          = GATE("ahb-wclk", PARENT_ID(ZX_CLK_M0_MUX), 0x54, 13,
					  CLK_IS_CRITICAL),

	/* SRAM1 and 2 clocks. Leave them on for now, as turning them off carelessly hangs the M0 */
	[ZX_CLK_SRAM1_PCLK]        = GATE("sram1-pclk", clk_main[0], 0x54, 18, CLK_IS_CRITICAL),
	[ZX_CLK_SRAM2_PCLK]        = GATE("sram2-pclk", clk_main[0], 0x54, 16, CLK_IS_CRITICAL),

	/*
	 * Pinmux (AON, TOP, IOCFG but not PDCFG). Critical as well until we have a driver that
	 * consumes it. I don't think we'll realistically shut this off ever.
	 *
	 * Setting either bit 0 or 1 in register 0x58 makes the device work.
	 */
	[ZX_CLK_PMM_WCLK]          = GATE("pmm-wclk", clk_main[0], 0x58, 0, CLK_IS_CRITICAL),
	[ZX_CLK_PMM_PCLK]          = GATE("pmm-pclk", clk_main[0], 0x58, 1, CLK_IS_CRITICAL),

	/*
	 * Timers. We don't use any of them, just shut them off. The timers are named and sorted
	 * by the IO address of the main timer controls. Some of the controls are documented in
	 * ZTE's kernel, the others I found by trial and error. Timers in register 0x54 have the
	 * pclk first, the others have the wclk first.
	 *
	 * Timer T17 is used by the ZSP firmware. The rproc driver will enable it as needed.
	 */
	[ZX_CLK_TIMER_T08_WCLK]    = GATE("timer-t08-wclk", PARENT_ID(ZX_CLK_TIMER_T08_DIV),
					  0x5c, 8, 0),
	[ZX_CLK_TIMER_T08_PCLK]    = GATE("timer-t08-pclk", clk_main[0], 0x5c, 9, 0),
	[ZX_CLK_TIMER_T09_WCLK]    = GATE("timer-t09-wclk", PARENT_ID(ZX_CLK_TIMER_T09_DIV),
					  0x5c, 4, 0),
	[ZX_CLK_TIMER_T09_PCLK]    = GATE("timer-t09-pclk", clk_main[0], 0x5c, 5, 0),
	[ZX_CLK_TIMER_T12_WCLK]    = GATE("timer-t12-wclk", PARENT_ID(ZX_CLK_TIMER_T12_DIV),
					  0x54, 5, 0),
	[ZX_CLK_TIMER_T12_PCLK]    = GATE("timer-t12-pclk", clk_main[0], 0x54, 4, 0),
	[ZX_CLK_TIMER_T13_WCLK]    = GATE("timer-t13-wclk", PARENT_ID(ZX_CLK_TIMER_T13_DIV),
					  0x60, 0, 0),
	[ZX_CLK_TIMER_T13_PCLK]    = GATE("timer-t13-pclk", clk_main[0], 0x60, 1, 0),
	[ZX_CLK_TIMER_T14_WCLK]    = GATE("timer-t14-wclk", PARENT_ID(ZX_CLK_TIMER_T14_DIV),
					  0x60, 4, 0),
	[ZX_CLK_TIMER_T14_PCLK]    = GATE("timer-t14-pclk", clk_main[0], 0x60, 5, 0),
	[ZX_CLK_TIMER_T15_WCLK]    = GATE("timer-t15-wclk", PARENT_ID(ZX_CLK_TIMER_T15_DIV),
					  0x54, 21, 0),
	[ZX_CLK_TIMER_T15_PCLK]    = GATE("timer-t15-pclk", clk_main[0], 0x54, 20, 0),
	[ZX_CLK_TIMER_T16_WCLK]    = GATE("timer-t16-wclk", PARENT_ID(ZX_CLK_TIMER_T16_DIV),
					  0x60, 8, 0),
	[ZX_CLK_TIMER_T16_PCLK]    = GATE("timer-t16-pclk", clk_main[0], 0x60, 9, 0),
	[ZX_CLK_TIMER_T17_WCLK]    = GATE("timer-t17-wclk", PARENT_ID(ZX_CLK_TIMER_T17_DIV),
					  0x128, 0, 0),
	[ZX_CLK_TIMER_T17_PCLK]    = GATE("timer-t17-pclk", clk_main[0], 0x128, 1, 0),

	/*
	 * This watchdog is set up by ZTE's bootloader and in ZTE's OS the m0 firmware will feed the
	 * dog. The m0 firmware in turn wants to be fed in its own way. Since we normally don't run
	 * any m0 firmware we shut it off by default and expose it to userspace via the watchdog
	 * driver.
	 */
	[ZX_CLK_WDT_T18_WCLK]     = GATE("wdt-t18-wclk", PARENT_ID(ZX_CLK_WDT_T18_DIV),
					 0x54, 25, 0),
	[ZX_CLK_WDT_T18_PCLK]     = GATE("wdt-t18-pclk", clk_main[0], 0x54, 24, 0),

	[ZX_CLK_I2C0_WCLK]        = GATE("i2c0-wclk", PARENT_ID(ZX_CLK_I2C0_MUX), 0x54, 9, 0),
	[ZX_CLK_I2C0_PCLK]        = GATE("i2c0-pclk", clk_main[0], 0x54, 8, 0),
	[ZX_CLK_UART0_WCLK]       = GATE("uart0-wclk", PARENT_ID(ZX_CLK_UART0_MUX), 0x5c, 12, 0),
	[ZX_CLK_UART0_PCLK]       = GATE("uart0-pclk", clk_main[0], 0x5c, 13, 0),

	/*
	 * ZTE says the USB input is a 24 MHz clock based on mpll. Testing shows that upll is not
	 * involved. The USB register space can be accessed with ahb-pclk gated, but not with
	 * ahb-wclk gated. ZTE also lists ahb-wclk as parent for the second clock.
	 *
	 * There doesn't seem to be a separate PHY clock. usb-wclk stops controller registers from
	 * updating, but doesn't cause the USB device to disconnect like asserting the PHY reset
	 * does. Bit 0 in this register can't be set, so no PHY clock is hiding there either.
	 */
	[ZX_CLK_USB_WCLK]         = GATE("usb-wclk", PARENT_ID(ZX_CLK_TOP_MPLL_D2_D13), 0x6c, 3, 0),
	[ZX_CLK_USB_PCLK]         = GATE("usb-pclk", PARENT_ID(ZX_CLK_AHB_WCLK), 0x6c, 4, 0),
	/*
	 * The HSIC hardware is listed in ZTE's code with a physical address of 0x01600000. Unlike
	 * the USB controller, it does consume upll. ZTE says 240 MHz, but 480 is the usual one for
	 * HSIC.
	 */
	[ZX_CLK_HSIC_WCLK]        = GATE("hsic-wclk", PARENT_ID(ZX_CLK_TOP_UPLL), 0x6c, 1, 0),
	[ZX_CLK_HSIC_PCLK]        = GATE("hsic-pclk", PARENT_ID(ZX_CLK_AHB_WCLK), 0x6c, 2, 0),

	/*
	 * How does this RTC work? I don't know, the ZTE kernel does not talk to it. The actual RTC
	 * is on the I2C-connected PMIC.
	 */
	[ZX_CLK_RTC_WCLK]         = GATE("rtc-wclk", clk_unknown, 0x54, 1, 0),
	[ZX_CLK_RTC_PCLK]         = GATE("rtc-pclk", clk_main[0], 0x54, 0, 0),

	[ZX_CLK_USIM1_WCLK]       = GATE("usim1-wclk", PARENT_ID(ZX_CLK_USIM1_DIV), 0x54, 29, 0),
	[ZX_CLK_USIM1_PCLK]       = GATE("usim1-pclk", clk_main[0], 0x54, 28, 0),

	/*
	 * LTE: gate only as far as I can see. I looked for resets and did not find any. There may
	 * be mux/div, but without understanding the behavior of this hardware it is impossible to
	 * tell. They are sorted by physical MMIO address of the devices, which happens to be the
	 * inverse order of the bits.
	 *
	 * Like PMM in this register, either bit enables register access, whether they control pclk
	 * or wclk cannot be distinguished.
	 *
	 * The work parent of GSM, TD and W is without doubt osc26m. The mmio space has registers
	 * that count up at an integer fraction of 26000000. Gating PLLs does not affect them,
	 * including gating mpll.
	 *
	 * LPM_LTE has some interaction with LTE_MODEM_WCLK. If LTE_MODEM_WCLK is disabled, LPM_LTE
	 * stops counting, and just flips between two states. This makes it look like LPM_LTE in
	 * topcrm has a clock parent in matrixcrm, which wouldn't make much sense. I think this
	 * behavior is a result of the devices interacting rather than a clock parentage.
	 *
	 * As for what these devices do? I am not sure. They generate a lot of IRQs, but the IRQs
	 * are independent of actual radio activity. The ZSP firmware programs them
	 */
	[ZX_CLK_LPM_GSM_WCLK]     = GATE("lpm-gsm-wclk", clk_main[0], 0x58, 10, 0),
	[ZX_CLK_LPM_GSM_PCLK]     = GATE("lpm-gsm-pclk", clk_main[0], 0x58, 11, 0),
	[ZX_CLK_LPM_LTE_WCLK]     = GATE("lpm-lte-wclk", clk_unknown, 0x58, 8, 0),
	[ZX_CLK_LPM_LTE_PCLK]     = GATE("lpm-lte-pclk", clk_main[0], 0x58, 9, 0),
	[ZX_CLK_LPM_TD_WCLK]      = GATE("lpm-td-wclk", clk_main[0], 0x58, 6, 0),
	[ZX_CLK_LPM_TD_PCLK]      = GATE("lpm-td-pclk", clk_main[0], 0x58, 7, 0),
	[ZX_CLK_LPM_W_WCLK]       = GATE("lpm-w-wclk", clk_main[0], 0x58, 4, 0),
	[ZX_CLK_LPM_W_PCLK]       = GATE("lpm-w-pclk", clk_main[0], 0x58, 5, 0),

	[ZX_CLK_OUT0_WCLK]        = GATE("out0-wclk", PARENT_ID(ZX_CLK_OUT0_MUX), 0x34, 0, 0),
	[ZX_CLK_OUT1_WCLK]        = GATE("out1-wclk", PARENT_ID(ZX_CLK_OUT1_MUX), 0x90, 2, 0),
	[ZX_CLK_OUT2_WCLK]        = GATE("out2-wclk", PARENT_ID(ZX_CLK_OUT2_MUX), 0x94, 2, 0),
	[ZX_CLK_OUT32K_WCLK]      = GATE("out32k-wclk", PARENT_FW("osc32k"), 0x34, 1, 0),
	[ZX_CLK_RMIIPHY_WCLK]     = GATE("rmiiphy-wclk", PARENT_ID(ZX_CLK_RMIIPHY_MUX),
					 0x11c, 2, 0),

	/*
	 * GSM clocks - they feed the 2G/3G radio components and SIM controller.
	 *
	 * The names are from the clock table in cpko.ko. The parents of gsm-104m and gsm-156m are
	 * experimentally verified. The gsm-48m parent is likely bit 2 in topcrm+0x140. I have not
	 * found an consumer that I understand well enough to say if it is running or not, so I have
	 * not been able to confirm this yet.
	 */
	[ZX_CLK_GSM_32K]          = GATE("gsm-32k", PARENT_FW("osc32k"), 0x68, 0, 0),
	[ZX_CLK_GSM_26M]          = GATE("gsm-26m", PARENT_FW("osc26m"), 0x68, 1, 0),
	[ZX_CLK_GSM_48M]          = GATE("gsm-48m", clk_unknown, 0x68, 2, 0),
	[ZX_CLK_GSM_104M]         = GATE("gsm-104m", PARENT_ID(ZX_CLK_TOP_MPLL_D6), 0x68, 3, 0),
	[ZX_CLK_GSM_156M]         = GATE("gsm-156m", PARENT_ID(ZX_CLK_TOP_MPLL_D4), 0x68, 4, 0),

	/* Analog Baseband, probably. cpko.ko enables it for LTE, and LTE scans come back empty
	 * without it. The MMIO location of the device is unknown. It is likely connected only to
	 * ZSP and inaccessible to the main CPU.
	 */
	[ZX_CLK_ABB_WCLK]         = GATE("abb-wclk", clk_unknown, 0x144, 1, 0),
};

static const unsigned int zx297520v3_top_exports[] = {
	[ZX297520V3_M0_WCLK] =		ZX_CLK_M0_MUX,
	[ZX297520V3_SRAM1_PCLK] =	ZX_CLK_SRAM1_PCLK,
	[ZX297520V3_SRAM2_PCLK] =	ZX_CLK_SRAM2_PCLK,
	[ZX297520V3_UART0_WCLK] =	ZX_CLK_UART0_WCLK,
	[ZX297520V3_UART0_PCLK] =	ZX_CLK_UART0_PCLK,
	[ZX297520V3_I2C0_WCLK] =	ZX_CLK_I2C0_WCLK,
	[ZX297520V3_I2C0_PCLK] =	ZX_CLK_I2C0_PCLK,
	[ZX297520V3_RTC_WCLK] =		ZX_CLK_RTC_WCLK,
	[ZX297520V3_RTC_PCLK] =		ZX_CLK_RTC_PCLK,
	[ZX297520V3_LPM_GSM_WCLK] =	ZX_CLK_LPM_GSM_WCLK,
	[ZX297520V3_LPM_GSM_PCLK] =	ZX_CLK_LPM_GSM_PCLK,
	[ZX297520V3_LPM_LTE_WCLK] =	ZX_CLK_LPM_LTE_WCLK,
	[ZX297520V3_LPM_LTE_PCLK] =	ZX_CLK_LPM_LTE_PCLK,
	[ZX297520V3_LPM_TD_WCLK] =	ZX_CLK_LPM_TD_WCLK,
	[ZX297520V3_LPM_TD_PCLK] =	ZX_CLK_LPM_TD_PCLK,
	[ZX297520V3_LPM_W_WCLK] =	ZX_CLK_LPM_W_WCLK,
	[ZX297520V3_LPM_W_PCLK] =	ZX_CLK_LPM_W_PCLK,
	[ZX297520V3_TIMER_T08_WCLK] =	ZX_CLK_TIMER_T08_WCLK,
	[ZX297520V3_TIMER_T08_PCLK] =	ZX_CLK_TIMER_T08_PCLK,
	[ZX297520V3_TIMER_T09_WCLK] =	ZX_CLK_TIMER_T09_WCLK,
	[ZX297520V3_TIMER_T09_PCLK] =	ZX_CLK_TIMER_T09_PCLK,
	[ZX297520V3_MPLL] =		ZX_CLK_TOMATRIX_MPLL,
	[ZX297520V3_MPLL_D5] =		ZX_CLK_TOMATRIX_MPLL_D5,
	[ZX297520V3_DPLL] =		ZX_CLK_TOMATRIX_DPLL,
	[ZX297520V3_GPLL] =		ZX_CLK_TOMATRIX_GPLL,
	[ZX297520V3_GPLL_D2] =		ZX_CLK_TOMATRIX_GPLL_D2,
	[ZX297520V3_GATED_OSC26M] =	ZX_CLK_TOMATRIX_OSC26M,
	[ZX297520V3_PMM_WCLK] =		ZX_CLK_PMM_WCLK,
	[ZX297520V3_PMM_PCLK] =		ZX_CLK_PMM_PCLK,
	[ZX297520V3_OUT0_WCLK] =	ZX_CLK_OUT0_WCLK,
	[ZX297520V3_OUT1_WCLK] =	ZX_CLK_OUT1_WCLK,
	[ZX297520V3_OUT2_WCLK] =	ZX_CLK_OUT2_WCLK,
	[ZX297520V3_OUT32K_WCLK] =	ZX_CLK_OUT32K_WCLK,
	[ZX297520V3_RMIIPHY_WCLK] =	ZX_CLK_RMIIPHY_WCLK,
	[ZX297520V3_TIMER_T12_WCLK] =	ZX_CLK_TIMER_T12_WCLK,
	[ZX297520V3_TIMER_T12_PCLK] =	ZX_CLK_TIMER_T12_PCLK,
	[ZX297520V3_TIMER_T13_WCLK] =	ZX_CLK_TIMER_T13_WCLK,
	[ZX297520V3_TIMER_T13_PCLK] =	ZX_CLK_TIMER_T13_PCLK,
	[ZX297520V3_TIMER_T14_WCLK] =	ZX_CLK_TIMER_T14_WCLK,
	[ZX297520V3_TIMER_T14_PCLK] =	ZX_CLK_TIMER_T14_PCLK,
	[ZX297520V3_TIMER_T15_WCLK] =	ZX_CLK_TIMER_T15_WCLK,
	[ZX297520V3_TIMER_T15_PCLK] =	ZX_CLK_TIMER_T15_PCLK,
	[ZX297520V3_TIMER_T16_WCLK] =	ZX_CLK_TIMER_T16_WCLK,
	[ZX297520V3_TIMER_T16_PCLK] =	ZX_CLK_TIMER_T16_PCLK,
	[ZX297520V3_TIMER_T17_WCLK] =	ZX_CLK_TIMER_T17_WCLK,
	[ZX297520V3_TIMER_T17_PCLK] =	ZX_CLK_TIMER_T17_PCLK,
	[ZX297520V3_WDT_T18_WCLK] =	ZX_CLK_WDT_T18_WCLK,
	[ZX297520V3_WDT_T18_PCLK] =	ZX_CLK_WDT_T18_PCLK,
	[ZX297520V3_USIM1_WCLK] =	ZX_CLK_USIM1_WCLK,
	[ZX297520V3_USIM1_PCLK] =	ZX_CLK_USIM1_PCLK,
	[ZX297520V3_AHB_WCLK] =		ZX_CLK_AHB_WCLK,
	[ZX297520V3_AHB_PCLK] =		ZX_CLK_AHB_PCLK,
	[ZX297520V3_USB_WCLK] =		ZX_CLK_USB_WCLK,
	[ZX297520V3_USB_PCLK] =		ZX_CLK_USB_PCLK,
	[ZX297520V3_HSIC_WCLK] =	ZX_CLK_HSIC_WCLK,
	[ZX297520V3_HSIC_PCLK] =	ZX_CLK_HSIC_PCLK,
	[ZX297520V3_GSM_32K] =		ZX_CLK_GSM_32K,
	[ZX297520V3_GSM_26M] =		ZX_CLK_GSM_26M,
	[ZX297520V3_GSM_48M] =		ZX_CLK_GSM_48M,
	[ZX297520V3_GSM_104M] =		ZX_CLK_GSM_104M,
	[ZX297520V3_GSM_156M] =		ZX_CLK_GSM_156M,
	[ZX297520V3_ABB_WCLK] =		ZX_CLK_ABB_WCLK,
};

static const struct zx_clk_data zx297520v3_topclk_data = {
	.clocks = zx297520v3_top_clocks,
	.num_clocks = ARRAY_SIZE(zx297520v3_top_clocks),
	.exports = zx297520v3_top_exports,
	.num_exports = ARRAY_SIZE(zx297520v3_top_exports),
};

static int clk_zx297520v3_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);

	if (!id)
		return -ENODEV;

	return zx_clk_common_probe(&pdev->dev, pdev->dev.parent->of_node,
				   (const struct zx_clk_data *)id->driver_data);
}

static const struct platform_device_id clk_zx297520v3_ids[] = {
	{
		.name = "zx297520v3-topclk",
		.driver_data = (kernel_ulong_t)&zx297520v3_topclk_data,
	},
	{ }
};
MODULE_DEVICE_TABLE(platform, clk_zx297520v3_ids);

static struct platform_driver clk_zx297520v3 = {
	.probe = clk_zx297520v3_probe,
	.driver = {
		.name = "clk-zx297520v3",
	},
	.id_table = clk_zx297520v3_ids,
};
module_platform_driver(clk_zx297520v3);

MODULE_AUTHOR("Stefan Dösinger <stefandoesinger@gmail.com>");
MODULE_DESCRIPTION("ZTE zx297520v3 clock driver");
MODULE_LICENSE("GPL");
