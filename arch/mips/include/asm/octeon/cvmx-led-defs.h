/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2008 Cavium Networks
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

#ifndef __CVMX_LED_DEFS_H__
#define __CVMX_LED_DEFS_H__

#define CVMX_LED_BLINK \
	 CVMX_ADD_IO_SEG(0x0001180000001A48ull)
#define CVMX_LED_CLK_PHASE \
	 CVMX_ADD_IO_SEG(0x0001180000001A08ull)
#define CVMX_LED_CYLON \
	 CVMX_ADD_IO_SEG(0x0001180000001AF8ull)
#define CVMX_LED_DBG \
	 CVMX_ADD_IO_SEG(0x0001180000001A18ull)
#define CVMX_LED_EN \
	 CVMX_ADD_IO_SEG(0x0001180000001A00ull)
#define CVMX_LED_POLARITY \
	 CVMX_ADD_IO_SEG(0x0001180000001A50ull)
#define CVMX_LED_PRT \
	 CVMX_ADD_IO_SEG(0x0001180000001A10ull)
#define CVMX_LED_PRT_FMT \
	 CVMX_ADD_IO_SEG(0x0001180000001A30ull)
#define CVMX_LED_PRT_STATUSX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000001A80ull + (((offset) & 7) * 8))
#define CVMX_LED_UDD_CNTX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000001A20ull + (((offset) & 1) * 8))
#define CVMX_LED_UDD_DATX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000001A38ull + (((offset) & 1) * 8))
#define CVMX_LED_UDD_DAT_CLRX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000001AC8ull + (((offset) & 1) * 16))
#define CVMX_LED_UDD_DAT_SETX(offset) \
	 CVMX_ADD_IO_SEG(0x0001180000001AC0ull + (((offset) & 1) * 16))

union cvmx_led_blink {
	uint64_t u64;
	struct cvmx_led_blink_s {
		uint64_t reserved_8_63:56;
		uint64_t rate:8;
	} s;
	struct cvmx_led_blink_s cn38xx;
	struct cvmx_led_blink_s cn38xxp2;
	struct cvmx_led_blink_s cn56xx;
	struct cvmx_led_blink_s cn56xxp1;
	struct cvmx_led_blink_s cn58xx;
	struct cvmx_led_blink_s cn58xxp1;
};

union cvmx_led_clk_phase {
	uint64_t u64;
	struct cvmx_led_clk_phase_s {
		uint64_t reserved_7_63:57;
		uint64_t phase:7;
	} s;
	struct cvmx_led_clk_phase_s cn38xx;
	struct cvmx_led_clk_phase_s cn38xxp2;
	struct cvmx_led_clk_phase_s cn56xx;
	struct cvmx_led_clk_phase_s cn56xxp1;
	struct cvmx_led_clk_phase_s cn58xx;
	struct cvmx_led_clk_phase_s cn58xxp1;
};

union cvmx_led_cylon {
	uint64_t u64;
	struct cvmx_led_cylon_s {
		uint64_t reserved_16_63:48;
		uint64_t rate:16;
	} s;
	struct cvmx_led_cylon_s cn38xx;
	struct cvmx_led_cylon_s cn38xxp2;
	struct cvmx_led_cylon_s cn56xx;
	struct cvmx_led_cylon_s cn56xxp1;
	struct cvmx_led_cylon_s cn58xx;
	struct cvmx_led_cylon_s cn58xxp1;
};

union cvmx_led_dbg {
	uint64_t u64;
	struct cvmx_led_dbg_s {
		uint64_t reserved_1_63:63;
		uint64_t dbg_en:1;
	} s;
	struct cvmx_led_dbg_s cn38xx;
	struct cvmx_led_dbg_s cn38xxp2;
	struct cvmx_led_dbg_s cn56xx;
	struct cvmx_led_dbg_s cn56xxp1;
	struct cvmx_led_dbg_s cn58xx;
	struct cvmx_led_dbg_s cn58xxp1;
};

union cvmx_led_en {
	uint64_t u64;
	struct cvmx_led_en_s {
		uint64_t reserved_1_63:63;
		uint64_t en:1;
	} s;
	struct cvmx_led_en_s cn38xx;
	struct cvmx_led_en_s cn38xxp2;
	struct cvmx_led_en_s cn56xx;
	struct cvmx_led_en_s cn56xxp1;
	struct cvmx_led_en_s cn58xx;
	struct cvmx_led_en_s cn58xxp1;
};

union cvmx_led_polarity {
	uint64_t u64;
	struct cvmx_led_polarity_s {
		uint64_t reserved_1_63:63;
		uint64_t polarity:1;
	} s;
	struct cvmx_led_polarity_s cn38xx;
	struct cvmx_led_polarity_s cn38xxp2;
	struct cvmx_led_polarity_s cn56xx;
	struct cvmx_led_polarity_s cn56xxp1;
	struct cvmx_led_polarity_s cn58xx;
	struct cvmx_led_polarity_s cn58xxp1;
};

union cvmx_led_prt {
	uint64_t u64;
	struct cvmx_led_prt_s {
		uint64_t reserved_8_63:56;
		uint64_t prt_en:8;
	} s;
	struct cvmx_led_prt_s cn38xx;
	struct cvmx_led_prt_s cn38xxp2;
	struct cvmx_led_prt_s cn56xx;
	struct cvmx_led_prt_s cn56xxp1;
	struct cvmx_led_prt_s cn58xx;
	struct cvmx_led_prt_s cn58xxp1;
};

union cvmx_led_prt_fmt {
	uint64_t u64;
	struct cvmx_led_prt_fmt_s {
		uint64_t reserved_4_63:60;
		uint64_t format:4;
	} s;
	struct cvmx_led_prt_fmt_s cn38xx;
	struct cvmx_led_prt_fmt_s cn38xxp2;
	struct cvmx_led_prt_fmt_s cn56xx;
	struct cvmx_led_prt_fmt_s cn56xxp1;
	struct cvmx_led_prt_fmt_s cn58xx;
	struct cvmx_led_prt_fmt_s cn58xxp1;
};

union cvmx_led_prt_statusx {
	uint64_t u64;
	struct cvmx_led_prt_statusx_s {
		uint64_t reserved_6_63:58;
		uint64_t status:6;
	} s;
	struct cvmx_led_prt_statusx_s cn38xx;
	struct cvmx_led_prt_statusx_s cn38xxp2;
	struct cvmx_led_prt_statusx_s cn56xx;
	struct cvmx_led_prt_statusx_s cn56xxp1;
	struct cvmx_led_prt_statusx_s cn58xx;
	struct cvmx_led_prt_statusx_s cn58xxp1;
};

union cvmx_led_udd_cntx {
	uint64_t u64;
	struct cvmx_led_udd_cntx_s {
		uint64_t reserved_6_63:58;
		uint64_t cnt:6;
	} s;
	struct cvmx_led_udd_cntx_s cn38xx;
	struct cvmx_led_udd_cntx_s cn38xxp2;
	struct cvmx_led_udd_cntx_s cn56xx;
	struct cvmx_led_udd_cntx_s cn56xxp1;
	struct cvmx_led_udd_cntx_s cn58xx;
	struct cvmx_led_udd_cntx_s cn58xxp1;
};

union cvmx_led_udd_datx {
	uint64_t u64;
	struct cvmx_led_udd_datx_s {
		uint64_t reserved_32_63:32;
		uint64_t dat:32;
	} s;
	struct cvmx_led_udd_datx_s cn38xx;
	struct cvmx_led_udd_datx_s cn38xxp2;
	struct cvmx_led_udd_datx_s cn56xx;
	struct cvmx_led_udd_datx_s cn56xxp1;
	struct cvmx_led_udd_datx_s cn58xx;
	struct cvmx_led_udd_datx_s cn58xxp1;
};

union cvmx_led_udd_dat_clrx {
	uint64_t u64;
	struct cvmx_led_udd_dat_clrx_s {
		uint64_t reserved_32_63:32;
		uint64_t clr:32;
	} s;
	struct cvmx_led_udd_dat_clrx_s cn38xx;
	struct cvmx_led_udd_dat_clrx_s cn38xxp2;
	struct cvmx_led_udd_dat_clrx_s cn56xx;
	struct cvmx_led_udd_dat_clrx_s cn56xxp1;
	struct cvmx_led_udd_dat_clrx_s cn58xx;
	struct cvmx_led_udd_dat_clrx_s cn58xxp1;
};

union cvmx_led_udd_dat_setx {
	uint64_t u64;
	struct cvmx_led_udd_dat_setx_s {
		uint64_t reserved_32_63:32;
		uint64_t set:32;
	} s;
	struct cvmx_led_udd_dat_setx_s cn38xx;
	struct cvmx_led_udd_dat_setx_s cn38xxp2;
	struct cvmx_led_udd_dat_setx_s cn56xx;
	struct cvmx_led_udd_dat_setx_s cn56xxp1;
	struct cvmx_led_udd_dat_setx_s cn58xx;
	struct cvmx_led_udd_dat_setx_s cn58xxp1;
};

#endif
