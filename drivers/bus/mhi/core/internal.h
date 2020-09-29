/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 *
 */

#ifndef _MHI_INT_H
#define _MHI_INT_H

#include <linux/mhi.h>

extern struct bus_type mhi_bus_type;

#define MHIREGLEN (0x0)
#define MHIREGLEN_MHIREGLEN_MASK (0xFFFFFFFF)
#define MHIREGLEN_MHIREGLEN_SHIFT (0)

#define MHIVER (0x8)
#define MHIVER_MHIVER_MASK (0xFFFFFFFF)
#define MHIVER_MHIVER_SHIFT (0)

#define MHICFG (0x10)
#define MHICFG_NHWER_MASK (0xFF000000)
#define MHICFG_NHWER_SHIFT (24)
#define MHICFG_NER_MASK (0xFF0000)
#define MHICFG_NER_SHIFT (16)
#define MHICFG_NHWCH_MASK (0xFF00)
#define MHICFG_NHWCH_SHIFT (8)
#define MHICFG_NCH_MASK (0xFF)
#define MHICFG_NCH_SHIFT (0)

#define CHDBOFF (0x18)
#define CHDBOFF_CHDBOFF_MASK (0xFFFFFFFF)
#define CHDBOFF_CHDBOFF_SHIFT (0)

#define ERDBOFF (0x20)
#define ERDBOFF_ERDBOFF_MASK (0xFFFFFFFF)
#define ERDBOFF_ERDBOFF_SHIFT (0)

#define BHIOFF (0x28)
#define BHIOFF_BHIOFF_MASK (0xFFFFFFFF)
#define BHIOFF_BHIOFF_SHIFT (0)

#define BHIEOFF (0x2C)
#define BHIEOFF_BHIEOFF_MASK (0xFFFFFFFF)
#define BHIEOFF_BHIEOFF_SHIFT (0)

#define DEBUGOFF (0x30)
#define DEBUGOFF_DEBUGOFF_MASK (0xFFFFFFFF)
#define DEBUGOFF_DEBUGOFF_SHIFT (0)

#define MHICTRL (0x38)
#define MHICTRL_MHISTATE_MASK (0x0000FF00)
#define MHICTRL_MHISTATE_SHIFT (8)
#define MHICTRL_RESET_MASK (0x2)
#define MHICTRL_RESET_SHIFT (1)

#define MHISTATUS (0x48)
#define MHISTATUS_MHISTATE_MASK (0x0000FF00)
#define MHISTATUS_MHISTATE_SHIFT (8)
#define MHISTATUS_SYSERR_MASK (0x4)
#define MHISTATUS_SYSERR_SHIFT (2)
#define MHISTATUS_READY_MASK (0x1)
#define MHISTATUS_READY_SHIFT (0)

#define CCABAP_LOWER (0x58)
#define CCABAP_LOWER_CCABAP_LOWER_MASK (0xFFFFFFFF)
#define CCABAP_LOWER_CCABAP_LOWER_SHIFT (0)

#define CCABAP_HIGHER (0x5C)
#define CCABAP_HIGHER_CCABAP_HIGHER_MASK (0xFFFFFFFF)
#define CCABAP_HIGHER_CCABAP_HIGHER_SHIFT (0)

#define ECABAP_LOWER (0x60)
#define ECABAP_LOWER_ECABAP_LOWER_MASK (0xFFFFFFFF)
#define ECABAP_LOWER_ECABAP_LOWER_SHIFT (0)

#define ECABAP_HIGHER (0x64)
#define ECABAP_HIGHER_ECABAP_HIGHER_MASK (0xFFFFFFFF)
#define ECABAP_HIGHER_ECABAP_HIGHER_SHIFT (0)

#define CRCBAP_LOWER (0x68)
#define CRCBAP_LOWER_CRCBAP_LOWER_MASK (0xFFFFFFFF)
#define CRCBAP_LOWER_CRCBAP_LOWER_SHIFT (0)

#define CRCBAP_HIGHER (0x6C)
#define CRCBAP_HIGHER_CRCBAP_HIGHER_MASK (0xFFFFFFFF)
#define CRCBAP_HIGHER_CRCBAP_HIGHER_SHIFT (0)

#define CRDB_LOWER (0x70)
#define CRDB_LOWER_CRDB_LOWER_MASK (0xFFFFFFFF)
#define CRDB_LOWER_CRDB_LOWER_SHIFT (0)

#define CRDB_HIGHER (0x74)
#define CRDB_HIGHER_CRDB_HIGHER_MASK (0xFFFFFFFF)
#define CRDB_HIGHER_CRDB_HIGHER_SHIFT (0)

#define MHICTRLBASE_LOWER (0x80)
#define MHICTRLBASE_LOWER_MHICTRLBASE_LOWER_MASK (0xFFFFFFFF)
#define MHICTRLBASE_LOWER_MHICTRLBASE_LOWER_SHIFT (0)

#define MHICTRLBASE_HIGHER (0x84)
#define MHICTRLBASE_HIGHER_MHICTRLBASE_HIGHER_MASK (0xFFFFFFFF)
#define MHICTRLBASE_HIGHER_MHICTRLBASE_HIGHER_SHIFT (0)

#define MHICTRLLIMIT_LOWER (0x88)
#define MHICTRLLIMIT_LOWER_MHICTRLLIMIT_LOWER_MASK (0xFFFFFFFF)
#define MHICTRLLIMIT_LOWER_MHICTRLLIMIT_LOWER_SHIFT (0)

#define MHICTRLLIMIT_HIGHER (0x8C)
#define MHICTRLLIMIT_HIGHER_MHICTRLLIMIT_HIGHER_MASK (0xFFFFFFFF)
#define MHICTRLLIMIT_HIGHER_MHICTRLLIMIT_HIGHER_SHIFT (0)

#define MHIDATABASE_LOWER (0x98)
#define MHIDATABASE_LOWER_MHIDATABASE_LOWER_MASK (0xFFFFFFFF)
#define MHIDATABASE_LOWER_MHIDATABASE_LOWER_SHIFT (0)

#define MHIDATABASE_HIGHER (0x9C)
#define MHIDATABASE_HIGHER_MHIDATABASE_HIGHER_MASK (0xFFFFFFFF)
#define MHIDATABASE_HIGHER_MHIDATABASE_HIGHER_SHIFT (0)

#define MHIDATALIMIT_LOWER (0xA0)
#define MHIDATALIMIT_LOWER_MHIDATALIMIT_LOWER_MASK (0xFFFFFFFF)
#define MHIDATALIMIT_LOWER_MHIDATALIMIT_LOWER_SHIFT (0)

#define MHIDATALIMIT_HIGHER (0xA4)
#define MHIDATALIMIT_HIGHER_MHIDATALIMIT_HIGHER_MASK (0xFFFFFFFF)
#define MHIDATALIMIT_HIGHER_MHIDATALIMIT_HIGHER_SHIFT (0)

/* Host request register */
#define MHI_SOC_RESET_REQ_OFFSET (0xB0)
#define MHI_SOC_RESET_REQ BIT(0)

/* MHI BHI offfsets */
#define BHI_BHIVERSION_MINOR (0x00)
#define BHI_BHIVERSION_MAJOR (0x04)
#define BHI_IMGADDR_LOW (0x08)
#define BHI_IMGADDR_HIGH (0x0C)
#define BHI_IMGSIZE (0x10)
#define BHI_RSVD1 (0x14)
#define BHI_IMGTXDB (0x18)
#define BHI_TXDB_SEQNUM_BMSK (0x3FFFFFFF)
#define BHI_TXDB_SEQNUM_SHFT (0)
#define BHI_RSVD2 (0x1C)
#define BHI_INTVEC (0x20)
#define BHI_RSVD3 (0x24)
#define BHI_EXECENV (0x28)
#define BHI_STATUS (0x2C)
#define BHI_ERRCODE (0x30)
#define BHI_ERRDBG1 (0x34)
#define BHI_ERRDBG2 (0x38)
#define BHI_ERRDBG3 (0x3C)
#define BHI_SERIALNU (0x40)
#define BHI_SBLANTIROLLVER (0x44)
#define BHI_NUMSEG (0x48)
#define BHI_MSMHWID(n) (0x4C + (0x4 * n))
#define BHI_OEMPKHASH(n) (0x64 + (0x4 * n))
#define BHI_RSVD5 (0xC4)
#define BHI_STATUS_MASK (0xC0000000)
#define BHI_STATUS_SHIFT (30)
#define BHI_STATUS_ERROR (3)
#define BHI_STATUS_SUCCESS (2)
#define BHI_STATUS_RESET (0)

/* MHI BHIE offsets */
#define BHIE_MSMSOCID_OFFS (0x0000)
#define BHIE_TXVECADDR_LOW_OFFS (0x002C)
#define BHIE_TXVECADDR_HIGH_OFFS (0x0030)
#define BHIE_TXVECSIZE_OFFS (0x0034)
#define BHIE_TXVECDB_OFFS (0x003C)
#define BHIE_TXVECDB_SEQNUM_BMSK (0x3FFFFFFF)
#define BHIE_TXVECDB_SEQNUM_SHFT (0)
#define BHIE_TXVECSTATUS_OFFS (0x0044)
#define BHIE_TXVECSTATUS_SEQNUM_BMSK (0x3FFFFFFF)
#define BHIE_TXVECSTATUS_SEQNUM_SHFT (0)
#define BHIE_TXVECSTATUS_STATUS_BMSK (0xC0000000)
#define BHIE_TXVECSTATUS_STATUS_SHFT (30)
#define BHIE_TXVECSTATUS_STATUS_RESET (0x00)
#define BHIE_TXVECSTATUS_STATUS_XFER_COMPL (0x02)
#define BHIE_TXVECSTATUS_STATUS_ERROR (0x03)
#define BHIE_RXVECADDR_LOW_OFFS (0x0060)
#define BHIE_RXVECADDR_HIGH_OFFS (0x0064)
#define BHIE_RXVECSIZE_OFFS (0x0068)
#define BHIE_RXVECDB_OFFS (0x0070)
#define BHIE_RXVECDB_SEQNUM_BMSK (0x3FFFFFFF)
#define BHIE_RXVECDB_SEQNUM_SHFT (0)
#define BHIE_RXVECSTATUS_OFFS (0x0078)
#define BHIE_RXVECSTATUS_SEQNUM_BMSK (0x3FFFFFFF)
#define BHIE_RXVECSTATUS_SEQNUM_SHFT (0)
#define BHIE_RXVECSTATUS_STATUS_BMSK (0xC0000000)
#define BHIE_RXVECSTATUS_STATUS_SHFT (30)
#define BHIE_RXVECSTATUS_STATUS_RESET (0x00)
#define BHIE_RXVECSTATUS_STATUS_XFER_COMPL (0x02)
#define BHIE_RXVECSTATUS_STATUS_ERROR (0x03)

#define SOC_HW_VERSION_OFFS (0x224)
#define SOC_HW_VERSION_FAM_NUM_BMSK (0xF0000000)
#define SOC_HW_VERSION_FAM_NUM_SHFT (28)
#define SOC_HW_VERSION_DEV_NUM_BMSK (0x0FFF0000)
#define SOC_HW_VERSION_DEV_NUM_SHFT (16)
#define SOC_HW_VERSION_MAJOR_VER_BMSK (0x0000FF00)
#define SOC_HW_VERSION_MAJOR_VER_SHFT (8)
#define SOC_HW_VERSION_MINOR_VER_BMSK (0x000000FF)
#define SOC_HW_VERSION_MINOR_VER_SHFT (0)

#define EV_CTX_RESERVED_MASK GENMASK(7, 0)
#define EV_CTX_INTMODC_MASK GENMASK(15, 8)
#define EV_CTX_INTMODC_SHIFT 8
#define EV_CTX_INTMODT_MASK GENMASK(31, 16)
#define EV_CTX_INTMODT_SHIFT 16
struct mhi_event_ctxt {
	__u32 intmod;
	__u32 ertype;
	__u32 msivec;

	__u64 rbase __packed __aligned(4);
	__u64 rlen __packed __aligned(4);
	__u64 rp __packed __aligned(4);
	__u64 wp __packed __aligned(4);
};

#define CHAN_CTX_CHSTATE_MASK GENMASK(7, 0)
#define CHAN_CTX_CHSTATE_SHIFT 0
#define CHAN_CTX_BRSTMODE_MASK GENMASK(9, 8)
#define CHAN_CTX_BRSTMODE_SHIFT 8
#define CHAN_CTX_POLLCFG_MASK GENMASK(15, 10)
#define CHAN_CTX_POLLCFG_SHIFT 10
#define CHAN_CTX_RESERVED_MASK GENMASK(31, 16)
struct mhi_chan_ctxt {
	__u32 chcfg;
	__u32 chtype;
	__u32 erindex;

	__u64 rbase __packed __aligned(4);
	__u64 rlen __packed __aligned(4);
	__u64 rp __packed __aligned(4);
	__u64 wp __packed __aligned(4);
};

struct mhi_cmd_ctxt {
	__u32 reserved0;
	__u32 reserved1;
	__u32 reserved2;

	__u64 rbase __packed __aligned(4);
	__u64 rlen __packed __aligned(4);
	__u64 rp __packed __aligned(4);
	__u64 wp __packed __aligned(4);
};

struct mhi_ctxt {
	struct mhi_event_ctxt *er_ctxt;
	struct mhi_chan_ctxt *chan_ctxt;
	struct mhi_cmd_ctxt *cmd_ctxt;
	dma_addr_t er_ctxt_addr;
	dma_addr_t chan_ctxt_addr;
	dma_addr_t cmd_ctxt_addr;
};

struct mhi_tre {
	u64 ptr;
	u32 dword[2];
};

struct bhi_vec_entry {
	u64 dma_addr;
	u64 size;
};

enum mhi_cmd_type {
	MHI_CMD_NOP = 1,
	MHI_CMD_RESET_CHAN = 16,
	MHI_CMD_STOP_CHAN = 17,
	MHI_CMD_START_CHAN = 18,
};

/* No operation command */
#define MHI_TRE_CMD_NOOP_PTR (0)
#define MHI_TRE_CMD_NOOP_DWORD0 (0)
#define MHI_TRE_CMD_NOOP_DWORD1 (MHI_CMD_NOP << 16)

/* Channel reset command */
#define MHI_TRE_CMD_RESET_PTR (0)
#define MHI_TRE_CMD_RESET_DWORD0 (0)
#define MHI_TRE_CMD_RESET_DWORD1(chid) ((chid << 24) | \
					(MHI_CMD_RESET_CHAN << 16))

/* Channel stop command */
#define MHI_TRE_CMD_STOP_PTR (0)
#define MHI_TRE_CMD_STOP_DWORD0 (0)
#define MHI_TRE_CMD_STOP_DWORD1(chid) ((chid << 24) | \
				       (MHI_CMD_STOP_CHAN << 16))

/* Channel start command */
#define MHI_TRE_CMD_START_PTR (0)
#define MHI_TRE_CMD_START_DWORD0 (0)
#define MHI_TRE_CMD_START_DWORD1(chid) ((chid << 24) | \
					(MHI_CMD_START_CHAN << 16))

#define MHI_TRE_GET_CMD_CHID(tre) (((tre)->dword[1] >> 24) & 0xFF)
#define MHI_TRE_GET_CMD_TYPE(tre) (((tre)->dword[1] >> 16) & 0xFF)

/* Event descriptor macros */
#define MHI_TRE_EV_PTR(ptr) (ptr)
#define MHI_TRE_EV_DWORD0(code, len) ((code << 24) | len)
#define MHI_TRE_EV_DWORD1(chid, type) ((chid << 24) | (type << 16))
#define MHI_TRE_GET_EV_PTR(tre) ((tre)->ptr)
#define MHI_TRE_GET_EV_CODE(tre) (((tre)->dword[0] >> 24) & 0xFF)
#define MHI_TRE_GET_EV_LEN(tre) ((tre)->dword[0] & 0xFFFF)
#define MHI_TRE_GET_EV_CHID(tre) (((tre)->dword[1] >> 24) & 0xFF)
#define MHI_TRE_GET_EV_TYPE(tre) (((tre)->dword[1] >> 16) & 0xFF)
#define MHI_TRE_GET_EV_STATE(tre) (((tre)->dword[0] >> 24) & 0xFF)
#define MHI_TRE_GET_EV_EXECENV(tre) (((tre)->dword[0] >> 24) & 0xFF)
#define MHI_TRE_GET_EV_SEQ(tre) ((tre)->dword[0])
#define MHI_TRE_GET_EV_TIME(tre) ((tre)->ptr)
#define MHI_TRE_GET_EV_COOKIE(tre) lower_32_bits((tre)->ptr)
#define MHI_TRE_GET_EV_VEID(tre) (((tre)->dword[0] >> 16) & 0xFF)
#define MHI_TRE_GET_EV_LINKSPEED(tre) (((tre)->dword[1] >> 24) & 0xFF)
#define MHI_TRE_GET_EV_LINKWIDTH(tre) ((tre)->dword[0] & 0xFF)

/* Transfer descriptor macros */
#define MHI_TRE_DATA_PTR(ptr) (ptr)
#define MHI_TRE_DATA_DWORD0(len) (len & MHI_MAX_MTU)
#define MHI_TRE_DATA_DWORD1(bei, ieot, ieob, chain) ((2 << 16) | (bei << 10) \
	| (ieot << 9) | (ieob << 8) | chain)

/* RSC transfer descriptor macros */
#define MHI_RSCTRE_DATA_PTR(ptr, len) (((u64)len << 48) | ptr)
#define MHI_RSCTRE_DATA_DWORD0(cookie) (cookie)
#define MHI_RSCTRE_DATA_DWORD1 (MHI_PKT_TYPE_COALESCING << 16)

enum mhi_pkt_type {
	MHI_PKT_TYPE_INVALID = 0x0,
	MHI_PKT_TYPE_NOOP_CMD = 0x1,
	MHI_PKT_TYPE_TRANSFER = 0x2,
	MHI_PKT_TYPE_COALESCING = 0x8,
	MHI_PKT_TYPE_RESET_CHAN_CMD = 0x10,
	MHI_PKT_TYPE_STOP_CHAN_CMD = 0x11,
	MHI_PKT_TYPE_START_CHAN_CMD = 0x12,
	MHI_PKT_TYPE_STATE_CHANGE_EVENT = 0x20,
	MHI_PKT_TYPE_CMD_COMPLETION_EVENT = 0x21,
	MHI_PKT_TYPE_TX_EVENT = 0x22,
	MHI_PKT_TYPE_RSC_TX_EVENT = 0x28,
	MHI_PKT_TYPE_EE_EVENT = 0x40,
	MHI_PKT_TYPE_TSYNC_EVENT = 0x48,
	MHI_PKT_TYPE_BW_REQ_EVENT = 0x50,
	MHI_PKT_TYPE_STALE_EVENT, /* internal event */
};

/* MHI transfer completion events */
enum mhi_ev_ccs {
	MHI_EV_CC_INVALID = 0x0,
	MHI_EV_CC_SUCCESS = 0x1,
	MHI_EV_CC_EOT = 0x2, /* End of transfer event */
	MHI_EV_CC_OVERFLOW = 0x3,
	MHI_EV_CC_EOB = 0x4, /* End of block event */
	MHI_EV_CC_OOB = 0x5, /* Out of block event */
	MHI_EV_CC_DB_MODE = 0x6,
	MHI_EV_CC_UNDEFINED_ERR = 0x10,
	MHI_EV_CC_BAD_TRE = 0x11,
};

enum mhi_ch_state {
	MHI_CH_STATE_DISABLED = 0x0,
	MHI_CH_STATE_ENABLED = 0x1,
	MHI_CH_STATE_RUNNING = 0x2,
	MHI_CH_STATE_SUSPENDED = 0x3,
	MHI_CH_STATE_STOP = 0x4,
	MHI_CH_STATE_ERROR = 0x5,
};

#define MHI_INVALID_BRSTMODE(mode) (mode != MHI_DB_BRST_DISABLE && \
				    mode != MHI_DB_BRST_ENABLE)

extern const char * const mhi_ee_str[MHI_EE_MAX];
#define TO_MHI_EXEC_STR(ee) (((ee) >= MHI_EE_MAX) ? \
			     "INVALID_EE" : mhi_ee_str[ee])

#define MHI_IN_PBL(ee) (ee == MHI_EE_PBL || ee == MHI_EE_PTHRU || \
			ee == MHI_EE_EDL)

#define MHI_IN_MISSION_MODE(ee) (ee == MHI_EE_AMSS || ee == MHI_EE_WFW)

enum dev_st_transition {
	DEV_ST_TRANSITION_PBL,
	DEV_ST_TRANSITION_READY,
	DEV_ST_TRANSITION_SBL,
	DEV_ST_TRANSITION_MISSION_MODE,
	DEV_ST_TRANSITION_SYS_ERR,
	DEV_ST_TRANSITION_DISABLE,
	DEV_ST_TRANSITION_MAX,
};

extern const char * const dev_state_tran_str[DEV_ST_TRANSITION_MAX];
#define TO_DEV_STATE_TRANS_STR(state) (((state) >= DEV_ST_TRANSITION_MAX) ? \
				"INVALID_STATE" : dev_state_tran_str[state])

extern const char * const mhi_state_str[MHI_STATE_MAX];
#define TO_MHI_STATE_STR(state) ((state >= MHI_STATE_MAX || \
				  !mhi_state_str[state]) ? \
				"INVALID_STATE" : mhi_state_str[state])

/* internal power states */
enum mhi_pm_state {
	MHI_PM_STATE_DISABLE,
	MHI_PM_STATE_POR,
	MHI_PM_STATE_M0,
	MHI_PM_STATE_M2,
	MHI_PM_STATE_M3_ENTER,
	MHI_PM_STATE_M3,
	MHI_PM_STATE_M3_EXIT,
	MHI_PM_STATE_FW_DL_ERR,
	MHI_PM_STATE_SYS_ERR_DETECT,
	MHI_PM_STATE_SYS_ERR_PROCESS,
	MHI_PM_STATE_SHUTDOWN_PROCESS,
	MHI_PM_STATE_LD_ERR_FATAL_DETECT,
	MHI_PM_STATE_MAX
};

#define MHI_PM_DISABLE			BIT(0)
#define MHI_PM_POR			BIT(1)
#define MHI_PM_M0			BIT(2)
#define MHI_PM_M2			BIT(3)
#define MHI_PM_M3_ENTER			BIT(4)
#define MHI_PM_M3			BIT(5)
#define MHI_PM_M3_EXIT			BIT(6)
/* firmware download failure state */
#define MHI_PM_FW_DL_ERR		BIT(7)
#define MHI_PM_SYS_ERR_DETECT		BIT(8)
#define MHI_PM_SYS_ERR_PROCESS		BIT(9)
#define MHI_PM_SHUTDOWN_PROCESS		BIT(10)
/* link not accessible */
#define MHI_PM_LD_ERR_FATAL_DETECT	BIT(11)

#define MHI_REG_ACCESS_VALID(pm_state) ((pm_state & (MHI_PM_POR | MHI_PM_M0 | \
		MHI_PM_M2 | MHI_PM_M3_ENTER | MHI_PM_M3_EXIT | \
		MHI_PM_SYS_ERR_DETECT | MHI_PM_SYS_ERR_PROCESS | \
		MHI_PM_SHUTDOWN_PROCESS | MHI_PM_FW_DL_ERR)))
#define MHI_PM_IN_ERROR_STATE(pm_state) (pm_state >= MHI_PM_FW_DL_ERR)
#define MHI_PM_IN_FATAL_STATE(pm_state) (pm_state == MHI_PM_LD_ERR_FATAL_DETECT)
#define MHI_DB_ACCESS_VALID(mhi_cntrl) (mhi_cntrl->pm_state & \
					mhi_cntrl->db_access)
#define MHI_WAKE_DB_CLEAR_VALID(pm_state) (pm_state & (MHI_PM_M0 | \
						MHI_PM_M2 | MHI_PM_M3_EXIT))
#define MHI_WAKE_DB_SET_VALID(pm_state) (pm_state & MHI_PM_M2)
#define MHI_WAKE_DB_FORCE_SET_VALID(pm_state) MHI_WAKE_DB_CLEAR_VALID(pm_state)
#define MHI_EVENT_ACCESS_INVALID(pm_state) (pm_state == MHI_PM_DISABLE || \
					    MHI_PM_IN_ERROR_STATE(pm_state))
#define MHI_PM_IN_SUSPEND_STATE(pm_state) (pm_state & \
					   (MHI_PM_M3_ENTER | MHI_PM_M3))

#define NR_OF_CMD_RINGS			1
#define CMD_EL_PER_RING			128
#define PRIMARY_CMD_RING		0
#define MHI_DEV_WAKE_DB			127
#define MHI_MAX_MTU			0xffff
#define MHI_RANDOM_U32_NONZERO(bmsk)	(prandom_u32_max(bmsk) + 1)

enum mhi_er_type {
	MHI_ER_TYPE_INVALID = 0x0,
	MHI_ER_TYPE_VALID = 0x1,
};

struct db_cfg {
	bool reset_req;
	bool db_mode;
	u32 pollcfg;
	enum mhi_db_brst_mode brstmode;
	dma_addr_t db_val;
	void (*process_db)(struct mhi_controller *mhi_cntrl,
			   struct db_cfg *db_cfg, void __iomem *io_addr,
			   dma_addr_t db_val);
};

struct mhi_pm_transitions {
	enum mhi_pm_state from_state;
	u32 to_states;
};

struct state_transition {
	struct list_head node;
	enum dev_st_transition state;
};

struct mhi_ring {
	dma_addr_t dma_handle;
	dma_addr_t iommu_base;
	u64 *ctxt_wp; /* point to ctxt wp */
	void *pre_aligned;
	void *base;
	void *rp;
	void *wp;
	size_t el_size;
	size_t len;
	size_t elements;
	size_t alloc_size;
	void __iomem *db_addr;
};

struct mhi_cmd {
	struct mhi_ring ring;
	spinlock_t lock;
};

struct mhi_buf_info {
	void *v_addr;
	void *bb_addr;
	void *wp;
	void *cb_buf;
	dma_addr_t p_addr;
	size_t len;
	enum dma_data_direction dir;
	bool used; /* Indicates whether the buffer is used or not */
	bool pre_mapped; /* Already pre-mapped by client */
};

struct mhi_event {
	struct mhi_controller *mhi_cntrl;
	struct mhi_chan *mhi_chan; /* dedicated to channel */
	u32 er_index;
	u32 intmod;
	u32 irq;
	int chan; /* this event ring is dedicated to a channel (optional) */
	u32 priority;
	enum mhi_er_data_type data_type;
	struct mhi_ring ring;
	struct db_cfg db_cfg;
	struct tasklet_struct task;
	spinlock_t lock;
	int (*process_event)(struct mhi_controller *mhi_cntrl,
			     struct mhi_event *mhi_event,
			     u32 event_quota);
	bool hw_ring;
	bool cl_manage;
	bool offload_ev; /* managed by a device driver */
};

struct mhi_chan {
	const char *name;
	/*
	 * Important: When consuming, increment tre_ring first and when
	 * releasing, decrement buf_ring first. If tre_ring has space, buf_ring
	 * is guranteed to have space so we do not need to check both rings.
	 */
	struct mhi_ring buf_ring;
	struct mhi_ring tre_ring;
	u32 chan;
	u32 er_index;
	u32 intmod;
	enum mhi_ch_type type;
	enum dma_data_direction dir;
	struct db_cfg db_cfg;
	enum mhi_ch_ee_mask ee_mask;
	enum mhi_ch_state ch_state;
	enum mhi_ev_ccs ccs;
	struct mhi_device *mhi_dev;
	void (*xfer_cb)(struct mhi_device *mhi_dev, struct mhi_result *result);
	struct mutex mutex;
	struct completion completion;
	rwlock_t lock;
	struct list_head node;
	bool lpm_notify;
	bool configured;
	bool offload_ch;
	bool pre_alloc;
	bool auto_start;
	bool wake_capable;
};

/* Default MHI timeout */
#define MHI_TIMEOUT_MS (1000)

struct mhi_device *mhi_alloc_device(struct mhi_controller *mhi_cntrl);

int mhi_destroy_device(struct device *dev, void *data);
void mhi_create_devices(struct mhi_controller *mhi_cntrl);

int mhi_alloc_bhie_table(struct mhi_controller *mhi_cntrl,
			 struct image_info **image_info, size_t alloc_size);
void mhi_free_bhie_table(struct mhi_controller *mhi_cntrl,
			 struct image_info *image_info);

/* Power management APIs */
enum mhi_pm_state __must_check mhi_tryset_pm_state(
					struct mhi_controller *mhi_cntrl,
					enum mhi_pm_state state);
const char *to_mhi_pm_state_str(enum mhi_pm_state state);
enum mhi_ee_type mhi_get_exec_env(struct mhi_controller *mhi_cntrl);
int mhi_queue_state_transition(struct mhi_controller *mhi_cntrl,
			       enum dev_st_transition state);
void mhi_pm_st_worker(struct work_struct *work);
void mhi_pm_sys_err_handler(struct mhi_controller *mhi_cntrl);
void mhi_fw_load_worker(struct work_struct *work);
int mhi_ready_state_transition(struct mhi_controller *mhi_cntrl);
int mhi_pm_m0_transition(struct mhi_controller *mhi_cntrl);
void mhi_pm_m1_transition(struct mhi_controller *mhi_cntrl);
int mhi_pm_m3_transition(struct mhi_controller *mhi_cntrl);
int __mhi_device_get_sync(struct mhi_controller *mhi_cntrl);
int mhi_send_cmd(struct mhi_controller *mhi_cntrl, struct mhi_chan *mhi_chan,
		 enum mhi_cmd_type cmd);

static inline void mhi_trigger_resume(struct mhi_controller *mhi_cntrl)
{
	pm_wakeup_event(&mhi_cntrl->mhi_dev->dev, 0);
	mhi_cntrl->runtime_get(mhi_cntrl);
	mhi_cntrl->runtime_put(mhi_cntrl);
}

/* Register access methods */
void mhi_db_brstmode(struct mhi_controller *mhi_cntrl, struct db_cfg *db_cfg,
		     void __iomem *db_addr, dma_addr_t db_val);
void mhi_db_brstmode_disable(struct mhi_controller *mhi_cntrl,
			     struct db_cfg *db_mode, void __iomem *db_addr,
			     dma_addr_t db_val);
int __must_check mhi_read_reg(struct mhi_controller *mhi_cntrl,
			      void __iomem *base, u32 offset, u32 *out);
int __must_check mhi_read_reg_field(struct mhi_controller *mhi_cntrl,
				    void __iomem *base, u32 offset, u32 mask,
				    u32 shift, u32 *out);
void mhi_write_reg(struct mhi_controller *mhi_cntrl, void __iomem *base,
		   u32 offset, u32 val);
void mhi_write_reg_field(struct mhi_controller *mhi_cntrl, void __iomem *base,
			 u32 offset, u32 mask, u32 shift, u32 val);
void mhi_ring_er_db(struct mhi_event *mhi_event);
void mhi_write_db(struct mhi_controller *mhi_cntrl, void __iomem *db_addr,
		  dma_addr_t db_val);
void mhi_ring_cmd_db(struct mhi_controller *mhi_cntrl, struct mhi_cmd *mhi_cmd);
void mhi_ring_chan_db(struct mhi_controller *mhi_cntrl,
		      struct mhi_chan *mhi_chan);

/* Initialization methods */
int mhi_init_mmio(struct mhi_controller *mhi_cntrl);
int mhi_init_dev_ctxt(struct mhi_controller *mhi_cntrl);
void mhi_deinit_dev_ctxt(struct mhi_controller *mhi_cntrl);
int mhi_init_irq_setup(struct mhi_controller *mhi_cntrl);
void mhi_deinit_free_irq(struct mhi_controller *mhi_cntrl);
void mhi_rddm_prepare(struct mhi_controller *mhi_cntrl,
		      struct image_info *img_info);
void mhi_fw_load_handler(struct mhi_controller *mhi_cntrl);
int mhi_prepare_channel(struct mhi_controller *mhi_cntrl,
			struct mhi_chan *mhi_chan);
int mhi_init_chan_ctxt(struct mhi_controller *mhi_cntrl,
		       struct mhi_chan *mhi_chan);
void mhi_deinit_chan_ctxt(struct mhi_controller *mhi_cntrl,
			  struct mhi_chan *mhi_chan);
void mhi_reset_chan(struct mhi_controller *mhi_cntrl,
		    struct mhi_chan *mhi_chan);

/* Memory allocation methods */
static inline void *mhi_alloc_coherent(struct mhi_controller *mhi_cntrl,
				       size_t size,
				       dma_addr_t *dma_handle,
				       gfp_t gfp)
{
	void *buf = dma_alloc_coherent(mhi_cntrl->cntrl_dev, size, dma_handle,
				       gfp);

	return buf;
}

static inline void mhi_free_coherent(struct mhi_controller *mhi_cntrl,
				     size_t size,
				     void *vaddr,
				     dma_addr_t dma_handle)
{
	dma_free_coherent(mhi_cntrl->cntrl_dev, size, vaddr, dma_handle);
}

/* Event processing methods */
void mhi_ctrl_ev_task(unsigned long data);
void mhi_ev_task(unsigned long data);
int mhi_process_data_event_ring(struct mhi_controller *mhi_cntrl,
				struct mhi_event *mhi_event, u32 event_quota);
int mhi_process_ctrl_ev_ring(struct mhi_controller *mhi_cntrl,
			     struct mhi_event *mhi_event, u32 event_quota);

/* ISR handlers */
irqreturn_t mhi_irq_handler(int irq_number, void *dev);
irqreturn_t mhi_intvec_threaded_handler(int irq_number, void *dev);
irqreturn_t mhi_intvec_handler(int irq_number, void *dev);

int mhi_gen_tre(struct mhi_controller *mhi_cntrl, struct mhi_chan *mhi_chan,
		struct mhi_buf_info *info, enum mhi_flags flags);
int mhi_map_single_no_bb(struct mhi_controller *mhi_cntrl,
			 struct mhi_buf_info *buf_info);
int mhi_map_single_use_bb(struct mhi_controller *mhi_cntrl,
			  struct mhi_buf_info *buf_info);
void mhi_unmap_single_no_bb(struct mhi_controller *mhi_cntrl,
			    struct mhi_buf_info *buf_info);
void mhi_unmap_single_use_bb(struct mhi_controller *mhi_cntrl,
			     struct mhi_buf_info *buf_info);

#endif /* _MHI_INT_H */
