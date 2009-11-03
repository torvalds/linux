/*
 * QLogic QLA41xx NIC HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qlge for copyright and licensing details.
 */
#ifndef _QLGE_H_
#define _QLGE_H_

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>

/*
 * General definitions...
 */
#define DRV_NAME  	"qlge"
#define DRV_STRING 	"QLogic 10 Gigabit PCI-E Ethernet Driver "
#define DRV_VERSION	"v1.00.00-b3"

#define PFX "qlge: "
#define QPRINTK(qdev, nlevel, klevel, fmt, args...)     \
       do {       \
	if (!((qdev)->msg_enable & NETIF_MSG_##nlevel))		\
		;						\
	else							\
		dev_printk(KERN_##klevel, &((qdev)->pdev->dev),	\
			   "%s: " fmt, __func__, ##args);  \
       } while (0)

#define WQ_ADDR_ALIGN	0x3	/* 4 byte alignment */

#define QLGE_VENDOR_ID    0x1077
#define QLGE_DEVICE_ID_8012	0x8012
#define QLGE_DEVICE_ID_8000	0x8000
#define MAX_CPUS 8
#define MAX_TX_RINGS MAX_CPUS
#define MAX_RX_RINGS ((MAX_CPUS * 2) + 1)

#define NUM_TX_RING_ENTRIES	256
#define NUM_RX_RING_ENTRIES	256

#define NUM_SMALL_BUFFERS   512
#define NUM_LARGE_BUFFERS   512
#define DB_PAGE_SIZE 4096

/* Calculate the number of (4k) pages required to
 * contain a buffer queue of the given length.
 */
#define MAX_DB_PAGES_PER_BQ(x) \
		(((x * sizeof(u64)) / DB_PAGE_SIZE) + \
		(((x * sizeof(u64)) % DB_PAGE_SIZE) ? 1 : 0))

#define RX_RING_SHADOW_SPACE	(sizeof(u64) + \
		MAX_DB_PAGES_PER_BQ(NUM_SMALL_BUFFERS) * sizeof(u64) + \
		MAX_DB_PAGES_PER_BQ(NUM_LARGE_BUFFERS) * sizeof(u64))
#define SMALL_BUFFER_SIZE 512
#define SMALL_BUF_MAP_SIZE (SMALL_BUFFER_SIZE / 2)
#define LARGE_BUFFER_MAX_SIZE 8192
#define LARGE_BUFFER_MIN_SIZE 2048
#define MAX_SPLIT_SIZE 1023
#define QLGE_SB_PAD 32

#define MAX_CQ 128
#define DFLT_COALESCE_WAIT 100	/* 100 usec wait for coalescing */
#define MAX_INTER_FRAME_WAIT 10	/* 10 usec max interframe-wait for coalescing */
#define DFLT_INTER_FRAME_WAIT (MAX_INTER_FRAME_WAIT/2)
#define UDELAY_COUNT 3
#define UDELAY_DELAY 100


#define TX_DESC_PER_IOCB 8
/* The maximum number of frags we handle is based
 * on PAGE_SIZE...
 */
#if (PAGE_SHIFT == 12) || (PAGE_SHIFT == 13)	/* 4k & 8k pages */
#define TX_DESC_PER_OAL ((MAX_SKB_FRAGS - TX_DESC_PER_IOCB) + 2)
#else /* all other page sizes */
#define TX_DESC_PER_OAL 0
#endif

/* MPI test register definitions. This register
 * is used for determining alternate NIC function's
 * PCI->func number.
 */
enum {
	MPI_TEST_FUNC_PORT_CFG = 0x1002,
	MPI_TEST_NIC1_FUNC_SHIFT = 1,
	MPI_TEST_NIC2_FUNC_SHIFT = 5,
	MPI_TEST_NIC_FUNC_MASK = 0x00000007,
};

/*
 * Processor Address Register (PROC_ADDR) bit definitions.
 */
enum {

	/* Misc. stuff */
	MAILBOX_COUNT = 16,
	MAILBOX_TIMEOUT = 5,

	PROC_ADDR_RDY = (1 << 31),
	PROC_ADDR_R = (1 << 30),
	PROC_ADDR_ERR = (1 << 29),
	PROC_ADDR_DA = (1 << 28),
	PROC_ADDR_FUNC0_MBI = 0x00001180,
	PROC_ADDR_FUNC0_MBO = (PROC_ADDR_FUNC0_MBI + MAILBOX_COUNT),
	PROC_ADDR_FUNC0_CTL = 0x000011a1,
	PROC_ADDR_FUNC2_MBI = 0x00001280,
	PROC_ADDR_FUNC2_MBO = (PROC_ADDR_FUNC2_MBI + MAILBOX_COUNT),
	PROC_ADDR_FUNC2_CTL = 0x000012a1,
	PROC_ADDR_MPI_RISC = 0x00000000,
	PROC_ADDR_MDE = 0x00010000,
	PROC_ADDR_REGBLOCK = 0x00020000,
	PROC_ADDR_RISC_REG = 0x00030000,
};

/*
 * System Register (SYS) bit definitions.
 */
enum {
	SYS_EFE = (1 << 0),
	SYS_FAE = (1 << 1),
	SYS_MDC = (1 << 2),
	SYS_DST = (1 << 3),
	SYS_DWC = (1 << 4),
	SYS_EVW = (1 << 5),
	SYS_OMP_DLY_MASK = 0x3f000000,
	/*
	 * There are no values defined as of edit #15.
	 */
	SYS_ODI = (1 << 14),
};

/*
 *  Reset/Failover Register (RST_FO) bit definitions.
 */
enum {
	RST_FO_TFO = (1 << 0),
	RST_FO_RR_MASK = 0x00060000,
	RST_FO_RR_CQ_CAM = 0x00000000,
	RST_FO_RR_DROP = 0x00000002,
	RST_FO_RR_DQ = 0x00000004,
	RST_FO_RR_RCV_FUNC_CQ = 0x00000006,
	RST_FO_FRB = (1 << 12),
	RST_FO_MOP = (1 << 13),
	RST_FO_REG = (1 << 14),
	RST_FO_FR = (1 << 15),
};

/*
 * Function Specific Control Register (FSC) bit definitions.
 */
enum {
	FSC_DBRST_MASK = 0x00070000,
	FSC_DBRST_256 = 0x00000000,
	FSC_DBRST_512 = 0x00000001,
	FSC_DBRST_768 = 0x00000002,
	FSC_DBRST_1024 = 0x00000003,
	FSC_DBL_MASK = 0x00180000,
	FSC_DBL_DBRST = 0x00000000,
	FSC_DBL_MAX_PLD = 0x00000008,
	FSC_DBL_MAX_BRST = 0x00000010,
	FSC_DBL_128_BYTES = 0x00000018,
	FSC_EC = (1 << 5),
	FSC_EPC_MASK = 0x00c00000,
	FSC_EPC_INBOUND = (1 << 6),
	FSC_EPC_OUTBOUND = (1 << 7),
	FSC_VM_PAGESIZE_MASK = 0x07000000,
	FSC_VM_PAGE_2K = 0x00000100,
	FSC_VM_PAGE_4K = 0x00000200,
	FSC_VM_PAGE_8K = 0x00000300,
	FSC_VM_PAGE_64K = 0x00000600,
	FSC_SH = (1 << 11),
	FSC_DSB = (1 << 12),
	FSC_STE = (1 << 13),
	FSC_FE = (1 << 15),
};

/*
 *  Host Command Status Register (CSR) bit definitions.
 */
enum {
	CSR_ERR_STS_MASK = 0x0000003f,
	/*
	 * There are no valued defined as of edit #15.
	 */
	CSR_RR = (1 << 8),
	CSR_HRI = (1 << 9),
	CSR_RP = (1 << 10),
	CSR_CMD_PARM_SHIFT = 22,
	CSR_CMD_NOP = 0x00000000,
	CSR_CMD_SET_RST = 0x10000000,
	CSR_CMD_CLR_RST = 0x20000000,
	CSR_CMD_SET_PAUSE = 0x30000000,
	CSR_CMD_CLR_PAUSE = 0x40000000,
	CSR_CMD_SET_H2R_INT = 0x50000000,
	CSR_CMD_CLR_H2R_INT = 0x60000000,
	CSR_CMD_PAR_EN = 0x70000000,
	CSR_CMD_SET_BAD_PAR = 0x80000000,
	CSR_CMD_CLR_BAD_PAR = 0x90000000,
	CSR_CMD_CLR_R2PCI_INT = 0xa0000000,
};

/*
 *  Configuration Register (CFG) bit definitions.
 */
enum {
	CFG_LRQ = (1 << 0),
	CFG_DRQ = (1 << 1),
	CFG_LR = (1 << 2),
	CFG_DR = (1 << 3),
	CFG_LE = (1 << 5),
	CFG_LCQ = (1 << 6),
	CFG_DCQ = (1 << 7),
	CFG_Q_SHIFT = 8,
	CFG_Q_MASK = 0x7f000000,
};

/*
 *  Status Register (STS) bit definitions.
 */
enum {
	STS_FE = (1 << 0),
	STS_PI = (1 << 1),
	STS_PL0 = (1 << 2),
	STS_PL1 = (1 << 3),
	STS_PI0 = (1 << 4),
	STS_PI1 = (1 << 5),
	STS_FUNC_ID_MASK = 0x000000c0,
	STS_FUNC_ID_SHIFT = 6,
	STS_F0E = (1 << 8),
	STS_F1E = (1 << 9),
	STS_F2E = (1 << 10),
	STS_F3E = (1 << 11),
	STS_NFE = (1 << 12),
};

/*
 * Interrupt Enable Register (INTR_EN) bit definitions.
 */
enum {
	INTR_EN_INTR_MASK = 0x007f0000,
	INTR_EN_TYPE_MASK = 0x03000000,
	INTR_EN_TYPE_ENABLE = 0x00000100,
	INTR_EN_TYPE_DISABLE = 0x00000200,
	INTR_EN_TYPE_READ = 0x00000300,
	INTR_EN_IHD = (1 << 13),
	INTR_EN_IHD_MASK = (INTR_EN_IHD << 16),
	INTR_EN_EI = (1 << 14),
	INTR_EN_EN = (1 << 15),
};

/*
 * Interrupt Mask Register (INTR_MASK) bit definitions.
 */
enum {
	INTR_MASK_PI = (1 << 0),
	INTR_MASK_HL0 = (1 << 1),
	INTR_MASK_LH0 = (1 << 2),
	INTR_MASK_HL1 = (1 << 3),
	INTR_MASK_LH1 = (1 << 4),
	INTR_MASK_SE = (1 << 5),
	INTR_MASK_LSC = (1 << 6),
	INTR_MASK_MC = (1 << 7),
	INTR_MASK_LINK_IRQS = INTR_MASK_LSC | INTR_MASK_SE | INTR_MASK_MC,
};

/*
 *  Register (REV_ID) bit definitions.
 */
enum {
	REV_ID_MASK = 0x0000000f,
	REV_ID_NICROLL_SHIFT = 0,
	REV_ID_NICREV_SHIFT = 4,
	REV_ID_XGROLL_SHIFT = 8,
	REV_ID_XGREV_SHIFT = 12,
	REV_ID_CHIPREV_SHIFT = 28,
};

/*
 *  Force ECC Error Register (FRC_ECC_ERR) bit definitions.
 */
enum {
	FRC_ECC_ERR_VW = (1 << 12),
	FRC_ECC_ERR_VB = (1 << 13),
	FRC_ECC_ERR_NI = (1 << 14),
	FRC_ECC_ERR_NO = (1 << 15),
	FRC_ECC_PFE_SHIFT = 16,
	FRC_ECC_ERR_DO = (1 << 18),
	FRC_ECC_P14 = (1 << 19),
};

/*
 *  Error Status Register (ERR_STS) bit definitions.
 */
enum {
	ERR_STS_NOF = (1 << 0),
	ERR_STS_NIF = (1 << 1),
	ERR_STS_DRP = (1 << 2),
	ERR_STS_XGP = (1 << 3),
	ERR_STS_FOU = (1 << 4),
	ERR_STS_FOC = (1 << 5),
	ERR_STS_FOF = (1 << 6),
	ERR_STS_FIU = (1 << 7),
	ERR_STS_FIC = (1 << 8),
	ERR_STS_FIF = (1 << 9),
	ERR_STS_MOF = (1 << 10),
	ERR_STS_TA = (1 << 11),
	ERR_STS_MA = (1 << 12),
	ERR_STS_MPE = (1 << 13),
	ERR_STS_SCE = (1 << 14),
	ERR_STS_STE = (1 << 15),
	ERR_STS_FOW = (1 << 16),
	ERR_STS_UE = (1 << 17),
	ERR_STS_MCH = (1 << 26),
	ERR_STS_LOC_SHIFT = 27,
};

/*
 *  RAM Debug Address Register (RAM_DBG_ADDR) bit definitions.
 */
enum {
	RAM_DBG_ADDR_FW = (1 << 30),
	RAM_DBG_ADDR_FR = (1 << 31),
};

/*
 * Semaphore Register (SEM) bit definitions.
 */
enum {
	/*
	 * Example:
	 * reg = SEM_XGMAC0_MASK | (SEM_SET << SEM_XGMAC0_SHIFT)
	 */
	SEM_CLEAR = 0,
	SEM_SET = 1,
	SEM_FORCE = 3,
	SEM_XGMAC0_SHIFT = 0,
	SEM_XGMAC1_SHIFT = 2,
	SEM_ICB_SHIFT = 4,
	SEM_MAC_ADDR_SHIFT = 6,
	SEM_FLASH_SHIFT = 8,
	SEM_PROBE_SHIFT = 10,
	SEM_RT_IDX_SHIFT = 12,
	SEM_PROC_REG_SHIFT = 14,
	SEM_XGMAC0_MASK = 0x00030000,
	SEM_XGMAC1_MASK = 0x000c0000,
	SEM_ICB_MASK = 0x00300000,
	SEM_MAC_ADDR_MASK = 0x00c00000,
	SEM_FLASH_MASK = 0x03000000,
	SEM_PROBE_MASK = 0x0c000000,
	SEM_RT_IDX_MASK = 0x30000000,
	SEM_PROC_REG_MASK = 0xc0000000,
};

/*
 *  10G MAC Address  Register (XGMAC_ADDR) bit definitions.
 */
enum {
	XGMAC_ADDR_RDY = (1 << 31),
	XGMAC_ADDR_R = (1 << 30),
	XGMAC_ADDR_XME = (1 << 29),

	/* XGMAC control registers */
	PAUSE_SRC_LO = 0x00000100,
	PAUSE_SRC_HI = 0x00000104,
	GLOBAL_CFG = 0x00000108,
	GLOBAL_CFG_RESET = (1 << 0),
	GLOBAL_CFG_JUMBO = (1 << 6),
	GLOBAL_CFG_TX_STAT_EN = (1 << 10),
	GLOBAL_CFG_RX_STAT_EN = (1 << 11),
	TX_CFG = 0x0000010c,
	TX_CFG_RESET = (1 << 0),
	TX_CFG_EN = (1 << 1),
	TX_CFG_PREAM = (1 << 2),
	RX_CFG = 0x00000110,
	RX_CFG_RESET = (1 << 0),
	RX_CFG_EN = (1 << 1),
	RX_CFG_PREAM = (1 << 2),
	FLOW_CTL = 0x0000011c,
	PAUSE_OPCODE = 0x00000120,
	PAUSE_TIMER = 0x00000124,
	PAUSE_FRM_DEST_LO = 0x00000128,
	PAUSE_FRM_DEST_HI = 0x0000012c,
	MAC_TX_PARAMS = 0x00000134,
	MAC_TX_PARAMS_JUMBO = (1 << 31),
	MAC_TX_PARAMS_SIZE_SHIFT = 16,
	MAC_RX_PARAMS = 0x00000138,
	MAC_SYS_INT = 0x00000144,
	MAC_SYS_INT_MASK = 0x00000148,
	MAC_MGMT_INT = 0x0000014c,
	MAC_MGMT_IN_MASK = 0x00000150,
	EXT_ARB_MODE = 0x000001fc,

	/* XGMAC TX statistics  registers */
	TX_PKTS = 0x00000200,
	TX_BYTES = 0x00000208,
	TX_MCAST_PKTS = 0x00000210,
	TX_BCAST_PKTS = 0x00000218,
	TX_UCAST_PKTS = 0x00000220,
	TX_CTL_PKTS = 0x00000228,
	TX_PAUSE_PKTS = 0x00000230,
	TX_64_PKT = 0x00000238,
	TX_65_TO_127_PKT = 0x00000240,
	TX_128_TO_255_PKT = 0x00000248,
	TX_256_511_PKT = 0x00000250,
	TX_512_TO_1023_PKT = 0x00000258,
	TX_1024_TO_1518_PKT = 0x00000260,
	TX_1519_TO_MAX_PKT = 0x00000268,
	TX_UNDERSIZE_PKT = 0x00000270,
	TX_OVERSIZE_PKT = 0x00000278,

	/* XGMAC statistics control registers */
	RX_HALF_FULL_DET = 0x000002a0,
	TX_HALF_FULL_DET = 0x000002a4,
	RX_OVERFLOW_DET = 0x000002a8,
	TX_OVERFLOW_DET = 0x000002ac,
	RX_HALF_FULL_MASK = 0x000002b0,
	TX_HALF_FULL_MASK = 0x000002b4,
	RX_OVERFLOW_MASK = 0x000002b8,
	TX_OVERFLOW_MASK = 0x000002bc,
	STAT_CNT_CTL = 0x000002c0,
	STAT_CNT_CTL_CLEAR_TX = (1 << 0),
	STAT_CNT_CTL_CLEAR_RX = (1 << 1),
	AUX_RX_HALF_FULL_DET = 0x000002d0,
	AUX_TX_HALF_FULL_DET = 0x000002d4,
	AUX_RX_OVERFLOW_DET = 0x000002d8,
	AUX_TX_OVERFLOW_DET = 0x000002dc,
	AUX_RX_HALF_FULL_MASK = 0x000002f0,
	AUX_TX_HALF_FULL_MASK = 0x000002f4,
	AUX_RX_OVERFLOW_MASK = 0x000002f8,
	AUX_TX_OVERFLOW_MASK = 0x000002fc,

	/* XGMAC RX statistics  registers */
	RX_BYTES = 0x00000300,
	RX_BYTES_OK = 0x00000308,
	RX_PKTS = 0x00000310,
	RX_PKTS_OK = 0x00000318,
	RX_BCAST_PKTS = 0x00000320,
	RX_MCAST_PKTS = 0x00000328,
	RX_UCAST_PKTS = 0x00000330,
	RX_UNDERSIZE_PKTS = 0x00000338,
	RX_OVERSIZE_PKTS = 0x00000340,
	RX_JABBER_PKTS = 0x00000348,
	RX_UNDERSIZE_FCERR_PKTS = 0x00000350,
	RX_DROP_EVENTS = 0x00000358,
	RX_FCERR_PKTS = 0x00000360,
	RX_ALIGN_ERR = 0x00000368,
	RX_SYMBOL_ERR = 0x00000370,
	RX_MAC_ERR = 0x00000378,
	RX_CTL_PKTS = 0x00000380,
	RX_PAUSE_PKTS = 0x00000388,
	RX_64_PKTS = 0x00000390,
	RX_65_TO_127_PKTS = 0x00000398,
	RX_128_255_PKTS = 0x000003a0,
	RX_256_511_PKTS = 0x000003a8,
	RX_512_TO_1023_PKTS = 0x000003b0,
	RX_1024_TO_1518_PKTS = 0x000003b8,
	RX_1519_TO_MAX_PKTS = 0x000003c0,
	RX_LEN_ERR_PKTS = 0x000003c8,

	/* XGMAC MDIO control registers */
	MDIO_TX_DATA = 0x00000400,
	MDIO_RX_DATA = 0x00000410,
	MDIO_CMD = 0x00000420,
	MDIO_PHY_ADDR = 0x00000430,
	MDIO_PORT = 0x00000440,
	MDIO_STATUS = 0x00000450,

	/* XGMAC AUX statistics  registers */
};

/*
 *  Enhanced Transmission Schedule Registers (NIC_ETS,CNA_ETS) bit definitions.
 */
enum {
	ETS_QUEUE_SHIFT = 29,
	ETS_REF = (1 << 26),
	ETS_RS = (1 << 27),
	ETS_P = (1 << 28),
	ETS_FC_COS_SHIFT = 23,
};

/*
 *  Flash Address Register (FLASH_ADDR) bit definitions.
 */
enum {
	FLASH_ADDR_RDY = (1 << 31),
	FLASH_ADDR_R = (1 << 30),
	FLASH_ADDR_ERR = (1 << 29),
};

/*
 *  Stop CQ Processing Register (CQ_STOP) bit definitions.
 */
enum {
	CQ_STOP_QUEUE_MASK = (0x007f0000),
	CQ_STOP_TYPE_MASK = (0x03000000),
	CQ_STOP_TYPE_START = 0x00000100,
	CQ_STOP_TYPE_STOP = 0x00000200,
	CQ_STOP_TYPE_READ = 0x00000300,
	CQ_STOP_EN = (1 << 15),
};

/*
 *  MAC Protocol Address Index Register (MAC_ADDR_IDX) bit definitions.
 */
enum {
	MAC_ADDR_IDX_SHIFT = 4,
	MAC_ADDR_TYPE_SHIFT = 16,
	MAC_ADDR_TYPE_MASK = 0x000f0000,
	MAC_ADDR_TYPE_CAM_MAC = 0x00000000,
	MAC_ADDR_TYPE_MULTI_MAC = 0x00010000,
	MAC_ADDR_TYPE_VLAN = 0x00020000,
	MAC_ADDR_TYPE_MULTI_FLTR = 0x00030000,
	MAC_ADDR_TYPE_FC_MAC = 0x00040000,
	MAC_ADDR_TYPE_MGMT_MAC = 0x00050000,
	MAC_ADDR_TYPE_MGMT_VLAN = 0x00060000,
	MAC_ADDR_TYPE_MGMT_V4 = 0x00070000,
	MAC_ADDR_TYPE_MGMT_V6 = 0x00080000,
	MAC_ADDR_TYPE_MGMT_TU_DP = 0x00090000,
	MAC_ADDR_ADR = (1 << 25),
	MAC_ADDR_RS = (1 << 26),
	MAC_ADDR_E = (1 << 27),
	MAC_ADDR_MR = (1 << 30),
	MAC_ADDR_MW = (1 << 31),
	MAX_MULTICAST_ENTRIES = 32,
};

/*
 *  MAC Protocol Address Index Register (SPLT_HDR) bit definitions.
 */
enum {
	SPLT_HDR_EP = (1 << 31),
};

/*
 *  FCoE Receive Configuration Register (FC_RCV_CFG) bit definitions.
 */
enum {
	FC_RCV_CFG_ECT = (1 << 15),
	FC_RCV_CFG_DFH = (1 << 20),
	FC_RCV_CFG_DVF = (1 << 21),
	FC_RCV_CFG_RCE = (1 << 27),
	FC_RCV_CFG_RFE = (1 << 28),
	FC_RCV_CFG_TEE = (1 << 29),
	FC_RCV_CFG_TCE = (1 << 30),
	FC_RCV_CFG_TFE = (1 << 31),
};

/*
 *  NIC Receive Configuration Register (NIC_RCV_CFG) bit definitions.
 */
enum {
	NIC_RCV_CFG_PPE = (1 << 0),
	NIC_RCV_CFG_VLAN_MASK = 0x00060000,
	NIC_RCV_CFG_VLAN_ALL = 0x00000000,
	NIC_RCV_CFG_VLAN_MATCH_ONLY = 0x00000002,
	NIC_RCV_CFG_VLAN_MATCH_AND_NON = 0x00000004,
	NIC_RCV_CFG_VLAN_NONE_AND_NON = 0x00000006,
	NIC_RCV_CFG_RV = (1 << 3),
	NIC_RCV_CFG_DFQ_MASK = (0x7f000000),
	NIC_RCV_CFG_DFQ_SHIFT = 8,
	NIC_RCV_CFG_DFQ = 0,	/* HARDCODE default queue to 0. */
};

/*
 *   Mgmt Receive Configuration Register (MGMT_RCV_CFG) bit definitions.
 */
enum {
	MGMT_RCV_CFG_ARP = (1 << 0),
	MGMT_RCV_CFG_DHC = (1 << 1),
	MGMT_RCV_CFG_DHS = (1 << 2),
	MGMT_RCV_CFG_NP = (1 << 3),
	MGMT_RCV_CFG_I6N = (1 << 4),
	MGMT_RCV_CFG_I6R = (1 << 5),
	MGMT_RCV_CFG_DH6 = (1 << 6),
	MGMT_RCV_CFG_UD1 = (1 << 7),
	MGMT_RCV_CFG_UD0 = (1 << 8),
	MGMT_RCV_CFG_BCT = (1 << 9),
	MGMT_RCV_CFG_MCT = (1 << 10),
	MGMT_RCV_CFG_DM = (1 << 11),
	MGMT_RCV_CFG_RM = (1 << 12),
	MGMT_RCV_CFG_STL = (1 << 13),
	MGMT_RCV_CFG_VLAN_MASK = 0xc0000000,
	MGMT_RCV_CFG_VLAN_ALL = 0x00000000,
	MGMT_RCV_CFG_VLAN_MATCH_ONLY = 0x00004000,
	MGMT_RCV_CFG_VLAN_MATCH_AND_NON = 0x00008000,
	MGMT_RCV_CFG_VLAN_NONE_AND_NON = 0x0000c000,
};

/*
 *  Routing Index Register (RT_IDX) bit definitions.
 */
enum {
	RT_IDX_IDX_SHIFT = 8,
	RT_IDX_TYPE_MASK = 0x000f0000,
	RT_IDX_TYPE_RT = 0x00000000,
	RT_IDX_TYPE_RT_INV = 0x00010000,
	RT_IDX_TYPE_NICQ = 0x00020000,
	RT_IDX_TYPE_NICQ_INV = 0x00030000,
	RT_IDX_DST_MASK = 0x00700000,
	RT_IDX_DST_RSS = 0x00000000,
	RT_IDX_DST_CAM_Q = 0x00100000,
	RT_IDX_DST_COS_Q = 0x00200000,
	RT_IDX_DST_DFLT_Q = 0x00300000,
	RT_IDX_DST_DEST_Q = 0x00400000,
	RT_IDX_RS = (1 << 26),
	RT_IDX_E = (1 << 27),
	RT_IDX_MR = (1 << 30),
	RT_IDX_MW = (1 << 31),

	/* Nic Queue format - type 2 bits */
	RT_IDX_BCAST = (1 << 0),
	RT_IDX_MCAST = (1 << 1),
	RT_IDX_MCAST_MATCH = (1 << 2),
	RT_IDX_MCAST_REG_MATCH = (1 << 3),
	RT_IDX_MCAST_HASH_MATCH = (1 << 4),
	RT_IDX_FC_MACH = (1 << 5),
	RT_IDX_ETH_FCOE = (1 << 6),
	RT_IDX_CAM_HIT = (1 << 7),
	RT_IDX_CAM_BIT0 = (1 << 8),
	RT_IDX_CAM_BIT1 = (1 << 9),
	RT_IDX_VLAN_TAG = (1 << 10),
	RT_IDX_VLAN_MATCH = (1 << 11),
	RT_IDX_VLAN_FILTER = (1 << 12),
	RT_IDX_ETH_SKIP1 = (1 << 13),
	RT_IDX_ETH_SKIP2 = (1 << 14),
	RT_IDX_BCAST_MCAST_MATCH = (1 << 15),
	RT_IDX_802_3 = (1 << 16),
	RT_IDX_LLDP = (1 << 17),
	RT_IDX_UNUSED018 = (1 << 18),
	RT_IDX_UNUSED019 = (1 << 19),
	RT_IDX_UNUSED20 = (1 << 20),
	RT_IDX_UNUSED21 = (1 << 21),
	RT_IDX_ERR = (1 << 22),
	RT_IDX_VALID = (1 << 23),
	RT_IDX_TU_CSUM_ERR = (1 << 24),
	RT_IDX_IP_CSUM_ERR = (1 << 25),
	RT_IDX_MAC_ERR = (1 << 26),
	RT_IDX_RSS_TCP6 = (1 << 27),
	RT_IDX_RSS_TCP4 = (1 << 28),
	RT_IDX_RSS_IPV6 = (1 << 29),
	RT_IDX_RSS_IPV4 = (1 << 30),
	RT_IDX_RSS_MATCH = (1 << 31),

	/* Hierarchy for the NIC Queue Mask */
	RT_IDX_ALL_ERR_SLOT = 0,
	RT_IDX_MAC_ERR_SLOT = 0,
	RT_IDX_IP_CSUM_ERR_SLOT = 1,
	RT_IDX_TCP_UDP_CSUM_ERR_SLOT = 2,
	RT_IDX_BCAST_SLOT = 3,
	RT_IDX_MCAST_MATCH_SLOT = 4,
	RT_IDX_ALLMULTI_SLOT = 5,
	RT_IDX_UNUSED6_SLOT = 6,
	RT_IDX_UNUSED7_SLOT = 7,
	RT_IDX_RSS_MATCH_SLOT = 8,
	RT_IDX_RSS_IPV4_SLOT = 8,
	RT_IDX_RSS_IPV6_SLOT = 9,
	RT_IDX_RSS_TCP4_SLOT = 10,
	RT_IDX_RSS_TCP6_SLOT = 11,
	RT_IDX_CAM_HIT_SLOT = 12,
	RT_IDX_UNUSED013 = 13,
	RT_IDX_UNUSED014 = 14,
	RT_IDX_PROMISCUOUS_SLOT = 15,
	RT_IDX_MAX_SLOTS = 16,
};

/*
 * Control Register Set Map
 */
enum {
	PROC_ADDR = 0,		/* Use semaphore */
	PROC_DATA = 0x04,	/* Use semaphore */
	SYS = 0x08,
	RST_FO = 0x0c,
	FSC = 0x10,
	CSR = 0x14,
	LED = 0x18,
	ICB_RID = 0x1c,		/* Use semaphore */
	ICB_L = 0x20,		/* Use semaphore */
	ICB_H = 0x24,		/* Use semaphore */
	CFG = 0x28,
	BIOS_ADDR = 0x2c,
	STS = 0x30,
	INTR_EN = 0x34,
	INTR_MASK = 0x38,
	ISR1 = 0x3c,
	ISR2 = 0x40,
	ISR3 = 0x44,
	ISR4 = 0x48,
	REV_ID = 0x4c,
	FRC_ECC_ERR = 0x50,
	ERR_STS = 0x54,
	RAM_DBG_ADDR = 0x58,
	RAM_DBG_DATA = 0x5c,
	ECC_ERR_CNT = 0x60,
	SEM = 0x64,
	GPIO_1 = 0x68,		/* Use semaphore */
	GPIO_2 = 0x6c,		/* Use semaphore */
	GPIO_3 = 0x70,		/* Use semaphore */
	RSVD2 = 0x74,
	XGMAC_ADDR = 0x78,	/* Use semaphore */
	XGMAC_DATA = 0x7c,	/* Use semaphore */
	NIC_ETS = 0x80,
	CNA_ETS = 0x84,
	FLASH_ADDR = 0x88,	/* Use semaphore */
	FLASH_DATA = 0x8c,	/* Use semaphore */
	CQ_STOP = 0x90,
	PAGE_TBL_RID = 0x94,
	WQ_PAGE_TBL_LO = 0x98,
	WQ_PAGE_TBL_HI = 0x9c,
	CQ_PAGE_TBL_LO = 0xa0,
	CQ_PAGE_TBL_HI = 0xa4,
	MAC_ADDR_IDX = 0xa8,	/* Use semaphore */
	MAC_ADDR_DATA = 0xac,	/* Use semaphore */
	COS_DFLT_CQ1 = 0xb0,
	COS_DFLT_CQ2 = 0xb4,
	ETYPE_SKIP1 = 0xb8,
	ETYPE_SKIP2 = 0xbc,
	SPLT_HDR = 0xc0,
	FC_PAUSE_THRES = 0xc4,
	NIC_PAUSE_THRES = 0xc8,
	FC_ETHERTYPE = 0xcc,
	FC_RCV_CFG = 0xd0,
	NIC_RCV_CFG = 0xd4,
	FC_COS_TAGS = 0xd8,
	NIC_COS_TAGS = 0xdc,
	MGMT_RCV_CFG = 0xe0,
	RT_IDX = 0xe4,
	RT_DATA = 0xe8,
	RSVD7 = 0xec,
	XG_SERDES_ADDR = 0xf0,
	XG_SERDES_DATA = 0xf4,
	PRB_MX_ADDR = 0xf8,	/* Use semaphore */
	PRB_MX_DATA = 0xfc,	/* Use semaphore */
};

/*
 * CAM output format.
 */
enum {
	CAM_OUT_ROUTE_FC = 0,
	CAM_OUT_ROUTE_NIC = 1,
	CAM_OUT_FUNC_SHIFT = 2,
	CAM_OUT_RV = (1 << 4),
	CAM_OUT_SH = (1 << 15),
	CAM_OUT_CQ_ID_SHIFT = 5,
};

/*
 * Mailbox  definitions
 */
enum {
	/* Asynchronous Event Notifications */
	AEN_SYS_ERR = 0x00008002,
	AEN_LINK_UP = 0x00008011,
	AEN_LINK_DOWN = 0x00008012,
	AEN_IDC_CMPLT = 0x00008100,
	AEN_IDC_REQ = 0x00008101,
	AEN_IDC_EXT = 0x00008102,
	AEN_DCBX_CHG = 0x00008110,
	AEN_AEN_LOST = 0x00008120,
	AEN_AEN_SFP_IN = 0x00008130,
	AEN_AEN_SFP_OUT = 0x00008131,
	AEN_FW_INIT_DONE = 0x00008400,
	AEN_FW_INIT_FAIL = 0x00008401,

	/* Mailbox Command Opcodes. */
	MB_CMD_NOP = 0x00000000,
	MB_CMD_EX_FW = 0x00000002,
	MB_CMD_MB_TEST = 0x00000006,
	MB_CMD_CSUM_TEST = 0x00000007,	/* Verify Checksum */
	MB_CMD_ABOUT_FW = 0x00000008,
	MB_CMD_COPY_RISC_RAM = 0x0000000a,
	MB_CMD_LOAD_RISC_RAM = 0x0000000b,
	MB_CMD_DUMP_RISC_RAM = 0x0000000c,
	MB_CMD_WRITE_RAM = 0x0000000d,
	MB_CMD_INIT_RISC_RAM = 0x0000000e,
	MB_CMD_READ_RAM = 0x0000000f,
	MB_CMD_STOP_FW = 0x00000014,
	MB_CMD_MAKE_SYS_ERR = 0x0000002a,
	MB_CMD_WRITE_SFP = 0x00000030,
	MB_CMD_READ_SFP = 0x00000031,
	MB_CMD_INIT_FW = 0x00000060,
	MB_CMD_GET_IFCB = 0x00000061,
	MB_CMD_GET_FW_STATE = 0x00000069,
	MB_CMD_IDC_REQ = 0x00000100,	/* Inter-Driver Communication */
	MB_CMD_IDC_ACK = 0x00000101,	/* Inter-Driver Communication */
	MB_CMD_SET_WOL_MODE = 0x00000110,	/* Wake On Lan */
	MB_WOL_DISABLE = 0,
	MB_WOL_MAGIC_PKT = (1 << 1),
	MB_WOL_FLTR = (1 << 2),
	MB_WOL_UCAST = (1 << 3),
	MB_WOL_MCAST = (1 << 4),
	MB_WOL_BCAST = (1 << 5),
	MB_WOL_LINK_UP = (1 << 6),
	MB_WOL_LINK_DOWN = (1 << 7),
	MB_WOL_MODE_ON = (1 << 16),		/* Wake on Lan Mode on */
	MB_CMD_SET_WOL_FLTR = 0x00000111,	/* Wake On Lan Filter */
	MB_CMD_CLEAR_WOL_FLTR = 0x00000112, /* Wake On Lan Filter */
	MB_CMD_SET_WOL_MAGIC = 0x00000113,	/* Wake On Lan Magic Packet */
	MB_CMD_CLEAR_WOL_MAGIC = 0x00000114,/* Wake On Lan Magic Packet */
	MB_CMD_SET_WOL_IMMED = 0x00000115,
	MB_CMD_PORT_RESET = 0x00000120,
	MB_CMD_SET_PORT_CFG = 0x00000122,
	MB_CMD_GET_PORT_CFG = 0x00000123,
	MB_CMD_GET_LINK_STS = 0x00000124,
	MB_CMD_SET_LED_CFG = 0x00000125, /* Set LED Configuration Register */
		QL_LED_BLINK = 0x03e803e8,
	MB_CMD_GET_LED_CFG = 0x00000126, /* Get LED Configuration Register */
	MB_CMD_SET_MGMNT_TFK_CTL = 0x00000160, /* Set Mgmnt Traffic Control */
	MB_SET_MPI_TFK_STOP = (1 << 0),
	MB_SET_MPI_TFK_RESUME = (1 << 1),
	MB_CMD_GET_MGMNT_TFK_CTL = 0x00000161, /* Get Mgmnt Traffic Control */
	MB_GET_MPI_TFK_STOPPED = (1 << 0),
	MB_GET_MPI_TFK_FIFO_EMPTY = (1 << 1),
	/* Sub-commands for IDC request.
	 * This describes the reason for the
	 * IDC request.
	 */
	MB_CMD_IOP_NONE = 0x0000,
	MB_CMD_IOP_PREP_UPDATE_MPI	= 0x0001,
	MB_CMD_IOP_COMP_UPDATE_MPI	= 0x0002,
	MB_CMD_IOP_PREP_LINK_DOWN	= 0x0010,
	MB_CMD_IOP_DVR_START	 = 0x0100,
	MB_CMD_IOP_FLASH_ACC	 = 0x0101,
	MB_CMD_IOP_RESTART_MPI	= 0x0102,
	MB_CMD_IOP_CORE_DUMP_MPI	= 0x0103,

	/* Mailbox Command Status. */
	MB_CMD_STS_GOOD = 0x00004000,	/* Success. */
	MB_CMD_STS_INTRMDT = 0x00001000,	/* Intermediate Complete. */
	MB_CMD_STS_INVLD_CMD = 0x00004001,	/* Invalid. */
	MB_CMD_STS_XFC_ERR = 0x00004002,	/* Interface Error. */
	MB_CMD_STS_CSUM_ERR = 0x00004003,	/* Csum Error. */
	MB_CMD_STS_ERR = 0x00004005,	/* System Error. */
	MB_CMD_STS_PARAM_ERR = 0x00004006,	/* Parameter Error. */
};

struct mbox_params {
	u32 mbox_in[MAILBOX_COUNT];
	u32 mbox_out[MAILBOX_COUNT];
	int in_count;
	int out_count;
};

struct flash_params_8012 {
	u8 dev_id_str[4];
	__le16 size;
	__le16 csum;
	__le16 ver;
	__le16 sub_dev_id;
	u8 mac_addr[6];
	__le16 res;
};

/* 8000 device's flash is a different structure
 * at a different offset in flash.
 */
#define FUNC0_FLASH_OFFSET 0x140200
#define FUNC1_FLASH_OFFSET 0x140600

/* Flash related data structures. */
struct flash_params_8000 {
	u8 dev_id_str[4];	/* "8000" */
	__le16 ver;
	__le16 size;
	__le16 csum;
	__le16 reserved0;
	__le16 total_size;
	__le16 entry_count;
	u8 data_type0;
	u8 data_size0;
	u8 mac_addr[6];
	u8 data_type1;
	u8 data_size1;
	u8 mac_addr1[6];
	u8 data_type2;
	u8 data_size2;
	__le16 vlan_id;
	u8 data_type3;
	u8 data_size3;
	__le16 last;
	u8 reserved1[464];
	__le16	subsys_ven_id;
	__le16	subsys_dev_id;
	u8 reserved2[4];
};

union flash_params {
	struct flash_params_8012 flash_params_8012;
	struct flash_params_8000 flash_params_8000;
};

/*
 * doorbell space for the rx ring context
 */
struct rx_doorbell_context {
	u32 cnsmr_idx;		/* 0x00 */
	u32 valid;		/* 0x04 */
	u32 reserved[4];	/* 0x08-0x14 */
	u32 lbq_prod_idx;	/* 0x18 */
	u32 sbq_prod_idx;	/* 0x1c */
};

/*
 * doorbell space for the tx ring context
 */
struct tx_doorbell_context {
	u32 prod_idx;		/* 0x00 */
	u32 valid;		/* 0x04 */
	u32 reserved[4];	/* 0x08-0x14 */
	u32 lbq_prod_idx;	/* 0x18 */
	u32 sbq_prod_idx;	/* 0x1c */
};

/* DATA STRUCTURES SHARED WITH HARDWARE. */
struct tx_buf_desc {
	__le64 addr;
	__le32 len;
#define TX_DESC_LEN_MASK	0x000fffff
#define TX_DESC_C	0x40000000
#define TX_DESC_E	0x80000000
} __attribute((packed));

/*
 * IOCB Definitions...
 */

#define OPCODE_OB_MAC_IOCB 			0x01
#define OPCODE_OB_MAC_TSO_IOCB		0x02
#define OPCODE_IB_MAC_IOCB			0x20
#define OPCODE_IB_MPI_IOCB			0x21
#define OPCODE_IB_AE_IOCB			0x3f

struct ob_mac_iocb_req {
	u8 opcode;
	u8 flags1;
#define OB_MAC_IOCB_REQ_OI	0x01
#define OB_MAC_IOCB_REQ_I	0x02
#define OB_MAC_IOCB_REQ_D	0x08
#define OB_MAC_IOCB_REQ_F	0x10
	u8 flags2;
	u8 flags3;
#define OB_MAC_IOCB_DFP	0x02
#define OB_MAC_IOCB_V	0x04
	__le32 reserved1[2];
	__le16 frame_len;
#define OB_MAC_IOCB_LEN_MASK 0x3ffff
	__le16 reserved2;
	u32 tid;
	u32 txq_idx;
	__le32 reserved3;
	__le16 vlan_tci;
	__le16 reserved4;
	struct tx_buf_desc tbd[TX_DESC_PER_IOCB];
} __attribute((packed));

struct ob_mac_iocb_rsp {
	u8 opcode;		/* */
	u8 flags1;		/* */
#define OB_MAC_IOCB_RSP_OI	0x01	/* */
#define OB_MAC_IOCB_RSP_I	0x02	/* */
#define OB_MAC_IOCB_RSP_E	0x08	/* */
#define OB_MAC_IOCB_RSP_S	0x10	/* too Short */
#define OB_MAC_IOCB_RSP_L	0x20	/* too Large */
#define OB_MAC_IOCB_RSP_P	0x40	/* Padded */
	u8 flags2;		/* */
	u8 flags3;		/* */
#define OB_MAC_IOCB_RSP_B	0x80	/* */
	u32 tid;
	u32 txq_idx;
	__le32 reserved[13];
} __attribute((packed));

struct ob_mac_tso_iocb_req {
	u8 opcode;
	u8 flags1;
#define OB_MAC_TSO_IOCB_OI	0x01
#define OB_MAC_TSO_IOCB_I	0x02
#define OB_MAC_TSO_IOCB_D	0x08
#define OB_MAC_TSO_IOCB_IP4	0x40
#define OB_MAC_TSO_IOCB_IP6	0x80
	u8 flags2;
#define OB_MAC_TSO_IOCB_LSO	0x20
#define OB_MAC_TSO_IOCB_UC	0x40
#define OB_MAC_TSO_IOCB_TC	0x80
	u8 flags3;
#define OB_MAC_TSO_IOCB_IC	0x01
#define OB_MAC_TSO_IOCB_DFP	0x02
#define OB_MAC_TSO_IOCB_V	0x04
	__le32 reserved1[2];
	__le32 frame_len;
	u32 tid;
	u32 txq_idx;
	__le16 total_hdrs_len;
	__le16 net_trans_offset;
#define OB_MAC_TRANSPORT_HDR_SHIFT 6
	__le16 vlan_tci;
	__le16 mss;
	struct tx_buf_desc tbd[TX_DESC_PER_IOCB];
} __attribute((packed));

struct ob_mac_tso_iocb_rsp {
	u8 opcode;
	u8 flags1;
#define OB_MAC_TSO_IOCB_RSP_OI	0x01
#define OB_MAC_TSO_IOCB_RSP_I	0x02
#define OB_MAC_TSO_IOCB_RSP_E	0x08
#define OB_MAC_TSO_IOCB_RSP_S	0x10
#define OB_MAC_TSO_IOCB_RSP_L	0x20
#define OB_MAC_TSO_IOCB_RSP_P	0x40
	u8 flags2;		/* */
	u8 flags3;		/* */
#define OB_MAC_TSO_IOCB_RSP_B	0x8000
	u32 tid;
	u32 txq_idx;
	__le32 reserved2[13];
} __attribute((packed));

struct ib_mac_iocb_rsp {
	u8 opcode;		/* 0x20 */
	u8 flags1;
#define IB_MAC_IOCB_RSP_OI	0x01	/* Overide intr delay */
#define IB_MAC_IOCB_RSP_I	0x02	/* Disble Intr Generation */
#define IB_MAC_CSUM_ERR_MASK 0x1c	/* A mask to use for csum errs */
#define IB_MAC_IOCB_RSP_TE	0x04	/* Checksum error */
#define IB_MAC_IOCB_RSP_NU	0x08	/* No checksum rcvd */
#define IB_MAC_IOCB_RSP_IE	0x10	/* IPv4 checksum error */
#define IB_MAC_IOCB_RSP_M_MASK	0x60	/* Multicast info */
#define IB_MAC_IOCB_RSP_M_NONE	0x00	/* Not mcast frame */
#define IB_MAC_IOCB_RSP_M_HASH	0x20	/* HASH mcast frame */
#define IB_MAC_IOCB_RSP_M_REG 	0x40	/* Registered mcast frame */
#define IB_MAC_IOCB_RSP_M_PROM 	0x60	/* Promiscuous mcast frame */
#define IB_MAC_IOCB_RSP_B	0x80	/* Broadcast frame */
	u8 flags2;
#define IB_MAC_IOCB_RSP_P	0x01	/* Promiscuous frame */
#define IB_MAC_IOCB_RSP_V	0x02	/* Vlan tag present */
#define IB_MAC_IOCB_RSP_ERR_MASK	0x1c	/*  */
#define IB_MAC_IOCB_RSP_ERR_CODE_ERR	0x04
#define IB_MAC_IOCB_RSP_ERR_OVERSIZE	0x08
#define IB_MAC_IOCB_RSP_ERR_UNDERSIZE	0x10
#define IB_MAC_IOCB_RSP_ERR_PREAMBLE	0x14
#define IB_MAC_IOCB_RSP_ERR_FRAME_LEN	0x18
#define IB_MAC_IOCB_RSP_ERR_CRC		0x1c
#define IB_MAC_IOCB_RSP_U	0x20	/* UDP packet */
#define IB_MAC_IOCB_RSP_T	0x40	/* TCP packet */
#define IB_MAC_IOCB_RSP_FO	0x80	/* Failover port */
	u8 flags3;
#define IB_MAC_IOCB_RSP_RSS_MASK	0x07	/* RSS mask */
#define IB_MAC_IOCB_RSP_M_NONE	0x00	/* No RSS match */
#define IB_MAC_IOCB_RSP_M_IPV4	0x04	/* IPv4 RSS match */
#define IB_MAC_IOCB_RSP_M_IPV6	0x02	/* IPv6 RSS match */
#define IB_MAC_IOCB_RSP_M_TCP_V4 	0x05	/* TCP with IPv4 */
#define IB_MAC_IOCB_RSP_M_TCP_V6 	0x03	/* TCP with IPv6 */
#define IB_MAC_IOCB_RSP_V4	0x08	/* IPV4 */
#define IB_MAC_IOCB_RSP_V6	0x10	/* IPV6 */
#define IB_MAC_IOCB_RSP_IH	0x20	/* Split after IP header */
#define IB_MAC_IOCB_RSP_DS	0x40	/* data is in small buffer */
#define IB_MAC_IOCB_RSP_DL	0x80	/* data is in large buffer */
	__le32 data_len;	/* */
	__le64 data_addr;	/* */
	__le32 rss;		/* */
	__le16 vlan_id;		/* 12 bits */
#define IB_MAC_IOCB_RSP_C	0x1000	/* VLAN CFI bit */
#define IB_MAC_IOCB_RSP_COS_SHIFT	12	/* class of service value */
#define IB_MAC_IOCB_RSP_VLAN_MASK	0x0ffff

	__le16 reserved1;
	__le32 reserved2[6];
	u8 reserved3[3];
	u8 flags4;
#define IB_MAC_IOCB_RSP_HV	0x20
#define IB_MAC_IOCB_RSP_HS	0x40
#define IB_MAC_IOCB_RSP_HL	0x80
	__le32 hdr_len;		/* */
	__le64 hdr_addr;	/* */
} __attribute((packed));

struct ib_ae_iocb_rsp {
	u8 opcode;
	u8 flags1;
#define IB_AE_IOCB_RSP_OI		0x01
#define IB_AE_IOCB_RSP_I		0x02
	u8 event;
#define LINK_UP_EVENT              0x00
#define LINK_DOWN_EVENT            0x01
#define CAM_LOOKUP_ERR_EVENT       0x06
#define SOFT_ECC_ERROR_EVENT       0x07
#define MGMT_ERR_EVENT             0x08
#define TEN_GIG_MAC_EVENT          0x09
#define GPI0_H2L_EVENT       	0x10
#define GPI0_L2H_EVENT       	0x20
#define GPI1_H2L_EVENT       	0x11
#define GPI1_L2H_EVENT       	0x21
#define PCI_ERR_ANON_BUF_RD        0x40
	u8 q_id;
	__le32 reserved[15];
} __attribute((packed));

/*
 * These three structures are for generic
 * handling of ib and ob iocbs.
 */
struct ql_net_rsp_iocb {
	u8 opcode;
	u8 flags0;
	__le16 length;
	__le32 tid;
	__le32 reserved[14];
} __attribute((packed));

struct net_req_iocb {
	u8 opcode;
	u8 flags0;
	__le16 flags1;
	__le32 tid;
	__le32 reserved1[30];
} __attribute((packed));

/*
 * tx ring initialization control block for chip.
 * It is defined as:
 * "Work Queue Initialization Control Block"
 */
struct wqicb {
	__le16 len;
#define Q_LEN_V		(1 << 4)
#define Q_LEN_CPP_CONT	0x0000
#define Q_LEN_CPP_16	0x0001
#define Q_LEN_CPP_32	0x0002
#define Q_LEN_CPP_64	0x0003
#define Q_LEN_CPP_512	0x0006
	__le16 flags;
#define Q_PRI_SHIFT	1
#define Q_FLAGS_LC	0x1000
#define Q_FLAGS_LB	0x2000
#define Q_FLAGS_LI	0x4000
#define Q_FLAGS_LO	0x8000
	__le16 cq_id_rss;
#define Q_CQ_ID_RSS_RV 0x8000
	__le16 rid;
	__le64 addr;
	__le64 cnsmr_idx_addr;
} __attribute((packed));

/*
 * rx ring initialization control block for chip.
 * It is defined as:
 * "Completion Queue Initialization Control Block"
 */
struct cqicb {
	u8 msix_vect;
	u8 reserved1;
	u8 reserved2;
	u8 flags;
#define FLAGS_LV	0x08
#define FLAGS_LS	0x10
#define FLAGS_LL	0x20
#define FLAGS_LI	0x40
#define FLAGS_LC	0x80
	__le16 len;
#define LEN_V		(1 << 4)
#define LEN_CPP_CONT	0x0000
#define LEN_CPP_32	0x0001
#define LEN_CPP_64	0x0002
#define LEN_CPP_128	0x0003
	__le16 rid;
	__le64 addr;
	__le64 prod_idx_addr;
	__le16 pkt_delay;
	__le16 irq_delay;
	__le64 lbq_addr;
	__le16 lbq_buf_size;
	__le16 lbq_len;		/* entry count */
	__le64 sbq_addr;
	__le16 sbq_buf_size;
	__le16 sbq_len;		/* entry count */
} __attribute((packed));

struct ricb {
	u8 base_cq;
#define RSS_L4K 0x80
	u8 flags;
#define RSS_L6K 0x01
#define RSS_LI  0x02
#define RSS_LB  0x04
#define RSS_LM  0x08
#define RSS_RI4 0x10
#define RSS_RT4 0x20
#define RSS_RI6 0x40
#define RSS_RT6 0x80
	__le16 mask;
	u8 hash_cq_id[1024];
	__le32 ipv6_hash_key[10];
	__le32 ipv4_hash_key[4];
} __attribute((packed));

/* SOFTWARE/DRIVER DATA STRUCTURES. */

struct oal {
	struct tx_buf_desc oal[TX_DESC_PER_OAL];
};

struct map_list {
	DECLARE_PCI_UNMAP_ADDR(mapaddr);
	DECLARE_PCI_UNMAP_LEN(maplen);
};

struct tx_ring_desc {
	struct sk_buff *skb;
	struct ob_mac_iocb_req *queue_entry;
	u32 index;
	struct oal oal;
	struct map_list map[MAX_SKB_FRAGS + 1];
	int map_cnt;
	struct tx_ring_desc *next;
};

struct page_chunk {
	struct page *page;	/* master page */
	char *va;		/* virt addr for this chunk */
	u64 map;		/* mapping for master */
	unsigned int offset;	/* offset for this chunk */
	unsigned int last_flag; /* flag set for last chunk in page */
};

struct bq_desc {
	union {
		struct page_chunk pg_chunk;
		struct sk_buff *skb;
	} p;
	__le64 *addr;
	u32 index;
	 DECLARE_PCI_UNMAP_ADDR(mapaddr);
	 DECLARE_PCI_UNMAP_LEN(maplen);
};

#define QL_TXQ_IDX(qdev, skb) (smp_processor_id()%(qdev->tx_ring_count))

struct tx_ring {
	/*
	 * queue info.
	 */
	struct wqicb wqicb;	/* structure used to inform chip of new queue */
	void *wq_base;		/* pci_alloc:virtual addr for tx */
	dma_addr_t wq_base_dma;	/* pci_alloc:dma addr for tx */
	__le32 *cnsmr_idx_sh_reg;	/* shadow copy of consumer idx */
	dma_addr_t cnsmr_idx_sh_reg_dma;	/* dma-shadow copy of consumer */
	u32 wq_size;		/* size in bytes of queue area */
	u32 wq_len;		/* number of entries in queue */
	void __iomem *prod_idx_db_reg;	/* doorbell area index reg at offset 0x00 */
	void __iomem *valid_db_reg;	/* doorbell area valid reg at offset 0x04 */
	u16 prod_idx;		/* current value for prod idx */
	u16 cq_id;		/* completion (rx) queue for tx completions */
	u8 wq_id;		/* queue id for this entry */
	u8 reserved1[3];
	struct tx_ring_desc *q;	/* descriptor list for the queue */
	spinlock_t lock;
	atomic_t tx_count;	/* counts down for every outstanding IO */
	atomic_t queue_stopped;	/* Turns queue off when full. */
	struct delayed_work tx_work;
	struct ql_adapter *qdev;
};

/*
 * Type of inbound queue.
 */
enum {
	DEFAULT_Q = 2,		/* Handles slow queue and chip/MPI events. */
	TX_Q = 3,		/* Handles outbound completions. */
	RX_Q = 4,		/* Handles inbound completions. */
};

struct rx_ring {
	struct cqicb cqicb;	/* The chip's completion queue init control block. */

	/* Completion queue elements. */
	void *cq_base;
	dma_addr_t cq_base_dma;
	u32 cq_size;
	u32 cq_len;
	u16 cq_id;
	__le32 *prod_idx_sh_reg;	/* Shadowed producer register. */
	dma_addr_t prod_idx_sh_reg_dma;
	void __iomem *cnsmr_idx_db_reg;	/* PCI doorbell mem area + 0 */
	u32 cnsmr_idx;		/* current sw idx */
	struct ql_net_rsp_iocb *curr_entry;	/* next entry on queue */
	void __iomem *valid_db_reg;	/* PCI doorbell mem area + 0x04 */

	/* Large buffer queue elements. */
	u32 lbq_len;		/* entry count */
	u32 lbq_size;		/* size in bytes of queue */
	u32 lbq_buf_size;
	void *lbq_base;
	dma_addr_t lbq_base_dma;
	void *lbq_base_indirect;
	dma_addr_t lbq_base_indirect_dma;
	struct page_chunk pg_chunk; /* current page for chunks */
	struct bq_desc *lbq;	/* array of control blocks */
	void __iomem *lbq_prod_idx_db_reg;	/* PCI doorbell mem area + 0x18 */
	u32 lbq_prod_idx;	/* current sw prod idx */
	u32 lbq_curr_idx;	/* next entry we expect */
	u32 lbq_clean_idx;	/* beginning of new descs */
	u32 lbq_free_cnt;	/* free buffer desc cnt */

	/* Small buffer queue elements. */
	u32 sbq_len;		/* entry count */
	u32 sbq_size;		/* size in bytes of queue */
	u32 sbq_buf_size;
	void *sbq_base;
	dma_addr_t sbq_base_dma;
	void *sbq_base_indirect;
	dma_addr_t sbq_base_indirect_dma;
	struct bq_desc *sbq;	/* array of control blocks */
	void __iomem *sbq_prod_idx_db_reg; /* PCI doorbell mem area + 0x1c */
	u32 sbq_prod_idx;	/* current sw prod idx */
	u32 sbq_curr_idx;	/* next entry we expect */
	u32 sbq_clean_idx;	/* beginning of new descs */
	u32 sbq_free_cnt;	/* free buffer desc cnt */

	/* Misc. handler elements. */
	u32 type;		/* Type of queue, tx, rx. */
	u32 irq;		/* Which vector this ring is assigned. */
	u32 cpu;		/* Which CPU this should run on. */
	char name[IFNAMSIZ + 5];
	struct napi_struct napi;
	u8 reserved;
	struct ql_adapter *qdev;
};

/*
 * RSS Initialization Control Block
 */
struct hash_id {
	u8 value[4];
};

struct nic_stats {
	/*
	 * These stats come from offset 200h to 278h
	 * in the XGMAC register.
	 */
	u64 tx_pkts;
	u64 tx_bytes;
	u64 tx_mcast_pkts;
	u64 tx_bcast_pkts;
	u64 tx_ucast_pkts;
	u64 tx_ctl_pkts;
	u64 tx_pause_pkts;
	u64 tx_64_pkt;
	u64 tx_65_to_127_pkt;
	u64 tx_128_to_255_pkt;
	u64 tx_256_511_pkt;
	u64 tx_512_to_1023_pkt;
	u64 tx_1024_to_1518_pkt;
	u64 tx_1519_to_max_pkt;
	u64 tx_undersize_pkt;
	u64 tx_oversize_pkt;

	/*
	 * These stats come from offset 300h to 3C8h
	 * in the XGMAC register.
	 */
	u64 rx_bytes;
	u64 rx_bytes_ok;
	u64 rx_pkts;
	u64 rx_pkts_ok;
	u64 rx_bcast_pkts;
	u64 rx_mcast_pkts;
	u64 rx_ucast_pkts;
	u64 rx_undersize_pkts;
	u64 rx_oversize_pkts;
	u64 rx_jabber_pkts;
	u64 rx_undersize_fcerr_pkts;
	u64 rx_drop_events;
	u64 rx_fcerr_pkts;
	u64 rx_align_err;
	u64 rx_symbol_err;
	u64 rx_mac_err;
	u64 rx_ctl_pkts;
	u64 rx_pause_pkts;
	u64 rx_64_pkts;
	u64 rx_65_to_127_pkts;
	u64 rx_128_255_pkts;
	u64 rx_256_511_pkts;
	u64 rx_512_to_1023_pkts;
	u64 rx_1024_to_1518_pkts;
	u64 rx_1519_to_max_pkts;
	u64 rx_len_err_pkts;
	/*
	 * These stats come from offset 500h to 5C8h
	 * in the XGMAC register.
	 */
	u64 tx_cbfc_pause_frames0;
	u64 tx_cbfc_pause_frames1;
	u64 tx_cbfc_pause_frames2;
	u64 tx_cbfc_pause_frames3;
	u64 tx_cbfc_pause_frames4;
	u64 tx_cbfc_pause_frames5;
	u64 tx_cbfc_pause_frames6;
	u64 tx_cbfc_pause_frames7;
	u64 rx_cbfc_pause_frames0;
	u64 rx_cbfc_pause_frames1;
	u64 rx_cbfc_pause_frames2;
	u64 rx_cbfc_pause_frames3;
	u64 rx_cbfc_pause_frames4;
	u64 rx_cbfc_pause_frames5;
	u64 rx_cbfc_pause_frames6;
	u64 rx_cbfc_pause_frames7;
	u64 rx_nic_fifo_drop;
};

/* Address/Length pairs for the coredump. */
enum {
	MPI_CORE_REGS_ADDR = 0x00030000,
	MPI_CORE_REGS_CNT = 127,
	MPI_CORE_SH_REGS_CNT = 16,
	TEST_REGS_ADDR = 0x00001000,
	TEST_REGS_CNT = 23,
	RMII_REGS_ADDR = 0x00001040,
	RMII_REGS_CNT = 64,
	FCMAC1_REGS_ADDR = 0x00001080,
	FCMAC2_REGS_ADDR = 0x000010c0,
	FCMAC_REGS_CNT = 64,
	FC1_MBX_REGS_ADDR = 0x00001100,
	FC2_MBX_REGS_ADDR = 0x00001240,
	FC_MBX_REGS_CNT = 64,
	IDE_REGS_ADDR = 0x00001140,
	IDE_REGS_CNT = 64,
	NIC1_MBX_REGS_ADDR = 0x00001180,
	NIC2_MBX_REGS_ADDR = 0x00001280,
	NIC_MBX_REGS_CNT = 64,
	SMBUS_REGS_ADDR = 0x00001200,
	SMBUS_REGS_CNT = 64,
	I2C_REGS_ADDR = 0x00001fc0,
	I2C_REGS_CNT = 64,
	MEMC_REGS_ADDR = 0x00003000,
	MEMC_REGS_CNT = 256,
	PBUS_REGS_ADDR = 0x00007c00,
	PBUS_REGS_CNT = 256,
	MDE_REGS_ADDR = 0x00010000,
	MDE_REGS_CNT = 6,
	CODE_RAM_ADDR = 0x00020000,
	CODE_RAM_CNT = 0x2000,
	MEMC_RAM_ADDR = 0x00100000,
	MEMC_RAM_CNT = 0x2000,
};

#define MPI_COREDUMP_COOKIE 0x5555aaaa
struct mpi_coredump_global_header {
	u32	cookie;
	u8	idString[16];
	u32	timeLo;
	u32	timeHi;
	u32	imageSize;
	u32	headerSize;
	u8	info[220];
};

struct mpi_coredump_segment_header {
	u32	cookie;
	u32	segNum;
	u32	segSize;
	u32	extra;
	u8	description[16];
};

/* Reg dump segment numbers. */
enum {
	CORE_SEG_NUM = 1,
	TEST_LOGIC_SEG_NUM = 2,
	RMII_SEG_NUM = 3,
	FCMAC1_SEG_NUM = 4,
	FCMAC2_SEG_NUM = 5,
	FC1_MBOX_SEG_NUM = 6,
	IDE_SEG_NUM = 7,
	NIC1_MBOX_SEG_NUM = 8,
	SMBUS_SEG_NUM = 9,
	FC2_MBOX_SEG_NUM = 10,
	NIC2_MBOX_SEG_NUM = 11,
	I2C_SEG_NUM = 12,
	MEMC_SEG_NUM = 13,
	PBUS_SEG_NUM = 14,
	MDE_SEG_NUM = 15,
	NIC1_CONTROL_SEG_NUM = 16,
	NIC2_CONTROL_SEG_NUM = 17,
	NIC1_XGMAC_SEG_NUM = 18,
	NIC2_XGMAC_SEG_NUM = 19,
	WCS_RAM_SEG_NUM = 20,
	MEMC_RAM_SEG_NUM = 21,
	XAUI_AN_SEG_NUM = 22,
	XAUI_HSS_PCS_SEG_NUM = 23,
	XFI_AN_SEG_NUM = 24,
	XFI_TRAIN_SEG_NUM = 25,
	XFI_HSS_PCS_SEG_NUM = 26,
	XFI_HSS_TX_SEG_NUM = 27,
	XFI_HSS_RX_SEG_NUM = 28,
	XFI_HSS_PLL_SEG_NUM = 29,
	MISC_NIC_INFO_SEG_NUM = 30,
	INTR_STATES_SEG_NUM = 31,
	CAM_ENTRIES_SEG_NUM = 32,
	ROUTING_WORDS_SEG_NUM = 33,
	ETS_SEG_NUM = 34,
	PROBE_DUMP_SEG_NUM = 35,
	ROUTING_INDEX_SEG_NUM = 36,
	MAC_PROTOCOL_SEG_NUM = 37,
	XAUI2_AN_SEG_NUM = 38,
	XAUI2_HSS_PCS_SEG_NUM = 39,
	XFI2_AN_SEG_NUM = 40,
	XFI2_TRAIN_SEG_NUM = 41,
	XFI2_HSS_PCS_SEG_NUM = 42,
	XFI2_HSS_TX_SEG_NUM = 43,
	XFI2_HSS_RX_SEG_NUM = 44,
	XFI2_HSS_PLL_SEG_NUM = 45,
	SEM_REGS_SEG_NUM = 50

};

struct ql_nic_misc {
	u32 rx_ring_count;
	u32 tx_ring_count;
	u32 intr_count;
	u32 function;
};

struct ql_reg_dump {

	/* segment 0 */
	struct mpi_coredump_global_header mpi_global_header;

	/* segment 16 */
	struct mpi_coredump_segment_header nic_regs_seg_hdr;
	u32 nic_regs[64];

	/* segment 30 */
	struct mpi_coredump_segment_header misc_nic_seg_hdr;
	struct ql_nic_misc misc_nic_info;

	/* segment 31 */
	/* one interrupt state for each CQ */
	struct mpi_coredump_segment_header intr_states_seg_hdr;
	u32 intr_states[MAX_CPUS];

	/* segment 32 */
	/* 3 cam words each for 16 unicast,
	 * 2 cam words for each of 32 multicast.
	 */
	struct mpi_coredump_segment_header cam_entries_seg_hdr;
	u32 cam_entries[(16 * 3) + (32 * 3)];

	/* segment 33 */
	struct mpi_coredump_segment_header nic_routing_words_seg_hdr;
	u32 nic_routing_words[16];

	/* segment 34 */
	struct mpi_coredump_segment_header ets_seg_hdr;
	u32 ets[8+2];
};

/*
 * intr_context structure is used during initialization
 * to hook the interrupts.  It is also used in a single
 * irq environment as a context to the ISR.
 */
struct intr_context {
	struct ql_adapter *qdev;
	u32 intr;
	u32 irq_mask;		/* Mask of which rings the vector services. */
	u32 hooked;
	u32 intr_en_mask;	/* value/mask used to enable this intr */
	u32 intr_dis_mask;	/* value/mask used to disable this intr */
	u32 intr_read_mask;	/* value/mask used to read this intr */
	char name[IFNAMSIZ * 2];
	atomic_t irq_cnt;	/* irq_cnt is used in single vector
				 * environment.  It's incremented for each
				 * irq handler that is scheduled.  When each
				 * handler finishes it decrements irq_cnt and
				 * enables interrupts if it's zero. */
	irq_handler_t handler;
};

/* adapter flags definitions. */
enum {
	QL_ADAPTER_UP = 0,	/* Adapter has been brought up. */
	QL_LEGACY_ENABLED = 1,
	QL_MSI_ENABLED = 2,
	QL_MSIX_ENABLED = 3,
	QL_DMA64 = 4,
	QL_PROMISCUOUS = 5,
	QL_ALLMULTI = 6,
	QL_PORT_CFG = 7,
	QL_CAM_RT_SET = 8,
	QL_SELFTEST = 9,
	QL_LB_LINK_UP = 10,
};

/* link_status bit definitions */
enum {
	STS_LOOPBACK_MASK = 0x00000700,
	STS_LOOPBACK_PCS = 0x00000100,
	STS_LOOPBACK_HSS = 0x00000200,
	STS_LOOPBACK_EXT = 0x00000300,
	STS_PAUSE_MASK = 0x000000c0,
	STS_PAUSE_STD = 0x00000040,
	STS_PAUSE_PRI = 0x00000080,
	STS_SPEED_MASK = 0x00000038,
	STS_SPEED_100Mb = 0x00000000,
	STS_SPEED_1Gb = 0x00000008,
	STS_SPEED_10Gb = 0x00000010,
	STS_LINK_TYPE_MASK = 0x00000007,
	STS_LINK_TYPE_XFI = 0x00000001,
	STS_LINK_TYPE_XAUI = 0x00000002,
	STS_LINK_TYPE_XFI_BP = 0x00000003,
	STS_LINK_TYPE_XAUI_BP = 0x00000004,
	STS_LINK_TYPE_10GBASET = 0x00000005,
};

/* link_config bit definitions */
enum {
	CFG_JUMBO_FRAME_SIZE = 0x00010000,
	CFG_PAUSE_MASK = 0x00000060,
	CFG_PAUSE_STD = 0x00000020,
	CFG_PAUSE_PRI = 0x00000040,
	CFG_DCBX = 0x00000010,
	CFG_LOOPBACK_MASK = 0x00000007,
	CFG_LOOPBACK_PCS = 0x00000002,
	CFG_LOOPBACK_HSS = 0x00000004,
	CFG_LOOPBACK_EXT = 0x00000006,
	CFG_DEFAULT_MAX_FRAME_SIZE = 0x00002580,
};

struct nic_operations {

	int (*get_flash) (struct ql_adapter *);
	int (*port_initialize) (struct ql_adapter *);
};

/*
 * The main Adapter structure definition.
 * This structure has all fields relevant to the hardware.
 */
struct ql_adapter {
	struct ricb ricb;
	unsigned long flags;
	u32 wol;

	struct nic_stats nic_stats;

	struct vlan_group *vlgrp;

	/* PCI Configuration information for this device */
	struct pci_dev *pdev;
	struct net_device *ndev;	/* Parent NET device */

	/* Hardware information */
	u32 chip_rev_id;
	u32 fw_rev_id;
	u32 func;		/* PCI function for this adapter */
	u32 alt_func;		/* PCI function for alternate adapter */
	u32 port;		/* Port number this adapter */

	spinlock_t adapter_lock;
	spinlock_t hw_lock;
	spinlock_t stats_lock;

	/* PCI Bus Relative Register Addresses */
	void __iomem *reg_base;
	void __iomem *doorbell_area;
	u32 doorbell_area_size;

	u32 msg_enable;

	/* Page for Shadow Registers */
	void *rx_ring_shadow_reg_area;
	dma_addr_t rx_ring_shadow_reg_dma;
	void *tx_ring_shadow_reg_area;
	dma_addr_t tx_ring_shadow_reg_dma;

	u32 mailbox_in;
	u32 mailbox_out;
	struct mbox_params idc_mbc;

	int tx_ring_size;
	int rx_ring_size;
	u32 intr_count;
	struct msix_entry *msi_x_entry;
	struct intr_context intr_context[MAX_RX_RINGS];

	int tx_ring_count;	/* One per online CPU. */
	u32 rss_ring_count;	/* One per irq vector.  */
	/*
	 * rx_ring_count =
	 *  (CPU count * outbound completion rx_ring) +
	 *  (irq_vector_cnt * inbound (RSS) completion rx_ring)
	 */
	int rx_ring_count;
	int ring_mem_size;
	void *ring_mem;

	struct rx_ring rx_ring[MAX_RX_RINGS];
	struct tx_ring tx_ring[MAX_TX_RINGS];
	unsigned int lbq_buf_order;

	int rx_csum;
	u32 default_rx_queue;

	u16 rx_coalesce_usecs;	/* cqicb->int_delay */
	u16 rx_max_coalesced_frames;	/* cqicb->pkt_int_delay */
	u16 tx_coalesce_usecs;	/* cqicb->int_delay */
	u16 tx_max_coalesced_frames;	/* cqicb->pkt_int_delay */

	u32 xg_sem_mask;
	u32 port_link_up;
	u32 port_init;
	u32 link_status;
	u32 link_config;
	u32 led_config;
	u32 max_frame_size;

	union flash_params flash;

	struct workqueue_struct *workqueue;
	struct delayed_work asic_reset_work;
	struct delayed_work mpi_reset_work;
	struct delayed_work mpi_work;
	struct delayed_work mpi_port_cfg_work;
	struct delayed_work mpi_idc_work;
	struct completion ide_completion;
	struct nic_operations *nic_ops;
	u16 device_id;
	atomic_t lb_count;
};

/*
 * Typical Register accessor for memory mapped device.
 */
static inline u32 ql_read32(const struct ql_adapter *qdev, int reg)
{
	return readl(qdev->reg_base + reg);
}

/*
 * Typical Register accessor for memory mapped device.
 */
static inline void ql_write32(const struct ql_adapter *qdev, int reg, u32 val)
{
	writel(val, qdev->reg_base + reg);
}

/*
 * Doorbell Registers:
 * Doorbell registers are virtual registers in the PCI memory space.
 * The space is allocated by the chip during PCI initialization.  The
 * device driver finds the doorbell address in BAR 3 in PCI config space.
 * The registers are used to control outbound and inbound queues. For
 * example, the producer index for an outbound queue.  Each queue uses
 * 1 4k chunk of memory.  The lower half of the space is for outbound
 * queues. The upper half is for inbound queues.
 */
static inline void ql_write_db_reg(u32 val, void __iomem *addr)
{
	writel(val, addr);
	mmiowb();
}

/*
 * Shadow Registers:
 * Outbound queues have a consumer index that is maintained by the chip.
 * Inbound queues have a producer index that is maintained by the chip.
 * For lower overhead, these registers are "shadowed" to host memory
 * which allows the device driver to track the queue progress without
 * PCI reads. When an entry is placed on an inbound queue, the chip will
 * update the relevant index register and then copy the value to the
 * shadow register in host memory.
 */
static inline u32 ql_read_sh_reg(__le32  *addr)
{
	u32 reg;
	reg =  le32_to_cpu(*addr);
	rmb();
	return reg;
}

extern char qlge_driver_name[];
extern const char qlge_driver_version[];
extern const struct ethtool_ops qlge_ethtool_ops;

extern int ql_sem_spinlock(struct ql_adapter *qdev, u32 sem_mask);
extern void ql_sem_unlock(struct ql_adapter *qdev, u32 sem_mask);
extern int ql_read_xgmac_reg(struct ql_adapter *qdev, u32 reg, u32 *data);
extern int ql_get_mac_addr_reg(struct ql_adapter *qdev, u32 type, u16 index,
			       u32 *value);
extern int ql_get_routing_reg(struct ql_adapter *qdev, u32 index, u32 *value);
extern int ql_write_cfg(struct ql_adapter *qdev, void *ptr, int size, u32 bit,
			u16 q_id);
void ql_queue_fw_error(struct ql_adapter *qdev);
void ql_mpi_work(struct work_struct *work);
void ql_mpi_reset_work(struct work_struct *work);
int ql_wait_reg_rdy(struct ql_adapter *qdev, u32 reg, u32 bit, u32 ebit);
void ql_queue_asic_error(struct ql_adapter *qdev);
u32 ql_enable_completion_interrupt(struct ql_adapter *qdev, u32 intr);
void ql_set_ethtool_ops(struct net_device *ndev);
int ql_read_xgmac_reg64(struct ql_adapter *qdev, u32 reg, u64 *data);
void ql_mpi_idc_work(struct work_struct *work);
void ql_mpi_port_cfg_work(struct work_struct *work);
int ql_mb_get_fw_state(struct ql_adapter *qdev);
int ql_cam_route_initialize(struct ql_adapter *qdev);
int ql_read_mpi_reg(struct ql_adapter *qdev, u32 reg, u32 *data);
int ql_mb_about_fw(struct ql_adapter *qdev);
int ql_wol(struct ql_adapter *qdev);
int ql_mb_wol_set_magic(struct ql_adapter *qdev, u32 enable_wol);
int ql_mb_wol_mode(struct ql_adapter *qdev, u32 wol);
int ql_mb_set_led_cfg(struct ql_adapter *qdev, u32 led_config);
int ql_mb_get_led_cfg(struct ql_adapter *qdev);
void ql_link_on(struct ql_adapter *qdev);
void ql_link_off(struct ql_adapter *qdev);
int ql_mb_set_mgmnt_traffic_ctl(struct ql_adapter *qdev, u32 control);
int ql_mb_get_port_cfg(struct ql_adapter *qdev);
int ql_mb_set_port_cfg(struct ql_adapter *qdev);
int ql_wait_fifo_empty(struct ql_adapter *qdev);
void ql_gen_reg_dump(struct ql_adapter *qdev,
			struct ql_reg_dump *mpi_coredump);
netdev_tx_t ql_lb_send(struct sk_buff *skb, struct net_device *ndev);
void ql_check_lb_frame(struct ql_adapter *, struct sk_buff *);
int ql_clean_lb_rx_ring(struct rx_ring *rx_ring, int budget);

#if 1
#define QL_ALL_DUMP
#define QL_REG_DUMP
#define QL_DEV_DUMP
#define QL_CB_DUMP
/* #define QL_IB_DUMP */
/* #define QL_OB_DUMP */
#endif

#ifdef QL_REG_DUMP
extern void ql_dump_xgmac_control_regs(struct ql_adapter *qdev);
extern void ql_dump_routing_entries(struct ql_adapter *qdev);
extern void ql_dump_regs(struct ql_adapter *qdev);
#define QL_DUMP_REGS(qdev) ql_dump_regs(qdev)
#define QL_DUMP_ROUTE(qdev) ql_dump_routing_entries(qdev)
#define QL_DUMP_XGMAC_CONTROL_REGS(qdev) ql_dump_xgmac_control_regs(qdev)
#else
#define QL_DUMP_REGS(qdev)
#define QL_DUMP_ROUTE(qdev)
#define QL_DUMP_XGMAC_CONTROL_REGS(qdev)
#endif

#ifdef QL_STAT_DUMP
extern void ql_dump_stat(struct ql_adapter *qdev);
#define QL_DUMP_STAT(qdev) ql_dump_stat(qdev)
#else
#define QL_DUMP_STAT(qdev)
#endif

#ifdef QL_DEV_DUMP
extern void ql_dump_qdev(struct ql_adapter *qdev);
#define QL_DUMP_QDEV(qdev) ql_dump_qdev(qdev)
#else
#define QL_DUMP_QDEV(qdev)
#endif

#ifdef QL_CB_DUMP
extern void ql_dump_wqicb(struct wqicb *wqicb);
extern void ql_dump_tx_ring(struct tx_ring *tx_ring);
extern void ql_dump_ricb(struct ricb *ricb);
extern void ql_dump_cqicb(struct cqicb *cqicb);
extern void ql_dump_rx_ring(struct rx_ring *rx_ring);
extern void ql_dump_hw_cb(struct ql_adapter *qdev, int size, u32 bit, u16 q_id);
#define QL_DUMP_RICB(ricb) ql_dump_ricb(ricb)
#define QL_DUMP_WQICB(wqicb) ql_dump_wqicb(wqicb)
#define QL_DUMP_TX_RING(tx_ring) ql_dump_tx_ring(tx_ring)
#define QL_DUMP_CQICB(cqicb) ql_dump_cqicb(cqicb)
#define QL_DUMP_RX_RING(rx_ring) ql_dump_rx_ring(rx_ring)
#define QL_DUMP_HW_CB(qdev, size, bit, q_id) \
		ql_dump_hw_cb(qdev, size, bit, q_id)
#else
#define QL_DUMP_RICB(ricb)
#define QL_DUMP_WQICB(wqicb)
#define QL_DUMP_TX_RING(tx_ring)
#define QL_DUMP_CQICB(cqicb)
#define QL_DUMP_RX_RING(rx_ring)
#define QL_DUMP_HW_CB(qdev, size, bit, q_id)
#endif

#ifdef QL_OB_DUMP
extern void ql_dump_tx_desc(struct tx_buf_desc *tbd);
extern void ql_dump_ob_mac_iocb(struct ob_mac_iocb_req *ob_mac_iocb);
extern void ql_dump_ob_mac_rsp(struct ob_mac_iocb_rsp *ob_mac_rsp);
#define QL_DUMP_OB_MAC_IOCB(ob_mac_iocb) ql_dump_ob_mac_iocb(ob_mac_iocb)
#define QL_DUMP_OB_MAC_RSP(ob_mac_rsp) ql_dump_ob_mac_rsp(ob_mac_rsp)
#else
#define QL_DUMP_OB_MAC_IOCB(ob_mac_iocb)
#define QL_DUMP_OB_MAC_RSP(ob_mac_rsp)
#endif

#ifdef QL_IB_DUMP
extern void ql_dump_ib_mac_rsp(struct ib_mac_iocb_rsp *ib_mac_rsp);
#define QL_DUMP_IB_MAC_RSP(ib_mac_rsp) ql_dump_ib_mac_rsp(ib_mac_rsp)
#else
#define QL_DUMP_IB_MAC_RSP(ib_mac_rsp)
#endif

#ifdef	QL_ALL_DUMP
extern void ql_dump_all(struct ql_adapter *qdev);
#define QL_DUMP_ALL(qdev) ql_dump_all(qdev)
#else
#define QL_DUMP_ALL(qdev)
#endif

#endif /* _QLGE_H_ */
