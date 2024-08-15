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

#ifndef __CVMX_UCTLX_DEFS_H__
#define __CVMX_UCTLX_DEFS_H__

#define CVMX_UCTLX_BIST_STATUS(block_id) (CVMX_ADD_IO_SEG(0x000118006F0000A0ull))
#define CVMX_UCTLX_CLK_RST_CTL(block_id) (CVMX_ADD_IO_SEG(0x000118006F000000ull))
#define CVMX_UCTLX_EHCI_CTL(block_id) (CVMX_ADD_IO_SEG(0x000118006F000080ull))
#define CVMX_UCTLX_EHCI_FLA(block_id) (CVMX_ADD_IO_SEG(0x000118006F0000A8ull))
#define CVMX_UCTLX_ERTO_CTL(block_id) (CVMX_ADD_IO_SEG(0x000118006F000090ull))
#define CVMX_UCTLX_IF_ENA(block_id) (CVMX_ADD_IO_SEG(0x000118006F000030ull))
#define CVMX_UCTLX_INT_ENA(block_id) (CVMX_ADD_IO_SEG(0x000118006F000028ull))
#define CVMX_UCTLX_INT_REG(block_id) (CVMX_ADD_IO_SEG(0x000118006F000020ull))
#define CVMX_UCTLX_OHCI_CTL(block_id) (CVMX_ADD_IO_SEG(0x000118006F000088ull))
#define CVMX_UCTLX_ORTO_CTL(block_id) (CVMX_ADD_IO_SEG(0x000118006F000098ull))
#define CVMX_UCTLX_PPAF_WM(block_id) (CVMX_ADD_IO_SEG(0x000118006F000038ull))
#define CVMX_UCTLX_UPHY_CTL_STATUS(block_id) (CVMX_ADD_IO_SEG(0x000118006F000008ull))
#define CVMX_UCTLX_UPHY_PORTX_CTL_STATUS(offset, block_id) (CVMX_ADD_IO_SEG(0x000118006F000010ull) + (((offset) & 1) + ((block_id) & 0) * 0x0ull) * 8)

union cvmx_uctlx_bist_status {
	uint64_t u64;
	struct cvmx_uctlx_bist_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t data_bis:1;
		uint64_t desc_bis:1;
		uint64_t erbm_bis:1;
		uint64_t orbm_bis:1;
		uint64_t wrbm_bis:1;
		uint64_t ppaf_bis:1;
#else
		uint64_t ppaf_bis:1;
		uint64_t wrbm_bis:1;
		uint64_t orbm_bis:1;
		uint64_t erbm_bis:1;
		uint64_t desc_bis:1;
		uint64_t data_bis:1;
		uint64_t reserved_6_63:58;
#endif
	} s;
};

union cvmx_uctlx_clk_rst_ctl {
	uint64_t u64;
	struct cvmx_uctlx_clk_rst_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_25_63:39;
		uint64_t clear_bist:1;
		uint64_t start_bist:1;
		uint64_t ehci_sm:1;
		uint64_t ohci_clkcktrst:1;
		uint64_t ohci_sm:1;
		uint64_t ohci_susp_lgcy:1;
		uint64_t app_start_clk:1;
		uint64_t o_clkdiv_rst:1;
		uint64_t h_clkdiv_byp:1;
		uint64_t h_clkdiv_rst:1;
		uint64_t h_clkdiv_en:1;
		uint64_t o_clkdiv_en:1;
		uint64_t h_div:4;
		uint64_t p_refclk_sel:2;
		uint64_t p_refclk_div:2;
		uint64_t reserved_4_4:1;
		uint64_t p_com_on:1;
		uint64_t p_por:1;
		uint64_t p_prst:1;
		uint64_t hrst:1;
#else
		uint64_t hrst:1;
		uint64_t p_prst:1;
		uint64_t p_por:1;
		uint64_t p_com_on:1;
		uint64_t reserved_4_4:1;
		uint64_t p_refclk_div:2;
		uint64_t p_refclk_sel:2;
		uint64_t h_div:4;
		uint64_t o_clkdiv_en:1;
		uint64_t h_clkdiv_en:1;
		uint64_t h_clkdiv_rst:1;
		uint64_t h_clkdiv_byp:1;
		uint64_t o_clkdiv_rst:1;
		uint64_t app_start_clk:1;
		uint64_t ohci_susp_lgcy:1;
		uint64_t ohci_sm:1;
		uint64_t ohci_clkcktrst:1;
		uint64_t ehci_sm:1;
		uint64_t start_bist:1;
		uint64_t clear_bist:1;
		uint64_t reserved_25_63:39;
#endif
	} s;
};

union cvmx_uctlx_ehci_ctl {
	uint64_t u64;
	struct cvmx_uctlx_ehci_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_20_63:44;
		uint64_t desc_rbm:1;
		uint64_t reg_nb:1;
		uint64_t l2c_dc:1;
		uint64_t l2c_bc:1;
		uint64_t l2c_0pag:1;
		uint64_t l2c_stt:1;
		uint64_t l2c_buff_emod:2;
		uint64_t l2c_desc_emod:2;
		uint64_t inv_reg_a2:1;
		uint64_t ehci_64b_addr_en:1;
		uint64_t l2c_addr_msb:8;
#else
		uint64_t l2c_addr_msb:8;
		uint64_t ehci_64b_addr_en:1;
		uint64_t inv_reg_a2:1;
		uint64_t l2c_desc_emod:2;
		uint64_t l2c_buff_emod:2;
		uint64_t l2c_stt:1;
		uint64_t l2c_0pag:1;
		uint64_t l2c_bc:1;
		uint64_t l2c_dc:1;
		uint64_t reg_nb:1;
		uint64_t desc_rbm:1;
		uint64_t reserved_20_63:44;
#endif
	} s;
};

union cvmx_uctlx_ehci_fla {
	uint64_t u64;
	struct cvmx_uctlx_ehci_fla_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_6_63:58;
		uint64_t fla:6;
#else
		uint64_t fla:6;
		uint64_t reserved_6_63:58;
#endif
	} s;
};

union cvmx_uctlx_erto_ctl {
	uint64_t u64;
	struct cvmx_uctlx_erto_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t to_val:27;
		uint64_t reserved_0_4:5;
#else
		uint64_t reserved_0_4:5;
		uint64_t to_val:27;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_uctlx_if_ena {
	uint64_t u64;
	struct cvmx_uctlx_if_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_1_63:63;
		uint64_t en:1;
#else
		uint64_t en:1;
		uint64_t reserved_1_63:63;
#endif
	} s;
};

union cvmx_uctlx_int_ena {
	uint64_t u64;
	struct cvmx_uctlx_int_ena_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t ec_ovf_e:1;
		uint64_t oc_ovf_e:1;
		uint64_t wb_pop_e:1;
		uint64_t wb_psh_f:1;
		uint64_t cf_psh_f:1;
		uint64_t or_psh_f:1;
		uint64_t er_psh_f:1;
		uint64_t pp_psh_f:1;
#else
		uint64_t pp_psh_f:1;
		uint64_t er_psh_f:1;
		uint64_t or_psh_f:1;
		uint64_t cf_psh_f:1;
		uint64_t wb_psh_f:1;
		uint64_t wb_pop_e:1;
		uint64_t oc_ovf_e:1;
		uint64_t ec_ovf_e:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
};

union cvmx_uctlx_int_reg {
	uint64_t u64;
	struct cvmx_uctlx_int_reg_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_8_63:56;
		uint64_t ec_ovf_e:1;
		uint64_t oc_ovf_e:1;
		uint64_t wb_pop_e:1;
		uint64_t wb_psh_f:1;
		uint64_t cf_psh_f:1;
		uint64_t or_psh_f:1;
		uint64_t er_psh_f:1;
		uint64_t pp_psh_f:1;
#else
		uint64_t pp_psh_f:1;
		uint64_t er_psh_f:1;
		uint64_t or_psh_f:1;
		uint64_t cf_psh_f:1;
		uint64_t wb_psh_f:1;
		uint64_t wb_pop_e:1;
		uint64_t oc_ovf_e:1;
		uint64_t ec_ovf_e:1;
		uint64_t reserved_8_63:56;
#endif
	} s;
};

union cvmx_uctlx_ohci_ctl {
	uint64_t u64;
	struct cvmx_uctlx_ohci_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_19_63:45;
		uint64_t reg_nb:1;
		uint64_t l2c_dc:1;
		uint64_t l2c_bc:1;
		uint64_t l2c_0pag:1;
		uint64_t l2c_stt:1;
		uint64_t l2c_buff_emod:2;
		uint64_t l2c_desc_emod:2;
		uint64_t inv_reg_a2:1;
		uint64_t reserved_8_8:1;
		uint64_t l2c_addr_msb:8;
#else
		uint64_t l2c_addr_msb:8;
		uint64_t reserved_8_8:1;
		uint64_t inv_reg_a2:1;
		uint64_t l2c_desc_emod:2;
		uint64_t l2c_buff_emod:2;
		uint64_t l2c_stt:1;
		uint64_t l2c_0pag:1;
		uint64_t l2c_bc:1;
		uint64_t l2c_dc:1;
		uint64_t reg_nb:1;
		uint64_t reserved_19_63:45;
#endif
	} s;
};

union cvmx_uctlx_orto_ctl {
	uint64_t u64;
	struct cvmx_uctlx_orto_ctl_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_32_63:32;
		uint64_t to_val:24;
		uint64_t reserved_0_7:8;
#else
		uint64_t reserved_0_7:8;
		uint64_t to_val:24;
		uint64_t reserved_32_63:32;
#endif
	} s;
};

union cvmx_uctlx_ppaf_wm {
	uint64_t u64;
	struct cvmx_uctlx_ppaf_wm_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_5_63:59;
		uint64_t wm:5;
#else
		uint64_t wm:5;
		uint64_t reserved_5_63:59;
#endif
	} s;
};

union cvmx_uctlx_uphy_ctl_status {
	uint64_t u64;
	struct cvmx_uctlx_uphy_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_10_63:54;
		uint64_t bist_done:1;
		uint64_t bist_err:1;
		uint64_t hsbist:1;
		uint64_t fsbist:1;
		uint64_t lsbist:1;
		uint64_t siddq:1;
		uint64_t vtest_en:1;
		uint64_t uphy_bist:1;
		uint64_t bist_en:1;
		uint64_t ate_reset:1;
#else
		uint64_t ate_reset:1;
		uint64_t bist_en:1;
		uint64_t uphy_bist:1;
		uint64_t vtest_en:1;
		uint64_t siddq:1;
		uint64_t lsbist:1;
		uint64_t fsbist:1;
		uint64_t hsbist:1;
		uint64_t bist_err:1;
		uint64_t bist_done:1;
		uint64_t reserved_10_63:54;
#endif
	} s;
};

union cvmx_uctlx_uphy_portx_ctl_status {
	uint64_t u64;
	struct cvmx_uctlx_uphy_portx_ctl_status_s {
#ifdef __BIG_ENDIAN_BITFIELD
		uint64_t reserved_43_63:21;
		uint64_t tdata_out:4;
		uint64_t txbiststuffenh:1;
		uint64_t txbiststuffen:1;
		uint64_t dmpulldown:1;
		uint64_t dppulldown:1;
		uint64_t vbusvldext:1;
		uint64_t portreset:1;
		uint64_t txhsvxtune:2;
		uint64_t txvreftune:4;
		uint64_t txrisetune:1;
		uint64_t txpreemphasistune:1;
		uint64_t txfslstune:4;
		uint64_t sqrxtune:3;
		uint64_t compdistune:3;
		uint64_t loop_en:1;
		uint64_t tclk:1;
		uint64_t tdata_sel:1;
		uint64_t taddr_in:4;
		uint64_t tdata_in:8;
#else
		uint64_t tdata_in:8;
		uint64_t taddr_in:4;
		uint64_t tdata_sel:1;
		uint64_t tclk:1;
		uint64_t loop_en:1;
		uint64_t compdistune:3;
		uint64_t sqrxtune:3;
		uint64_t txfslstune:4;
		uint64_t txpreemphasistune:1;
		uint64_t txrisetune:1;
		uint64_t txvreftune:4;
		uint64_t txhsvxtune:2;
		uint64_t portreset:1;
		uint64_t vbusvldext:1;
		uint64_t dppulldown:1;
		uint64_t dmpulldown:1;
		uint64_t txbiststuffen:1;
		uint64_t txbiststuffenh:1;
		uint64_t tdata_out:4;
		uint64_t reserved_43_63:21;
#endif
	} s;
};

#endif
