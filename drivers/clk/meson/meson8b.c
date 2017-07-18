/*
 * AmLogic S802 (Meson8) / S805 (Meson8b) / S812 (Meson8m2) Clock Controller
 * Driver
 *
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * Copyright (c) 2016 BayLibre, Inc.
 * Michael Turquette <mturquette@baylibre.com>
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
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/init.h>

#include "clkc.h"
#include "meson8b.h"

static DEFINE_SPINLOCK(clk_lock);

static const struct pll_rate_table sys_pll_rate_table[] = {
	PLL_RATE(312000000, 52, 1, 2),
	PLL_RATE(336000000, 56, 1, 2),
	PLL_RATE(360000000, 60, 1, 2),
	PLL_RATE(384000000, 64, 1, 2),
	PLL_RATE(408000000, 68, 1, 2),
	PLL_RATE(432000000, 72, 1, 2),
	PLL_RATE(456000000, 76, 1, 2),
	PLL_RATE(480000000, 80, 1, 2),
	PLL_RATE(504000000, 84, 1, 2),
	PLL_RATE(528000000, 88, 1, 2),
	PLL_RATE(552000000, 92, 1, 2),
	PLL_RATE(576000000, 96, 1, 2),
	PLL_RATE(600000000, 50, 1, 1),
	PLL_RATE(624000000, 52, 1, 1),
	PLL_RATE(648000000, 54, 1, 1),
	PLL_RATE(672000000, 56, 1, 1),
	PLL_RATE(696000000, 58, 1, 1),
	PLL_RATE(720000000, 60, 1, 1),
	PLL_RATE(744000000, 62, 1, 1),
	PLL_RATE(768000000, 64, 1, 1),
	PLL_RATE(792000000, 66, 1, 1),
	PLL_RATE(816000000, 68, 1, 1),
	PLL_RATE(840000000, 70, 1, 1),
	PLL_RATE(864000000, 72, 1, 1),
	PLL_RATE(888000000, 74, 1, 1),
	PLL_RATE(912000000, 76, 1, 1),
	PLL_RATE(936000000, 78, 1, 1),
	PLL_RATE(960000000, 80, 1, 1),
	PLL_RATE(984000000, 82, 1, 1),
	PLL_RATE(1008000000, 84, 1, 1),
	PLL_RATE(1032000000, 86, 1, 1),
	PLL_RATE(1056000000, 88, 1, 1),
	PLL_RATE(1080000000, 90, 1, 1),
	PLL_RATE(1104000000, 92, 1, 1),
	PLL_RATE(1128000000, 94, 1, 1),
	PLL_RATE(1152000000, 96, 1, 1),
	PLL_RATE(1176000000, 98, 1, 1),
	PLL_RATE(1200000000, 50, 1, 0),
	PLL_RATE(1224000000, 51, 1, 0),
	PLL_RATE(1248000000, 52, 1, 0),
	PLL_RATE(1272000000, 53, 1, 0),
	PLL_RATE(1296000000, 54, 1, 0),
	PLL_RATE(1320000000, 55, 1, 0),
	PLL_RATE(1344000000, 56, 1, 0),
	PLL_RATE(1368000000, 57, 1, 0),
	PLL_RATE(1392000000, 58, 1, 0),
	PLL_RATE(1416000000, 59, 1, 0),
	PLL_RATE(1440000000, 60, 1, 0),
	PLL_RATE(1464000000, 61, 1, 0),
	PLL_RATE(1488000000, 62, 1, 0),
	PLL_RATE(1512000000, 63, 1, 0),
	PLL_RATE(1536000000, 64, 1, 0),
	{ /* sentinel */ },
};

static const struct clk_div_table cpu_div_table[] = {
	{ .val = 1, .div = 1 },
	{ .val = 2, .div = 2 },
	{ .val = 3, .div = 3 },
	{ .val = 2, .div = 4 },
	{ .val = 3, .div = 6 },
	{ .val = 4, .div = 8 },
	{ .val = 5, .div = 10 },
	{ .val = 6, .div = 12 },
	{ .val = 7, .div = 14 },
	{ .val = 8, .div = 16 },
	{ /* sentinel */ },
};

static struct clk_fixed_rate meson8b_xtal = {
	.fixed_rate = 24000000,
	.hw.init = &(struct clk_init_data){
		.name = "xtal",
		.num_parents = 0,
		.ops = &clk_fixed_rate_ops,
	},
};

static struct meson_clk_pll meson8b_fixed_pll = {
	.m = {
		.reg_off = HHI_MPLL_CNTL,
		.shift   = 0,
		.width   = 9,
	},
	.n = {
		.reg_off = HHI_MPLL_CNTL,
		.shift   = 9,
		.width   = 5,
	},
	.od = {
		.reg_off = HHI_MPLL_CNTL,
		.shift   = 16,
		.width   = 2,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "fixed_pll",
		.ops = &meson_clk_pll_ro_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct meson_clk_pll meson8b_vid_pll = {
	.m = {
		.reg_off = HHI_VID_PLL_CNTL,
		.shift   = 0,
		.width   = 9,
	},
	.n = {
		.reg_off = HHI_VID_PLL_CNTL,
		.shift   = 9,
		.width   = 5,
	},
	.od = {
		.reg_off = HHI_VID_PLL_CNTL,
		.shift   = 16,
		.width   = 2,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "vid_pll",
		.ops = &meson_clk_pll_ro_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct meson_clk_pll meson8b_sys_pll = {
	.m = {
		.reg_off = HHI_SYS_PLL_CNTL,
		.shift   = 0,
		.width   = 9,
	},
	.n = {
		.reg_off = HHI_SYS_PLL_CNTL,
		.shift   = 9,
		.width   = 5,
	},
	.od = {
		.reg_off = HHI_SYS_PLL_CNTL,
		.shift   = 16,
		.width   = 2,
	},
	.rate_table = sys_pll_rate_table,
	.rate_count = ARRAY_SIZE(sys_pll_rate_table),
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "sys_pll",
		.ops = &meson_clk_pll_ops,
		.parent_names = (const char *[]){ "xtal" },
		.num_parents = 1,
		.flags = CLK_GET_RATE_NOCACHE,
	},
};

static struct clk_fixed_factor meson8b_fclk_div2 = {
	.mult = 1,
	.div = 2,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div2",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_fclk_div3 = {
	.mult = 1,
	.div = 3,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div3",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_fclk_div4 = {
	.mult = 1,
	.div = 4,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div4",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_fclk_div5 = {
	.mult = 1,
	.div = 5,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div5",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct clk_fixed_factor meson8b_fclk_div7 = {
	.mult = 1,
	.div = 7,
	.hw.init = &(struct clk_init_data){
		.name = "fclk_div7",
		.ops = &clk_fixed_factor_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct meson_clk_mpll meson8b_mpll0 = {
	.sdm = {
		.reg_off = HHI_MPLL_CNTL7,
		.shift   = 0,
		.width   = 14,
	},
	.sdm_en = {
		.reg_off = HHI_MPLL_CNTL7,
		.shift   = 15,
		.width   = 1,
	},
	.n2 = {
		.reg_off = HHI_MPLL_CNTL7,
		.shift   = 16,
		.width   = 9,
	},
	.en = {
		.reg_off = HHI_MPLL_CNTL7,
		.shift   = 14,
		.width   = 1,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mpll0",
		.ops = &meson_clk_mpll_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct meson_clk_mpll meson8b_mpll1 = {
	.sdm = {
		.reg_off = HHI_MPLL_CNTL8,
		.shift   = 0,
		.width   = 14,
	},
	.sdm_en = {
		.reg_off = HHI_MPLL_CNTL8,
		.shift   = 15,
		.width   = 1,
	},
	.n2 = {
		.reg_off = HHI_MPLL_CNTL8,
		.shift   = 16,
		.width   = 9,
	},
	.en = {
		.reg_off = HHI_MPLL_CNTL8,
		.shift   = 14,
		.width   = 1,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mpll1",
		.ops = &meson_clk_mpll_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

static struct meson_clk_mpll meson8b_mpll2 = {
	.sdm = {
		.reg_off = HHI_MPLL_CNTL9,
		.shift   = 0,
		.width   = 14,
	},
	.sdm_en = {
		.reg_off = HHI_MPLL_CNTL9,
		.shift   = 15,
		.width   = 1,
	},
	.n2 = {
		.reg_off = HHI_MPLL_CNTL9,
		.shift   = 16,
		.width   = 9,
	},
	.en = {
		.reg_off = HHI_MPLL_CNTL9,
		.shift   = 14,
		.width   = 1,
	},
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mpll2",
		.ops = &meson_clk_mpll_ops,
		.parent_names = (const char *[]){ "fixed_pll" },
		.num_parents = 1,
	},
};

/*
 * FIXME cpu clocks and the legacy composite clocks (e.g. clk81) are both PLL
 * post-dividers and should be modeled with their respective PLLs via the
 * forthcoming coordinated clock rates feature
 */
static struct meson_clk_cpu meson8b_cpu_clk = {
	.reg_off = HHI_SYS_CPU_CLK_CNTL1,
	.div_table = cpu_div_table,
	.clk_nb.notifier_call = meson_clk_cpu_notifier_cb,
	.hw.init = &(struct clk_init_data){
		.name = "cpu_clk",
		.ops = &meson_clk_cpu_ops,
		.parent_names = (const char *[]){ "sys_pll" },
		.num_parents = 1,
	},
};

static u32 mux_table_clk81[]	= { 6, 5, 7 };

struct clk_mux meson8b_mpeg_clk_sel = {
	.reg = (void *)HHI_MPEG_CLK_CNTL,
	.mask = 0x7,
	.shift = 12,
	.flags = CLK_MUX_READ_ONLY,
	.table = mux_table_clk81,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mpeg_clk_sel",
		.ops = &clk_mux_ro_ops,
		/*
		 * FIXME bits 14:12 selects from 8 possible parents:
		 * xtal, 1'b0 (wtf), fclk_div7, mpll_clkout1, mpll_clkout2,
		 * fclk_div4, fclk_div3, fclk_div5
		 */
		.parent_names = (const char *[]){ "fclk_div3", "fclk_div4",
			"fclk_div5" },
		.num_parents = 3,
		.flags = (CLK_SET_RATE_NO_REPARENT | CLK_IGNORE_UNUSED),
	},
};

struct clk_divider meson8b_mpeg_clk_div = {
	.reg = (void *)HHI_MPEG_CLK_CNTL,
	.shift = 0,
	.width = 7,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "mpeg_clk_div",
		.ops = &clk_divider_ops,
		.parent_names = (const char *[]){ "mpeg_clk_sel" },
		.num_parents = 1,
		.flags = (CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED),
	},
};

struct clk_gate meson8b_clk81 = {
	.reg = (void *)HHI_MPEG_CLK_CNTL,
	.bit_idx = 7,
	.lock = &clk_lock,
	.hw.init = &(struct clk_init_data){
		.name = "clk81",
		.ops = &clk_gate_ops,
		.parent_names = (const char *[]){ "mpeg_clk_div" },
		.num_parents = 1,
		.flags = (CLK_SET_RATE_PARENT | CLK_IS_CRITICAL),
	},
};

/* Everything Else (EE) domain gates */

static MESON_GATE(meson8b_ddr, HHI_GCLK_MPEG0, 0);
static MESON_GATE(meson8b_dos, HHI_GCLK_MPEG0, 1);
static MESON_GATE(meson8b_isa, HHI_GCLK_MPEG0, 5);
static MESON_GATE(meson8b_pl301, HHI_GCLK_MPEG0, 6);
static MESON_GATE(meson8b_periphs, HHI_GCLK_MPEG0, 7);
static MESON_GATE(meson8b_spicc, HHI_GCLK_MPEG0, 8);
static MESON_GATE(meson8b_i2c, HHI_GCLK_MPEG0, 9);
static MESON_GATE(meson8b_sar_adc, HHI_GCLK_MPEG0, 10);
static MESON_GATE(meson8b_smart_card, HHI_GCLK_MPEG0, 11);
static MESON_GATE(meson8b_rng0, HHI_GCLK_MPEG0, 12);
static MESON_GATE(meson8b_uart0, HHI_GCLK_MPEG0, 13);
static MESON_GATE(meson8b_sdhc, HHI_GCLK_MPEG0, 14);
static MESON_GATE(meson8b_stream, HHI_GCLK_MPEG0, 15);
static MESON_GATE(meson8b_async_fifo, HHI_GCLK_MPEG0, 16);
static MESON_GATE(meson8b_sdio, HHI_GCLK_MPEG0, 17);
static MESON_GATE(meson8b_abuf, HHI_GCLK_MPEG0, 18);
static MESON_GATE(meson8b_hiu_iface, HHI_GCLK_MPEG0, 19);
static MESON_GATE(meson8b_assist_misc, HHI_GCLK_MPEG0, 23);
static MESON_GATE(meson8b_spi, HHI_GCLK_MPEG0, 30);

static MESON_GATE(meson8b_i2s_spdif, HHI_GCLK_MPEG1, 2);
static MESON_GATE(meson8b_eth, HHI_GCLK_MPEG1, 3);
static MESON_GATE(meson8b_demux, HHI_GCLK_MPEG1, 4);
static MESON_GATE(meson8b_aiu_glue, HHI_GCLK_MPEG1, 6);
static MESON_GATE(meson8b_iec958, HHI_GCLK_MPEG1, 7);
static MESON_GATE(meson8b_i2s_out, HHI_GCLK_MPEG1, 8);
static MESON_GATE(meson8b_amclk, HHI_GCLK_MPEG1, 9);
static MESON_GATE(meson8b_aififo2, HHI_GCLK_MPEG1, 10);
static MESON_GATE(meson8b_mixer, HHI_GCLK_MPEG1, 11);
static MESON_GATE(meson8b_mixer_iface, HHI_GCLK_MPEG1, 12);
static MESON_GATE(meson8b_adc, HHI_GCLK_MPEG1, 13);
static MESON_GATE(meson8b_blkmv, HHI_GCLK_MPEG1, 14);
static MESON_GATE(meson8b_aiu, HHI_GCLK_MPEG1, 15);
static MESON_GATE(meson8b_uart1, HHI_GCLK_MPEG1, 16);
static MESON_GATE(meson8b_g2d, HHI_GCLK_MPEG1, 20);
static MESON_GATE(meson8b_usb0, HHI_GCLK_MPEG1, 21);
static MESON_GATE(meson8b_usb1, HHI_GCLK_MPEG1, 22);
static MESON_GATE(meson8b_reset, HHI_GCLK_MPEG1, 23);
static MESON_GATE(meson8b_nand, HHI_GCLK_MPEG1, 24);
static MESON_GATE(meson8b_dos_parser, HHI_GCLK_MPEG1, 25);
static MESON_GATE(meson8b_usb, HHI_GCLK_MPEG1, 26);
static MESON_GATE(meson8b_vdin1, HHI_GCLK_MPEG1, 28);
static MESON_GATE(meson8b_ahb_arb0, HHI_GCLK_MPEG1, 29);
static MESON_GATE(meson8b_efuse, HHI_GCLK_MPEG1, 30);
static MESON_GATE(meson8b_boot_rom, HHI_GCLK_MPEG1, 31);

static MESON_GATE(meson8b_ahb_data_bus, HHI_GCLK_MPEG2, 1);
static MESON_GATE(meson8b_ahb_ctrl_bus, HHI_GCLK_MPEG2, 2);
static MESON_GATE(meson8b_hdmi_intr_sync, HHI_GCLK_MPEG2, 3);
static MESON_GATE(meson8b_hdmi_pclk, HHI_GCLK_MPEG2, 4);
static MESON_GATE(meson8b_usb1_ddr_bridge, HHI_GCLK_MPEG2, 8);
static MESON_GATE(meson8b_usb0_ddr_bridge, HHI_GCLK_MPEG2, 9);
static MESON_GATE(meson8b_mmc_pclk, HHI_GCLK_MPEG2, 11);
static MESON_GATE(meson8b_dvin, HHI_GCLK_MPEG2, 12);
static MESON_GATE(meson8b_uart2, HHI_GCLK_MPEG2, 15);
static MESON_GATE(meson8b_sana, HHI_GCLK_MPEG2, 22);
static MESON_GATE(meson8b_vpu_intr, HHI_GCLK_MPEG2, 25);
static MESON_GATE(meson8b_sec_ahb_ahb3_bridge, HHI_GCLK_MPEG2, 26);
static MESON_GATE(meson8b_clk81_a9, HHI_GCLK_MPEG2, 29);

static MESON_GATE(meson8b_vclk2_venci0, HHI_GCLK_OTHER, 1);
static MESON_GATE(meson8b_vclk2_venci1, HHI_GCLK_OTHER, 2);
static MESON_GATE(meson8b_vclk2_vencp0, HHI_GCLK_OTHER, 3);
static MESON_GATE(meson8b_vclk2_vencp1, HHI_GCLK_OTHER, 4);
static MESON_GATE(meson8b_gclk_venci_int, HHI_GCLK_OTHER, 8);
static MESON_GATE(meson8b_gclk_vencp_int, HHI_GCLK_OTHER, 9);
static MESON_GATE(meson8b_dac_clk, HHI_GCLK_OTHER, 10);
static MESON_GATE(meson8b_aoclk_gate, HHI_GCLK_OTHER, 14);
static MESON_GATE(meson8b_iec958_gate, HHI_GCLK_OTHER, 16);
static MESON_GATE(meson8b_enc480p, HHI_GCLK_OTHER, 20);
static MESON_GATE(meson8b_rng1, HHI_GCLK_OTHER, 21);
static MESON_GATE(meson8b_gclk_vencl_int, HHI_GCLK_OTHER, 22);
static MESON_GATE(meson8b_vclk2_venclmcc, HHI_GCLK_OTHER, 24);
static MESON_GATE(meson8b_vclk2_vencl, HHI_GCLK_OTHER, 25);
static MESON_GATE(meson8b_vclk2_other, HHI_GCLK_OTHER, 26);
static MESON_GATE(meson8b_edp, HHI_GCLK_OTHER, 31);

/* Always On (AO) domain gates */

static MESON_GATE(meson8b_ao_media_cpu, HHI_GCLK_AO, 0);
static MESON_GATE(meson8b_ao_ahb_sram, HHI_GCLK_AO, 1);
static MESON_GATE(meson8b_ao_ahb_bus, HHI_GCLK_AO, 2);
static MESON_GATE(meson8b_ao_iface, HHI_GCLK_AO, 3);

static struct clk_hw_onecell_data meson8b_hw_onecell_data = {
	.hws = {
		[CLKID_XTAL] = &meson8b_xtal.hw,
		[CLKID_PLL_FIXED] = &meson8b_fixed_pll.hw,
		[CLKID_PLL_VID] = &meson8b_vid_pll.hw,
		[CLKID_PLL_SYS] = &meson8b_sys_pll.hw,
		[CLKID_FCLK_DIV2] = &meson8b_fclk_div2.hw,
		[CLKID_FCLK_DIV3] = &meson8b_fclk_div3.hw,
		[CLKID_FCLK_DIV4] = &meson8b_fclk_div4.hw,
		[CLKID_FCLK_DIV5] = &meson8b_fclk_div5.hw,
		[CLKID_FCLK_DIV7] = &meson8b_fclk_div7.hw,
		[CLKID_CPUCLK] = &meson8b_cpu_clk.hw,
		[CLKID_MPEG_SEL] = &meson8b_mpeg_clk_sel.hw,
		[CLKID_MPEG_DIV] = &meson8b_mpeg_clk_div.hw,
		[CLKID_CLK81] = &meson8b_clk81.hw,
		[CLKID_DDR]		    = &meson8b_ddr.hw,
		[CLKID_DOS]		    = &meson8b_dos.hw,
		[CLKID_ISA]		    = &meson8b_isa.hw,
		[CLKID_PL301]		    = &meson8b_pl301.hw,
		[CLKID_PERIPHS]		    = &meson8b_periphs.hw,
		[CLKID_SPICC]		    = &meson8b_spicc.hw,
		[CLKID_I2C]		    = &meson8b_i2c.hw,
		[CLKID_SAR_ADC]		    = &meson8b_sar_adc.hw,
		[CLKID_SMART_CARD]	    = &meson8b_smart_card.hw,
		[CLKID_RNG0]		    = &meson8b_rng0.hw,
		[CLKID_UART0]		    = &meson8b_uart0.hw,
		[CLKID_SDHC]		    = &meson8b_sdhc.hw,
		[CLKID_STREAM]		    = &meson8b_stream.hw,
		[CLKID_ASYNC_FIFO]	    = &meson8b_async_fifo.hw,
		[CLKID_SDIO]		    = &meson8b_sdio.hw,
		[CLKID_ABUF]		    = &meson8b_abuf.hw,
		[CLKID_HIU_IFACE]	    = &meson8b_hiu_iface.hw,
		[CLKID_ASSIST_MISC]	    = &meson8b_assist_misc.hw,
		[CLKID_SPI]		    = &meson8b_spi.hw,
		[CLKID_I2S_SPDIF]	    = &meson8b_i2s_spdif.hw,
		[CLKID_ETH]		    = &meson8b_eth.hw,
		[CLKID_DEMUX]		    = &meson8b_demux.hw,
		[CLKID_AIU_GLUE]	    = &meson8b_aiu_glue.hw,
		[CLKID_IEC958]		    = &meson8b_iec958.hw,
		[CLKID_I2S_OUT]		    = &meson8b_i2s_out.hw,
		[CLKID_AMCLK]		    = &meson8b_amclk.hw,
		[CLKID_AIFIFO2]		    = &meson8b_aififo2.hw,
		[CLKID_MIXER]		    = &meson8b_mixer.hw,
		[CLKID_MIXER_IFACE]	    = &meson8b_mixer_iface.hw,
		[CLKID_ADC]		    = &meson8b_adc.hw,
		[CLKID_BLKMV]		    = &meson8b_blkmv.hw,
		[CLKID_AIU]		    = &meson8b_aiu.hw,
		[CLKID_UART1]		    = &meson8b_uart1.hw,
		[CLKID_G2D]		    = &meson8b_g2d.hw,
		[CLKID_USB0]		    = &meson8b_usb0.hw,
		[CLKID_USB1]		    = &meson8b_usb1.hw,
		[CLKID_RESET]		    = &meson8b_reset.hw,
		[CLKID_NAND]		    = &meson8b_nand.hw,
		[CLKID_DOS_PARSER]	    = &meson8b_dos_parser.hw,
		[CLKID_USB]		    = &meson8b_usb.hw,
		[CLKID_VDIN1]		    = &meson8b_vdin1.hw,
		[CLKID_AHB_ARB0]	    = &meson8b_ahb_arb0.hw,
		[CLKID_EFUSE]		    = &meson8b_efuse.hw,
		[CLKID_BOOT_ROM]	    = &meson8b_boot_rom.hw,
		[CLKID_AHB_DATA_BUS]	    = &meson8b_ahb_data_bus.hw,
		[CLKID_AHB_CTRL_BUS]	    = &meson8b_ahb_ctrl_bus.hw,
		[CLKID_HDMI_INTR_SYNC]	    = &meson8b_hdmi_intr_sync.hw,
		[CLKID_HDMI_PCLK]	    = &meson8b_hdmi_pclk.hw,
		[CLKID_USB1_DDR_BRIDGE]	    = &meson8b_usb1_ddr_bridge.hw,
		[CLKID_USB0_DDR_BRIDGE]	    = &meson8b_usb0_ddr_bridge.hw,
		[CLKID_MMC_PCLK]	    = &meson8b_mmc_pclk.hw,
		[CLKID_DVIN]		    = &meson8b_dvin.hw,
		[CLKID_UART2]		    = &meson8b_uart2.hw,
		[CLKID_SANA]		    = &meson8b_sana.hw,
		[CLKID_VPU_INTR]	    = &meson8b_vpu_intr.hw,
		[CLKID_SEC_AHB_AHB3_BRIDGE] = &meson8b_sec_ahb_ahb3_bridge.hw,
		[CLKID_CLK81_A9]	    = &meson8b_clk81_a9.hw,
		[CLKID_VCLK2_VENCI0]	    = &meson8b_vclk2_venci0.hw,
		[CLKID_VCLK2_VENCI1]	    = &meson8b_vclk2_venci1.hw,
		[CLKID_VCLK2_VENCP0]	    = &meson8b_vclk2_vencp0.hw,
		[CLKID_VCLK2_VENCP1]	    = &meson8b_vclk2_vencp1.hw,
		[CLKID_GCLK_VENCI_INT]	    = &meson8b_gclk_venci_int.hw,
		[CLKID_GCLK_VENCP_INT]	    = &meson8b_gclk_vencp_int.hw,
		[CLKID_DAC_CLK]		    = &meson8b_dac_clk.hw,
		[CLKID_AOCLK_GATE]	    = &meson8b_aoclk_gate.hw,
		[CLKID_IEC958_GATE]	    = &meson8b_iec958_gate.hw,
		[CLKID_ENC480P]		    = &meson8b_enc480p.hw,
		[CLKID_RNG1]		    = &meson8b_rng1.hw,
		[CLKID_GCLK_VENCL_INT]	    = &meson8b_gclk_vencl_int.hw,
		[CLKID_VCLK2_VENCLMCC]	    = &meson8b_vclk2_venclmcc.hw,
		[CLKID_VCLK2_VENCL]	    = &meson8b_vclk2_vencl.hw,
		[CLKID_VCLK2_OTHER]	    = &meson8b_vclk2_other.hw,
		[CLKID_EDP]		    = &meson8b_edp.hw,
		[CLKID_AO_MEDIA_CPU]	    = &meson8b_ao_media_cpu.hw,
		[CLKID_AO_AHB_SRAM]	    = &meson8b_ao_ahb_sram.hw,
		[CLKID_AO_AHB_BUS]	    = &meson8b_ao_ahb_bus.hw,
		[CLKID_AO_IFACE]	    = &meson8b_ao_iface.hw,
		[CLKID_MPLL0]		    = &meson8b_mpll0.hw,
		[CLKID_MPLL1]		    = &meson8b_mpll1.hw,
		[CLKID_MPLL2]		    = &meson8b_mpll2.hw,
	},
	.num = CLK_NR_CLKS,
};

static struct meson_clk_pll *const meson8b_clk_plls[] = {
	&meson8b_fixed_pll,
	&meson8b_vid_pll,
	&meson8b_sys_pll,
};

static struct meson_clk_mpll *const meson8b_clk_mplls[] = {
	&meson8b_mpll0,
	&meson8b_mpll1,
	&meson8b_mpll2,
};

static struct clk_gate *const meson8b_clk_gates[] = {
	&meson8b_clk81,
	&meson8b_ddr,
	&meson8b_dos,
	&meson8b_isa,
	&meson8b_pl301,
	&meson8b_periphs,
	&meson8b_spicc,
	&meson8b_i2c,
	&meson8b_sar_adc,
	&meson8b_smart_card,
	&meson8b_rng0,
	&meson8b_uart0,
	&meson8b_sdhc,
	&meson8b_stream,
	&meson8b_async_fifo,
	&meson8b_sdio,
	&meson8b_abuf,
	&meson8b_hiu_iface,
	&meson8b_assist_misc,
	&meson8b_spi,
	&meson8b_i2s_spdif,
	&meson8b_eth,
	&meson8b_demux,
	&meson8b_aiu_glue,
	&meson8b_iec958,
	&meson8b_i2s_out,
	&meson8b_amclk,
	&meson8b_aififo2,
	&meson8b_mixer,
	&meson8b_mixer_iface,
	&meson8b_adc,
	&meson8b_blkmv,
	&meson8b_aiu,
	&meson8b_uart1,
	&meson8b_g2d,
	&meson8b_usb0,
	&meson8b_usb1,
	&meson8b_reset,
	&meson8b_nand,
	&meson8b_dos_parser,
	&meson8b_usb,
	&meson8b_vdin1,
	&meson8b_ahb_arb0,
	&meson8b_efuse,
	&meson8b_boot_rom,
	&meson8b_ahb_data_bus,
	&meson8b_ahb_ctrl_bus,
	&meson8b_hdmi_intr_sync,
	&meson8b_hdmi_pclk,
	&meson8b_usb1_ddr_bridge,
	&meson8b_usb0_ddr_bridge,
	&meson8b_mmc_pclk,
	&meson8b_dvin,
	&meson8b_uart2,
	&meson8b_sana,
	&meson8b_vpu_intr,
	&meson8b_sec_ahb_ahb3_bridge,
	&meson8b_clk81_a9,
	&meson8b_vclk2_venci0,
	&meson8b_vclk2_venci1,
	&meson8b_vclk2_vencp0,
	&meson8b_vclk2_vencp1,
	&meson8b_gclk_venci_int,
	&meson8b_gclk_vencp_int,
	&meson8b_dac_clk,
	&meson8b_aoclk_gate,
	&meson8b_iec958_gate,
	&meson8b_enc480p,
	&meson8b_rng1,
	&meson8b_gclk_vencl_int,
	&meson8b_vclk2_venclmcc,
	&meson8b_vclk2_vencl,
	&meson8b_vclk2_other,
	&meson8b_edp,
	&meson8b_ao_media_cpu,
	&meson8b_ao_ahb_sram,
	&meson8b_ao_ahb_bus,
	&meson8b_ao_iface,
};

static struct clk_mux *const meson8b_clk_muxes[] = {
	&meson8b_mpeg_clk_sel,
};

static struct clk_divider *const meson8b_clk_dividers[] = {
	&meson8b_mpeg_clk_div,
};

static int meson8b_clkc_probe(struct platform_device *pdev)
{
	void __iomem *clk_base;
	int ret, clkid, i;
	struct clk_hw *parent_hw;
	struct clk *parent_clk;
	struct device *dev = &pdev->dev;

	/*  Generic clocks and PLLs */
	clk_base = of_iomap(dev->of_node, 1);
	if (!clk_base) {
		pr_err("%s: Unable to map clk base\n", __func__);
		return -ENXIO;
	}

	/* Populate base address for PLLs */
	for (i = 0; i < ARRAY_SIZE(meson8b_clk_plls); i++)
		meson8b_clk_plls[i]->base = clk_base;

	/* Populate base address for MPLLs */
	for (i = 0; i < ARRAY_SIZE(meson8b_clk_mplls); i++)
		meson8b_clk_mplls[i]->base = clk_base;

	/* Populate the base address for CPU clk */
	meson8b_cpu_clk.base = clk_base;

	/* Populate base address for gates */
	for (i = 0; i < ARRAY_SIZE(meson8b_clk_gates); i++)
		meson8b_clk_gates[i]->reg = clk_base +
			(u32)meson8b_clk_gates[i]->reg;

	/* Populate base address for muxes */
	for (i = 0; i < ARRAY_SIZE(meson8b_clk_muxes); i++)
		meson8b_clk_muxes[i]->reg = clk_base +
			(u32)meson8b_clk_muxes[i]->reg;

	/* Populate base address for dividers */
	for (i = 0; i < ARRAY_SIZE(meson8b_clk_dividers); i++)
		meson8b_clk_dividers[i]->reg = clk_base +
			(u32)meson8b_clk_dividers[i]->reg;

	/*
	 * register all clks
	 * CLKID_UNUSED = 0, so skip it and start with CLKID_XTAL = 1
	 */
	for (clkid = CLKID_XTAL; clkid < CLK_NR_CLKS; clkid++) {
		/* array might be sparse */
		if (!meson8b_hw_onecell_data.hws[clkid])
			continue;

		/* FIXME convert to devm_clk_register */
		ret = devm_clk_hw_register(dev, meson8b_hw_onecell_data.hws[clkid]);
		if (ret)
			goto iounmap;
	}

	/*
	 * Register CPU clk notifier
	 *
	 * FIXME this is wrong for a lot of reasons. First, the muxes should be
	 * struct clk_hw objects. Second, we shouldn't program the muxes in
	 * notifier handlers. The tricky programming sequence will be handled
	 * by the forthcoming coordinated clock rates mechanism once that
	 * feature is released.
	 *
	 * Furthermore, looking up the parent this way is terrible. At some
	 * point we will stop allocating a default struct clk when registering
	 * a new clk_hw, and this hack will no longer work. Releasing the ccr
	 * feature before that time solves the problem :-)
	 */
	parent_hw = clk_hw_get_parent(&meson8b_cpu_clk.hw);
	parent_clk = parent_hw->clk;
	ret = clk_notifier_register(parent_clk, &meson8b_cpu_clk.clk_nb);
	if (ret) {
		pr_err("%s: failed to register clock notifier for cpu_clk\n",
				__func__);
		goto iounmap;
	}

	return of_clk_add_hw_provider(dev->of_node, of_clk_hw_onecell_get,
			&meson8b_hw_onecell_data);

iounmap:
	iounmap(clk_base);
	return ret;
}

static const struct of_device_id meson8b_clkc_match_table[] = {
	{ .compatible = "amlogic,meson8-clkc" },
	{ .compatible = "amlogic,meson8b-clkc" },
	{ .compatible = "amlogic,meson8m2-clkc" },
	{ }
};

static struct platform_driver meson8b_driver = {
	.probe		= meson8b_clkc_probe,
	.driver		= {
		.name	= "meson8b-clkc",
		.of_match_table = meson8b_clkc_match_table,
	},
};

builtin_platform_driver(meson8b_driver);
