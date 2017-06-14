/***********************license start***************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2017 Cavium, Inc.
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

#ifndef __CVMX_PCIERCX_DEFS_H__
#define __CVMX_PCIERCX_DEFS_H__

#include <uapi/asm/bitfield.h>

#define CVMX_PCIERCX_CFG001(block_id) (0x0000000000000004ull)
#define CVMX_PCIERCX_CFG006(block_id) (0x0000000000000018ull)
#define CVMX_PCIERCX_CFG008(block_id) (0x0000000000000020ull)
#define CVMX_PCIERCX_CFG009(block_id) (0x0000000000000024ull)
#define CVMX_PCIERCX_CFG010(block_id) (0x0000000000000028ull)
#define CVMX_PCIERCX_CFG011(block_id) (0x000000000000002Cull)
#define CVMX_PCIERCX_CFG030(block_id) (0x0000000000000078ull)
#define CVMX_PCIERCX_CFG031(block_id) (0x000000000000007Cull)
#define CVMX_PCIERCX_CFG032(block_id) (0x0000000000000080ull)
#define CVMX_PCIERCX_CFG034(block_id) (0x0000000000000088ull)
#define CVMX_PCIERCX_CFG035(block_id) (0x000000000000008Cull)
#define CVMX_PCIERCX_CFG040(block_id) (0x00000000000000A0ull)
#define CVMX_PCIERCX_CFG066(block_id) (0x0000000000000108ull)
#define CVMX_PCIERCX_CFG069(block_id) (0x0000000000000114ull)
#define CVMX_PCIERCX_CFG070(block_id) (0x0000000000000118ull)
#define CVMX_PCIERCX_CFG075(block_id) (0x000000000000012Cull)
#define CVMX_PCIERCX_CFG448(block_id) (0x0000000000000700ull)
#define CVMX_PCIERCX_CFG452(block_id) (0x0000000000000710ull)
#define CVMX_PCIERCX_CFG455(block_id) (0x000000000000071Cull)
#define CVMX_PCIERCX_CFG515(block_id) (0x000000000000080Cull)

union cvmx_pciercx_cfg001 {
	uint32_t u32;
	struct cvmx_pciercx_cfg001_s {
		__BITFIELD_FIELD(uint32_t dpe:1,
		__BITFIELD_FIELD(uint32_t sse:1,
		__BITFIELD_FIELD(uint32_t rma:1,
		__BITFIELD_FIELD(uint32_t rta:1,
		__BITFIELD_FIELD(uint32_t sta:1,
		__BITFIELD_FIELD(uint32_t devt:2,
		__BITFIELD_FIELD(uint32_t mdpe:1,
		__BITFIELD_FIELD(uint32_t fbb:1,
		__BITFIELD_FIELD(uint32_t reserved_22_22:1,
		__BITFIELD_FIELD(uint32_t m66:1,
		__BITFIELD_FIELD(uint32_t cl:1,
		__BITFIELD_FIELD(uint32_t i_stat:1,
		__BITFIELD_FIELD(uint32_t reserved_11_18:8,
		__BITFIELD_FIELD(uint32_t i_dis:1,
		__BITFIELD_FIELD(uint32_t fbbe:1,
		__BITFIELD_FIELD(uint32_t see:1,
		__BITFIELD_FIELD(uint32_t ids_wcc:1,
		__BITFIELD_FIELD(uint32_t per:1,
		__BITFIELD_FIELD(uint32_t vps:1,
		__BITFIELD_FIELD(uint32_t mwice:1,
		__BITFIELD_FIELD(uint32_t scse:1,
		__BITFIELD_FIELD(uint32_t me:1,
		__BITFIELD_FIELD(uint32_t msae:1,
		__BITFIELD_FIELD(uint32_t isae:1,
		;))))))))))))))))))))))))
	} s;
};

union cvmx_pciercx_cfg006 {
	uint32_t u32;
	struct cvmx_pciercx_cfg006_s {
		__BITFIELD_FIELD(uint32_t slt:8,
		__BITFIELD_FIELD(uint32_t subbnum:8,
		__BITFIELD_FIELD(uint32_t sbnum:8,
		__BITFIELD_FIELD(uint32_t pbnum:8,
		;))))
	} s;
};

union cvmx_pciercx_cfg008 {
	uint32_t u32;
	struct cvmx_pciercx_cfg008_s {
		__BITFIELD_FIELD(uint32_t ml_addr:12,
		__BITFIELD_FIELD(uint32_t reserved_16_19:4,
		__BITFIELD_FIELD(uint32_t mb_addr:12,
		__BITFIELD_FIELD(uint32_t reserved_0_3:4,
		;))))
	} s;
};

union cvmx_pciercx_cfg009 {
	uint32_t u32;
	struct cvmx_pciercx_cfg009_s {
		__BITFIELD_FIELD(uint32_t lmem_limit:12,
		__BITFIELD_FIELD(uint32_t reserved_17_19:3,
		__BITFIELD_FIELD(uint32_t mem64b:1,
		__BITFIELD_FIELD(uint32_t lmem_base:12,
		__BITFIELD_FIELD(uint32_t reserved_1_3:3,
		__BITFIELD_FIELD(uint32_t mem64a:1,
		;))))))
	} s;
};

union cvmx_pciercx_cfg010 {
	uint32_t u32;
	struct cvmx_pciercx_cfg010_s {
		uint32_t umem_base;
	} s;
};

union cvmx_pciercx_cfg011 {
	uint32_t u32;
	struct cvmx_pciercx_cfg011_s {
		uint32_t umem_limit;
	} s;
};

union cvmx_pciercx_cfg030 {
	uint32_t u32;
	struct cvmx_pciercx_cfg030_s {
		__BITFIELD_FIELD(uint32_t reserved_22_31:10,
		__BITFIELD_FIELD(uint32_t tp:1,
		__BITFIELD_FIELD(uint32_t ap_d:1,
		__BITFIELD_FIELD(uint32_t ur_d:1,
		__BITFIELD_FIELD(uint32_t fe_d:1,
		__BITFIELD_FIELD(uint32_t nfe_d:1,
		__BITFIELD_FIELD(uint32_t ce_d:1,
		__BITFIELD_FIELD(uint32_t reserved_15_15:1,
		__BITFIELD_FIELD(uint32_t mrrs:3,
		__BITFIELD_FIELD(uint32_t ns_en:1,
		__BITFIELD_FIELD(uint32_t ap_en:1,
		__BITFIELD_FIELD(uint32_t pf_en:1,
		__BITFIELD_FIELD(uint32_t etf_en:1,
		__BITFIELD_FIELD(uint32_t mps:3,
		__BITFIELD_FIELD(uint32_t ro_en:1,
		__BITFIELD_FIELD(uint32_t ur_en:1,
		__BITFIELD_FIELD(uint32_t fe_en:1,
		__BITFIELD_FIELD(uint32_t nfe_en:1,
		__BITFIELD_FIELD(uint32_t ce_en:1,
		;)))))))))))))))))))
	} s;
};

union cvmx_pciercx_cfg031 {
	uint32_t u32;
	struct cvmx_pciercx_cfg031_s {
		__BITFIELD_FIELD(uint32_t pnum:8,
		__BITFIELD_FIELD(uint32_t reserved_23_23:1,
		__BITFIELD_FIELD(uint32_t aspm:1,
		__BITFIELD_FIELD(uint32_t lbnc:1,
		__BITFIELD_FIELD(uint32_t dllarc:1,
		__BITFIELD_FIELD(uint32_t sderc:1,
		__BITFIELD_FIELD(uint32_t cpm:1,
		__BITFIELD_FIELD(uint32_t l1el:3,
		__BITFIELD_FIELD(uint32_t l0el:3,
		__BITFIELD_FIELD(uint32_t aslpms:2,
		__BITFIELD_FIELD(uint32_t mlw:6,
		__BITFIELD_FIELD(uint32_t mls:4,
		;))))))))))))
	} s;
};

union cvmx_pciercx_cfg032 {
	uint32_t u32;
	struct cvmx_pciercx_cfg032_s {
		__BITFIELD_FIELD(uint32_t lab:1,
		__BITFIELD_FIELD(uint32_t lbm:1,
		__BITFIELD_FIELD(uint32_t dlla:1,
		__BITFIELD_FIELD(uint32_t scc:1,
		__BITFIELD_FIELD(uint32_t lt:1,
		__BITFIELD_FIELD(uint32_t reserved_26_26:1,
		__BITFIELD_FIELD(uint32_t nlw:6,
		__BITFIELD_FIELD(uint32_t ls:4,
		__BITFIELD_FIELD(uint32_t reserved_12_15:4,
		__BITFIELD_FIELD(uint32_t lab_int_enb:1,
		__BITFIELD_FIELD(uint32_t lbm_int_enb:1,
		__BITFIELD_FIELD(uint32_t hawd:1,
		__BITFIELD_FIELD(uint32_t ecpm:1,
		__BITFIELD_FIELD(uint32_t es:1,
		__BITFIELD_FIELD(uint32_t ccc:1,
		__BITFIELD_FIELD(uint32_t rl:1,
		__BITFIELD_FIELD(uint32_t ld:1,
		__BITFIELD_FIELD(uint32_t rcb:1,
		__BITFIELD_FIELD(uint32_t reserved_2_2:1,
		__BITFIELD_FIELD(uint32_t aslpc:2,
		;))))))))))))))))))))
	} s;
};

union cvmx_pciercx_cfg034 {
	uint32_t u32;
	struct cvmx_pciercx_cfg034_s {
		__BITFIELD_FIELD(uint32_t reserved_25_31:7,
		__BITFIELD_FIELD(uint32_t dlls_c:1,
		__BITFIELD_FIELD(uint32_t emis:1,
		__BITFIELD_FIELD(uint32_t pds:1,
		__BITFIELD_FIELD(uint32_t mrlss:1,
		__BITFIELD_FIELD(uint32_t ccint_d:1,
		__BITFIELD_FIELD(uint32_t pd_c:1,
		__BITFIELD_FIELD(uint32_t mrls_c:1,
		__BITFIELD_FIELD(uint32_t pf_d:1,
		__BITFIELD_FIELD(uint32_t abp_d:1,
		__BITFIELD_FIELD(uint32_t reserved_13_15:3,
		__BITFIELD_FIELD(uint32_t dlls_en:1,
		__BITFIELD_FIELD(uint32_t emic:1,
		__BITFIELD_FIELD(uint32_t pcc:1,
		__BITFIELD_FIELD(uint32_t pic:1,
		__BITFIELD_FIELD(uint32_t aic:1,
		__BITFIELD_FIELD(uint32_t hpint_en:1,
		__BITFIELD_FIELD(uint32_t ccint_en:1,
		__BITFIELD_FIELD(uint32_t pd_en:1,
		__BITFIELD_FIELD(uint32_t mrls_en:1,
		__BITFIELD_FIELD(uint32_t pf_en:1,
		__BITFIELD_FIELD(uint32_t abp_en:1,
		;))))))))))))))))))))))
	} s;
};

union cvmx_pciercx_cfg035 {
	uint32_t u32;
	struct cvmx_pciercx_cfg035_s {
		__BITFIELD_FIELD(uint32_t reserved_17_31:15,
		__BITFIELD_FIELD(uint32_t crssv:1,
		__BITFIELD_FIELD(uint32_t reserved_5_15:11,
		__BITFIELD_FIELD(uint32_t crssve:1,
		__BITFIELD_FIELD(uint32_t pmeie:1,
		__BITFIELD_FIELD(uint32_t sefee:1,
		__BITFIELD_FIELD(uint32_t senfee:1,
		__BITFIELD_FIELD(uint32_t secee:1,
		;))))))))
	} s;
};

union cvmx_pciercx_cfg040 {
	uint32_t u32;
	struct cvmx_pciercx_cfg040_s {
		__BITFIELD_FIELD(uint32_t reserved_22_31:10,
		__BITFIELD_FIELD(uint32_t ler:1,
		__BITFIELD_FIELD(uint32_t ep3s:1,
		__BITFIELD_FIELD(uint32_t ep2s:1,
		__BITFIELD_FIELD(uint32_t ep1s:1,
		__BITFIELD_FIELD(uint32_t eqc:1,
		__BITFIELD_FIELD(uint32_t cdl:1,
		__BITFIELD_FIELD(uint32_t cde:4,
		__BITFIELD_FIELD(uint32_t csos:1,
		__BITFIELD_FIELD(uint32_t emc:1,
		__BITFIELD_FIELD(uint32_t tm:3,
		__BITFIELD_FIELD(uint32_t sde:1,
		__BITFIELD_FIELD(uint32_t hasd:1,
		__BITFIELD_FIELD(uint32_t ec:1,
		__BITFIELD_FIELD(uint32_t tls:4,
		;)))))))))))))))
	} s;
};

union cvmx_pciercx_cfg070 {
	uint32_t u32;
	struct cvmx_pciercx_cfg070_s {
		__BITFIELD_FIELD(uint32_t reserved_12_31:20,
		__BITFIELD_FIELD(uint32_t tplp:1,
		__BITFIELD_FIELD(uint32_t reserved_9_10:2,
		__BITFIELD_FIELD(uint32_t ce:1,
		__BITFIELD_FIELD(uint32_t cc:1,
		__BITFIELD_FIELD(uint32_t ge:1,
		__BITFIELD_FIELD(uint32_t gc:1,
		__BITFIELD_FIELD(uint32_t fep:5,
		;))))))))
	} s;
};

union cvmx_pciercx_cfg075 {
	uint32_t u32;
	struct cvmx_pciercx_cfg075_s {
		__BITFIELD_FIELD(uint32_t reserved_3_31:29,
		__BITFIELD_FIELD(uint32_t fere:1,
		__BITFIELD_FIELD(uint32_t nfere:1,
		__BITFIELD_FIELD(uint32_t cere:1,
		;))))
	} s;
};

union cvmx_pciercx_cfg448 {
	uint32_t u32;
	struct cvmx_pciercx_cfg448_s {
		__BITFIELD_FIELD(uint32_t rtl:16,
		__BITFIELD_FIELD(uint32_t rtltl:16,
		;))
	} s;
};

union cvmx_pciercx_cfg452 {
	uint32_t u32;
	struct cvmx_pciercx_cfg452_s {
		__BITFIELD_FIELD(uint32_t reserved_26_31:6,
		__BITFIELD_FIELD(uint32_t eccrc:1,
		__BITFIELD_FIELD(uint32_t reserved_22_24:3,
		__BITFIELD_FIELD(uint32_t lme:6,
		__BITFIELD_FIELD(uint32_t reserved_12_15:4,
		__BITFIELD_FIELD(uint32_t link_rate:4,
		__BITFIELD_FIELD(uint32_t flm:1,
		__BITFIELD_FIELD(uint32_t reserved_6_6:1,
		__BITFIELD_FIELD(uint32_t dllle:1,
		__BITFIELD_FIELD(uint32_t reserved_4_4:1,
		__BITFIELD_FIELD(uint32_t ra:1,
		__BITFIELD_FIELD(uint32_t le:1,
		__BITFIELD_FIELD(uint32_t sd:1,
		__BITFIELD_FIELD(uint32_t omr:1,
		;))))))))))))))
	} s;
};

union cvmx_pciercx_cfg455 {
	uint32_t u32;
	struct cvmx_pciercx_cfg455_s {
		__BITFIELD_FIELD(uint32_t m_cfg0_filt:1,
		__BITFIELD_FIELD(uint32_t m_io_filt:1,
		__BITFIELD_FIELD(uint32_t msg_ctrl:1,
		__BITFIELD_FIELD(uint32_t m_cpl_ecrc_filt:1,
		__BITFIELD_FIELD(uint32_t m_ecrc_filt:1,
		__BITFIELD_FIELD(uint32_t m_cpl_len_err:1,
		__BITFIELD_FIELD(uint32_t m_cpl_attr_err:1,
		__BITFIELD_FIELD(uint32_t m_cpl_tc_err:1,
		__BITFIELD_FIELD(uint32_t m_cpl_fun_err:1,
		__BITFIELD_FIELD(uint32_t m_cpl_rid_err:1,
		__BITFIELD_FIELD(uint32_t m_cpl_tag_err:1,
		__BITFIELD_FIELD(uint32_t m_lk_filt:1,
		__BITFIELD_FIELD(uint32_t m_cfg1_filt:1,
		__BITFIELD_FIELD(uint32_t m_bar_match:1,
		__BITFIELD_FIELD(uint32_t m_pois_filt:1,
		__BITFIELD_FIELD(uint32_t m_fun:1,
		__BITFIELD_FIELD(uint32_t dfcwt:1,
		__BITFIELD_FIELD(uint32_t reserved_11_14:4,
		__BITFIELD_FIELD(uint32_t skpiv:11,
		;)))))))))))))))))))
	} s;
};

union cvmx_pciercx_cfg515 {
	uint32_t u32;
	struct cvmx_pciercx_cfg515_s {
		__BITFIELD_FIELD(uint32_t reserved_21_31:11,
		__BITFIELD_FIELD(uint32_t s_d_e:1,
		__BITFIELD_FIELD(uint32_t ctcrb:1,
		__BITFIELD_FIELD(uint32_t cpyts:1,
		__BITFIELD_FIELD(uint32_t dsc:1,
		__BITFIELD_FIELD(uint32_t le:9,
		__BITFIELD_FIELD(uint32_t n_fts:8,
		;)))))))
	} s;
};

#endif
