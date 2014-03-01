/*
 * Copyright (c) 2012, 2013, NVIDIA CORPORATION.  All rights reserved.
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
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/clk/tegra.h>

#include "clk.h"
#include "clk-id.h"

#define CLK_SOURCE_I2S0 0x1d8
#define CLK_SOURCE_I2S1 0x100
#define CLK_SOURCE_I2S2 0x104
#define CLK_SOURCE_NDFLASH 0x160
#define CLK_SOURCE_I2S3 0x3bc
#define CLK_SOURCE_I2S4 0x3c0
#define CLK_SOURCE_SPDIF_OUT 0x108
#define CLK_SOURCE_SPDIF_IN 0x10c
#define CLK_SOURCE_PWM 0x110
#define CLK_SOURCE_ADX 0x638
#define CLK_SOURCE_ADX1 0x670
#define CLK_SOURCE_AMX 0x63c
#define CLK_SOURCE_AMX1 0x674
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
#define CLK_SOURCE_NDSPEED 0x3f8
#define CLK_SOURCE_VFIR 0x168
#define CLK_SOURCE_SDMMC1 0x150
#define CLK_SOURCE_SDMMC2 0x154
#define CLK_SOURCE_SDMMC3 0x1bc
#define CLK_SOURCE_SDMMC4 0x164
#define CLK_SOURCE_CVE 0x140
#define CLK_SOURCE_TVO 0x188
#define CLK_SOURCE_TVDAC 0x194
#define CLK_SOURCE_VDE 0x1c8
#define CLK_SOURCE_CSITE 0x1d4
#define CLK_SOURCE_LA 0x1f8
#define CLK_SOURCE_TRACE 0x634
#define CLK_SOURCE_OWR 0x1cc
#define CLK_SOURCE_NOR 0x1d0
#define CLK_SOURCE_MIPI 0x174
#define CLK_SOURCE_I2C1 0x124
#define CLK_SOURCE_I2C2 0x198
#define CLK_SOURCE_I2C3 0x1b8
#define CLK_SOURCE_I2C4 0x3c4
#define CLK_SOURCE_I2C5 0x128
#define CLK_SOURCE_I2C6 0x65c
#define CLK_SOURCE_UARTA 0x178
#define CLK_SOURCE_UARTB 0x17c
#define CLK_SOURCE_UARTC 0x1a0
#define CLK_SOURCE_UARTD 0x1c0
#define CLK_SOURCE_UARTE 0x1c4
#define CLK_SOURCE_3D 0x158
#define CLK_SOURCE_2D 0x15c
#define CLK_SOURCE_MPE 0x170
#define CLK_SOURCE_UARTE 0x1c4
#define CLK_SOURCE_VI_SENSOR 0x1a8
#define CLK_SOURCE_VI 0x148
#define CLK_SOURCE_EPP 0x16c
#define CLK_SOURCE_MSENC 0x1f0
#define CLK_SOURCE_TSEC 0x1f4
#define CLK_SOURCE_HOST1X 0x180
#define CLK_SOURCE_HDMI 0x18c
#define CLK_SOURCE_DISP1 0x138
#define CLK_SOURCE_DISP2 0x13c
#define CLK_SOURCE_CILAB 0x614
#define CLK_SOURCE_CILCD 0x618
#define CLK_SOURCE_CILE 0x61c
#define CLK_SOURCE_DSIALP 0x620
#define CLK_SOURCE_DSIBLP 0x624
#define CLK_SOURCE_TSENSOR 0x3b8
#define CLK_SOURCE_D_AUDIO 0x3d0
#define CLK_SOURCE_DAM0 0x3d8
#define CLK_SOURCE_DAM1 0x3dc
#define CLK_SOURCE_DAM2 0x3e0
#define CLK_SOURCE_ACTMON 0x3e8
#define CLK_SOURCE_EXTERN1 0x3ec
#define CLK_SOURCE_EXTERN2 0x3f0
#define CLK_SOURCE_EXTERN3 0x3f4
#define CLK_SOURCE_I2CSLOW 0x3fc
#define CLK_SOURCE_SE 0x42c
#define CLK_SOURCE_MSELECT 0x3b4
#define CLK_SOURCE_DFLL_REF 0x62c
#define CLK_SOURCE_DFLL_SOC 0x630
#define CLK_SOURCE_SOC_THERM 0x644
#define CLK_SOURCE_XUSB_HOST_SRC 0x600
#define CLK_SOURCE_XUSB_FALCON_SRC 0x604
#define CLK_SOURCE_XUSB_FS_SRC 0x608
#define CLK_SOURCE_XUSB_SS_SRC 0x610
#define CLK_SOURCE_XUSB_DEV_SRC 0x60c
#define CLK_SOURCE_ISP 0x144
#define CLK_SOURCE_SOR0 0x414
#define CLK_SOURCE_DPAUX 0x418
#define CLK_SOURCE_SATA_OOB 0x420
#define CLK_SOURCE_SATA 0x424
#define CLK_SOURCE_ENTROPY 0x628
#define CLK_SOURCE_VI_SENSOR2 0x658
#define CLK_SOURCE_HDMI_AUDIO 0x668
#define CLK_SOURCE_VIC03 0x678
#define CLK_SOURCE_CLK72MHZ 0x66c

#define MASK(x) (BIT(x) - 1)

#define MUX(_name, _parents, _offset,	\
			    _clk_num, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,\
			30, MASK(2), 0, 0, 8, 1, TEGRA_DIVIDER_ROUND_UP, \
			_clk_num,  _gate_flags, _clk_id, _parents##_idx, 0,\
			NULL)

#define MUX_FLAGS(_name, _parents, _offset,\
			    _clk_num, _gate_flags, _clk_id, flags)\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,\
			30, MASK(2), 0, 0, 8, 1, TEGRA_DIVIDER_ROUND_UP,\
			_clk_num, _gate_flags, _clk_id, _parents##_idx, flags,\
			NULL)

#define MUX8(_name, _parents, _offset, \
			     _clk_num, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,\
			29, MASK(3), 0, 0, 8, 1, TEGRA_DIVIDER_ROUND_UP,\
			_clk_num, _gate_flags, _clk_id, _parents##_idx, 0,\
			NULL)

#define MUX8_NOGATE_LOCK(_name, _parents, _offset, _clk_id, _lock)	\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,	\
			      29, MASK(3), 0, 0, 8, 1, TEGRA_DIVIDER_ROUND_UP,\
			      0, TEGRA_PERIPH_NO_GATE, _clk_id,\
			      _parents##_idx, 0, _lock)

#define INT(_name, _parents, _offset,	\
			    _clk_num, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,\
			30, MASK(2), 0, 0, 8, 1, TEGRA_DIVIDER_INT| \
			TEGRA_DIVIDER_ROUND_UP, _clk_num, _gate_flags,\
			_clk_id, _parents##_idx, 0, NULL)

#define INT_FLAGS(_name, _parents, _offset,\
			    _clk_num, _gate_flags, _clk_id, flags)\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,\
			30, MASK(2), 0, 0, 8, 1, TEGRA_DIVIDER_INT| \
			TEGRA_DIVIDER_ROUND_UP, _clk_num,  _gate_flags,\
			_clk_id, _parents##_idx, flags, NULL)

#define INT8(_name, _parents, _offset,\
			    _clk_num, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,\
			29, MASK(3), 0, 0, 8, 1, TEGRA_DIVIDER_INT| \
			TEGRA_DIVIDER_ROUND_UP, _clk_num, _gate_flags,\
			_clk_id, _parents##_idx, 0, NULL)

#define UART(_name, _parents, _offset,\
			     _clk_num, _clk_id)			\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,\
			30, MASK(2), 0, 0, 16, 1, TEGRA_DIVIDER_UART| \
			TEGRA_DIVIDER_ROUND_UP, _clk_num, 0, _clk_id,\
			_parents##_idx, 0, NULL)

#define I2C(_name, _parents, _offset,\
			     _clk_num, _clk_id)			\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,\
			30, MASK(2), 0, 0, 16, 0, TEGRA_DIVIDER_ROUND_UP,\
			_clk_num, 0, _clk_id, _parents##_idx, 0, NULL)

#define XUSB(_name, _parents, _offset, \
			     _clk_num, _gate_flags, _clk_id)	 \
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset, \
			29, MASK(3), 0, 0, 8, 1, TEGRA_DIVIDER_INT| \
			TEGRA_DIVIDER_ROUND_UP, _clk_num, _gate_flags,\
			_clk_id, _parents##_idx, 0, NULL)

#define AUDIO(_name, _offset,  _clk_num,\
				 _gate_flags, _clk_id)		\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, mux_d_audio_clk,	\
			_offset, 16, 0xE01F, 0, 0, 8, 1,		\
			TEGRA_DIVIDER_ROUND_UP, _clk_num, _gate_flags,	\
			_clk_id, mux_d_audio_clk_idx, 0, NULL)

#define NODIV(_name, _parents, _offset, \
			      _mux_shift, _mux_mask, _clk_num, \
			      _gate_flags, _clk_id, _lock)		\
	TEGRA_INIT_DATA_TABLE(_name, NULL, NULL, _parents, _offset,\
			_mux_shift, _mux_mask, 0, 0, 0, 0, 0,\
			_clk_num, (_gate_flags) | TEGRA_PERIPH_NO_DIV,\
			_clk_id, _parents##_idx, 0, _lock)

#define GATE(_name, _parent_name,	\
			     _clk_num, _gate_flags,  _clk_id, _flags)	\
	{								\
		.name = _name,						\
		.clk_id = _clk_id,					\
		.p.parent_name = _parent_name,				\
		.periph = TEGRA_CLK_PERIPH(0, 0, 0, 0, 0, 0, 0,		\
				_clk_num, _gate_flags, 0, NULL),	\
		.flags = _flags						\
	}

#define PLLP_BASE 0xa0
#define PLLP_MISC 0xac
#define PLLP_OUTA 0xa4
#define PLLP_OUTB 0xa8
#define PLLP_OUTC 0x67c

#define PLL_BASE_LOCK BIT(27)
#define PLL_MISC_LOCK_ENABLE 18

static DEFINE_SPINLOCK(PLLP_OUTA_lock);
static DEFINE_SPINLOCK(PLLP_OUTB_lock);
static DEFINE_SPINLOCK(PLLP_OUTC_lock);
static DEFINE_SPINLOCK(sor0_lock);

#define MUX_I2S_SPDIF(_id)						\
static const char *mux_pllaout0_##_id##_2x_pllp_clkm[] = { "pll_a_out0", \
							   #_id, "pll_p",\
							   "clk_m"};
MUX_I2S_SPDIF(audio0)
MUX_I2S_SPDIF(audio1)
MUX_I2S_SPDIF(audio2)
MUX_I2S_SPDIF(audio3)
MUX_I2S_SPDIF(audio4)
MUX_I2S_SPDIF(audio)

#define mux_pllaout0_audio0_2x_pllp_clkm_idx NULL
#define mux_pllaout0_audio1_2x_pllp_clkm_idx NULL
#define mux_pllaout0_audio2_2x_pllp_clkm_idx NULL
#define mux_pllaout0_audio3_2x_pllp_clkm_idx NULL
#define mux_pllaout0_audio4_2x_pllp_clkm_idx NULL
#define mux_pllaout0_audio_2x_pllp_clkm_idx NULL

static const char *mux_pllp_pllc_pllm_clkm[] = {
	"pll_p", "pll_c", "pll_m", "clk_m"
};
#define mux_pllp_pllc_pllm_clkm_idx NULL

static const char *mux_pllp_pllc_pllm[] = { "pll_p", "pll_c", "pll_m" };
#define mux_pllp_pllc_pllm_idx NULL

static const char *mux_pllp_pllc_clk32_clkm[] = {
	"pll_p", "pll_c", "clk_32k", "clk_m"
};
#define mux_pllp_pllc_clk32_clkm_idx NULL

static const char *mux_plla_pllc_pllp_clkm[] = {
	"pll_a_out0", "pll_c", "pll_p", "clk_m"
};
#define mux_plla_pllc_pllp_clkm_idx mux_pllp_pllc_pllm_clkm_idx

static const char *mux_pllp_pllc2_c_c3_pllm_clkm[] = {
	"pll_p", "pll_c2", "pll_c", "pll_c3", "pll_m", "clk_m"
};
static u32 mux_pllp_pllc2_c_c3_pllm_clkm_idx[] = {
	[0] = 0, [1] = 1, [2] = 2, [3] = 3, [4] = 4, [5] = 6,
};

static const char *mux_pllp_clkm[] = {
	"pll_p", "clk_m"
};
static u32 mux_pllp_clkm_idx[] = {
	[0] = 0, [1] = 3,
};

static const char *mux_pllm_pllc2_c_c3_pllp_plla[] = {
	"pll_m", "pll_c2", "pll_c", "pll_c3", "pll_p", "pll_a_out0"
};
#define mux_pllm_pllc2_c_c3_pllp_plla_idx mux_pllp_pllc2_c_c3_pllm_clkm_idx

static const char *mux_pllp_pllm_plld_plla_pllc_plld2_clkm[] = {
	"pll_p", "pll_m", "pll_d_out0", "pll_a_out0", "pll_c",
	"pll_d2_out0", "clk_m"
};
#define mux_pllp_pllm_plld_plla_pllc_plld2_clkm_idx NULL

static const char *mux_pllm_pllc_pllp_plla[] = {
	"pll_m", "pll_c", "pll_p", "pll_a_out0"
};
#define mux_pllm_pllc_pllp_plla_idx mux_pllp_pllc_pllm_clkm_idx

static const char *mux_pllp_pllc_clkm[] = {
	"pll_p", "pll_c", "pll_m"
};
static u32 mux_pllp_pllc_clkm_idx[] = {
	[0] = 0, [1] = 1, [2] = 3,
};

static const char *mux_pllp_pllc_clkm_clk32[] = {
	"pll_p", "pll_c", "clk_m", "clk_32k"
};
#define mux_pllp_pllc_clkm_clk32_idx NULL

static const char *mux_plla_clk32_pllp_clkm_plle[] = {
	"pll_a_out0", "clk_32k", "pll_p", "clk_m", "pll_e_out0"
};
#define mux_plla_clk32_pllp_clkm_plle_idx NULL

static const char *mux_clkm_pllp_pllc_pllre[] = {
	"clk_m", "pll_p", "pll_c", "pll_re_out"
};
static u32 mux_clkm_pllp_pllc_pllre_idx[] = {
	[0] = 0, [1] = 1, [2] = 3, [3] = 5,
};

static const char *mux_clkm_48M_pllp_480M[] = {
	"clk_m", "pll_u_48M", "pll_p", "pll_u_480M"
};
#define mux_clkm_48M_pllp_480M_idx NULL

static const char *mux_clkm_pllre_clk32_480M_pllc_ref[] = {
	"clk_m", "pll_re_out", "clk_32k", "pll_u_480M", "pll_c", "pll_ref"
};
static u32 mux_clkm_pllre_clk32_480M_pllc_ref_idx[] = {
	[0] = 0, [1] = 1, [2] = 3, [3] = 3, [4] = 4, [5] = 7,
};

static const char *mux_d_audio_clk[] = {
	"pll_a_out0", "pll_p", "clk_m", "spdif_in_sync", "i2s0_sync",
	"i2s1_sync", "i2s2_sync", "i2s3_sync", "i2s4_sync", "vimclk_sync",
};
static u32 mux_d_audio_clk_idx[] = {
	[0] = 0, [1] = 0x8000, [2] = 0xc000, [3] = 0xE000, [4] = 0xE001,
	[5] = 0xE002, [6] = 0xE003, [7] = 0xE004, [8] = 0xE005, [9] = 0xE007,
};

static const char *mux_pllp_plld_pllc_clkm[] = {
	"pll_p", "pll_d_out0", "pll_c", "clk_m"
};
#define mux_pllp_plld_pllc_clkm_idx NULL
static const char *mux_pllm_pllc_pllp_plla_clkm_pllc4[] = {
	"pll_m", "pll_c", "pll_p", "pll_a_out0", "clk_m", "pll_c4",
};
static u32 mux_pllm_pllc_pllp_plla_clkm_pllc4_idx[] = {
	[0] = 0, [1] = 1, [2] = 3, [3] = 3, [4] = 6, [5] = 7,
};

static const char *mux_pllp_clkm1[] = {
	"pll_p", "clk_m",
};
#define mux_pllp_clkm1_idx NULL

static const char *mux_pllp3_pllc_clkm[] = {
	"pll_p_out3", "pll_c", "pll_c2", "clk_m",
};
#define mux_pllp3_pllc_clkm_idx NULL

static const char *mux_pllm_pllc_pllp_plla_pllc2_c3_clkm[] = {
	"pll_m", "pll_c", "pll_p", "pll_a", "pll_c2", "pll_c3", "clk_m"
};
static u32 mux_pllm_pllc_pllp_plla_pllc2_c3_clkm_idx[] = {
	[0] = 0, [1] = 1, [2] = 2, [3] = 3, [4] = 4, [5] = 6,
};

static const char *mux_pllm_pllc2_c_c3_pllp_plla_pllc4[] = {
	"pll_m", "pll_c2", "pll_c", "pll_c3", "pll_p", "pll_a_out0", "pll_c4",
};
static u32 mux_pllm_pllc2_c_c3_pllp_plla_pllc4_idx[] = {
	[0] = 0, [1] = 1, [2] = 2, [3] = 3, [4] = 4, [5] = 6, [6] = 7,
};

static const char *mux_clkm_plldp_sor0lvds[] = {
	"clk_m", "pll_dp", "sor0_lvds",
};
#define mux_clkm_plldp_sor0lvds_idx NULL

static struct tegra_periph_init_data periph_clks[] = {
	AUDIO("d_audio", CLK_SOURCE_D_AUDIO, 106, TEGRA_PERIPH_ON_APB, tegra_clk_d_audio),
	AUDIO("dam0", CLK_SOURCE_DAM0, 108, TEGRA_PERIPH_ON_APB, tegra_clk_dam0),
	AUDIO("dam1", CLK_SOURCE_DAM1, 109, TEGRA_PERIPH_ON_APB, tegra_clk_dam1),
	AUDIO("dam2", CLK_SOURCE_DAM2, 110, TEGRA_PERIPH_ON_APB, tegra_clk_dam2),
	I2C("i2c1", mux_pllp_clkm, CLK_SOURCE_I2C1, 12, tegra_clk_i2c1),
	I2C("i2c2", mux_pllp_clkm, CLK_SOURCE_I2C2, 54, tegra_clk_i2c2),
	I2C("i2c3", mux_pllp_clkm, CLK_SOURCE_I2C3, 67, tegra_clk_i2c3),
	I2C("i2c4", mux_pllp_clkm, CLK_SOURCE_I2C4, 103, tegra_clk_i2c4),
	I2C("i2c5", mux_pllp_clkm, CLK_SOURCE_I2C5, 47, tegra_clk_i2c5),
	INT("vde", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_VDE, 61, 0, tegra_clk_vde),
	INT("vi", mux_pllm_pllc_pllp_plla, CLK_SOURCE_VI, 20, 0, tegra_clk_vi),
	INT("epp", mux_pllm_pllc_pllp_plla, CLK_SOURCE_EPP, 19, 0, tegra_clk_epp),
	INT("host1x", mux_pllm_pllc_pllp_plla, CLK_SOURCE_HOST1X, 28, 0, tegra_clk_host1x),
	INT("mpe", mux_pllm_pllc_pllp_plla, CLK_SOURCE_MPE, 60, 0, tegra_clk_mpe),
	INT("2d", mux_pllm_pllc_pllp_plla, CLK_SOURCE_2D, 21, 0, tegra_clk_gr2d),
	INT("3d", mux_pllm_pllc_pllp_plla, CLK_SOURCE_3D, 24, 0, tegra_clk_gr3d),
	INT8("vde", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_VDE, 61, 0, tegra_clk_vde_8),
	INT8("vi", mux_pllm_pllc2_c_c3_pllp_plla, CLK_SOURCE_VI, 20, 0, tegra_clk_vi_8),
	INT8("vi", mux_pllm_pllc2_c_c3_pllp_plla_pllc4, CLK_SOURCE_VI, 20, 0, tegra_clk_vi_9),
	INT8("epp", mux_pllm_pllc2_c_c3_pllp_plla, CLK_SOURCE_EPP, 19, 0, tegra_clk_epp_8),
	INT8("msenc", mux_pllm_pllc2_c_c3_pllp_plla, CLK_SOURCE_MSENC, 91, TEGRA_PERIPH_WAR_1005168, tegra_clk_msenc),
	INT8("tsec", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_TSEC, 83, 0, tegra_clk_tsec),
	INT8("host1x", mux_pllm_pllc2_c_c3_pllp_plla, CLK_SOURCE_HOST1X, 28, 0, tegra_clk_host1x_8),
	INT8("se", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_SE, 127, TEGRA_PERIPH_ON_APB, tegra_clk_se),
	INT8("2d", mux_pllm_pllc2_c_c3_pllp_plla, CLK_SOURCE_2D, 21, 0, tegra_clk_gr2d_8),
	INT8("3d", mux_pllm_pllc2_c_c3_pllp_plla, CLK_SOURCE_3D, 24, 0, tegra_clk_gr3d_8),
	INT8("vic03", mux_pllm_pllc_pllp_plla_pllc2_c3_clkm, CLK_SOURCE_VIC03, 178, 0, tegra_clk_vic03),
	INT_FLAGS("mselect", mux_pllp_clkm, CLK_SOURCE_MSELECT, 99, 0, tegra_clk_mselect, CLK_IGNORE_UNUSED),
	MUX("i2s0", mux_pllaout0_audio0_2x_pllp_clkm, CLK_SOURCE_I2S0, 30, TEGRA_PERIPH_ON_APB, tegra_clk_i2s0),
	MUX("i2s1", mux_pllaout0_audio1_2x_pllp_clkm, CLK_SOURCE_I2S1, 11, TEGRA_PERIPH_ON_APB, tegra_clk_i2s1),
	MUX("i2s2", mux_pllaout0_audio2_2x_pllp_clkm, CLK_SOURCE_I2S2, 18, TEGRA_PERIPH_ON_APB, tegra_clk_i2s2),
	MUX("i2s3", mux_pllaout0_audio3_2x_pllp_clkm, CLK_SOURCE_I2S3, 101, TEGRA_PERIPH_ON_APB, tegra_clk_i2s3),
	MUX("i2s4", mux_pllaout0_audio4_2x_pllp_clkm, CLK_SOURCE_I2S4, 102, TEGRA_PERIPH_ON_APB, tegra_clk_i2s4),
	MUX("spdif_out", mux_pllaout0_audio_2x_pllp_clkm, CLK_SOURCE_SPDIF_OUT, 10, TEGRA_PERIPH_ON_APB, tegra_clk_spdif_out),
	MUX("spdif_in", mux_pllp_pllc_pllm, CLK_SOURCE_SPDIF_IN, 10, TEGRA_PERIPH_ON_APB, tegra_clk_spdif_in),
	MUX("pwm", mux_pllp_pllc_clk32_clkm, CLK_SOURCE_PWM, 17, TEGRA_PERIPH_ON_APB, tegra_clk_pwm),
	MUX("adx", mux_plla_pllc_pllp_clkm, CLK_SOURCE_ADX, 154, TEGRA_PERIPH_ON_APB, tegra_clk_adx),
	MUX("amx", mux_plla_pllc_pllp_clkm, CLK_SOURCE_AMX, 153, TEGRA_PERIPH_ON_APB, tegra_clk_amx),
	MUX("hda", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_HDA, 125, TEGRA_PERIPH_ON_APB, tegra_clk_hda),
	MUX("hda2codec_2x", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_HDA2CODEC_2X, 111, TEGRA_PERIPH_ON_APB, tegra_clk_hda2codec_2x),
	MUX("vfir", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_VFIR, 7, TEGRA_PERIPH_ON_APB, tegra_clk_vfir),
	MUX("sdmmc1", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SDMMC1, 14, 0, tegra_clk_sdmmc1),
	MUX("sdmmc2", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SDMMC2, 9, 0, tegra_clk_sdmmc2),
	MUX("sdmmc3", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SDMMC3, 69, 0, tegra_clk_sdmmc3),
	MUX("sdmmc4", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SDMMC4, 15, 0, tegra_clk_sdmmc4),
	MUX("la", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_LA, 76, TEGRA_PERIPH_ON_APB, tegra_clk_la),
	MUX("trace", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_TRACE, 77, TEGRA_PERIPH_ON_APB, tegra_clk_trace),
	MUX("owr", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_OWR, 71, TEGRA_PERIPH_ON_APB, tegra_clk_owr),
	MUX("nor", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_NOR, 42, 0, tegra_clk_nor),
	MUX("mipi", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_MIPI, 50, TEGRA_PERIPH_ON_APB, tegra_clk_mipi),
	MUX("vi_sensor", mux_pllm_pllc_pllp_plla, CLK_SOURCE_VI_SENSOR, 20, TEGRA_PERIPH_NO_RESET, tegra_clk_vi_sensor),
	MUX("cilab", mux_pllp_pllc_clkm, CLK_SOURCE_CILAB, 144, 0, tegra_clk_cilab),
	MUX("cilcd", mux_pllp_pllc_clkm, CLK_SOURCE_CILCD, 145, 0, tegra_clk_cilcd),
	MUX("cile", mux_pllp_pllc_clkm, CLK_SOURCE_CILE, 146, 0, tegra_clk_cile),
	MUX("dsialp", mux_pllp_pllc_clkm, CLK_SOURCE_DSIALP, 147, 0, tegra_clk_dsialp),
	MUX("dsiblp", mux_pllp_pllc_clkm, CLK_SOURCE_DSIBLP, 148, 0, tegra_clk_dsiblp),
	MUX("tsensor", mux_pllp_pllc_clkm_clk32, CLK_SOURCE_TSENSOR, 100, TEGRA_PERIPH_ON_APB, tegra_clk_tsensor),
	MUX("actmon", mux_pllp_pllc_clk32_clkm, CLK_SOURCE_ACTMON, 119, 0, tegra_clk_actmon),
	MUX("dfll_ref", mux_pllp_clkm, CLK_SOURCE_DFLL_REF, 155, TEGRA_PERIPH_ON_APB, tegra_clk_dfll_ref),
	MUX("dfll_soc", mux_pllp_clkm, CLK_SOURCE_DFLL_SOC, 155, TEGRA_PERIPH_ON_APB, tegra_clk_dfll_soc),
	MUX("i2cslow", mux_pllp_pllc_clk32_clkm, CLK_SOURCE_I2CSLOW, 81, TEGRA_PERIPH_ON_APB, tegra_clk_i2cslow),
	MUX("sbc1", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SBC1, 41, TEGRA_PERIPH_ON_APB, tegra_clk_sbc1),
	MUX("sbc2", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SBC2, 44, TEGRA_PERIPH_ON_APB, tegra_clk_sbc2),
	MUX("sbc3", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SBC3, 46, TEGRA_PERIPH_ON_APB, tegra_clk_sbc3),
	MUX("sbc4", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SBC4, 68, TEGRA_PERIPH_ON_APB, tegra_clk_sbc4),
	MUX("sbc5", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SBC5, 104, TEGRA_PERIPH_ON_APB, tegra_clk_sbc5),
	MUX("sbc6", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SBC6, 105, TEGRA_PERIPH_ON_APB, tegra_clk_sbc6),
	MUX("cve", mux_pllp_plld_pllc_clkm, CLK_SOURCE_CVE, 49, 0, tegra_clk_cve),
	MUX("tvo", mux_pllp_plld_pllc_clkm, CLK_SOURCE_TVO, 49, 0, tegra_clk_tvo),
	MUX("tvdac", mux_pllp_plld_pllc_clkm, CLK_SOURCE_TVDAC, 53, 0, tegra_clk_tvdac),
	MUX("ndflash", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_NDFLASH, 13, TEGRA_PERIPH_ON_APB, tegra_clk_ndflash),
	MUX("ndspeed", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_NDSPEED, 80, TEGRA_PERIPH_ON_APB, tegra_clk_ndspeed),
	MUX("sata_oob", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SATA_OOB, 123, TEGRA_PERIPH_ON_APB, tegra_clk_sata_oob),
	MUX("sata", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_SATA, 124, TEGRA_PERIPH_ON_APB, tegra_clk_sata),
	MUX("adx1", mux_plla_pllc_pllp_clkm, CLK_SOURCE_ADX1, 180, TEGRA_PERIPH_ON_APB, tegra_clk_adx1),
	MUX("amx1", mux_plla_pllc_pllp_clkm, CLK_SOURCE_AMX1, 185, TEGRA_PERIPH_ON_APB, tegra_clk_amx1),
	MUX("vi_sensor2", mux_pllm_pllc2_c_c3_pllp_plla, CLK_SOURCE_VI_SENSOR2, 20, TEGRA_PERIPH_NO_RESET, tegra_clk_vi_sensor2),
	MUX8("sbc1", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_SBC1, 41, TEGRA_PERIPH_ON_APB, tegra_clk_sbc1_8),
	MUX8("sbc2", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_SBC2, 44, TEGRA_PERIPH_ON_APB, tegra_clk_sbc2_8),
	MUX8("sbc3", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_SBC3, 46, TEGRA_PERIPH_ON_APB, tegra_clk_sbc3_8),
	MUX8("sbc4", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_SBC4, 68, TEGRA_PERIPH_ON_APB, tegra_clk_sbc4_8),
	MUX8("sbc5", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_SBC5, 104, TEGRA_PERIPH_ON_APB, tegra_clk_sbc5_8),
	MUX8("sbc6", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_SBC6, 105, TEGRA_PERIPH_ON_APB, tegra_clk_sbc6_8),
	MUX8("ndflash", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_NDFLASH, 13, TEGRA_PERIPH_ON_APB, tegra_clk_ndflash_8),
	MUX8("ndspeed", mux_pllp_pllc2_c_c3_pllm_clkm, CLK_SOURCE_NDSPEED, 80, TEGRA_PERIPH_ON_APB, tegra_clk_ndspeed_8),
	MUX8("hdmi", mux_pllp_pllm_plld_plla_pllc_plld2_clkm, CLK_SOURCE_HDMI, 51, 0, tegra_clk_hdmi),
	MUX8("extern1", mux_plla_clk32_pllp_clkm_plle, CLK_SOURCE_EXTERN1, 120, 0, tegra_clk_extern1),
	MUX8("extern2", mux_plla_clk32_pllp_clkm_plle, CLK_SOURCE_EXTERN2, 121, 0, tegra_clk_extern2),
	MUX8("extern3", mux_plla_clk32_pllp_clkm_plle, CLK_SOURCE_EXTERN3, 122, 0, tegra_clk_extern3),
	MUX8("soc_therm", mux_pllm_pllc_pllp_plla, CLK_SOURCE_SOC_THERM, 78, TEGRA_PERIPH_ON_APB, tegra_clk_soc_therm),
	MUX8("vi_sensor", mux_pllm_pllc2_c_c3_pllp_plla, CLK_SOURCE_VI_SENSOR, 20, TEGRA_PERIPH_NO_RESET, tegra_clk_vi_sensor_8),
	MUX8("isp", mux_pllm_pllc_pllp_plla_clkm_pllc4, CLK_SOURCE_ISP, 23, TEGRA_PERIPH_ON_APB, tegra_clk_isp_8),
	MUX8("entropy", mux_pllp_clkm1, CLK_SOURCE_ENTROPY, 149,  0, tegra_clk_entropy),
	MUX8("hdmi_audio", mux_pllp3_pllc_clkm, CLK_SOURCE_HDMI_AUDIO, 176, TEGRA_PERIPH_NO_RESET, tegra_clk_hdmi_audio),
	MUX8("clk72mhz", mux_pllp3_pllc_clkm, CLK_SOURCE_CLK72MHZ, 177, TEGRA_PERIPH_NO_RESET, tegra_clk_clk72Mhz),
	MUX8_NOGATE_LOCK("sor0_lvds", mux_pllp_pllm_plld_plla_pllc_plld2_clkm, CLK_SOURCE_SOR0, tegra_clk_sor0_lvds, &sor0_lock),
	MUX_FLAGS("csite", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_CSITE, 73, TEGRA_PERIPH_ON_APB, tegra_clk_csite, CLK_IGNORE_UNUSED),
	NODIV("disp1", mux_pllp_pllm_plld_plla_pllc_plld2_clkm, CLK_SOURCE_DISP1, 29, 7, 27, 0, tegra_clk_disp1, NULL),
	NODIV("disp2", mux_pllp_pllm_plld_plla_pllc_plld2_clkm, CLK_SOURCE_DISP2, 29, 7, 26, 0, tegra_clk_disp2, NULL),
	NODIV("sor0", mux_clkm_plldp_sor0lvds, CLK_SOURCE_SOR0, 14, 3, 182, 0, tegra_clk_sor0, &sor0_lock),
	UART("uarta", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_UARTA, 6, tegra_clk_uarta),
	UART("uartb", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_UARTB, 7, tegra_clk_uartb),
	UART("uartc", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_UARTC, 55, tegra_clk_uartc),
	UART("uartd", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_UARTD, 65, tegra_clk_uartd),
	UART("uarte", mux_pllp_pllc_pllm_clkm, CLK_SOURCE_UARTE, 65, tegra_clk_uarte),
	XUSB("xusb_host_src", mux_clkm_pllp_pllc_pllre, CLK_SOURCE_XUSB_HOST_SRC, 143, TEGRA_PERIPH_ON_APB | TEGRA_PERIPH_NO_RESET, tegra_clk_xusb_host_src),
	XUSB("xusb_falcon_src", mux_clkm_pllp_pllc_pllre, CLK_SOURCE_XUSB_FALCON_SRC, 143, TEGRA_PERIPH_NO_RESET, tegra_clk_xusb_falcon_src),
	XUSB("xusb_fs_src", mux_clkm_48M_pllp_480M, CLK_SOURCE_XUSB_FS_SRC, 143, TEGRA_PERIPH_NO_RESET, tegra_clk_xusb_fs_src),
	XUSB("xusb_ss_src", mux_clkm_pllre_clk32_480M_pllc_ref, CLK_SOURCE_XUSB_SS_SRC, 143, TEGRA_PERIPH_NO_RESET, tegra_clk_xusb_ss_src),
	XUSB("xusb_dev_src", mux_clkm_pllp_pllc_pllre, CLK_SOURCE_XUSB_DEV_SRC, 95, TEGRA_PERIPH_ON_APB | TEGRA_PERIPH_NO_RESET, tegra_clk_xusb_dev_src),
};

static struct tegra_periph_init_data gate_clks[] = {
	GATE("rtc", "clk_32k", 4, TEGRA_PERIPH_ON_APB | TEGRA_PERIPH_NO_RESET, tegra_clk_rtc, 0),
	GATE("timer", "clk_m", 5, 0, tegra_clk_timer, 0),
	GATE("isp", "clk_m", 23, 0, tegra_clk_isp, 0),
	GATE("vcp", "clk_m", 29, 0, tegra_clk_vcp, 0),
	GATE("apbdma", "clk_m", 34, 0, tegra_clk_apbdma, 0),
	GATE("kbc", "clk_32k", 36, TEGRA_PERIPH_ON_APB | TEGRA_PERIPH_NO_RESET, tegra_clk_kbc, 0),
	GATE("fuse", "clk_m", 39, TEGRA_PERIPH_ON_APB, tegra_clk_fuse, 0),
	GATE("fuse_burn", "clk_m", 39, TEGRA_PERIPH_ON_APB, tegra_clk_fuse_burn, 0),
	GATE("kfuse", "clk_m", 40, TEGRA_PERIPH_ON_APB, tegra_clk_kfuse, 0),
	GATE("apbif", "clk_m", 107, TEGRA_PERIPH_ON_APB, tegra_clk_apbif, 0),
	GATE("hda2hdmi", "clk_m", 128, TEGRA_PERIPH_ON_APB, tegra_clk_hda2hdmi, 0),
	GATE("bsea", "clk_m", 62, 0, tegra_clk_bsea, 0),
	GATE("bsev", "clk_m", 63, 0, tegra_clk_bsev, 0),
	GATE("mipi-cal", "clk_m", 56, 0, tegra_clk_mipi_cal, 0),
	GATE("usbd", "clk_m", 22, 0, tegra_clk_usbd, 0),
	GATE("usb2", "clk_m", 58, 0, tegra_clk_usb2, 0),
	GATE("usb3", "clk_m", 59, 0, tegra_clk_usb3, 0),
	GATE("csi", "pll_p_out3", 52, 0, tegra_clk_csi, 0),
	GATE("afi", "clk_m", 72, 0, tegra_clk_afi, 0),
	GATE("csus", "clk_m", 92, TEGRA_PERIPH_NO_RESET, tegra_clk_csus, 0),
	GATE("dds", "clk_m", 150, TEGRA_PERIPH_ON_APB, tegra_clk_dds, 0),
	GATE("dp2", "clk_m", 152, TEGRA_PERIPH_ON_APB, tegra_clk_dp2, 0),
	GATE("dtv", "clk_m", 79, TEGRA_PERIPH_ON_APB, tegra_clk_dtv, 0),
	GATE("xusb_host", "xusb_host_src", 89, 0, tegra_clk_xusb_host, 0),
	GATE("xusb_ss", "xusb_ss_src", 156, 0, tegra_clk_xusb_ss, 0),
	GATE("xusb_dev", "xusb_dev_src", 95, 0, tegra_clk_xusb_dev, 0),
	GATE("dsia", "dsia_mux", 48, 0, tegra_clk_dsia, 0),
	GATE("dsib", "dsib_mux", 82, 0, tegra_clk_dsib, 0),
	GATE("emc", "emc_mux", 57, 0, tegra_clk_emc, CLK_IGNORE_UNUSED),
	GATE("sata_cold", "clk_m", 129, TEGRA_PERIPH_ON_APB, tegra_clk_sata_cold, 0),
	GATE("ispb", "clk_m", 3, 0, tegra_clk_ispb, 0),
	GATE("vim2_clk", "clk_m", 11, 0, tegra_clk_vim2_clk, 0),
	GATE("pcie", "clk_m", 70, 0, tegra_clk_pcie, 0),
	GATE("dpaux", "clk_m", 181, 0, tegra_clk_dpaux, 0),
	GATE("gpu", "pll_ref", 184, 0, tegra_clk_gpu, 0),
};

struct pll_out_data {
	char *div_name;
	char *pll_out_name;
	u32 offset;
	int clk_id;
	u8 div_shift;
	u8 div_flags;
	u8 rst_shift;
	spinlock_t *lock;
};

#define PLL_OUT(_num, _offset, _div_shift, _div_flags, _rst_shift, _id) \
	{\
		.div_name = "pll_p_out" #_num "_div",\
		.pll_out_name = "pll_p_out" #_num,\
		.offset = _offset,\
		.div_shift = _div_shift,\
		.div_flags = _div_flags | TEGRA_DIVIDER_FIXED |\
					TEGRA_DIVIDER_ROUND_UP,\
		.rst_shift = _rst_shift,\
		.clk_id = tegra_clk_ ## _id,\
		.lock = &_offset ##_lock,\
	}

static struct pll_out_data pllp_out_clks[] = {
	PLL_OUT(1, PLLP_OUTA, 8, 0, 0, pll_p_out1),
	PLL_OUT(2, PLLP_OUTA, 24, 0, 16, pll_p_out2),
	PLL_OUT(2, PLLP_OUTA, 24, TEGRA_DIVIDER_INT, 16, pll_p_out2_int),
	PLL_OUT(3, PLLP_OUTB, 8, 0, 0, pll_p_out3),
	PLL_OUT(4, PLLP_OUTB, 24, 0, 16, pll_p_out4),
	PLL_OUT(5, PLLP_OUTC, 24, 0, 16, pll_p_out5),
};

static void __init periph_clk_init(void __iomem *clk_base,
				struct tegra_clk *tegra_clks)
{
	int i;
	struct clk *clk;
	struct clk **dt_clk;

	for (i = 0; i < ARRAY_SIZE(periph_clks); i++) {
		struct tegra_clk_periph_regs *bank;
		struct tegra_periph_init_data *data;

		data = periph_clks + i;

		dt_clk = tegra_lookup_dt_id(data->clk_id, tegra_clks);
		if (!dt_clk)
			continue;

		bank = get_reg_bank(data->periph.gate.clk_num);
		if (!bank)
			continue;

		data->periph.gate.regs = bank;
		clk = tegra_clk_register_periph(data->name,
			data->p.parent_names, data->num_parents,
			&data->periph, clk_base, data->offset,
			data->flags);
		*dt_clk = clk;
	}
}

static void __init gate_clk_init(void __iomem *clk_base,
				struct tegra_clk *tegra_clks)
{
	int i;
	struct clk *clk;
	struct clk **dt_clk;

	for (i = 0; i < ARRAY_SIZE(gate_clks); i++) {
		struct tegra_periph_init_data *data;

		data = gate_clks + i;

		dt_clk = tegra_lookup_dt_id(data->clk_id, tegra_clks);
		if (!dt_clk)
			continue;

		clk = tegra_clk_register_periph_gate(data->name,
				data->p.parent_name, data->periph.gate.flags,
				clk_base, data->flags,
				data->periph.gate.clk_num,
				periph_clk_enb_refcnt);
		*dt_clk = clk;
	}
}

static void __init init_pllp(void __iomem *clk_base, void __iomem *pmc_base,
				struct tegra_clk *tegra_clks,
				struct tegra_clk_pll_params *pll_params)
{
	struct clk *clk;
	struct clk **dt_clk;
	int i;

	dt_clk = tegra_lookup_dt_id(tegra_clk_pll_p, tegra_clks);
	if (dt_clk) {
		/* PLLP */
		clk = tegra_clk_register_pll("pll_p", "pll_ref", clk_base,
					pmc_base, 0, pll_params, NULL);
		clk_register_clkdev(clk, "pll_p", NULL);
		*dt_clk = clk;
	}

	for (i = 0; i < ARRAY_SIZE(pllp_out_clks); i++) {
		struct pll_out_data *data;

		data = pllp_out_clks + i;

		dt_clk = tegra_lookup_dt_id(data->clk_id, tegra_clks);
		if (!dt_clk)
			continue;

		clk = tegra_clk_register_divider(data->div_name, "pll_p",
				clk_base + data->offset, 0, data->div_flags,
				data->div_shift, 8, 1, data->lock);
		clk = tegra_clk_register_pll_out(data->pll_out_name,
				data->div_name, clk_base + data->offset,
				data->rst_shift + 1, data->rst_shift,
				CLK_IGNORE_UNUSED | CLK_SET_RATE_PARENT, 0,
				data->lock);
		*dt_clk = clk;
	}
}

void __init tegra_periph_clk_init(void __iomem *clk_base,
			void __iomem *pmc_base, struct tegra_clk *tegra_clks,
			struct tegra_clk_pll_params *pll_params)
{
	init_pllp(clk_base, pmc_base, tegra_clks, pll_params);
	periph_clk_init(clk_base, tegra_clks);
	gate_clk_init(clk_base, tegra_clks);
}
