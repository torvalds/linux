/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright(c) 2015 EZchip Technologies.
 */

#ifndef _PLAT_EZNPS_CTOP_H
#define _PLAT_EZNPS_CTOP_H

#ifndef CONFIG_ARC_PLAT_EZNPS
#error "Incorrect ctop.h include"
#endif

#include <linux/bits.h>
#include <linux/types.h>
#include <soc/nps/common.h>

/* core auxiliary registers */
#ifdef __ASSEMBLY__
#define CTOP_AUX_BASE				(-0x800)
#else
#define CTOP_AUX_BASE				0xFFFFF800
#endif

#define CTOP_AUX_GLOBAL_ID			(CTOP_AUX_BASE + 0x000)
#define CTOP_AUX_CLUSTER_ID			(CTOP_AUX_BASE + 0x004)
#define CTOP_AUX_CORE_ID			(CTOP_AUX_BASE + 0x008)
#define CTOP_AUX_THREAD_ID			(CTOP_AUX_BASE + 0x00C)
#define CTOP_AUX_LOGIC_GLOBAL_ID		(CTOP_AUX_BASE + 0x010)
#define CTOP_AUX_LOGIC_CLUSTER_ID		(CTOP_AUX_BASE + 0x014)
#define CTOP_AUX_LOGIC_CORE_ID			(CTOP_AUX_BASE + 0x018)
#define CTOP_AUX_MT_CTRL			(CTOP_AUX_BASE + 0x020)
#define CTOP_AUX_HW_COMPLY			(CTOP_AUX_BASE + 0x024)
#define CTOP_AUX_DPC				(CTOP_AUX_BASE + 0x02C)
#define CTOP_AUX_LPC				(CTOP_AUX_BASE + 0x030)
#define CTOP_AUX_EFLAGS				(CTOP_AUX_BASE + 0x080)
#define CTOP_AUX_GPA1				(CTOP_AUX_BASE + 0x08C)
#define CTOP_AUX_UDMC				(CTOP_AUX_BASE + 0x300)

/* EZchip core instructions */
#define CTOP_INST_HWSCHD_WFT_IE12		0x3E6F7344
#define CTOP_INST_HWSCHD_OFF_R4			0x3C6F00BF
#define CTOP_INST_HWSCHD_RESTORE_R4		0x3E6F7103
#define CTOP_INST_SCHD_RW			0x3E6F7004
#define CTOP_INST_SCHD_RD			0x3E6F7084
#define CTOP_INST_ASRI_0_R3			0x3B56003E
#define CTOP_INST_XEX_DI_R2_R2_R3		0x4A664C00
#define CTOP_INST_EXC_DI_R2_R2_R3		0x4A664C01
#define CTOP_INST_AADD_DI_R2_R2_R3		0x4A664C02
#define CTOP_INST_AAND_DI_R2_R2_R3		0x4A664C04
#define CTOP_INST_AOR_DI_R2_R2_R3		0x4A664C05
#define CTOP_INST_AXOR_DI_R2_R2_R3		0x4A664C06

/* Do not use D$ for address in 2G-3G */
#define HW_COMPLY_KRN_NOT_D_CACHED		BIT(28)

#define NPS_MSU_EN_CFG				0x80
#define NPS_CRG_BLKID				0x480
#define NPS_CRG_SYNC_BIT			BIT(0)
#define NPS_GIM_BLKID				0x5C0

/* GIM registers and fields*/
#define NPS_GIM_UART_LINE			BIT(7)
#define NPS_GIM_DBG_LAN_EAST_TX_DONE_LINE	BIT(10)
#define NPS_GIM_DBG_LAN_EAST_RX_RDY_LINE	BIT(11)
#define NPS_GIM_DBG_LAN_WEST_TX_DONE_LINE	BIT(25)
#define NPS_GIM_DBG_LAN_WEST_RX_RDY_LINE	BIT(26)

#ifndef __ASSEMBLY__
/* Functional registers definition */
struct nps_host_reg_mtm_cfg {
	union {
		struct {
			u32 gen:1, gdis:1, clk_gate_dis:1, asb:1,
			__reserved:9, nat:3, ten:16;
		};
		u32 value;
	};
};

struct nps_host_reg_mtm_cpu_cfg {
	union {
		struct {
			u32 csa:22, dmsid:6, __reserved:3, cs:1;
		};
		u32 value;
	};
};

struct nps_host_reg_thr_init {
	union {
		struct {
			u32 str:1, __reserved:27, thr_id:4;
		};
		u32 value;
	};
};

struct nps_host_reg_thr_init_sts {
	union {
		struct {
			u32 bsy:1, err:1, __reserved:26, thr_id:4;
		};
		u32 value;
	};
};

struct nps_host_reg_msu_en_cfg {
	union {
		struct {
			u32     __reserved1:11,
			rtc_en:1, ipc_en:1, gim_1_en:1,
			gim_0_en:1, ipi_en:1, buff_e_rls_bmuw:1,
			buff_e_alc_bmuw:1, buff_i_rls_bmuw:1, buff_i_alc_bmuw:1,
			buff_e_rls_bmue:1, buff_e_alc_bmue:1, buff_i_rls_bmue:1,
			buff_i_alc_bmue:1, __reserved2:1, buff_e_pre_en:1,
			buff_i_pre_en:1, pmuw_ja_en:1, pmue_ja_en:1,
			pmuw_nj_en:1, pmue_nj_en:1, msu_en:1;
		};
		u32 value;
	};
};

struct nps_host_reg_gim_p_int_dst {
	union {
		struct {
			u32 int_out_en:1, __reserved1:4,
			is:1, intm:2, __reserved2:4,
			nid:4, __reserved3:4, cid:4,
			 __reserved4:4, tid:4;
		};
		u32 value;
	};
};

/* AUX registers definition */
struct nps_host_reg_aux_dpc {
	union {
		struct {
			u32 ien:1, men:1, hen:1, reserved:29;
		};
		u32 value;
	};
};

struct nps_host_reg_aux_udmc {
	union {
		struct {
			u32 dcp:1, cme:1, __reserved:19, nat:3,
			__reserved2:5, dcas:3;
		};
		u32 value;
	};
};

struct nps_host_reg_aux_mt_ctrl {
	union {
		struct {
			u32 mten:1, hsen:1, scd:1, sten:1,
			st_cnt:8, __reserved:8,
			hs_cnt:8, __reserved1:4;
		};
		u32 value;
	};
};

struct nps_host_reg_aux_hw_comply {
	union {
		struct {
			u32 me:1, le:1, te:1, knc:1, __reserved:28;
		};
		u32 value;
	};
};

struct nps_host_reg_aux_lpc {
	union {
		struct {
			u32 mep:1, __reserved:31;
		};
		u32 value;
	};
};

/* CRG registers */
#define REG_GEN_PURP_0          nps_host_reg_non_cl(NPS_CRG_BLKID, 0x1BF)

/* GIM registers */
#define REG_GIM_P_INT_EN_0      nps_host_reg_non_cl(NPS_GIM_BLKID, 0x100)
#define REG_GIM_P_INT_POL_0     nps_host_reg_non_cl(NPS_GIM_BLKID, 0x110)
#define REG_GIM_P_INT_SENS_0    nps_host_reg_non_cl(NPS_GIM_BLKID, 0x114)
#define REG_GIM_P_INT_BLK_0     nps_host_reg_non_cl(NPS_GIM_BLKID, 0x118)
#define REG_GIM_P_INT_DST_10    nps_host_reg_non_cl(NPS_GIM_BLKID, 0x13A)
#define REG_GIM_P_INT_DST_11    nps_host_reg_non_cl(NPS_GIM_BLKID, 0x13B)
#define REG_GIM_P_INT_DST_25    nps_host_reg_non_cl(NPS_GIM_BLKID, 0x149)
#define REG_GIM_P_INT_DST_26    nps_host_reg_non_cl(NPS_GIM_BLKID, 0x14A)

#else

.macro  GET_CPU_ID  reg
	lr  \reg, [CTOP_AUX_LOGIC_GLOBAL_ID]
#ifndef CONFIG_EZNPS_MTM_EXT
	lsr \reg, \reg, 4
#endif
.endm

#endif /* __ASSEMBLY__ */

#endif /* _PLAT_EZNPS_CTOP_H */
