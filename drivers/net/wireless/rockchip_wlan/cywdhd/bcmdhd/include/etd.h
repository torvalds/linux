/*
 * Extended Trap data component interface file.
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef _ETD_H_
#define _ETD_H_

#if defined(ETD) && !defined(WLETD)
#include <hnd_trap.h>
#endif // endif
#include <bcmutils.h>
/* Tags for structures being used by etd info iovar.
 * Related structures are defined in wlioctl.h.
 */
#define ETD_TAG_JOIN_CLASSIFICATION_INFO 10 /* general information about join request */
#define ETD_TAG_JOIN_TARGET_CLASSIFICATION_INFO 11	/* per target (AP) join information */
#define ETD_TAG_ASSOC_STATE 12 /* current state of the Device association state machine */
#define ETD_TAG_CHANNEL 13	/* current channel on which the association was performed */
#define ETD_TAG_TOTAL_NUM_OF_JOIN_ATTEMPTS 14 /* number of join attempts (bss_retries) */

#define  PSMDBG_REG_READ_CNT_FOR_PSMWDTRAP_V1	3
#define  PSMDBG_REG_READ_CNT_FOR_PSMWDTRAP_V2	6

#ifndef _LANGUAGE_ASSEMBLY

#define HND_EXTENDED_TRAP_VERSION  1
#define HND_EXTENDED_TRAP_BUFLEN   512

typedef struct hnd_ext_trap_hdr {
	uint8 version;    /* Extended trap version info */
	uint8 reserved;   /* currently unused */
	uint16 len;       /* Length of data excluding this header */
	uint8 data[];     /* TLV data */
} hnd_ext_trap_hdr_t;

typedef enum {
	TAG_TRAP_NONE		 = 0,  /* None trap type */
	TAG_TRAP_SIGNATURE       = 1,  /* Processor register dumps */
	TAG_TRAP_STACK           = 2,  /* Processor stack dump (possible code locations) */
	TAG_TRAP_MEMORY          = 3,  /* Memory subsystem dump */
	TAG_TRAP_DEEPSLEEP       = 4,  /* Deep sleep health check failures */
	TAG_TRAP_PSM_WD          = 5,  /* PSM watchdog information */
	TAG_TRAP_PHY             = 6,  /* Phy related issues */
	TAG_TRAP_BUS             = 7,  /* Bus level issues */
	TAG_TRAP_MAC_SUSP        = 8,  /* Mac level suspend issues */
	TAG_TRAP_BACKPLANE       = 9,  /* Backplane related errors */
	/* Values 10 through 14 are in use by etd_data info iovar */
	TAG_TRAP_PCIE_Q         = 15,  /* PCIE Queue state during memory trap */
	TAG_TRAP_WLC_STATE      = 16,  /* WLAN state during memory trap */
	TAG_TRAP_MAC_WAKE       = 17,  /* Mac level wake issues */
	TAG_TRAP_PHYTXERR_THRESH = 18, /* Phy Tx Err */
	TAG_TRAP_HC_DATA        = 19,  /* Data collected by HC module */
	TAG_TRAP_LOG_DATA	= 20,
	TAG_TRAP_CODE		= 21, /* The trap type */
	TAG_TRAP_HMAP		= 22, /* HMAP violation Address and Info */
	TAG_TRAP_PCIE_ERR_ATTN	= 23, /* PCIE error attn log */
	TAG_TRAP_AXI_ERROR	= 24, /* AXI Error */
	TAG_TRAP_AXI_HOST_INFO  = 25, /* AXI Host log */
	TAG_TRAP_AXI_SR_ERROR	= 26, /* AXI SR error log */
	TAG_TRAP_LAST  /* This must be the last entry */
} hnd_ext_tag_trap_t;

typedef struct hnd_ext_trap_bp_err
{
	uint32 error;
	uint32 coreid;
	uint32 baseaddr;
	uint32 ioctrl;
	uint32 iostatus;
	uint32 resetctrl;
	uint32 resetstatus;
	uint32 resetreadid;
	uint32 resetwriteid;
	uint32 errlogctrl;
	uint32 errlogdone;
	uint32 errlogstatus;
	uint32 errlogaddrlo;
	uint32 errlogaddrhi;
	uint32 errlogid;
	uint32 errloguser;
	uint32 errlogflags;
	uint32 itipoobaout;
	uint32 itipoobbout;
	uint32 itipoobcout;
	uint32 itipoobdout;
} hnd_ext_trap_bp_err_t;

#define HND_EXT_TRAP_AXISR_INFO_VER_1	1
typedef struct hnd_ext_trap_axi_sr_err_v1
{
	uint8 version;
	uint8 pad[3];
	uint32 error;
	uint32 coreid;
	uint32 baseaddr;
	uint32 ioctrl;
	uint32 iostatus;
	uint32 resetctrl;
	uint32 resetstatus;
	uint32 resetreadid;
	uint32 resetwriteid;
	uint32 errlogctrl;
	uint32 errlogdone;
	uint32 errlogstatus;
	uint32 errlogaddrlo;
	uint32 errlogaddrhi;
	uint32 errlogid;
	uint32 errloguser;
	uint32 errlogflags;
	uint32 itipoobaout;
	uint32 itipoobbout;
	uint32 itipoobcout;
	uint32 itipoobdout;

	/* axi_sr_issue_debug */
	uint32 sr_pwr_control;
	uint32 sr_corereset_wrapper_main;
	uint32 sr_corereset_wrapper_aux;
	uint32 sr_main_gci_status_0;
	uint32 sr_aux_gci_status_0;
	uint32 sr_dig_gci_status_0;
} hnd_ext_trap_axi_sr_err_v1_t;

#define HND_EXT_TRAP_PSMWD_INFO_VER	1
typedef struct hnd_ext_trap_psmwd_v1 {
	uint16 xtag;
	uint16 version; /* version of the information following this */
	uint32 i32_maccontrol;
	uint32 i32_maccommand;
	uint32 i32_macintstatus;
	uint32 i32_phydebug;
	uint32 i32_clk_ctl_st;
	uint32 i32_psmdebug[PSMDBG_REG_READ_CNT_FOR_PSMWDTRAP_V1];
	uint16 i16_0x1a8; /* gated clock en */
	uint16 i16_0x406; /* Rcv Fifo Ctrl */
	uint16 i16_0x408; /* Rx ctrl 1 */
	uint16 i16_0x41a; /* Rxe Status 1 */
	uint16 i16_0x41c; /* Rxe Status 2 */
	uint16 i16_0x424; /* rcv wrd count 0 */
	uint16 i16_0x426; /* rcv wrd count 1 */
	uint16 i16_0x456; /* RCV_LFIFO_STS */
	uint16 i16_0x480; /* PSM_SLP_TMR */
	uint16 i16_0x490; /* PSM BRC */
	uint16 i16_0x500; /* TXE CTRL */
	uint16 i16_0x50e; /* TXE Status */
	uint16 i16_0x55e; /* TXE_xmtdmabusy */
	uint16 i16_0x566; /* TXE_XMTfifosuspflush */
	uint16 i16_0x690; /* IFS Stat */
	uint16 i16_0x692; /* IFS_MEDBUSY_CTR */
	uint16 i16_0x694; /* IFS_TX_DUR */
	uint16 i16_0x6a0; /* SLow_CTL */
	uint16 i16_0x838; /* TXE_AQM fifo Ready */
	uint16 i16_0x8c0; /* Dagg ctrl */
	uint16 shm_prewds_cnt;
	uint16 shm_txtplufl_cnt;
	uint16 shm_txphyerr_cnt;
	uint16 pad;
} hnd_ext_trap_psmwd_v1_t;

typedef struct hnd_ext_trap_psmwd {
	uint16 xtag;
	uint16 version; /* version of the information following this */
	uint32 i32_maccontrol;
	uint32 i32_maccommand;
	uint32 i32_macintstatus;
	uint32 i32_phydebug;
	uint32 i32_clk_ctl_st;
	uint32 i32_psmdebug[PSMDBG_REG_READ_CNT_FOR_PSMWDTRAP_V2];
	uint16 i16_0x4b8; /* psm_brwk_0 */
	uint16 i16_0x4ba; /* psm_brwk_1 */
	uint16 i16_0x4bc; /* psm_brwk_2 */
	uint16 i16_0x4be; /* psm_brwk_2 */
	uint16 i16_0x1a8; /* gated clock en */
	uint16 i16_0x406; /* Rcv Fifo Ctrl */
	uint16 i16_0x408; /* Rx ctrl 1 */
	uint16 i16_0x41a; /* Rxe Status 1 */
	uint16 i16_0x41c; /* Rxe Status 2 */
	uint16 i16_0x424; /* rcv wrd count 0 */
	uint16 i16_0x426; /* rcv wrd count 1 */
	uint16 i16_0x456; /* RCV_LFIFO_STS */
	uint16 i16_0x480; /* PSM_SLP_TMR */
	uint16 i16_0x500; /* TXE CTRL */
	uint16 i16_0x50e; /* TXE Status */
	uint16 i16_0x55e; /* TXE_xmtdmabusy */
	uint16 i16_0x566; /* TXE_XMTfifosuspflush */
	uint16 i16_0x690; /* IFS Stat */
	uint16 i16_0x692; /* IFS_MEDBUSY_CTR */
	uint16 i16_0x694; /* IFS_TX_DUR */
	uint16 i16_0x6a0; /* SLow_CTL */
	uint16 i16_0x490; /* psm_brc */
	uint16 i16_0x4da; /* psm_brc_1 */
	uint16 i16_0x838; /* TXE_AQM fifo Ready */
	uint16 i16_0x8c0; /* Dagg ctrl */
	uint16 shm_prewds_cnt;
	uint16 shm_txtplufl_cnt;
	uint16 shm_txphyerr_cnt;
} hnd_ext_trap_psmwd_t;

#define HEAP_HISTOGRAM_DUMP_LEN	6
#define HEAP_MAX_SZ_BLKS_LEN	2

/* Ignore chunks for which there are fewer than this many instances, irrespective of size */
#define HEAP_HISTOGRAM_INSTANCE_MIN		4

/*
 * Use the last two length values for chunks larger than this, or when we run out of
 * histogram entries (because we have too many different sized chunks) to store "other"
 */
#define HEAP_HISTOGRAM_SPECIAL	0xfffeu

#define HEAP_HISTOGRAM_GRTR256K	0xffffu

typedef struct hnd_ext_trap_heap_err {
	uint32 arena_total;
	uint32 heap_free;
	uint32 heap_inuse;
	uint32 mf_count;
	uint32 stack_lwm;
	uint16 heap_histogm[HEAP_HISTOGRAM_DUMP_LEN * 2]; /* size/number */
	uint16 max_sz_free_blk[HEAP_MAX_SZ_BLKS_LEN];
} hnd_ext_trap_heap_err_t;

#define MEM_TRAP_NUM_WLC_TX_QUEUES		6
#define HND_EXT_TRAP_WLC_MEM_ERR_VER_V2		2

typedef struct hnd_ext_trap_wlc_mem_err {
	uint8 instance;
	uint8 associated;
	uint8 soft_ap_client_cnt;
	uint8 peer_cnt;
	uint16 txqueue_len[MEM_TRAP_NUM_WLC_TX_QUEUES];
} hnd_ext_trap_wlc_mem_err_t;

typedef struct hnd_ext_trap_wlc_mem_err_v2 {
	uint16 version;
	uint16 pad;
	uint8 instance;
	uint8 stas_associated;
	uint8 aps_associated;
	uint8 soft_ap_client_cnt;
	uint16 txqueue_len[MEM_TRAP_NUM_WLC_TX_QUEUES];
} hnd_ext_trap_wlc_mem_err_v2_t;

#define HND_EXT_TRAP_WLC_MEM_ERR_VER_V3		3

typedef struct hnd_ext_trap_wlc_mem_err_v3 {
	uint8 version;
	uint8 instance;
	uint8 stas_associated;
	uint8 aps_associated;
	uint8 soft_ap_client_cnt;
	uint8 peer_cnt;
	uint16 txqueue_len[MEM_TRAP_NUM_WLC_TX_QUEUES];
} hnd_ext_trap_wlc_mem_err_v3_t;

typedef struct hnd_ext_trap_pcie_mem_err {
	uint16 d2h_queue_len;
	uint16 d2h_req_queue_len;
} hnd_ext_trap_pcie_mem_err_t;

#define MAX_DMAFIFO_ENTRIES_V1			1
#define MAX_DMAFIFO_DESC_ENTRIES_V1		2
#define HND_EXT_TRAP_AXIERROR_SIGNATURE		0xbabebabe
#define HND_EXT_TRAP_AXIERROR_VERSION_1		1

/* Structure to collect debug info of descriptor entry for dma channel on encountering AXI Error */
/* Below three structures are dependant, any change will bump version of all the three */

typedef struct hnd_ext_trap_desc_entry_v1 {
	uint32  ctrl1;   /* descriptor entry at din < misc control bits > */
	uint32  ctrl2;   /* descriptor entry at din <buffer count and address extension> */
	uint32  addrlo;  /* descriptor entry at din <address of data buffer, bits 31:0> */
	uint32  addrhi;  /* descriptor entry at din <address of data buffer, bits 63:32> */
} dma_dentry_v1_t;

/* Structure to collect debug info about a dma channel on encountering AXI Error */
typedef struct hnd_ext_trap_dma_fifo_v1 {
	uint8	valid;		/* no of valid desc entries filled, non zero = fifo entry valid */
	uint8	direction;	/* TX=1, RX=2, currently only using TX */
	uint16	index;		/* Index of the DMA channel in system */
	uint32	dpa;		/* Expected Address of Descriptor table from software state */
	uint32	desc_lo;	/* Low Address of Descriptor table programmed in DMA register */
	uint32	desc_hi;	/* High Address of Descriptor table programmed in DMA register */
	uint16	din;		/* rxin / txin */
	uint16	dout;		/* rxout / txout */
	dma_dentry_v1_t dentry[MAX_DMAFIFO_DESC_ENTRIES_V1]; /* Descriptor Entires */
} dma_fifo_v1_t;

typedef struct hnd_ext_trap_axi_error_v1 {
	uint8 version;			/* version = 1 */
	uint8 dma_fifo_valid_count;	/* Number of valid dma_fifo entries */
	uint16 length;			/* length of whole structure */
	uint32 signature;		/* indicate that its filled with AXI Error data */
	uint32 axi_errorlog_status;	/* errlog_status from slave wrapper */
	uint32 axi_errorlog_core;	/* errlog_core from slave wrapper */
	uint32 axi_errorlog_lo;		/* errlog_lo from slave wrapper */
	uint32 axi_errorlog_hi;		/* errlog_hi from slave wrapper */
	uint32 axi_errorlog_id;		/* errlog_id from slave wrapper */
	dma_fifo_v1_t dma_fifo[MAX_DMAFIFO_ENTRIES_V1];
} hnd_ext_trap_axi_error_v1_t;

#define HND_EXT_TRAP_MACSUSP_INFO_VER	1
typedef struct hnd_ext_trap_macsusp {
	uint16 xtag;
	uint8 version; /* version of the information following this */
	uint8 trap_reason;
	uint32 i32_maccontrol;
	uint32 i32_maccommand;
	uint32 i32_macintstatus;
	uint32 i32_phydebug[4];
	uint32 i32_psmdebug[8];
	uint16 i16_0x41a; /* Rxe Status 1 */
	uint16 i16_0x41c; /* Rxe Status 2 */
	uint16 i16_0x490; /* PSM BRC */
	uint16 i16_0x50e; /* TXE Status */
	uint16 i16_0x55e; /* TXE_xmtdmabusy */
	uint16 i16_0x566; /* TXE_XMTfifosuspflush */
	uint16 i16_0x690; /* IFS Stat */
	uint16 i16_0x692; /* IFS_MEDBUSY_CTR */
	uint16 i16_0x694; /* IFS_TX_DUR */
	uint16 i16_0x7c0; /* WEP CTL */
	uint16 i16_0x838; /* TXE_AQM fifo Ready */
	uint16 i16_0x880; /* MHP_status */
	uint16 shm_prewds_cnt;
	uint16 shm_ucode_dbgst;
} hnd_ext_trap_macsusp_t;

#define HND_EXT_TRAP_MACENAB_INFO_VER	1
typedef struct hnd_ext_trap_macenab {
	uint16 xtag;
	uint8 version; /* version of the information following this */
	uint8 trap_reason;
	uint32 i32_maccontrol;
	uint32 i32_maccommand;
	uint32 i32_macintstatus;
	uint32 i32_psmdebug[8];
	uint32 i32_clk_ctl_st;
	uint32 i32_powerctl;
	uint16 i16_0x1a8; /* gated clock en */
	uint16 i16_0x480; /* PSM_SLP_TMR */
	uint16 i16_0x490; /* PSM BRC */
	uint16 i16_0x600; /* TSF CTL */
	uint16 i16_0x690; /* IFS Stat */
	uint16 i16_0x692; /* IFS_MEDBUSY_CTR */
	uint16 i16_0x6a0; /* SLow_CTL */
	uint16 i16_0x6a6; /* SLow_FRAC */
	uint16 i16_0x6a8; /* fast power up delay */
	uint16 i16_0x6aa; /* SLow_PER */
	uint16 shm_ucode_dbgst;
	uint16 PAD;
} hnd_ext_trap_macenab_t;

#define HND_EXT_TRAP_PHY_INFO_VER_1 (1)
typedef struct hnd_ext_trap_phydbg {
	uint16 err;
	uint16 RxFeStatus;
	uint16 TxFIFOStatus0;
	uint16 TxFIFOStatus1;
	uint16 RfseqMode;
	uint16 RfseqStatus0;
	uint16 RfseqStatus1;
	uint16 RfseqStatus_Ocl;
	uint16 RfseqStatus_Ocl1;
	uint16 OCLControl1;
	uint16 TxError;
	uint16 bphyTxError;
	uint16 TxCCKError;
	uint16 TxCtrlWrd0;
	uint16 TxCtrlWrd1;
	uint16 TxCtrlWrd2;
	uint16 TxLsig0;
	uint16 TxLsig1;
	uint16 TxVhtSigA10;
	uint16 TxVhtSigA11;
	uint16 TxVhtSigA20;
	uint16 TxVhtSigA21;
	uint16 txPktLength;
	uint16 txPsdulengthCtr;
	uint16 gpioClkControl;
	uint16 gpioSel;
	uint16 pktprocdebug;
	uint16 PAD;
	uint32 gpioOut[3];
} hnd_ext_trap_phydbg_t;

/* unique IDs for separate cores in SI */
#define REGDUMP_MASK_MAC0		BCM_BIT(1)
#define REGDUMP_MASK_ARM		BCM_BIT(2)
#define REGDUMP_MASK_PCIE		BCM_BIT(3)
#define REGDUMP_MASK_MAC1		BCM_BIT(4)
#define REGDUMP_MASK_PMU		BCM_BIT(5)

typedef struct {
	uint16 reg_offset;
	uint16 core_mask;
} reg_dump_config_t;

#define HND_EXT_TRAP_PHY_INFO_VER              2
typedef struct hnd_ext_trap_phydbg_v2 {
	uint8 version;
	uint8 len;
	uint16 err;
	uint16 RxFeStatus;
	uint16 TxFIFOStatus0;
	uint16 TxFIFOStatus1;
	uint16 RfseqMode;
	uint16 RfseqStatus0;
	uint16 RfseqStatus1;
	uint16 RfseqStatus_Ocl;
	uint16 RfseqStatus_Ocl1;
	uint16 OCLControl1;
	uint16 TxError;
	uint16 bphyTxError;
	uint16 TxCCKError;
	uint16 TxCtrlWrd0;
	uint16 TxCtrlWrd1;
	uint16 TxCtrlWrd2;
	uint16 TxLsig0;
	uint16 TxLsig1;
	uint16 TxVhtSigA10;
	uint16 TxVhtSigA11;
	uint16 TxVhtSigA20;
	uint16 TxVhtSigA21;
	uint16 txPktLength;
	uint16 txPsdulengthCtr;
	uint16 gpioClkControl;
	uint16 gpioSel;
	uint16 pktprocdebug;
	uint32 gpioOut[3];
	uint32 additional_regs[1];
} hnd_ext_trap_phydbg_v2_t;

#define HND_EXT_TRAP_PHY_INFO_VER_3		(3)
typedef struct hnd_ext_trap_phydbg_v3 {
	uint8 version;
	uint8 len;
	uint16 err;
	uint16 RxFeStatus;
	uint16 TxFIFOStatus0;
	uint16 TxFIFOStatus1;
	uint16 RfseqMode;
	uint16 RfseqStatus0;
	uint16 RfseqStatus1;
	uint16 RfseqStatus_Ocl;
	uint16 RfseqStatus_Ocl1;
	uint16 OCLControl1;
	uint16 TxError;
	uint16 bphyTxError;
	uint16 TxCCKError;
	uint16 TxCtrlWrd0;
	uint16 TxCtrlWrd1;
	uint16 TxCtrlWrd2;
	uint16 TxLsig0;
	uint16 TxLsig1;
	uint16 TxVhtSigA10;
	uint16 TxVhtSigA11;
	uint16 TxVhtSigA20;
	uint16 TxVhtSigA21;
	uint16 txPktLength;
	uint16 txPsdulengthCtr;
	uint16 gpioClkControl;
	uint16 gpioSel;
	uint16 pktprocdebug;
	uint32 gpioOut[3];
	uint16 HESigURateFlagStatus;
	uint16 HESigUsRateFlagStatus;
	uint32 additional_regs[1];
} hnd_ext_trap_phydbg_v3_t;

/* Phy TxErr Dump Structure */
#define HND_EXT_TRAP_PHYTXERR_INFO_VER		1
#define HND_EXT_TRAP_PHYTXERR_INFO_VER_V2	2
typedef struct hnd_ext_trap_macphytxerr {
	uint8 version; /* version of the information following this */
	uint8 trap_reason;
	uint16 i16_0x63E; /* tsf_tmr_rx_ts */
	uint16 i16_0x640; /* tsf_tmr_tx_ts */
	uint16 i16_0x642; /* tsf_tmr_rx_end_ts  */
	uint16 i16_0x846; /* TDC_FrmLen0 */
	uint16 i16_0x848; /* TDC_FrmLen1 */
	uint16 i16_0x84a; /* TDC_Txtime */
	uint16 i16_0xa5a; /* TXE_BytCntInTxFrmLo  */
	uint16 i16_0xa5c; /* TXE_BytCntInTxFrmHi */
	uint16 i16_0x856; /* TDC_VhtPsduLen0 */
	uint16 i16_0x858; /* TDC_VhtPsduLen1 */
	uint16 i16_0x490; /* psm_brc  */
	uint16 i16_0x4d8; /* psm_brc_1 */
	uint16 shm_txerr_reason;
	uint16 shm_pctl0;
	uint16 shm_pctl1;
	uint16 shm_pctl2;
	uint16 shm_lsig0;
	uint16 shm_lsig1;
	uint16 shm_plcp0;
	uint16 shm_plcp1;
	uint16 shm_plcp2;
	uint16 shm_vht_sigb0;
	uint16 shm_vht_sigb1;
	uint16 shm_tx_tst;
	uint16 shm_txerr_tm;
	uint16 shm_curchannel;
	uint16 shm_crx_rxtsf_pos;
	uint16 shm_lasttx_tsf;
	uint16 shm_s_rxtsftmrval;
	uint16 i16_0x29;	/* Phy indirect address */
	uint16 i16_0x2a;	/* Phy indirect address */
} hnd_ext_trap_macphytxerr_t;

typedef struct hnd_ext_trap_macphytxerr_v2 {
	uint8 version; /* version of the information following this */
	uint8 trap_reason;
	uint16 i16_0x63E; /* tsf_tmr_rx_ts */
	uint16 i16_0x640; /* tsf_tmr_tx_ts */
	uint16 i16_0x642; /* tsf_tmr_rx_end_ts  */
	uint16 i16_0x846; /* TDC_FrmLen0 */
	uint16 i16_0x848; /* TDC_FrmLen1 */
	uint16 i16_0x84a; /* TDC_Txtime */
	uint16 i16_0xa5a; /* TXE_BytCntInTxFrmLo  */
	uint16 i16_0xa5c; /* TXE_BytCntInTxFrmHi */
	uint16 i16_0x856; /* TDC_VhtPsduLen0 */
	uint16 i16_0x858; /* TDC_VhtPsduLen1 */
	uint16 i16_0x490; /* psm_brc  */
	uint16 i16_0x4d8; /* psm_brc_1 */
	uint16 shm_txerr_reason;
	uint16 shm_pctl0;
	uint16 shm_pctl1;
	uint16 shm_pctl2;
	uint16 shm_lsig0;
	uint16 shm_lsig1;
	uint16 shm_plcp0;
	uint16 shm_plcp1;
	uint16 shm_plcp2;
	uint16 shm_vht_sigb0;
	uint16 shm_vht_sigb1;
	uint16 shm_tx_tst;
	uint16 shm_txerr_tm;
	uint16 shm_curchannel;
	uint16 shm_crx_rxtsf_pos;
	uint16 shm_lasttx_tsf;
	uint16 shm_s_rxtsftmrval;
	uint16 i16_0x29;        /* Phy indirect address */
	uint16 i16_0x2a;        /* Phy indirect address */
	uint8 phyerr_bmac_cnt; /* number of times bmac raised phy tx err */
	uint8 phyerr_bmac_rsn; /* bmac reason for phy tx error */
	uint16 pad;
	uint32 recv_fifo_status[3][2]; /* Rcv Status0 & Rcv Status1 for 3 Rx fifos */
} hnd_ext_trap_macphytxerr_v2_t;

#define HND_EXT_TRAP_PCIE_ERR_ATTN_VER_1	(1u)
#define MAX_AER_HDR_LOG_REGS			(4u)
typedef struct hnd_ext_trap_pcie_err_attn_v1 {
	uint8 version;
	uint8 pad[3];
	uint32 err_hdr_logreg1;
	uint32 err_hdr_logreg2;
	uint32 err_hdr_logreg3;
	uint32 err_hdr_logreg4;
	uint32 err_code_logreg;
	uint32 err_type;
	uint32 err_code_state;
	uint32 last_err_attn_ts;
	uint32 cfg_tlp_hdr[MAX_AER_HDR_LOG_REGS];
} hnd_ext_trap_pcie_err_attn_v1_t;

#define MAX_EVENTLOG_BUFFERS	48
typedef struct eventlog_trapdata_info {
	uint32 num_elements;
	uint32 seq_num;
	uint32 log_arr_addr;
} eventlog_trapdata_info_t;

typedef struct eventlog_trap_buf_info {
	uint32 len;
	uint32 buf_addr;
} eventlog_trap_buf_info_t;

#if defined(ETD) && !defined(WLETD)
#define ETD_SW_FLAG_MEM		0x00000001

int etd_init(osl_t *osh);
int etd_register_trap_ext_callback(void *cb, void *arg);
int (etd_register_trap_ext_callback_late)(void *cb, void *arg);
uint32 *etd_get_trap_ext_data(void);
uint32 etd_get_trap_ext_swflags(void);
void etd_set_trap_ext_swflag(uint32 flag);
void etd_notify_trap_ext_callback(trap_t *tr);
reg_dump_config_t *etd_get_reg_dump_config_tbl(void);
uint etd_get_reg_dump_config_len(void);

extern bool _etd_enab;

	#define ETD_ENAB(pub)		(_etd_enab)

#else
#define ETD_ENAB(pub)		(0)
#endif /* WLETD */

#endif /* !LANGUAGE_ASSEMBLY */

#endif /* _ETD_H_ */
