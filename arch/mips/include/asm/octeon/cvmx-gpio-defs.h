/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2012 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
 ***********************license end**************************************/

#ifndef __CVMX_GPIO_DEFS_H__
#define __CVMX_GPIO_DEFS_H__

#define CVMX_GPIO_BIT_CFGX(offset) (CVMX_ADD_IO_SEG(0x0001070000000800ull) + ((offset) & 15) * 8)
#define CVMX_GPIO_BOOT_ENA (CVMX_ADD_IO_SEG(0x00010700000008A8ull))
#define CVMX_GPIO_CLK_GENX(offset) (CVMX_ADD_IO_SEG(0x00010700000008C0ull) + ((offset) & 3) * 8)
#define CVMX_GPIO_CLK_QLMX(offset) (CVMX_ADD_IO_SEG(0x00010700000008E0ull) + ((offset) & 1) * 8)
#define CVMX_GPIO_DBG_ENA (CVMX_ADD_IO_SEG(0x00010700000008A0ull))
#define CVMX_GPIO_INT_CLR (CVMX_ADD_IO_SEG(0x0001070000000898ull))
#define CVMX_GPIO_MULTI_CAST (CVMX_ADD_IO_SEG(0x00010700000008B0ull))
#define CVMX_GPIO_PIN_ENA (CVMX_ADD_IO_SEG(0x00010700000008B8ull))
#define CVMX_GPIO_RX_DAT (CVMX_ADD_IO_SEG(0x0001070000000880ull))
#define CVMX_GPIO_TIM_CTL (CVMX_ADD_IO_SEG(0x00010700000008A0ull))
#define CVMX_GPIO_TX_CLR (CVMX_ADD_IO_SEG(0x0001070000000890ull))
#define CVMX_GPIO_TX_SET (CVMX_ADD_IO_SEG(0x0001070000000888ull))
#define CVMX_GPIO_XBIT_CFGX(offset) (CVMX_ADD_IO_SEG(0x0001070000000900ull) + ((offset) & 31) * 8 - 8*16)

union cvmx_gpio_bit_cfgx {
	uint64_t u64;
	struct cvmx_gpio_bit_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_21_63:42;
		uint64_t output_sel:5;
		uint64_t synce_sel:2;
		uint64_t clk_gen:1;
		uint64_t clk_sel:2;
		uint64_t fil_sel:4;
		uint64_t fil_cnt:4;
		uint64_t int_type:1;
		uint64_t int_en:1;
		uint64_t rx_xor:1;
		uint64_t tx_oe:1;
#else
		uint64_t tx_oe:1;
		uint64_t rx_xor:1;
		uint64_t int_en:1;
		uint64_t int_type:1;
		uint64_t fil_cnt:4;
		uint64_t fil_sel:4;
		uint64_t clk_sel:2;
		uint64_t clk_gen:1;
		uint64_t synce_sel:2;
		uint64_t output_sel:5;
		uint64_t reserved_21_63:42;
#endif
	} s;
	struct cvmx_gpio_bit_cfgx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t fil_sel:4;
		uint64_t fil_cnt:4;
		uint64_t int_type:1;
		uint64_t int_en:1;
		uint64_t rx_xor:1;
		uint64_t tx_oe:1;
#else
		uint64_t tx_oe:1;
		uint64_t rx_xor:1;
		uint64_t int_en:1;
		uint64_t int_type:1;
		uint64_t fil_cnt:4;
		uint64_t fil_sel:4;
		uint64_t reserved_12_63:52;
#endif
	} cn30xx;
	struct cvmx_gpio_bit_cfgx_cn30xx cn31xx;
	struct cvmx_gpio_bit_cfgx_cn30xx cn38xx;
	struct cvmx_gpio_bit_cfgx_cn30xx cn38xxp2;
	struct cvmx_gpio_bit_cfgx_cn30xx cn50xx;
	struct cvmx_gpio_bit_cfgx_cn52xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_15_63:49;
		uint64_t clk_gen:1;
		uint64_t clk_sel:2;
		uint64_t fil_sel:4;
		uint64_t fil_cnt:4;
		uint64_t int_type:1;
		uint64_t int_en:1;
		uint64_t rx_xor:1;
		uint64_t tx_oe:1;
#else
		uint64_t tx_oe:1;
		uint64_t rx_xor:1;
		uint64_t int_en:1;
		uint64_t int_type:1;
		uint64_t fil_cnt:4;
		uint64_t fil_sel:4;
		uint64_t clk_sel:2;
		uint64_t clk_gen:1;
		uint64_t reserved_15_63:49;
#endif
	} cn52xx;
	struct cvmx_gpio_bit_cfgx_cn52xx cn52xxp1;
	struct cvmx_gpio_bit_cfgx_cn52xx cn56xx;
	struct cvmx_gpio_bit_cfgx_cn52xx cn56xxp1;
	struct cvmx_gpio_bit_cfgx_cn30xx cn58xx;
	struct cvmx_gpio_bit_cfgx_cn30xx cn58xxp1;
	struct cvmx_gpio_bit_cfgx_s cn61xx;
	struct cvmx_gpio_bit_cfgx_s cn63xx;
	struct cvmx_gpio_bit_cfgx_s cn63xxp1;
	struct cvmx_gpio_bit_cfgx_s cn66xx;
	struct cvmx_gpio_bit_cfgx_s cn68xx;
	struct cvmx_gpio_bit_cfgx_s cn68xxp1;
	struct cvmx_gpio_bit_cfgx_s cn70xx;
	struct cvmx_gpio_bit_cfgx_s cn73xx;
	struct cvmx_gpio_bit_cfgx_s cnf71xx;
};

union cvmx_gpio_boot_ena {
	uint64_t u64;
	struct cvmx_gpio_boot_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t boot_ena:4;
		uint64_t reserved_0_7:8;
#else
		uint64_t reserved_0_7:8;
		uint64_t boot_ena:4;
		uint64_t reserved_12_63:52;
#endif
	} s;
	struct cvmx_gpio_boot_ena_s cn30xx;
	struct cvmx_gpio_boot_ena_s cn31xx;
	struct cvmx_gpio_boot_ena_s cn50xx;
};

union cvmx_gpio_clk_genx {
	uint64_t u64;
	struct cvmx_gpio_clk_genx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t n:32;
#else
		uint64_t n:32;
		uint64_t reserved_32_63:32;
#endif
	} s;
	struct cvmx_gpio_clk_genx_s cn52xx;
	struct cvmx_gpio_clk_genx_s cn52xxp1;
	struct cvmx_gpio_clk_genx_s cn56xx;
	struct cvmx_gpio_clk_genx_s cn56xxp1;
	struct cvmx_gpio_clk_genx_s cn61xx;
	struct cvmx_gpio_clk_genx_s cn63xx;
	struct cvmx_gpio_clk_genx_s cn63xxp1;
	struct cvmx_gpio_clk_genx_s cn66xx;
	struct cvmx_gpio_clk_genx_s cn68xx;
	struct cvmx_gpio_clk_genx_s cn68xxp1;
	struct cvmx_gpio_clk_genx_s cnf71xx;
};

union cvmx_gpio_clk_qlmx {
	uint64_t u64;
	struct cvmx_gpio_clk_qlmx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_11_63:53;
		uint64_t qlm_sel:3;
		uint64_t reserved_3_7:5;
		uint64_t div:1;
		uint64_t lane_sel:2;
#else
		uint64_t lane_sel:2;
		uint64_t div:1;
		uint64_t reserved_3_7:5;
		uint64_t qlm_sel:3;
		uint64_t reserved_11_63:53;
#endif
	} s;
	struct cvmx_gpio_clk_qlmx_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t qlm_sel:2;
		uint64_t reserved_3_7:5;
		uint64_t div:1;
		uint64_t lane_sel:2;
#else
		uint64_t lane_sel:2;
		uint64_t div:1;
		uint64_t reserved_3_7:5;
		uint64_t qlm_sel:2;
		uint64_t reserved_10_63:54;
#endif
	} cn61xx;
	struct cvmx_gpio_clk_qlmx_cn63xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_3_63:61;
		uint64_t div:1;
		uint64_t lane_sel:2;
#else
		uint64_t lane_sel:2;
		uint64_t div:1;
		uint64_t reserved_3_63:61;
#endif
	} cn63xx;
	struct cvmx_gpio_clk_qlmx_cn63xx cn63xxp1;
	struct cvmx_gpio_clk_qlmx_cn61xx cn66xx;
	struct cvmx_gpio_clk_qlmx_s cn68xx;
	struct cvmx_gpio_clk_qlmx_s cn68xxp1;
	struct cvmx_gpio_clk_qlmx_cn61xx cnf71xx;
};

union cvmx_gpio_dbg_ena {
	uint64_t u64;
	struct cvmx_gpio_dbg_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_21_63:43;
		uint64_t dbg_ena:21;
#else
		uint64_t dbg_ena:21;
		uint64_t reserved_21_63:43;
#endif
	} s;
	struct cvmx_gpio_dbg_ena_s cn30xx;
	struct cvmx_gpio_dbg_ena_s cn31xx;
	struct cvmx_gpio_dbg_ena_s cn50xx;
};

union cvmx_gpio_int_clr {
	uint64_t u64;
	struct cvmx_gpio_int_clr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t type:16;
#else
		uint64_t type:16;
		uint64_t reserved_16_63:48;
#endif
	} s;
	struct cvmx_gpio_int_clr_s cn30xx;
	struct cvmx_gpio_int_clr_s cn31xx;
	struct cvmx_gpio_int_clr_s cn38xx;
	struct cvmx_gpio_int_clr_s cn38xxp2;
	struct cvmx_gpio_int_clr_s cn50xx;
	struct cvmx_gpio_int_clr_s cn52xx;
	struct cvmx_gpio_int_clr_s cn52xxp1;
	struct cvmx_gpio_int_clr_s cn56xx;
	struct cvmx_gpio_int_clr_s cn56xxp1;
	struct cvmx_gpio_int_clr_s cn58xx;
	struct cvmx_gpio_int_clr_s cn58xxp1;
	struct cvmx_gpio_int_clr_s cn61xx;
	struct cvmx_gpio_int_clr_s cn63xx;
	struct cvmx_gpio_int_clr_s cn63xxp1;
	struct cvmx_gpio_int_clr_s cn66xx;
	struct cvmx_gpio_int_clr_s cn68xx;
	struct cvmx_gpio_int_clr_s cn68xxp1;
	struct cvmx_gpio_int_clr_s cnf71xx;
};

union cvmx_gpio_multi_cast {
	uint64_t u64;
	struct cvmx_gpio_multi_cast_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
	struct cvmx_gpio_multi_cast_s cn61xx;
	struct cvmx_gpio_multi_cast_s cnf71xx;
};

union cvmx_gpio_pin_ena {
	uint64_t u64;
	struct cvmx_gpio_pin_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t ena19:1;
		uint64_t ena18:1;
		uint64_t reserved_0_17:18;
#else
		uint64_t reserved_0_17:18;
		uint64_t ena18:1;
		uint64_t ena19:1;
		uint64_t reserved_20_63:44;
#endif
	} s;
	struct cvmx_gpio_pin_ena_s cn66xx;
};

union cvmx_gpio_rx_dat {
	uint64_t u64;
	struct cvmx_gpio_rx_dat_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t dat:24;
#else
		uint64_t dat:24;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_gpio_rx_dat_s cn30xx;
	struct cvmx_gpio_rx_dat_s cn31xx;
	struct cvmx_gpio_rx_dat_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t dat:16;
#else
		uint64_t dat:16;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_gpio_rx_dat_cn38xx cn38xxp2;
	struct cvmx_gpio_rx_dat_s cn50xx;
	struct cvmx_gpio_rx_dat_cn38xx cn52xx;
	struct cvmx_gpio_rx_dat_cn38xx cn52xxp1;
	struct cvmx_gpio_rx_dat_cn38xx cn56xx;
	struct cvmx_gpio_rx_dat_cn38xx cn56xxp1;
	struct cvmx_gpio_rx_dat_cn38xx cn58xx;
	struct cvmx_gpio_rx_dat_cn38xx cn58xxp1;
	struct cvmx_gpio_rx_dat_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t dat:20;
#else
		uint64_t dat:20;
		uint64_t reserved_20_63:44;
#endif
	} cn61xx;
	struct cvmx_gpio_rx_dat_cn38xx cn63xx;
	struct cvmx_gpio_rx_dat_cn38xx cn63xxp1;
	struct cvmx_gpio_rx_dat_cn61xx cn66xx;
	struct cvmx_gpio_rx_dat_cn38xx cn68xx;
	struct cvmx_gpio_rx_dat_cn38xx cn68xxp1;
	struct cvmx_gpio_rx_dat_cn61xx cnf71xx;
};

union cvmx_gpio_tim_ctl {
	uint64_t u64;
	struct cvmx_gpio_tim_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_4_63:60;
		uint64_t sel:4;
#else
		uint64_t sel:4;
		uint64_t reserved_4_63:60;
#endif
	} s;
	struct cvmx_gpio_tim_ctl_s cn68xx;
	struct cvmx_gpio_tim_ctl_s cn68xxp1;
};

union cvmx_gpio_tx_clr {
	uint64_t u64;
	struct cvmx_gpio_tx_clr_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t clr:24;
#else
		uint64_t clr:24;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_gpio_tx_clr_s cn30xx;
	struct cvmx_gpio_tx_clr_s cn31xx;
	struct cvmx_gpio_tx_clr_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t clr:16;
#else
		uint64_t clr:16;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_gpio_tx_clr_cn38xx cn38xxp2;
	struct cvmx_gpio_tx_clr_s cn50xx;
	struct cvmx_gpio_tx_clr_cn38xx cn52xx;
	struct cvmx_gpio_tx_clr_cn38xx cn52xxp1;
	struct cvmx_gpio_tx_clr_cn38xx cn56xx;
	struct cvmx_gpio_tx_clr_cn38xx cn56xxp1;
	struct cvmx_gpio_tx_clr_cn38xx cn58xx;
	struct cvmx_gpio_tx_clr_cn38xx cn58xxp1;
	struct cvmx_gpio_tx_clr_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t clr:20;
#else
		uint64_t clr:20;
		uint64_t reserved_20_63:44;
#endif
	} cn61xx;
	struct cvmx_gpio_tx_clr_cn38xx cn63xx;
	struct cvmx_gpio_tx_clr_cn38xx cn63xxp1;
	struct cvmx_gpio_tx_clr_cn61xx cn66xx;
	struct cvmx_gpio_tx_clr_cn38xx cn68xx;
	struct cvmx_gpio_tx_clr_cn38xx cn68xxp1;
	struct cvmx_gpio_tx_clr_cn61xx cnf71xx;
};

union cvmx_gpio_tx_set {
	uint64_t u64;
	struct cvmx_gpio_tx_set_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_24_63:40;
		uint64_t set:24;
#else
		uint64_t set:24;
		uint64_t reserved_24_63:40;
#endif
	} s;
	struct cvmx_gpio_tx_set_s cn30xx;
	struct cvmx_gpio_tx_set_s cn31xx;
	struct cvmx_gpio_tx_set_cn38xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_16_63:48;
		uint64_t set:16;
#else
		uint64_t set:16;
		uint64_t reserved_16_63:48;
#endif
	} cn38xx;
	struct cvmx_gpio_tx_set_cn38xx cn38xxp2;
	struct cvmx_gpio_tx_set_s cn50xx;
	struct cvmx_gpio_tx_set_cn38xx cn52xx;
	struct cvmx_gpio_tx_set_cn38xx cn52xxp1;
	struct cvmx_gpio_tx_set_cn38xx cn56xx;
	struct cvmx_gpio_tx_set_cn38xx cn56xxp1;
	struct cvmx_gpio_tx_set_cn38xx cn58xx;
	struct cvmx_gpio_tx_set_cn38xx cn58xxp1;
	struct cvmx_gpio_tx_set_cn61xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t set:20;
#else
		uint64_t set:20;
		uint64_t reserved_20_63:44;
#endif
	} cn61xx;
	struct cvmx_gpio_tx_set_cn38xx cn63xx;
	struct cvmx_gpio_tx_set_cn38xx cn63xxp1;
	struct cvmx_gpio_tx_set_cn61xx cn66xx;
	struct cvmx_gpio_tx_set_cn38xx cn68xx;
	struct cvmx_gpio_tx_set_cn38xx cn68xxp1;
	struct cvmx_gpio_tx_set_cn61xx cnf71xx;
};

union cvmx_gpio_xbit_cfgx {
	uint64_t u64;
	struct cvmx_gpio_xbit_cfgx_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_17_63:47;
		uint64_t synce_sel:2;
		uint64_t clk_gen:1;
		uint64_t clk_sel:2;
		uint64_t fil_sel:4;
		uint64_t fil_cnt:4;
		uint64_t int_type:1;
		uint64_t int_en:1;
		uint64_t rx_xor:1;
		uint64_t tx_oe:1;
#else
		uint64_t tx_oe:1;
		uint64_t rx_xor:1;
		uint64_t int_en:1;
		uint64_t int_type:1;
		uint64_t fil_cnt:4;
		uint64_t fil_sel:4;
		uint64_t clk_sel:2;
		uint64_t clk_gen:1;
		uint64_t synce_sel:2;
		uint64_t reserved_17_63:47;
#endif
	} s;
	struct cvmx_gpio_xbit_cfgx_cn30xx {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_12_63:52;
		uint64_t fil_sel:4;
		uint64_t fil_cnt:4;
		uint64_t reserved_2_3:2;
		uint64_t rx_xor:1;
		uint64_t tx_oe:1;
#else
		uint64_t tx_oe:1;
		uint64_t rx_xor:1;
		uint64_t reserved_2_3:2;
		uint64_t fil_cnt:4;
		uint64_t fil_sel:4;
		uint64_t reserved_12_63:52;
#endif
	} cn30xx;
	struct cvmx_gpio_xbit_cfgx_cn30xx cn31xx;
	struct cvmx_gpio_xbit_cfgx_cn30xx cn50xx;
	struct cvmx_gpio_xbit_cfgx_s cn61xx;
	struct cvmx_gpio_xbit_cfgx_s cn66xx;
	struct cvmx_gpio_xbit_cfgx_s cnf71xx;
};

#endif
