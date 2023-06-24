/* SPDX-License-Identifier: GPL-2.0+ */
// Copyright (c) 2021-2021 Hisilicon Limited.

#ifndef __HCLGE_COMM_CMD_H
#define __HCLGE_COMM_CMD_H
#include <linux/types.h>

#include "hnae3.h"

#define HCLGE_COMM_CMD_FLAG_IN			BIT(0)
#define HCLGE_COMM_CMD_FLAG_NEXT		BIT(2)
#define HCLGE_COMM_CMD_FLAG_WR			BIT(3)
#define HCLGE_COMM_CMD_FLAG_NO_INTR		BIT(4)

#define HCLGE_COMM_SEND_SYNC(flag) \
	((flag) & HCLGE_COMM_CMD_FLAG_NO_INTR)

#define HCLGE_COMM_LINK_EVENT_REPORT_EN_B	0
#define HCLGE_COMM_NCSI_ERROR_REPORT_EN_B	1
#define HCLGE_COMM_PHY_IMP_EN_B			2
#define HCLGE_COMM_MAC_STATS_EXT_EN_B		3
#define HCLGE_COMM_SYNC_RX_RING_HEAD_EN_B	4
#define HCLGE_COMM_LLRS_FEC_EN_B		5

#define hclge_comm_dev_phy_imp_supported(ae_dev) \
	test_bit(HNAE3_DEV_SUPPORT_PHY_IMP_B, (ae_dev)->caps)

#define HCLGE_COMM_TYPE_CRQ			0
#define HCLGE_COMM_TYPE_CSQ			1

#define HCLGE_COMM_CMDQ_CLEAR_WAIT_TIME		200

/* bar registers for cmdq */
#define HCLGE_COMM_NIC_CSQ_BASEADDR_L_REG	0x27000
#define HCLGE_COMM_NIC_CSQ_BASEADDR_H_REG	0x27004
#define HCLGE_COMM_NIC_CSQ_DEPTH_REG		0x27008
#define HCLGE_COMM_NIC_CSQ_TAIL_REG		0x27010
#define HCLGE_COMM_NIC_CSQ_HEAD_REG		0x27014
#define HCLGE_COMM_NIC_CRQ_BASEADDR_L_REG	0x27018
#define HCLGE_COMM_NIC_CRQ_BASEADDR_H_REG	0x2701C
#define HCLGE_COMM_NIC_CRQ_DEPTH_REG		0x27020
#define HCLGE_COMM_NIC_CRQ_TAIL_REG		0x27024
#define HCLGE_COMM_NIC_CRQ_HEAD_REG		0x27028
/* Vector0 interrupt CMDQ event source register(RW) */
#define HCLGE_COMM_VECTOR0_CMDQ_SRC_REG		0x27100
/* Vector0 interrupt CMDQ event status register(RO) */
#define HCLGE_COMM_VECTOR0_CMDQ_STATE_REG	0x27104
#define HCLGE_COMM_CMDQ_INTR_EN_REG		0x27108
#define HCLGE_COMM_CMDQ_INTR_GEN_REG		0x2710C
#define HCLGE_COMM_CMDQ_INTR_STS_REG		0x27104

/* this bit indicates that the driver is ready for hardware reset */
#define HCLGE_COMM_NIC_SW_RST_RDY_B		16
#define HCLGE_COMM_NIC_SW_RST_RDY		BIT(HCLGE_COMM_NIC_SW_RST_RDY_B)
#define HCLGE_COMM_NIC_CMQ_DESC_NUM_S		3
#define HCLGE_COMM_NIC_CMQ_DESC_NUM		1024
#define HCLGE_COMM_CMDQ_TX_TIMEOUT		30000

enum hclge_opcode_type {
	/* Generic commands */
	HCLGE_OPC_QUERY_FW_VER		= 0x0001,
	HCLGE_OPC_CFG_RST_TRIGGER	= 0x0020,
	HCLGE_OPC_GBL_RST_STATUS	= 0x0021,
	HCLGE_OPC_QUERY_FUNC_STATUS	= 0x0022,
	HCLGE_OPC_QUERY_PF_RSRC		= 0x0023,
	HCLGE_OPC_QUERY_VF_RSRC		= 0x0024,
	HCLGE_OPC_GET_CFG_PARAM		= 0x0025,
	HCLGE_OPC_PF_RST_DONE		= 0x0026,
	HCLGE_OPC_QUERY_VF_RST_RDY	= 0x0027,

	HCLGE_OPC_STATS_64_BIT		= 0x0030,
	HCLGE_OPC_STATS_32_BIT		= 0x0031,
	HCLGE_OPC_STATS_MAC		= 0x0032,
	HCLGE_OPC_QUERY_MAC_REG_NUM	= 0x0033,
	HCLGE_OPC_STATS_MAC_ALL		= 0x0034,

	HCLGE_OPC_QUERY_REG_NUM		= 0x0040,
	HCLGE_OPC_QUERY_32_BIT_REG	= 0x0041,
	HCLGE_OPC_QUERY_64_BIT_REG	= 0x0042,
	HCLGE_OPC_DFX_BD_NUM		= 0x0043,
	HCLGE_OPC_DFX_BIOS_COMMON_REG	= 0x0044,
	HCLGE_OPC_DFX_SSU_REG_0		= 0x0045,
	HCLGE_OPC_DFX_SSU_REG_1		= 0x0046,
	HCLGE_OPC_DFX_IGU_EGU_REG	= 0x0047,
	HCLGE_OPC_DFX_RPU_REG_0		= 0x0048,
	HCLGE_OPC_DFX_RPU_REG_1		= 0x0049,
	HCLGE_OPC_DFX_NCSI_REG		= 0x004A,
	HCLGE_OPC_DFX_RTC_REG		= 0x004B,
	HCLGE_OPC_DFX_PPP_REG		= 0x004C,
	HCLGE_OPC_DFX_RCB_REG		= 0x004D,
	HCLGE_OPC_DFX_TQP_REG		= 0x004E,
	HCLGE_OPC_DFX_SSU_REG_2		= 0x004F,

	HCLGE_OPC_QUERY_DEV_SPECS	= 0x0050,

	/* MAC command */
	HCLGE_OPC_CONFIG_MAC_MODE	= 0x0301,
	HCLGE_OPC_CONFIG_AN_MODE	= 0x0304,
	HCLGE_OPC_QUERY_LINK_STATUS	= 0x0307,
	HCLGE_OPC_CONFIG_MAX_FRM_SIZE	= 0x0308,
	HCLGE_OPC_CONFIG_SPEED_DUP	= 0x0309,
	HCLGE_OPC_QUERY_MAC_TNL_INT	= 0x0310,
	HCLGE_OPC_MAC_TNL_INT_EN	= 0x0311,
	HCLGE_OPC_CLEAR_MAC_TNL_INT	= 0x0312,
	HCLGE_OPC_COMMON_LOOPBACK       = 0x0315,
	HCLGE_OPC_QUERY_FEC_STATS	= 0x0316,
	HCLGE_OPC_CONFIG_FEC_MODE	= 0x031A,
	HCLGE_OPC_QUERY_ROH_TYPE_INFO	= 0x0389,

	/* PTP commands */
	HCLGE_OPC_PTP_INT_EN		= 0x0501,
	HCLGE_OPC_PTP_MODE_CFG		= 0x0507,

	/* PFC/Pause commands */
	HCLGE_OPC_CFG_MAC_PAUSE_EN      = 0x0701,
	HCLGE_OPC_CFG_PFC_PAUSE_EN      = 0x0702,
	HCLGE_OPC_CFG_MAC_PARA          = 0x0703,
	HCLGE_OPC_CFG_PFC_PARA          = 0x0704,
	HCLGE_OPC_QUERY_MAC_TX_PKT_CNT  = 0x0705,
	HCLGE_OPC_QUERY_MAC_RX_PKT_CNT  = 0x0706,
	HCLGE_OPC_QUERY_PFC_TX_PKT_CNT  = 0x0707,
	HCLGE_OPC_QUERY_PFC_RX_PKT_CNT  = 0x0708,
	HCLGE_OPC_PRI_TO_TC_MAPPING     = 0x0709,
	HCLGE_OPC_QOS_MAP               = 0x070A,

	/* ETS/scheduler commands */
	HCLGE_OPC_TM_PG_TO_PRI_LINK	= 0x0804,
	HCLGE_OPC_TM_QS_TO_PRI_LINK     = 0x0805,
	HCLGE_OPC_TM_NQ_TO_QS_LINK      = 0x0806,
	HCLGE_OPC_TM_RQ_TO_QS_LINK      = 0x0807,
	HCLGE_OPC_TM_PORT_WEIGHT        = 0x0808,
	HCLGE_OPC_TM_PG_WEIGHT          = 0x0809,
	HCLGE_OPC_TM_QS_WEIGHT          = 0x080A,
	HCLGE_OPC_TM_PRI_WEIGHT         = 0x080B,
	HCLGE_OPC_TM_PRI_C_SHAPPING     = 0x080C,
	HCLGE_OPC_TM_PRI_P_SHAPPING     = 0x080D,
	HCLGE_OPC_TM_PG_C_SHAPPING      = 0x080E,
	HCLGE_OPC_TM_PG_P_SHAPPING      = 0x080F,
	HCLGE_OPC_TM_PORT_SHAPPING      = 0x0810,
	HCLGE_OPC_TM_PG_SCH_MODE_CFG    = 0x0812,
	HCLGE_OPC_TM_PRI_SCH_MODE_CFG   = 0x0813,
	HCLGE_OPC_TM_QS_SCH_MODE_CFG    = 0x0814,
	HCLGE_OPC_TM_BP_TO_QSET_MAPPING = 0x0815,
	HCLGE_OPC_TM_NODES		= 0x0816,
	HCLGE_OPC_ETS_TC_WEIGHT		= 0x0843,
	HCLGE_OPC_QSET_DFX_STS		= 0x0844,
	HCLGE_OPC_PRI_DFX_STS		= 0x0845,
	HCLGE_OPC_PG_DFX_STS		= 0x0846,
	HCLGE_OPC_PORT_DFX_STS		= 0x0847,
	HCLGE_OPC_SCH_NQ_CNT		= 0x0848,
	HCLGE_OPC_SCH_RQ_CNT		= 0x0849,
	HCLGE_OPC_TM_INTERNAL_STS	= 0x0850,
	HCLGE_OPC_TM_INTERNAL_CNT	= 0x0851,
	HCLGE_OPC_TM_INTERNAL_STS_1	= 0x0852,

	/* Packet buffer allocate commands */
	HCLGE_OPC_TX_BUFF_ALLOC		= 0x0901,
	HCLGE_OPC_RX_PRIV_BUFF_ALLOC	= 0x0902,
	HCLGE_OPC_RX_PRIV_WL_ALLOC	= 0x0903,
	HCLGE_OPC_RX_COM_THRD_ALLOC	= 0x0904,
	HCLGE_OPC_RX_COM_WL_ALLOC	= 0x0905,
	HCLGE_OPC_RX_GBL_PKT_CNT	= 0x0906,

	/* TQP management command */
	HCLGE_OPC_SET_TQP_MAP		= 0x0A01,

	/* TQP commands */
	HCLGE_OPC_CFG_TX_QUEUE		= 0x0B01,
	HCLGE_OPC_QUERY_TX_POINTER	= 0x0B02,
	HCLGE_OPC_QUERY_TX_STATS	= 0x0B03,
	HCLGE_OPC_TQP_TX_QUEUE_TC	= 0x0B04,
	HCLGE_OPC_CFG_RX_QUEUE		= 0x0B11,
	HCLGE_OPC_QUERY_RX_POINTER	= 0x0B12,
	HCLGE_OPC_QUERY_RX_STATS	= 0x0B13,
	HCLGE_OPC_STASH_RX_QUEUE_LRO	= 0x0B16,
	HCLGE_OPC_CFG_RX_QUEUE_LRO	= 0x0B17,
	HCLGE_OPC_CFG_COM_TQP_QUEUE	= 0x0B20,
	HCLGE_OPC_RESET_TQP_QUEUE	= 0x0B22,

	/* PPU commands */
	HCLGE_OPC_PPU_PF_OTHER_INT_DFX	= 0x0B4A,

	/* TSO command */
	HCLGE_OPC_TSO_GENERIC_CONFIG	= 0x0C01,
	HCLGE_OPC_GRO_GENERIC_CONFIG    = 0x0C10,

	/* RSS commands */
	HCLGE_OPC_RSS_GENERIC_CONFIG	= 0x0D01,
	HCLGE_OPC_RSS_INDIR_TABLE	= 0x0D07,
	HCLGE_OPC_RSS_TC_MODE		= 0x0D08,
	HCLGE_OPC_RSS_INPUT_TUPLE	= 0x0D02,

	/* Promisuous mode command */
	HCLGE_OPC_CFG_PROMISC_MODE	= 0x0E01,

	/* Vlan offload commands */
	HCLGE_OPC_VLAN_PORT_TX_CFG	= 0x0F01,
	HCLGE_OPC_VLAN_PORT_RX_CFG	= 0x0F02,

	/* Interrupts commands */
	HCLGE_OPC_ADD_RING_TO_VECTOR	= 0x1503,
	HCLGE_OPC_DEL_RING_TO_VECTOR	= 0x1504,

	/* MAC commands */
	HCLGE_OPC_MAC_VLAN_ADD		    = 0x1000,
	HCLGE_OPC_MAC_VLAN_REMOVE	    = 0x1001,
	HCLGE_OPC_MAC_VLAN_TYPE_ID	    = 0x1002,
	HCLGE_OPC_MAC_VLAN_INSERT	    = 0x1003,
	HCLGE_OPC_MAC_VLAN_ALLOCATE	    = 0x1004,
	HCLGE_OPC_MAC_ETHTYPE_ADD	    = 0x1010,
	HCLGE_OPC_MAC_ETHTYPE_REMOVE	= 0x1011,

	/* MAC VLAN commands */
	HCLGE_OPC_MAC_VLAN_SWITCH_PARAM	= 0x1033,

	/* VLAN commands */
	HCLGE_OPC_VLAN_FILTER_CTRL	    = 0x1100,
	HCLGE_OPC_VLAN_FILTER_PF_CFG	= 0x1101,
	HCLGE_OPC_VLAN_FILTER_VF_CFG	= 0x1102,
	HCLGE_OPC_PORT_VLAN_BYPASS	= 0x1103,

	/* Flow Director commands */
	HCLGE_OPC_FD_MODE_CTRL		= 0x1200,
	HCLGE_OPC_FD_GET_ALLOCATION	= 0x1201,
	HCLGE_OPC_FD_KEY_CONFIG		= 0x1202,
	HCLGE_OPC_FD_TCAM_OP		= 0x1203,
	HCLGE_OPC_FD_AD_OP		= 0x1204,
	HCLGE_OPC_FD_CNT_OP		= 0x1205,
	HCLGE_OPC_FD_USER_DEF_OP	= 0x1207,
	HCLGE_OPC_FD_QB_CTRL		= 0x1210,
	HCLGE_OPC_FD_QB_AD_OP		= 0x1211,

	/* MDIO command */
	HCLGE_OPC_MDIO_CONFIG		= 0x1900,

	/* QCN commands */
	HCLGE_OPC_QCN_MOD_CFG		= 0x1A01,
	HCLGE_OPC_QCN_GRP_TMPLT_CFG	= 0x1A02,
	HCLGE_OPC_QCN_SHAPPING_CFG	= 0x1A03,
	HCLGE_OPC_QCN_SHAPPING_BS_CFG	= 0x1A04,
	HCLGE_OPC_QCN_QSET_LINK_CFG	= 0x1A05,
	HCLGE_OPC_QCN_RP_STATUS_GET	= 0x1A06,
	HCLGE_OPC_QCN_AJUST_INIT	= 0x1A07,
	HCLGE_OPC_QCN_DFX_CNT_STATUS    = 0x1A08,

	/* Mailbox command */
	HCLGEVF_OPC_MBX_PF_TO_VF	= 0x2000,
	HCLGEVF_OPC_MBX_VF_TO_PF	= 0x2001,

	/* Led command */
	HCLGE_OPC_LED_STATUS_CFG	= 0xB000,

	/* clear hardware resource command */
	HCLGE_OPC_CLEAR_HW_RESOURCE	= 0x700B,

	/* NCL config command */
	HCLGE_OPC_QUERY_NCL_CONFIG	= 0x7011,

	/* IMP stats command */
	HCLGE_OPC_IMP_STATS_BD		= 0x7012,
	HCLGE_OPC_IMP_STATS_INFO		= 0x7013,
	HCLGE_OPC_IMP_COMPAT_CFG		= 0x701A,

	/* SFP command */
	HCLGE_OPC_GET_SFP_EEPROM	= 0x7100,
	HCLGE_OPC_GET_SFP_EXIST		= 0x7101,
	HCLGE_OPC_GET_SFP_INFO		= 0x7104,

	/* Error INT commands */
	HCLGE_MAC_COMMON_INT_EN		= 0x030E,
	HCLGE_TM_SCH_ECC_INT_EN		= 0x0829,
	HCLGE_SSU_ECC_INT_CMD		= 0x0989,
	HCLGE_SSU_COMMON_INT_CMD	= 0x098C,
	HCLGE_PPU_MPF_ECC_INT_CMD	= 0x0B40,
	HCLGE_PPU_MPF_OTHER_INT_CMD	= 0x0B41,
	HCLGE_PPU_PF_OTHER_INT_CMD	= 0x0B42,
	HCLGE_COMMON_ECC_INT_CFG	= 0x1505,
	HCLGE_QUERY_RAS_INT_STS_BD_NUM	= 0x1510,
	HCLGE_QUERY_CLEAR_MPF_RAS_INT	= 0x1511,
	HCLGE_QUERY_CLEAR_PF_RAS_INT	= 0x1512,
	HCLGE_QUERY_MSIX_INT_STS_BD_NUM	= 0x1513,
	HCLGE_QUERY_CLEAR_ALL_MPF_MSIX_INT	= 0x1514,
	HCLGE_QUERY_CLEAR_ALL_PF_MSIX_INT	= 0x1515,
	HCLGE_QUERY_ALL_ERR_BD_NUM		= 0x1516,
	HCLGE_QUERY_ALL_ERR_INFO		= 0x1517,
	HCLGE_CONFIG_ROCEE_RAS_INT_EN	= 0x1580,
	HCLGE_QUERY_CLEAR_ROCEE_RAS_INT = 0x1581,
	HCLGE_ROCEE_PF_RAS_INT_CMD	= 0x1584,
	HCLGE_QUERY_ROCEE_ECC_RAS_INFO_CMD	= 0x1585,
	HCLGE_QUERY_ROCEE_AXI_RAS_INFO_CMD	= 0x1586,
	HCLGE_IGU_EGU_TNL_INT_EN	= 0x1803,
	HCLGE_IGU_COMMON_INT_EN		= 0x1806,
	HCLGE_TM_QCN_MEM_INT_CFG	= 0x1A14,
	HCLGE_PPP_CMD0_INT_CMD		= 0x2100,
	HCLGE_PPP_CMD1_INT_CMD		= 0x2101,
	HCLGE_MAC_ETHERTYPE_IDX_RD      = 0x2105,
	HCLGE_NCSI_INT_EN		= 0x2401,

	/* ROH MAC commands */
	HCLGE_OPC_MAC_ADDR_CHECK	= 0x9004,

	/* PHY command */
	HCLGE_OPC_PHY_LINK_KSETTING	= 0x7025,
	HCLGE_OPC_PHY_REG		= 0x7026,

	/* Query link diagnosis info command */
	HCLGE_OPC_QUERY_LINK_DIAGNOSIS	= 0x702A,
};

enum hclge_comm_cmd_return_status {
	HCLGE_COMM_CMD_EXEC_SUCCESS	= 0,
	HCLGE_COMM_CMD_NO_AUTH		= 1,
	HCLGE_COMM_CMD_NOT_SUPPORTED	= 2,
	HCLGE_COMM_CMD_QUEUE_FULL	= 3,
	HCLGE_COMM_CMD_NEXT_ERR		= 4,
	HCLGE_COMM_CMD_UNEXE_ERR	= 5,
	HCLGE_COMM_CMD_PARA_ERR		= 6,
	HCLGE_COMM_CMD_RESULT_ERR	= 7,
	HCLGE_COMM_CMD_TIMEOUT		= 8,
	HCLGE_COMM_CMD_HILINK_ERR	= 9,
	HCLGE_COMM_CMD_QUEUE_ILLEGAL	= 10,
	HCLGE_COMM_CMD_INVALID		= 11,
};

enum HCLGE_COMM_CAP_BITS {
	HCLGE_COMM_CAP_UDP_GSO_B,
	HCLGE_COMM_CAP_QB_B,
	HCLGE_COMM_CAP_FD_FORWARD_TC_B,
	HCLGE_COMM_CAP_PTP_B,
	HCLGE_COMM_CAP_INT_QL_B,
	HCLGE_COMM_CAP_HW_TX_CSUM_B,
	HCLGE_COMM_CAP_TX_PUSH_B,
	HCLGE_COMM_CAP_PHY_IMP_B,
	HCLGE_COMM_CAP_TQP_TXRX_INDEP_B,
	HCLGE_COMM_CAP_HW_PAD_B,
	HCLGE_COMM_CAP_STASH_B,
	HCLGE_COMM_CAP_UDP_TUNNEL_CSUM_B,
	HCLGE_COMM_CAP_RAS_IMP_B = 12,
	HCLGE_COMM_CAP_FEC_B = 13,
	HCLGE_COMM_CAP_PAUSE_B = 14,
	HCLGE_COMM_CAP_RXD_ADV_LAYOUT_B = 15,
	HCLGE_COMM_CAP_PORT_VLAN_BYPASS_B = 17,
	HCLGE_COMM_CAP_CQ_B = 18,
	HCLGE_COMM_CAP_GRO_B = 20,
	HCLGE_COMM_CAP_FD_B = 21,
	HCLGE_COMM_CAP_FEC_STATS_B = 25,
	HCLGE_COMM_CAP_LANE_NUM_B = 27,
};

enum HCLGE_COMM_API_CAP_BITS {
	HCLGE_COMM_API_CAP_FLEX_RSS_TBL_B,
};

/* capabilities bits map between imp firmware and local driver */
struct hclge_comm_caps_bit_map {
	u16 imp_bit;
	u16 local_bit;
};

struct hclge_comm_firmware_compat_cmd {
	__le32 compat;
	u8 rsv[20];
};

enum hclge_comm_cmd_state {
	HCLGE_COMM_STATE_CMD_DISABLE,
};

struct hclge_comm_errcode {
	u32 imp_errcode;
	int common_errno;
};

#define HCLGE_COMM_QUERY_CAP_LENGTH		3
struct hclge_comm_query_version_cmd {
	__le32 firmware;
	__le32 hardware;
	__le32 api_caps;
	__le32 caps[HCLGE_COMM_QUERY_CAP_LENGTH]; /* capabilities of device */
};

#define HCLGE_DESC_DATA_LEN		6
struct hclge_desc {
	__le16 opcode;
	__le16 flag;
	__le16 retval;
	__le16 rsv;
	__le32 data[HCLGE_DESC_DATA_LEN];
};

struct hclge_comm_cmq_ring {
	dma_addr_t desc_dma_addr;
	struct hclge_desc *desc;
	struct pci_dev *pdev;
	u32 head;
	u32 tail;

	u16 buf_size;
	u16 desc_num;
	int next_to_use;
	int next_to_clean;
	u8 ring_type; /* cmq ring type */
	spinlock_t lock; /* Command queue lock */
};

enum hclge_comm_cmd_status {
	HCLGE_COMM_STATUS_SUCCESS	= 0,
	HCLGE_COMM_ERR_CSQ_FULL		= -1,
	HCLGE_COMM_ERR_CSQ_TIMEOUT	= -2,
	HCLGE_COMM_ERR_CSQ_ERROR	= -3,
};

struct hclge_comm_cmq {
	struct hclge_comm_cmq_ring csq;
	struct hclge_comm_cmq_ring crq;
	u16 tx_timeout;
	enum hclge_comm_cmd_status last_status;
};

struct hclge_comm_hw {
	void __iomem *io_base;
	void __iomem *mem_base;
	struct hclge_comm_cmq cmq;
	unsigned long comm_state;
};

static inline void hclge_comm_write_reg(void __iomem *base, u32 reg, u32 value)
{
	writel(value, base + reg);
}

static inline u32 hclge_comm_read_reg(u8 __iomem *base, u32 reg)
{
	u8 __iomem *reg_addr = READ_ONCE(base);

	return readl(reg_addr + reg);
}

#define hclge_comm_write_dev(a, reg, value) \
	hclge_comm_write_reg((a)->io_base, reg, value)
#define hclge_comm_read_dev(a, reg) \
	hclge_comm_read_reg((a)->io_base, reg)

void hclge_comm_cmd_init_regs(struct hclge_comm_hw *hw);
int hclge_comm_cmd_query_version_and_capability(struct hnae3_ae_dev *ae_dev,
						struct hclge_comm_hw *hw,
						u32 *fw_version, bool is_pf);
int hclge_comm_alloc_cmd_queue(struct hclge_comm_hw *hw, int ring_type);
int hclge_comm_cmd_send(struct hclge_comm_hw *hw, struct hclge_desc *desc,
			int num);
void hclge_comm_cmd_reuse_desc(struct hclge_desc *desc, bool is_read);
int hclge_comm_firmware_compat_config(struct hnae3_ae_dev *ae_dev,
				      struct hclge_comm_hw *hw, bool en);
void hclge_comm_free_cmd_desc(struct hclge_comm_cmq_ring *ring);
void hclge_comm_cmd_setup_basic_desc(struct hclge_desc *desc,
				     enum hclge_opcode_type opcode,
				     bool is_read);
void hclge_comm_cmd_uninit(struct hnae3_ae_dev *ae_dev,
			   struct hclge_comm_hw *hw);
int hclge_comm_cmd_queue_init(struct pci_dev *pdev, struct hclge_comm_hw *hw);
int hclge_comm_cmd_init(struct hnae3_ae_dev *ae_dev, struct hclge_comm_hw *hw,
			u32 *fw_version, bool is_pf,
			unsigned long reset_pending);

#endif
