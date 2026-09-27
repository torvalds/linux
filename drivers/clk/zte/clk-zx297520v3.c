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

enum matrix_clock_ids {
	ZX_CLK_MATRIX_OSC26M_D4,
	ZX_CLK_MATRIX_MPLL_D2_PREGATE,
	ZX_CLK_MATRIX_MPLL_D4_PREGATE,
	ZX_CLK_MATRIX_MPLL_D6_PREGATE,
	ZX_CLK_MATRIX_MPLL_D8_PREGATE,
	ZX_CLK_MATRIX_MPLL_D12_PREGATE,
	ZX_CLK_MATRIX_MPLL_D16_PREGATE,
	ZX_CLK_MATRIX_GPLL_D2_D2_PREGATE,
	ZX_CLK_MATRIX_GPLL_D2_D4_PREGATE,
	ZX_CLK_MATRIX_DPLL_D2_PREGATE,
	ZX_CLK_MATRIX_DPLL_D4_PREGATE,

	ZX_CLK_MATRIX_MPLL_D16,
	ZX_CLK_MATRIX_MPLL_D12,
	ZX_CLK_MATRIX_MPLL_D8,
	ZX_CLK_MATRIX_MPLL_D6,
	ZX_CLK_MATRIX_MPLL_D4,
	ZX_CLK_MATRIX_MPLL_D2,
	ZX_CLK_MATRIX_DPLL_D4,
	ZX_CLK_MATRIX_DPLL_D2,
	ZX_CLK_MATRIX_GPLL_D2_D4,
	ZX_CLK_MATRIX_GPLL_D2_D2,

	ZX_CLK_CPU_MUX,
	ZX_CLK_ZSP_MUX,
	ZX_CLK_DDR_CTRL_MUX,
	ZX_CLK_SD0_MUX,
	ZX_CLK_SD1_MUX,
	ZX_CLK_NAND_MUX,
	ZX_CLK_EDCP_MUX,
	ZX_CLK_TDM_MUX,
	ZX_CLK_AXI_MUX,

	ZX_CLK_SYS_TIMER_WCLK,
	ZX_CLK_CPU_WCLK,
	ZX_CLK_CPU_PCLK,
	ZX_CLK_ZSP_WCLK,
	ZX_CLK_ZSP_PCLK,
	ZX_CLK_DDR_CTRL_PCLK,
	ZX_CLK_DDR_CTRL_WCLK,
	ZX_CLK_SD0_PCLK,
	ZX_CLK_SD0_WCLK,
	ZX_CLK_SD0_CDET,
	ZX_CLK_SD1_PCLK,
	ZX_CLK_SD1_WCLK,
	ZX_CLK_SD1_CDET,
	ZX_CLK_NAND_WCLK,
	ZX_CLK_NAND_PCLK,
	ZX_CLK_LTE_MODEM_WCLK,
	ZX_CLK_LTE_MODEM_PCLK,
	ZX_CLK_EDCP_PCLK,
	ZX_CLK_EDCP_WCLK,
	ZX_CLK_RFFE_WCLK,
	ZX_CLK_SSC_WCLK,
	ZX_CLK_SSC_PCLK,
	ZX_CLK_PDCFG_WCLK,
	ZX_CLK_PDCFG_PCLK,
	ZX_CLK_MBOX_PCLK,
	ZX_CLK_SRAM0_PCLK,
	ZX_CLK_GSM_CFG_PCLK,
	ZX_CLK_GMAC_WCLK,
	ZX_CLK_GMAC_PCLK,
	ZX_CLK_GMAC_AHB,
	ZX_CLK_AXI_WCLK,
	ZX_CLK_DMA_PCLK,
	ZX_CLK_VOU_WCLK,
	ZX_CLK_VOU_PCLK,
	ZX_CLK_TOLSP_MPLL_D5_WCLK,
	ZX_CLK_TOLSP_MPLL_D4_WCLK,
	ZX_CLK_TOLSP_MPLL_D6_WCLK,
	ZX_CLK_TOLSP_MPLL_D8_WCLK,
	ZX_CLK_TOLSP_MPLL_D12_WCLK,
	ZX_CLK_TOLSP_OSC26M_WCLK,
	ZX_CLK_TOLSP_OSC32K_WCLK,
	ZX_CLK_TOLSP_PCLK,
	ZX_CLK_TOLSP_TDM_WCLK,
	ZX_CLK_TOLSP_DPLL_D4_WCLK,
};

/*
 * For devices which have a working driver the work clock can be figured out by gating off registers
 * in topcrm+0x140. This is used for devices where I can't interpret the register contents yet.
 */
static const struct zx_parent_desc matrix_unk = PARENT_FW("wclk-osc26m");

static const struct zx_parent_desc cpu_sel[] = {
	PARENT_FW("wclk-osc26m"),
	PARENT_FW("mpll"),			/* 624 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D2),	/* 312 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D4),	/* 156 MHz */
};

/*
 * Figuring these values out is a bit tricky. The best liveness check of the ZSP is reading its TCM
 * at 0x81000000. If it is clocked up and out of reset reading it will succeed. Otherwise, reading
 * will stall. Value 1 depends on topcrm+0x140, bit 13, but not any matrixcrm+0x118 gate, so it is
 * likely dpll without a div. Mux value 2 depends on 0x118 bit 5, so mpll-d2. Mux value 3 depends on
 * 0x118 bit 4, so mpll-d4. Mux value 0 in turn depends on topcrm+0x140, bit 24 (osc26m), so it
 * follows the usual pattern that mux 0 is osc26m.
 *
 * If the firmware is running and fed with initialization data it will start writing status messages
 * on UART1. An interruption of these messages or a failure to respond to rpmsg is not proof that
 * the ZSP's clock has been disabled. The firmware's workings depend on regular timer and LTE device
 * IRQs, so one of these devices might have been stopped instead.
 */
static const struct zx_parent_desc zsp_sel[] = {
	PARENT_FW("wclk-osc26m"),
	PARENT_FW("dpll"),			/* 491.52 MHz, boot default */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D2),	/* 312 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D4),	/* 156 MHz */
};

/*
 * We can't realistically change DDR speed while running an OS out of DDR (it reads garbage for a
 * short while on transition), but we need to know if we have to keep gpll alive. 32 MB devices use
 * 200 MHz, while 64/128 MB ones use 156 MHz.
 */
static const struct zx_parent_desc ddr_ctrl_sel[] = {
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D4),	/* 156 MHz */
	PARENT_FW("gpll"),			/* 200 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D6),	/* 104 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D8),	/* 78 MHz */
};

static const struct zx_parent_desc sd0_sel[] = {
	PARENT_FW("wclk-osc26m"),
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D4),	/* 156 MHz */
	PARENT_FW("gpll-d2"),			/* 100 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D8),	/* 78 MHz */
	PARENT_ID(ZX_CLK_MATRIX_GPLL_D2_D2),	/* 50 MHz */
	PARENT_ID(ZX_CLK_MATRIX_GPLL_D2_D4),	/* 25 MHz */
};

static const struct zx_parent_desc sd1_sel[] = {
	PARENT_FW("wclk-osc26m"),
	PARENT_FW("gpll-d2"),			/* 100 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D8),	/* 78 MHz */
	PARENT_ID(ZX_CLK_MATRIX_GPLL_D2_D2),	/* 50 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D16),	/* 39 MHz */
	PARENT_ID(ZX_CLK_MATRIX_GPLL_D2_D4),	/* 25 MHz */
};

/*
 * ZTE's kernel puts the high frequency first, but this is against the usual convention on this SoC.
 * I don't have any device that has a raw NAND controller though.
 */
static const struct zx_parent_desc nand_sel[] = {
	PARENT_FW("wclk-osc26m"),
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D4),	/* 156 MHz */
};

static const struct zx_parent_desc edcp_sel[] = {
	PARENT_FW("wclk-osc26m"),
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D4),	/* 156 MHz */
	PARENT_FW("mpll-d5"),			/* 124.8 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D6),	/* 104 MHz */
};

/*
 * The 122.88 MHz selection of value 1 is given in ZTE's kernel and matches up to observation: Print
 * the TDM counter at regular intervals and look at its increase at values 0/1/2. It increases by
 * about 310*26 for value 0, 310*122.88 for value 1 and 310*104 for value 2.
 *
 * Mux value 1 is gated off by matrix+0x118 bit 8. Values 2 and 3 are both gated off by 0x118 bit 3,
 * suggesting that they select the same parent.
 */
static const struct zx_parent_desc tdm_sel[] = {
	PARENT_FW("wclk-osc26m"),
	PARENT_ID(ZX_CLK_MATRIX_DPLL_D4),	/* 122.88 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D6),	/* 104 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D6),
};

/*
 * ZTE's firmware uses 0x128 as the PHY's vote and names 0x120 as "PS" and 0x124 as "AP" vote.
 * However, their cpufreq driver ultimately uses the "PS" define and thus writes to 0x120, which can
 * be observed in the running system.
 */
#define ZX297520V3_AXI_AP_VOTE 0x120
static const struct zx_parent_desc axi_sel[] = {
	PARENT_FW("wclk-osc26m"),
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D4),	/* 156 MHz */
	PARENT_FW("mpll-d5"),			/* 124.8 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D6),	/* 104 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D8),	/* 78 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D12),	/* 52 MHz */
	PARENT_ID(ZX_CLK_MATRIX_MPLL_D16),	/* 39 MHz */
	PARENT_ID(ZX_CLK_MATRIX_OSC26M_D4),	/* 6.5 MHz */
};

static const struct zx_clock zx297520v3_matrix_clocks[] = {
	[ZX_CLK_MATRIX_OSC26M_D4] = FIXED_DIV("matrix-osc26m-d4", PARENT_FW("wclk-osc26m"), 4),
	[ZX_CLK_MATRIX_MPLL_D2_PREGATE] = FIXED_DIV("matrix-mpll-d2-pregate", PARENT_FW("mpll"), 2),
	[ZX_CLK_MATRIX_MPLL_D4_PREGATE] = FIXED_DIV("matrix-mpll-d4-pregate", PARENT_FW("mpll"), 4),
	/* div 5: provided as a separate line from topcrm */
	[ZX_CLK_MATRIX_MPLL_D6_PREGATE] = FIXED_DIV("matrix-mpll-d6-pregate", PARENT_FW("mpll"), 6),
	[ZX_CLK_MATRIX_MPLL_D8_PREGATE] = FIXED_DIV("matrix-mpll-d8-pregate", PARENT_FW("mpll"), 8),
	[ZX_CLK_MATRIX_MPLL_D12_PREGATE] = FIXED_DIV("matrix-mpll-d12-pregate", PARENT_FW("mpll"),
						     12),
	[ZX_CLK_MATRIX_MPLL_D16_PREGATE] = FIXED_DIV("matrix-mpll-d16-pregate", PARENT_FW("mpll"),
						     16),

	[ZX_CLK_MATRIX_GPLL_D2_D2_PREGATE] = FIXED_DIV("matrix-gpll-d2-d2-pregate",
						       PARENT_FW("gpll-d2"), 2),
	[ZX_CLK_MATRIX_GPLL_D2_D4_PREGATE] = FIXED_DIV("matrix-gpll-d2-d4-pregate",
						       PARENT_FW("gpll-d2"), 4),

	[ZX_CLK_MATRIX_DPLL_D2_PREGATE] = FIXED_DIV("matrix-dpll-d2-pregate", PARENT_FW("dpll"), 2),
	[ZX_CLK_MATRIX_DPLL_D4_PREGATE] = FIXED_DIV("matrix-dpll-d4-pregate", PARENT_FW("dpll"), 4),

	/*
	 * Matrix distribution gates: Every matrix-generated subdivision of the input PLLs
	 * has a gate in 0x118. The original inputs from topcrm (mpll 624, mpll 124.8, gpll 200,
	 * gpll 100, dpll 491.52) do not.
	 *
	 * Bit 8 is consumed by TDM and the frequency dpll/4 holds up to observation. Bit 9 is
	 * consumed by an unknown device and the frequency is guessed from the pattern that lower
	 * bits gate lower frequencies.
	 */
	[ZX_CLK_MATRIX_MPLL_D16] = GATE("matrix-mpll-d16",
					PARENT_ID(ZX_CLK_MATRIX_MPLL_D16_PREGATE), 0x118, 0,
					ZX297520V3_AXI_CANDIDATE),
	[ZX_CLK_MATRIX_MPLL_D12] = GATE("matrix-mpll-d12",
					PARENT_ID(ZX_CLK_MATRIX_MPLL_D12_PREGATE), 0x118, 1,
					ZX297520V3_AXI_CANDIDATE),
	[ZX_CLK_MATRIX_MPLL_D8] = GATE("matrix-mpll-d8",
				       PARENT_ID(ZX_CLK_MATRIX_MPLL_D8_PREGATE), 0x118, 2,
				       ZX297520V3_AXI_CANDIDATE),
	[ZX_CLK_MATRIX_MPLL_D6] = GATE("matrix-mpll-d6",
				       PARENT_ID(ZX_CLK_MATRIX_MPLL_D6_PREGATE), 0x118, 3,
				       ZX297520V3_AXI_CANDIDATE),
	[ZX_CLK_MATRIX_MPLL_D4] = GATE("matrix-mpll-d4",
				       PARENT_ID(ZX_CLK_MATRIX_MPLL_D4_PREGATE), 0x118, 4,
				       ZX297520V3_AXI_CANDIDATE),
	[ZX_CLK_MATRIX_MPLL_D2] = GATE("matrix-mpll-d2",
					PARENT_ID(ZX_CLK_MATRIX_MPLL_D2_PREGATE), 0x118, 5, 0),

	[ZX_CLK_MATRIX_DPLL_D4] = GATE("matrix-dpll-d4",
				       PARENT_ID(ZX_CLK_MATRIX_DPLL_D4_PREGATE), 0x118, 8, 0),
	[ZX_CLK_MATRIX_DPLL_D2] = GATE("matrix-dpll-d2",
				       PARENT_ID(ZX_CLK_MATRIX_DPLL_D2_PREGATE), 0x118, 9, 0),

	[ZX_CLK_MATRIX_GPLL_D2_D4] = GATE("matrix-gpll-d2-d4",
					  PARENT_ID(ZX_CLK_MATRIX_GPLL_D2_D4_PREGATE),
					  0x118, 12, 0),
	[ZX_CLK_MATRIX_GPLL_D2_D2] = GATE("matrix-gpll-d2-d2",
					  PARENT_ID(ZX_CLK_MATRIX_GPLL_D2_D2_PREGATE),
					  0x118, 13, 0),

	[ZX_CLK_CPU_MUX]          = MUX("cpu-mux", cpu_sel, 0x20, 0, 2),
	[ZX_CLK_ZSP_MUX]          = MUX("zsp-mux", zsp_sel, 0x30, 0, 2),
	[ZX_CLK_DDR_CTRL_MUX]     = MUX("ddr-ctrl-mux", ddr_ctrl_sel, 0x50, 0, 2),
	[ZX_CLK_SD0_MUX]          = MUX("sd0-mux", sd0_sel, 0x50, 4, 3),
	[ZX_CLK_SD1_MUX]          = MUX("sd1-mux", sd1_sel, 0x50, 8, 3),
	[ZX_CLK_NAND_MUX]         = MUX("nand-mux", nand_sel, 0x50, 12, 2),
	[ZX_CLK_EDCP_MUX]         = MUX("edcp-mux", edcp_sel, 0x50, 16, 2),
	[ZX_CLK_TDM_MUX]          = MUX("tdm-mux", tdm_sel, 0x50, 24, 2),
	[ZX_CLK_AXI_MUX]          = MUX("axi-mux", axi_sel, ZX297520V3_AXI_AP_VOTE, 0, 3),

	/*
	 * This bit cuts off the clock signal to the ARM architected timer, which the kernel uses
	 * as its main timer. It isn't critical per se - there are plenty of proprietary timers
	 * available that could be used - but the arm arch timer binding does not accept a clock, so
	 * this CCF driver won't know if the timer is in use.
	 *
	 * This clock is fed by the osc26m gate in topcrm+0x140 - this critical clock here protects
	 * its parent.
	 *
	 * Registers 0x148, 0x14c, 0x150, 0x154 allow setting and clearing the timer counter. To
	 * set the timer, load a value into 0x148 (high 32 bits) and 0x14c (low 32 bits), then
	 * toggle 0x150 to 1. 0x150 will automatically reset to 0. 0x154 sets the timer to some
	 * "clear" value, but this value isn't 0.
	 */
	[ZX_CLK_SYS_TIMER_WCLK]   = GATE("sys-timer-wclk", PARENT_FW("wclk-osc26m"), 0x144, 1,
					 CLK_IS_CRITICAL),

	/*
	 * Both 0x24 and 0x28 bits 1 and 2 stop the CPU. There is also a bit in topcrm+0x138, which
	 * ZTE's uboot calls "A53 reset", which also stops the CPU. I can't really tell the
	 * difference between matrix+0x28 and top+0x138. The clock (matrix+0x24) can be disabled
	 * and enabled from the Cortex M0 and it will nicely stop and restart the A53, retaining
	 * all state.
	 */
	[ZX_CLK_CPU_WCLK]         = GATE("cpu-wclk", PARENT_ID(ZX_CLK_CPU_MUX), 0x24, 1,
					 CLK_IS_CRITICAL),
	[ZX_CLK_CPU_PCLK]         = GATE("cpu-pclk", clk_main[0], 0x24, 2, CLK_IS_CRITICAL),

	/*
	 * There are a lot more controls in matrix+0x100. 13-16 appear to be 4 different AXI
	 * channels for different priorities. Bit 19 appears to be the DDR PHY wclk.
	 *
	 * The important task is to keep gpll enabled if the bootloader selected a gpll-based rate
	 * for RAM.
	 */
	[ZX_CLK_DDR_CTRL_PCLK]    = GATE("ddr-ctrl-pclk", clk_main[0], 0x100, 17, CLK_IS_CRITICAL),
	[ZX_CLK_DDR_CTRL_WCLK]    = GATE("ddr-ctrl-wclk", PARENT_ID(ZX_CLK_DDR_CTRL_MUX), 0x100, 18,
					 CLK_IS_CRITICAL),

	[ZX_CLK_ZSP_PCLK]         = GATE("zsp-pclk", clk_main[0], 0x34, 0, 0),
	[ZX_CLK_ZSP_WCLK]         = GATE("zsp-wclk", PARENT_ID(ZX_CLK_ZSP_MUX), 0x34, 1, 0),

	/* Both SDIO controllers depend on the ahb-pclk for register access. */
	[ZX_CLK_SD0_PCLK]         = GATE("sd0-pclk", PARENT_FW("ahb"), 0x54, 12, 0),
	[ZX_CLK_SD0_WCLK]         = GATE("sd0-wclk", PARENT_ID(ZX_CLK_SD0_MUX), 0x54, 13, 0),
	[ZX_CLK_SD0_CDET]         = GATE("sd0-cdet", PARENT_FW("osc32k"), 0x54, 14, 0),
	[ZX_CLK_SD1_PCLK]         = GATE("sd1-pclk", PARENT_FW("ahb"), 0x54, 4, 0),
	[ZX_CLK_SD1_WCLK]         = GATE("sd1-wclk", PARENT_ID(ZX_CLK_SD1_MUX), 0x54, 5, 0),
	/*
	 * I don't know how the cdet clock works. Card detection in the way the dwc,mmc driver uses
	 * it appears broken no matter this clock's setting.
	 */
	[ZX_CLK_SD1_CDET]         = GATE("sd1-cdet", PARENT_FW("osc32k"), 0x54, 6, 0),

	/* This is some "denali" NAND, not the qspi connected one */
	[ZX_CLK_NAND_WCLK]        = GATE("nand-wclk", PARENT_ID(ZX_CLK_NAND_MUX), 0x54, 20, 0),
	[ZX_CLK_NAND_PCLK]        = GATE("nand-pclk", clk_main[0], 0x54, 21, 0),

	/*
	 * There is a set of gates for an unknown device at matrix+0x60. It is some LTE related
	 * device and the pclk gates off mmio areas 0xf8000000 and 0xfc000000. These areas are only
	 * accessible to M0 and ZSP, not the AP.
	 *
	 * If it is fed an incorrect clock, e.g. because dpll is not locked and outputs 26 MHz, it
	 * will spam IRQs at SPI 68. This is likely a distress notification that some
	 * internal PLL did not lock or similar. ZTE's kernel has two defines for SPI 68:
	 * "LTE_TPU_INT_PS_INT" and "VOU_OSD_INT". The latter is unlikely because VOU related clocks
	 * sit in matrix+0x168.
	 *
	 * The address space in 0xfc000000 appears to contain a number of different devices, so I
	 * have named the clock LTE_MODEM_{W/P}CLK. It is not known how this mmio area and clock
	 * relates to SPI 68 or the ABB clock in topcrm+0x144. Because it is not accessible to the
	 * AP at all, I don't think we need to care beyond turning on the clocks before booting the
	 * ZSP.
	 *
	 * Bit 9 in register 0x118 turns the IRQs off, so it is the immediate wclk parent. It isn't
	 * clear what divisor of dpll it is using though. Following the pattern that lower
	 * frequencies are gated by lower bits, it would indicate dpll/2 or dpll/3.
	 */
	[ZX_CLK_LTE_MODEM_WCLK]   = GATE("lte-modem-wclk", PARENT_ID(ZX_CLK_MATRIX_DPLL_D2),
					 0x60, 0, 0),
	[ZX_CLK_LTE_MODEM_PCLK]   = GATE("lte-modem-pclk", clk_main[0],
					 0x60, 2, 0),

	[ZX_CLK_EDCP_PCLK]        = GATE("edcp-pclk", clk_main[0], 0x64, 1, 0),
	[ZX_CLK_EDCP_WCLK]        = GATE("edcp-wclk", PARENT_ID(ZX_CLK_EDCP_MUX), 0x64, 2, 0),

	/*
	 * This seems to be another SPI-Like device. cpko.ko uses it to send calibration blobs to
	 * the LTE radio hardware. After initialization the ZSP firmware takes over control of this
	 * device.
	 *
	 * Testing reveals the parent as matrix's 104 MHz mpll fraction.
	 */
	[ZX_CLK_RFFE_WCLK]        = GATE("rffe-wclk", PARENT_ID(ZX_CLK_MATRIX_MPLL_D6), 0x84, 0, 0),
	[ZX_CLK_SSC_WCLK]         = GATE("ssc-wclk", PARENT_ID(ZX_CLK_MATRIX_MPLL_D6), 0x84, 1, 0),
	[ZX_CLK_SSC_PCLK]         = GATE("ssc-pclk", clk_main[0], 0x84, 2, 0),

	/*
	 * PDCFG. Like PMM, either clock bit will allow the device to function. Probably there is
	 * no wclk line at all and the two bits are just an artifact of generally having two bits
	 * per device.
	 */
	[ZX_CLK_PDCFG_WCLK]       = GATE("pdcfg-wclk", matrix_unk, 0x88, 0, CLK_IS_CRITICAL),
	[ZX_CLK_PDCFG_PCLK]       = GATE("pdcfg-pclk", clk_main[0], 0x88, 1, CLK_IS_CRITICAL),
	[ZX_CLK_MBOX_PCLK]        = GATE("mbox-pclk", clk_main[0], 0x88, 2, 0),
	[ZX_CLK_SRAM0_PCLK]       = GATE("sram0-pclk", clk_main[0], 0x88, 4, 0),
	[ZX_CLK_GSM_CFG_PCLK]     = GATE("gsm-cfg-pclk", clk_main[0], 0x88, 8, 0),

	/*
	 * ZTE's driver has a statement to the effect of *(top->base+0x11c) = 5, with a comment
	 * suggesting that this sets a 50 MHz clock. The clock code itself lists GMAC clocks in
	 * matrix+0x110 and lists the parents of these clocks as 50 MHz gpll output, but
	 * the downstream ZTE GMAC driver never enables the clocks. It turns out ZTE's code is
	 * highly misleading.
	 *
	 * The GMAC's work clock is definitely not any gpll output because it keeps working fine
	 * with gpll disabled. Gating off matrix-osc26m breaks GMAC, so it must be its parent.
	 *
	 * The GMAC Gates are left enabled by the boot loader and are required for the GMAC to work.
	 *
	 * As for the 50 MHz comment: See rmiiphy-wclk.
	 *
	 * The gmac-ahb clock is a child of topcrm's ahb-pclk and is required for DMA transfers in
	 * and out of GMAC. Without it, GMAC still receives ethernet frames, but can't drain them
	 * to memory and will eventually set the RX FIFO overflow flag.
	 */
	[ZX_CLK_GMAC_WCLK]        = GATE("gmac-wclk", PARENT_FW("wclk-osc26m"), 0x110, 0, 0),
	[ZX_CLK_GMAC_PCLK]        = GATE("gmac-pclk", clk_main[0], 0x110, 1, 0),
	[ZX_CLK_GMAC_AHB]         = GATE("gmac-ahb", PARENT_FW("ahb"), 0x110, 2, 0),

	/*
	 * Is there an AXI bus gate? The symptom of cutting off the AXI mux selection in top+0x140
	 * is that matrixcrm becomes unreadable from m0 and A53 hangs. Inside matrix itself only 3
	 * bits fit that bill: 0x8c bits 5, 6, 7. It seems a bit self-defeating to have a clock
	 * gate that shuts off access to itself though. I expect a clock gate for the bus
	 * somewhere, and the mux exists, so exposing one AXI clock in the bindings is the correct
	 * thing to do. It also serves to tell the kernel to keep the mux's parent enabled.
	 *
	 * Register 0x8c has 12 settable bits (0xfff). Ultimately it doesn't matter much which
	 * do-not-remove bit we don't remove. Other bits in this register behave like gates (e.g
	 * bits 11:8 cut off USB temporarily), so I think we are looking in the right place.
	 *
	 * If an explanation for the remaining bits surfaces and they are further gates and/or
	 * resets, add them to the bindings.
	 */
	[ZX_CLK_AXI_WCLK]         = GATE("axi-wclk", PARENT_ID(ZX_CLK_AXI_MUX), 0x8c, 5,
					 CLK_IS_CRITICAL),

	[ZX_CLK_DMA_PCLK]         = GATE("dma-pclk", clk_main[0], 0x94, 3, 0),

	/*
	 * There are a lot more VOU related controls in these registers, but turning off the main
	 * clock seems to shut off the entire VOU MMIO range.
	 */
	[ZX_CLK_VOU_WCLK]         = GATE("vou-wclk", matrix_unk, 0x168, 0, 0),
	[ZX_CLK_VOU_PCLK]         = GATE("vou-pclk", clk_main[0], 0x168, 1, 0),

	/*
	 * LSP uplink clocks. The pclk is fairly obvious (disabling it shuts off the entire LSP
	 * register area). The wclk speeds were deduced by setting timers and qspi muxes to a
	 * specific speed and seeing which bit in matrix+0x7c needs to be enabled for the device
	 * to work.
	 *
	 * Due to the timers I am certain about the 26 MHz and 32 kHz clocks. I cannot directly
	 * observe the qspi mux frequency, so the clock rates depend on ZTE's qspi mux selection
	 * being correct.
	 *
	 * Two additional bits are specific to sound components - the mux for the LSP's TDM IP is
	 * in matrixcrm and gets passed down. I2S has a mux in LSP, which can select the dpll-d4
	 * clock.
	 */
	[ZX_CLK_TOLSP_MPLL_D5_WCLK]  = GATE("lsp-mpll-d5", PARENT_FW("mpll-d5"), 0x7c, 0, 0),
	[ZX_CLK_TOLSP_MPLL_D4_WCLK]  = GATE("lsp-mpll-d4", PARENT_ID(ZX_CLK_MATRIX_MPLL_D4),
					    0x7c, 1, 0),
	[ZX_CLK_TOLSP_MPLL_D6_WCLK]  = GATE("lsp-mpll-d6", PARENT_ID(ZX_CLK_MATRIX_MPLL_D6),
					    0x7c, 2, 0),
	[ZX_CLK_TOLSP_MPLL_D8_WCLK]  = GATE("lsp-mpll-d8", PARENT_ID(ZX_CLK_MATRIX_MPLL_D8),
					    0x7c, 3, 0),
	[ZX_CLK_TOLSP_MPLL_D12_WCLK] = GATE("lsp-mpll-d12", PARENT_ID(ZX_CLK_MATRIX_MPLL_D12),
					    0x7c, 4, 0),
	[ZX_CLK_TOLSP_OSC26M_WCLK]   = GATE("lsp-osc26m", PARENT_FW("wclk-osc26m"), 0x7c, 5, 0),
	[ZX_CLK_TOLSP_OSC32K_WCLK]   = GATE("lsp-osc32k", PARENT_FW("osc32k"), 0x7c, 6, 0),
	[ZX_CLK_TOLSP_PCLK]          = GATE("lsp-pclk", clk_main[0], 0x7c, 7, 0),
	[ZX_CLK_TOLSP_TDM_WCLK]      = GATE("lsp-tdm-wclk", PARENT_ID(ZX_CLK_TDM_MUX), 0x7c, 8, 0),
	[ZX_CLK_TOLSP_DPLL_D4_WCLK]  = GATE("lsp-dpll-d4", PARENT_ID(ZX_CLK_MATRIX_DPLL_D4),
					    0x7c, 9, 0),
};

static int zx297520v3_matrix_init(struct regmap *map)
{
	u32 axi_val;
	int res;

	/* Read the global AXI speed selection, insert it into our ballot and enable voting */
	res = regmap_read(map, 0x0, &axi_val);
	if (res)
		return res;

	axi_val &= 0x7;
	res = regmap_write(map, ZX297520V3_AXI_AP_VOTE, axi_val);
	if (res)
		return res;

	return regmap_write(map, 0x12c, 1);
}

static const unsigned int zx297520v3_matrix_exports[] = {
	[ZX297520V3_SYS_TIMER_WCLK] =		ZX_CLK_SYS_TIMER_WCLK,
	[ZX297520V3_CPU_WCLK] =			ZX_CLK_CPU_WCLK,
	[ZX297520V3_CPU_PCLK] =			ZX_CLK_CPU_PCLK,
	[ZX297520V3_ZSP_WCLK] =			ZX_CLK_ZSP_WCLK,
	[ZX297520V3_ZSP_PCLK] =			ZX_CLK_ZSP_PCLK,
	[ZX297520V3_DDR_CTRL_PCLK] =		ZX_CLK_DDR_CTRL_PCLK,
	[ZX297520V3_DDR_CTRL_WCLK] =		ZX_CLK_DDR_CTRL_WCLK,
	[ZX297520V3_EDCP_WCLK] =		ZX_CLK_EDCP_WCLK,
	[ZX297520V3_EDCP_PCLK] =		ZX_CLK_EDCP_PCLK,
	[ZX297520V3_SD0_WCLK] =			ZX_CLK_SD0_WCLK,
	[ZX297520V3_SD0_PCLK] =			ZX_CLK_SD0_PCLK,
	[ZX297520V3_SD0_CDET] =			ZX_CLK_SD0_CDET,
	[ZX297520V3_SD1_WCLK] =			ZX_CLK_SD1_WCLK,
	[ZX297520V3_SD1_PCLK] =			ZX_CLK_SD1_PCLK,
	[ZX297520V3_SD1_CDET] =			ZX_CLK_SD1_CDET,
	[ZX297520V3_NAND_WCLK] =		ZX_CLK_NAND_WCLK,
	[ZX297520V3_NAND_PCLK] =		ZX_CLK_NAND_PCLK,
	[ZX297520V3_DMA_PCLK] =			ZX_CLK_DMA_PCLK,
	[ZX297520V3_MBOX_PCLK] =		ZX_CLK_MBOX_PCLK,
	[ZX297520V3_PDCFG_WCLK] =		ZX_CLK_PDCFG_WCLK,
	[ZX297520V3_PDCFG_PCLK] =		ZX_CLK_PDCFG_PCLK,
	[ZX297520V3_RFFE_WCLK] =		ZX_CLK_RFFE_WCLK,
	[ZX297520V3_SSC_WCLK] =			ZX_CLK_SSC_WCLK,
	[ZX297520V3_SSC_PCLK] =			ZX_CLK_SSC_PCLK,
	[ZX297520V3_AXI_WCLK] =			ZX_CLK_AXI_WCLK,
	[ZX297520V3_GMAC_WCLK] =		ZX_CLK_GMAC_WCLK,
	[ZX297520V3_GMAC_PCLK] =		ZX_CLK_GMAC_PCLK,
	[ZX297520V3_GMAC_AHB] =			ZX_CLK_GMAC_AHB,
	[ZX297520V3_VOU_WCLK] =			ZX_CLK_VOU_WCLK,
	[ZX297520V3_VOU_PCLK] =			ZX_CLK_VOU_PCLK,
	[ZX297520V3_LSP_MPLL_D5_WCLK] =		ZX_CLK_TOLSP_MPLL_D5_WCLK,
	[ZX297520V3_LSP_MPLL_D4_WCLK] =		ZX_CLK_TOLSP_MPLL_D4_WCLK,
	[ZX297520V3_LSP_MPLL_D6_WCLK] =		ZX_CLK_TOLSP_MPLL_D6_WCLK,
	[ZX297520V3_LSP_MPLL_D8_WCLK] =		ZX_CLK_TOLSP_MPLL_D8_WCLK,
	[ZX297520V3_LSP_MPLL_D12_WCLK] =	ZX_CLK_TOLSP_MPLL_D12_WCLK,
	[ZX297520V3_LSP_OSC26M_WCLK] =		ZX_CLK_TOLSP_OSC26M_WCLK,
	[ZX297520V3_LSP_OSC32K_WCLK] =		ZX_CLK_TOLSP_OSC32K_WCLK,
	[ZX297520V3_LSP_PCLK] =			ZX_CLK_TOLSP_PCLK,
	[ZX297520V3_LSP_TDM_WCLK] =		ZX_CLK_TOLSP_TDM_WCLK,
	[ZX297520V3_LSP_DPLL_D4_WCLK] =		ZX_CLK_TOLSP_DPLL_D4_WCLK,
	[ZX297520V3_SRAM0_PCLK] =		ZX_CLK_SRAM0_PCLK,
	[ZX297520V3_GSM_CFG_PCLK] =		ZX_CLK_GSM_CFG_PCLK,
	[ZX297520V3_LTE_MODEM_WCLK] =		ZX_CLK_LTE_MODEM_WCLK,
	[ZX297520V3_LTE_MODEM_PCLK] =		ZX_CLK_LTE_MODEM_PCLK,
};

static const struct zx_clk_data zx297520v3_matrixclk_data = {
	.init = zx297520v3_matrix_init,
	.clocks = zx297520v3_matrix_clocks,
	.num_clocks = ARRAY_SIZE(zx297520v3_matrix_clocks),
	.exports = zx297520v3_matrix_exports,
	.num_exports = ARRAY_SIZE(zx297520v3_matrix_exports),
};

/*
 * LSP clock entries have a common pattern: Bit 0 for pclk, Bit 1 for wclk. Bit 4 (and sometimes
 * more) for wclk mux.
 *
 * Bit 8 and 9 are resets handled by the reset-zte-zx297520v3 driver.
 *
 * Bits 15:12 can be a divisor, but not all clocks have it. Some clocks have a divisor in 19:16.
 *
 * The ID given in this table is the first register in the device's MMIO space. ZTE's drivers
 * usually call this a version register, but it looks more like a device identifier.
 *
 * It looks like the registers map to devices like this:
 *
 * CRM Reg	function	div	dev offset(lsp + xxxx)	ID
 * 0x0: Read-only, probably device identifier			0x00752100
 * 0x4:		timer_l1	Y	0x1000			0x02020000
 * 0x8:		watchdog_l2	Y	0x2000			0x02020000
 * 0xc:		watchdog_l3	Y	0x3000			0x02020000
 * 0x10:	pwm		N	0x4000			0x01020000
 * 0x14:	i2s0		Yh	0x5000			0x01030000
 * 0x18:	always 0	-	-			-
 * 0x1c:	i2s1		Yh	0x6000			0x01030000
 * 0x20:	always 0	-	-			-
 * 0x24:	qspi		N	0x7000			0x01040000
 * 0x28:	uart1		N	0x8000			0x01060000
 * 0x2c:	i2c1		N	0x9000			0x01020000
 * 0x30:	spi0		Y	0xa000			0x01040000
 * 0x34:	timer_lb	Y	0xb000			0x02020000
 * 0x38:	timer_lc	Y	0xc000			0x02020000
 * 0x3c:	uart2		N	0xd000			0x01060000
 * 0x40:	watchdog_le	Y	0xe000			0x02020000
 * 0x44:	timer_lf	Y	0xf000			0x02020000
 * 0x48:	spi1		Y	0x10000			0x01040000
 * 0x4c:	timer_l11	Y	0x11000			0x02020000
 * 0x50:	tdm		Yh	0x12000			0x01040000
 *
 * Registers 0x58, 0x5c, 0x60, 0x64, 0x68 seem to contain more controls for i2s and tdm.
 *
 * The device at offset 0x4000 (clk reg 0x10) is a PWM device. The ID matches that of i2c, it has a
 * larger register set. ZTE's kernel does not operate it at all. The old upstream pwm-zx.c driver
 * provided enough instructions to blink the Wifi led and isolate the parent.
 */

enum lsp_clock_ids {
	ZX_CLK_TIMER_L1_MUX,
	ZX_CLK_WDT_L2_MUX,
	ZX_CLK_WDT_L3_MUX,
	ZX_CLK_I2S0_MUX,
	ZX_CLK_I2S1_MUX,
	ZX_CLK_QSPI_MUX,
	ZX_CLK_UART1_MUX,
	ZX_CLK_I2C1_MUX,
	ZX_CLK_SPI0_MUX,
	ZX_CLK_TIMER_LB_MUX,
	ZX_CLK_TIMER_LC_MUX,
	ZX_CLK_UART2_MUX,
	ZX_CLK_WDT_LE_MUX,
	ZX_CLK_TIMER_LF_MUX,
	ZX_CLK_SPI1_MUX,
	ZX_CLK_TIMER_L11_MUX,

	ZX_CLK_TIMER_L1_DIV,
	ZX_CLK_WDT_L2_DIV,
	ZX_CLK_WDT_L3_DIV,
	ZX_CLK_I2S0_DIV,
	ZX_CLK_I2S1_DIV,
	ZX_CLK_SPI0_DIV,
	ZX_CLK_TIMER_LB_DIV,
	ZX_CLK_TIMER_LC_DIV,
	ZX_CLK_WDT_LE_DIV,
	ZX_CLK_TIMER_LF_DIV,
	ZX_CLK_SPI1_DIV,
	ZX_CLK_TIMER_L11_DIV,
	ZX_CLK_TDM_DIV,

	ZX_CLK_TIMER_L1_PCLK,
	ZX_CLK_TIMER_L1_WCLK,
	ZX_CLK_WDT_L2_PCLK,
	ZX_CLK_WDT_L2_WCLK,
	ZX_CLK_WDT_L3_PCLK,
	ZX_CLK_WDT_L3_WCLK,
	ZX_CLK_PWM_PCLK,
	ZX_CLK_PWM_WCLK,
	ZX_CLK_I2S0_PCLK,
	ZX_CLK_I2S0_WCLK,
	ZX_CLK_I2S1_PCLK,
	ZX_CLK_I2S1_WCLK,
	ZX_CLK_QSPI_PCLK,
	ZX_CLK_QSPI_WCLK,
	ZX_CLK_UART1_PCLK,
	ZX_CLK_UART1_WCLK,
	ZX_CLK_I2C1_PCLK,
	ZX_CLK_I2C1_WCLK,
	ZX_CLK_SPI0_PCLK,
	ZX_CLK_SPI0_WCLK,
	ZX_CLK_TIMER_LB_PCLK,
	ZX_CLK_TIMER_LB_WCLK,
	ZX_CLK_TIMER_LC_PCLK,
	ZX_CLK_TIMER_LC_WCLK,
	ZX_CLK_UART2_PCLK,
	ZX_CLK_UART2_WCLK,
	ZX_CLK_WDT_LE_PCLK,
	ZX_CLK_WDT_LE_WCLK,
	ZX_CLK_TIMER_LF_PCLK,
	ZX_CLK_TIMER_LF_WCLK,
	ZX_CLK_SPI1_PCLK,
	ZX_CLK_SPI1_WCLK,
	ZX_CLK_TIMER_L11_PCLK,
	ZX_CLK_TIMER_L11_WCLK,
	ZX_CLK_TDM_PCLK,
	ZX_CLK_TDM_WCLK,
};

static const struct zx_parent_desc timer_lsp_sel[] = {
	PARENT_FW("osc32k"),
	PARENT_FW("osc26m"),
};

static const struct zx_parent_desc uart_lsp_sel[] = {
	PARENT_FW("osc26m"),
	PARENT_FW("mpll-d6"),
};

static const struct zx_parent_desc i2s_lsp_sel[] = {
	PARENT_FW("osc26m"),
	PARENT_FW("dpll-d4"),
	PARENT_FW("mpll-d6"),
	/* Unknown */
};

static const struct zx_parent_desc spi_lsp_sel[] = {
	PARENT_FW("osc26m"),
	PARENT_FW("mpll-d4"),
	PARENT_FW("mpll-d6"),
	/* Unknown */
};

static const struct zx_parent_desc qspi_lsp_sel[] = {
	PARENT_FW("osc26m"),
	PARENT_FW("mpll-d4"),
	PARENT_FW("mpll-d5"),
	PARENT_FW("mpll-d6"),
	PARENT_FW("mpll-d8"),
	PARENT_FW("mpll-d12"),
	PARENT_FW("osc26m"),
	PARENT_FW("osc26m"),
};

static const struct zx_clock zx297520v3_lsp_clocks[] = {
	[ZX_CLK_TIMER_L1_MUX]     = MUX("timer-l1-mux",   timer_lsp_sel,    0x04,  4, 1),
	[ZX_CLK_WDT_L2_MUX]       = MUX("wdt-l2-mux",     timer_lsp_sel,    0x08,  4, 1),
	[ZX_CLK_WDT_L3_MUX]       = MUX("wdt-l3-mux",     timer_lsp_sel,    0x0c,  4, 1),
	/* PWM: No mux bit can be set */
	[ZX_CLK_I2S0_MUX]         = MUX("i2s0-mux",       i2s_lsp_sel,      0x14,  4, 2),
	/* 0x18: Always 0 */
	[ZX_CLK_I2S1_MUX]         = MUX("i2s1-mux",       i2s_lsp_sel,      0x1c,  4, 2),
	/* 0x20: Always 0 */
	[ZX_CLK_QSPI_MUX]         = MUX("qspi-mux",       qspi_lsp_sel,     0x24,  4, 3),
	[ZX_CLK_UART1_MUX]        = MUX("uart1-mux",      uart_lsp_sel,     0x28,  4, 1),
	[ZX_CLK_I2C1_MUX]         = MUX("i2c1-mux",       uart_lsp_sel,     0x2c,  4, 1),
	[ZX_CLK_SPI0_MUX]         = MUX("spi0-mux",       spi_lsp_sel,      0x30,  4, 2),
	[ZX_CLK_TIMER_LB_MUX]     = MUX("timer-lb-mux",   timer_lsp_sel,    0x34,  4, 1),
	[ZX_CLK_TIMER_LC_MUX]     = MUX("timer-lc-mux",   timer_lsp_sel,    0x38,  4, 1),
	[ZX_CLK_UART2_MUX]        = MUX("uart2-mux",      uart_lsp_sel,     0x3c,  4, 1),
	[ZX_CLK_WDT_LE_MUX]       = MUX("wdt-le-mux",     timer_lsp_sel,    0x40,  4, 1),
	[ZX_CLK_TIMER_LF_MUX]     = MUX("timer-lf-mux",   timer_lsp_sel,    0x44,  4, 1),
	[ZX_CLK_SPI1_MUX]         = MUX("spi1-mux",       spi_lsp_sel,      0x48,  4, 2),
	[ZX_CLK_TIMER_L11_MUX]    = MUX("timer-l11-mux",  timer_lsp_sel,    0x4c,  4, 1),
	/* TDM: No mux in LSP. Instead, it is in matrix with a separate clk line to LSP */

	[ZX_CLK_TIMER_L1_DIV]     = DIV("timer-l1-div", PARENT_ID(ZX_CLK_TIMER_L1_MUX),
					0x04, 12, 4),
	[ZX_CLK_WDT_L2_DIV]       = DIV("wdt-l2-div", PARENT_ID(ZX_CLK_WDT_L2_MUX), 0x08, 12, 4),
	[ZX_CLK_WDT_L3_DIV]       = DIV("wdt-l3-div", PARENT_ID(ZX_CLK_WDT_L3_MUX), 0x0c, 12, 4),
	/* PWM: No div */
	[ZX_CLK_I2S0_DIV]         = DIV("i2s0-div", PARENT_ID(ZX_CLK_I2S0_MUX), 0x14, 16, 4),
	/* 0x18: Always 0 */
	[ZX_CLK_I2S1_DIV]         = DIV("i2s1-div", PARENT_ID(ZX_CLK_I2S1_MUX), 0x1c, 16, 4),
	/* 0x20: Always 0 */
	/* qspi, uart1, i2c1: No div */
	[ZX_CLK_SPI0_DIV]         = DIV("spi0-div", PARENT_ID(ZX_CLK_SPI0_MUX), 0x30, 12, 4),
	[ZX_CLK_TIMER_LB_DIV]     = DIV("timer-lb-div", PARENT_ID(ZX_CLK_TIMER_LB_MUX),
					0x34, 12, 4),
	[ZX_CLK_TIMER_LC_DIV]     = DIV("timer-lc-div", PARENT_ID(ZX_CLK_TIMER_LC_MUX),
					0x38, 12, 4),
	/* uart2: No div */
	[ZX_CLK_WDT_LE_DIV]       = DIV("wdt-le-div", PARENT_ID(ZX_CLK_WDT_LE_MUX), 0x40, 12, 4),
	[ZX_CLK_TIMER_LF_DIV]     = DIV("timer-lf-div", PARENT_ID(ZX_CLK_TIMER_LF_MUX),
					0x44, 12, 4),
	[ZX_CLK_SPI1_DIV]         = DIV("spi1-div", PARENT_ID(ZX_CLK_SPI1_MUX), 0x48, 12, 4),
	[ZX_CLK_TIMER_L11_DIV]    = DIV("timer-l11-div", PARENT_ID(ZX_CLK_TIMER_L11_MUX),
					0x4c, 12, 4),
	[ZX_CLK_TDM_DIV]        = DIV("tdm-div", PARENT_FW("tdm-wclk"), 0x50, 16, 4),

	[ZX_CLK_TIMER_L1_PCLK]  = GATE("timer-l1-pclk", PARENT_FW("pclk"), 0x04, 0, 0),
	[ZX_CLK_TIMER_L1_WCLK]  = GATE("timer-l1-wclk", PARENT_ID(ZX_CLK_TIMER_L1_DIV), 0x04, 1, 0),
	[ZX_CLK_WDT_L2_PCLK]    = GATE("wdt-l2-pclk", PARENT_FW("pclk"), 0x08, 0, 0),
	[ZX_CLK_WDT_L2_WCLK]    = GATE("wdt-l2-wclk", PARENT_ID(ZX_CLK_WDT_L2_DIV), 0x08, 1, 0),
	[ZX_CLK_WDT_L3_PCLK]    = GATE("wdt-l3-pclk", PARENT_FW("pclk"), 0x0c, 0, 0),
	[ZX_CLK_WDT_L3_WCLK]    = GATE("wdt-l3-wclk", PARENT_ID(ZX_CLK_WDT_L3_DIV), 0x0c, 1, 0),
	[ZX_CLK_PWM_PCLK]       = GATE("pwm-pclk", PARENT_FW("pclk"), 0x10, 0, 0),
	[ZX_CLK_PWM_WCLK]       = GATE("pwm-wclk", PARENT_FW("mpll-d6"), 0x10, 1, 0),
	[ZX_CLK_I2S0_PCLK]      = GATE("i2s0-pclk", PARENT_FW("pclk"), 0x14, 0, 0),
	[ZX_CLK_I2S0_WCLK]      = GATE("i2s0-wclk", PARENT_ID(ZX_CLK_I2S0_DIV), 0x14, 1, 0),
	/* 0x18: Always 0 */
	[ZX_CLK_I2S1_PCLK]      = GATE("i2s1-pclk", PARENT_FW("pclk"), 0x1c, 0, 0),
	[ZX_CLK_I2S1_WCLK]      = GATE("i2s1-wclk", PARENT_ID(ZX_CLK_I2S1_DIV), 0x1c, 1, 0),
	/* 0x20: Always 0 */
	[ZX_CLK_QSPI_PCLK]      = GATE("qspi-pclk", PARENT_FW("pclk"), 0x24, 0, 0),
	[ZX_CLK_QSPI_WCLK]      = GATE("qspi-wclk", PARENT_ID(ZX_CLK_QSPI_MUX), 0x24, 1, 0),
	[ZX_CLK_UART1_PCLK]     = GATE("uart1-pclk", PARENT_FW("pclk"), 0x28, 0, 0),
	[ZX_CLK_UART1_WCLK]     = GATE("uart1-wclk", PARENT_ID(ZX_CLK_UART1_MUX), 0x28, 1, 0),
	[ZX_CLK_I2C1_PCLK]      = GATE("i2c1-pclk", PARENT_FW("pclk"), 0x2c, 0, 0),
	[ZX_CLK_I2C1_WCLK]      = GATE("i2c1-wclk", PARENT_ID(ZX_CLK_I2C1_MUX), 0x2c, 1, 0),
	[ZX_CLK_SPI0_PCLK]      = GATE("spi0-pclk", PARENT_FW("pclk"), 0x30, 0, 0),
	[ZX_CLK_SPI0_WCLK]      = GATE("spi0-wclk", PARENT_ID(ZX_CLK_SPI0_DIV), 0x30, 1, 0),
	[ZX_CLK_TIMER_LB_PCLK]  = GATE("timer-lb-pclk", PARENT_FW("pclk"), 0x34, 0, 0),
	[ZX_CLK_TIMER_LB_WCLK]  = GATE("timer-lb-wclk", PARENT_ID(ZX_CLK_TIMER_LB_DIV), 0x34, 1, 0),
	[ZX_CLK_TIMER_LC_PCLK]  = GATE("timer-lc-pclk", PARENT_FW("pclk"), 0x38, 0, 0),
	[ZX_CLK_TIMER_LC_WCLK]  = GATE("timer-lc-wclk", PARENT_ID(ZX_CLK_TIMER_LC_DIV), 0x38, 1, 0),
	[ZX_CLK_UART2_PCLK]     = GATE("uart2-pclk", PARENT_FW("pclk"), 0x3c, 0, 0),
	[ZX_CLK_UART2_WCLK]     = GATE("uart2-wclk", PARENT_ID(ZX_CLK_UART2_MUX), 0x3c, 1, 0),
	[ZX_CLK_WDT_LE_PCLK]    = GATE("wdt-le-pclk", PARENT_FW("pclk"), 0x40, 0, 0),
	[ZX_CLK_WDT_LE_WCLK]    = GATE("wdt-le-wclk", PARENT_ID(ZX_CLK_WDT_LE_DIV), 0x40, 1, 0),
	[ZX_CLK_TIMER_LF_PCLK]  = GATE("timer-lf-pclk", PARENT_FW("pclk"), 0x44, 0, 0),
	[ZX_CLK_TIMER_LF_WCLK]  = GATE("timer-lf-wclk", PARENT_ID(ZX_CLK_TIMER_LF_DIV), 0x44, 1, 0),
	[ZX_CLK_SPI1_PCLK]      = GATE("spi1-pclk", PARENT_FW("pclk"), 0x48, 0, 0),
	[ZX_CLK_SPI1_WCLK]      = GATE("spi1-wclk", PARENT_ID(ZX_CLK_SPI1_DIV), 0x48, 1, 0),
	[ZX_CLK_TIMER_L11_PCLK] = GATE("timer-l11-pclk", PARENT_FW("pclk"), 0x4c, 0, 0),
	[ZX_CLK_TIMER_L11_WCLK] = GATE("timer-l11-wclk", PARENT_ID(ZX_CLK_TIMER_L11_DIV),
				       0x4c, 1, 0),
	[ZX_CLK_TDM_PCLK]       = GATE("tdm-pclk", PARENT_FW("pclk"), 0x50, 0, 0),
	[ZX_CLK_TDM_WCLK]       = GATE("tdm-wclk", PARENT_ID(ZX_CLK_TDM_DIV), 0x50, 1, 0),
};

static const unsigned int zx297520v3_lsp_exports[] = {
	[ZX297520V3_TIMER_L1_WCLK] =	ZX_CLK_TIMER_L1_WCLK,
	[ZX297520V3_TIMER_L1_PCLK] =	ZX_CLK_TIMER_L1_PCLK,
	[ZX297520V3_WDT_L2_WCLK] =	ZX_CLK_WDT_L2_WCLK,
	[ZX297520V3_WDT_L2_PCLK] =	ZX_CLK_WDT_L2_PCLK,
	[ZX297520V3_WDT_L3_WCLK] =	ZX_CLK_WDT_L3_WCLK,
	[ZX297520V3_WDT_L3_PCLK] =	ZX_CLK_WDT_L3_PCLK,
	[ZX297520V3_PWM_WCLK] =		ZX_CLK_PWM_WCLK,
	[ZX297520V3_PWM_PCLK] =		ZX_CLK_PWM_PCLK,
	[ZX297520V3_I2S0_WCLK] =	ZX_CLK_I2S0_WCLK,
	[ZX297520V3_I2S0_PCLK] =	ZX_CLK_I2S0_PCLK,
	[ZX297520V3_I2S1_WCLK] =	ZX_CLK_I2S1_WCLK,
	[ZX297520V3_I2S1_PCLK] =	ZX_CLK_I2S1_PCLK,
	[ZX297520V3_QSPI_WCLK] =	ZX_CLK_QSPI_WCLK,
	[ZX297520V3_QSPI_PCLK] =	ZX_CLK_QSPI_PCLK,
	[ZX297520V3_UART1_WCLK] =	ZX_CLK_UART1_WCLK,
	[ZX297520V3_UART1_PCLK] =	ZX_CLK_UART1_PCLK,
	[ZX297520V3_I2C1_WCLK] =	ZX_CLK_I2C1_WCLK,
	[ZX297520V3_I2C1_PCLK] =	ZX_CLK_I2C1_PCLK,
	[ZX297520V3_SPI0_WCLK] =	ZX_CLK_SPI0_WCLK,
	[ZX297520V3_SPI0_PCLK] =	ZX_CLK_SPI0_PCLK,
	[ZX297520V3_TIMER_LB_WCLK] =	ZX_CLK_TIMER_LB_WCLK,
	[ZX297520V3_TIMER_LB_PCLK] =	ZX_CLK_TIMER_LB_PCLK,
	[ZX297520V3_TIMER_LC_WCLK] =	ZX_CLK_TIMER_LC_WCLK,
	[ZX297520V3_TIMER_LC_PCLK] =	ZX_CLK_TIMER_LC_PCLK,
	[ZX297520V3_UART2_WCLK] =	ZX_CLK_UART2_WCLK,
	[ZX297520V3_UART2_PCLK] =	ZX_CLK_UART2_PCLK,
	[ZX297520V3_WDT_LE_WCLK] =	ZX_CLK_WDT_LE_WCLK,
	[ZX297520V3_WDT_LE_PCLK] =	ZX_CLK_WDT_LE_PCLK,
	[ZX297520V3_TIMER_LF_WCLK] =	ZX_CLK_TIMER_LF_WCLK,
	[ZX297520V3_TIMER_LF_PCLK] =	ZX_CLK_TIMER_LF_PCLK,
	[ZX297520V3_SPI1_WCLK] =	ZX_CLK_SPI1_WCLK,
	[ZX297520V3_SPI1_PCLK] =	ZX_CLK_SPI1_PCLK,
	[ZX297520V3_TIMER_L11_WCLK] =	ZX_CLK_TIMER_L11_WCLK,
	[ZX297520V3_TIMER_L11_PCLK] =	ZX_CLK_TIMER_L11_PCLK,
	[ZX297520V3_TDM_WCLK] =		ZX_CLK_TDM_WCLK,
	[ZX297520V3_TDM_PCLK] =		ZX_CLK_TDM_PCLK,
};

static const struct zx_clk_data zx297520v3_lspclk_data = {
	.clocks = zx297520v3_lsp_clocks,
	.num_clocks = ARRAY_SIZE(zx297520v3_lsp_clocks),
	.exports = zx297520v3_lsp_exports,
	.num_exports = ARRAY_SIZE(zx297520v3_lsp_exports),
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
	{
		.name = "zx297520v3-matrixclk",
		.driver_data = (kernel_ulong_t)&zx297520v3_matrixclk_data,
	},
	{
		.name = "zx297520v3-lspclk",
		.driver_data = (kernel_ulong_t)&zx297520v3_lspclk_data,
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
