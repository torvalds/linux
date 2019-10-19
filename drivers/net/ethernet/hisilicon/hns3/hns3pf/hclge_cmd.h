// SPDX-License-Identifier: GPL-2.0+
// Copyright (c) 2016-2017 Hisilicon Limited.

#ifndef __HCLGE_CMD_H
#define __HCLGE_CMD_H
#include <linux/types.h>
#include <linux/io.h>
#include <linux/etherdevice.h>

#define HCLGE_CMDQ_TX_TIMEOUT		30000

struct hclge_dev;
struct hclge_desc {
	__le16 opcode;

#define HCLGE_CMDQ_RX_INVLD_B		0
#define HCLGE_CMDQ_RX_OUTVLD_B		1

	__le16 flag;
	__le16 retval;
	__le16 rsv;
	__le32 data[6];
};

struct hclge_cmq_ring {
	dma_addr_t desc_dma_addr;
	struct hclge_desc *desc;
	struct hclge_dev *dev;
	u32 head;
	u32 tail;

	u16 buf_size;
	u16 desc_num;
	int next_to_use;
	int next_to_clean;
	u8 ring_type; /* cmq ring type */
	spinlock_t lock; /* Command queue lock */
};

enum hclge_cmd_return_status {
	HCLGE_CMD_EXEC_SUCCESS	= 0,
	HCLGE_CMD_NO_AUTH	= 1,
	HCLGE_CMD_NOT_SUPPORTED	= 2,
	HCLGE_CMD_QUEUE_FULL	= 3,
	HCLGE_CMD_NEXT_ERR	= 4,
	HCLGE_CMD_UNEXE_ERR	= 5,
	HCLGE_CMD_PARA_ERR	= 6,
	HCLGE_CMD_RESULT_ERR	= 7,
	HCLGE_CMD_TIMEOUT	= 8,
	HCLGE_CMD_HILINK_ERR	= 9,
	HCLGE_CMD_QUEUE_ILLEGAL	= 10,
	HCLGE_CMD_INVALID	= 11,
};

enum hclge_cmd_status {
	HCLGE_STATUS_SUCCESS	= 0,
	HCLGE_ERR_CSQ_FULL	= -1,
	HCLGE_ERR_CSQ_TIMEOUT	= -2,
	HCLGE_ERR_CSQ_ERROR	= -3,
};

struct hclge_misc_vector {
	u8 __iomem *addr;
	int vector_irq;
};

struct hclge_cmq {
	struct hclge_cmq_ring csq;
	struct hclge_cmq_ring crq;
	u16 tx_timeout;
	enum hclge_cmd_status last_status;
};

#define HCLGE_CMD_FLAG_IN	BIT(0)
#define HCLGE_CMD_FLAG_OUT	BIT(1)
#define HCLGE_CMD_FLAG_NEXT	BIT(2)
#define HCLGE_CMD_FLAG_WR	BIT(3)
#define HCLGE_CMD_FLAG_NO_INTR	BIT(4)
#define HCLGE_CMD_FLAG_ERR_INTR	BIT(5)

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
	HCLGE_OPC_DFX_QUERY_CHIP_CAP	= 0x0050,

	/* MAC command */
	HCLGE_OPC_CONFIG_MAC_MODE	= 0x0301,
	HCLGE_OPC_CONFIG_AN_MODE	= 0x0304,
	HCLGE_OPC_QUERY_LINK_STATUS	= 0x0307,
	HCLGE_OPC_CONFIG_MAX_FRM_SIZE	= 0x0308,
	HCLGE_OPC_CONFIG_SPEED_DUP	= 0x0309,
	HCLGE_OPC_QUERY_MAC_TNL_INT	= 0x0310,
	HCLGE_OPC_MAC_TNL_INT_EN	= 0x0311,
	HCLGE_OPC_CLEAR_MAC_TNL_INT	= 0x0312,
	HCLGE_OPC_SERDES_LOOPBACK       = 0x0315,
	HCLGE_OPC_CONFIG_FEC_MODE	= 0x031A,

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
	HCLGE_OPC_QUERY_TX_STATUS	= 0x0B03,
	HCLGE_OPC_TQP_TX_QUEUE_TC	= 0x0B04,
	HCLGE_OPC_CFG_RX_QUEUE		= 0x0B11,
	HCLGE_OPC_QUERY_RX_POINTER	= 0x0B12,
	HCLGE_OPC_QUERY_RX_STATUS	= 0x0B13,
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

	/* Flow Director commands */
	HCLGE_OPC_FD_MODE_CTRL		= 0x1200,
	HCLGE_OPC_FD_GET_ALLOCATION	= 0x1201,
	HCLGE_OPC_FD_KEY_CONFIG		= 0x1202,
	HCLGE_OPC_FD_TCAM_OP		= 0x1203,
	HCLGE_OPC_FD_AD_OP		= 0x1204,

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

	/* Led command */
	HCLGE_OPC_LED_STATUS_CFG	= 0xB000,

	/* NCL config command */
	HCLGE_OPC_QUERY_NCL_CONFIG	= 0x7011,
	/* M7 stats command */
	HCLGE_OPC_M7_STATS_BD		= 0x7012,
	HCLGE_OPC_M7_STATS_INFO		= 0x7013,
	HCLGE_OPC_M7_COMPAT_CFG		= 0x701A,

	/* SFP command */
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
};

#define HCLGE_TQP_REG_OFFSET		0x80000
#define HCLGE_TQP_REG_SIZE		0x200

#define HCLGE_RCB_INIT_QUERY_TIMEOUT	10
#define HCLGE_RCB_INIT_FLAG_EN_B	0
#define HCLGE_RCB_INIT_FLAG_FINI_B	8
struct hclge_config_rcb_init_cmd {
	__le16 rcb_init_flag;
	u8 rsv[22];
};

struct hclge_tqp_map_cmd {
	__le16 tqp_id;	/* Absolute tqp id for in this pf */
	u8 tqp_vf;	/* VF id */
#define HCLGE_TQP_MAP_TYPE_PF		0
#define HCLGE_TQP_MAP_TYPE_VF		1
#define HCLGE_TQP_MAP_TYPE_B		0
#define HCLGE_TQP_MAP_EN_B		1
	u8 tqp_flag;	/* Indicate it's pf or vf tqp */
	__le16 tqp_vid; /* Virtual id in this pf/vf */
	u8 rsv[18];
};

#define HCLGE_VECTOR_ELEMENTS_PER_CMD	10

enum hclge_int_type {
	HCLGE_INT_TX,
	HCLGE_INT_RX,
	HCLGE_INT_EVENT,
};

struct hclge_ctrl_vector_chain_cmd {
	u8 int_vector_id;
	u8 int_cause_num;
#define HCLGE_INT_TYPE_S	0
#define HCLGE_INT_TYPE_M	GENMASK(1, 0)
#define HCLGE_TQP_ID_S		2
#define HCLGE_TQP_ID_M		GENMASK(12, 2)
#define HCLGE_INT_GL_IDX_S	13
#define HCLGE_INT_GL_IDX_M	GENMASK(14, 13)
	__le16 tqp_type_and_id[HCLGE_VECTOR_ELEMENTS_PER_CMD];
	u8 vfid;
	u8 rsv;
};

#define HCLGE_MAX_TC_NUM		8
#define HCLGE_TC0_PRI_BUF_EN_B	15 /* Bit 15 indicate enable or not */
#define HCLGE_BUF_UNIT_S	7  /* Buf size is united by 128 bytes */
struct hclge_tx_buff_alloc_cmd {
	__le16 tx_pkt_buff[HCLGE_MAX_TC_NUM];
	u8 tx_buff_rsv[8];
};

struct hclge_rx_priv_buff_cmd {
	__le16 buf_num[HCLGE_MAX_TC_NUM];
	__le16 shared_buf;
	u8 rsv[6];
};

struct hclge_query_version_cmd {
	__le32 firmware;
	__le32 firmware_rsv[5];
};

#define HCLGE_RX_PRIV_EN_B	15
#define HCLGE_TC_NUM_ONE_DESC	4
struct hclge_priv_wl {
	__le16 high;
	__le16 low;
};

struct hclge_rx_priv_wl_buf {
	struct hclge_priv_wl tc_wl[HCLGE_TC_NUM_ONE_DESC];
};

struct hclge_rx_com_thrd {
	struct hclge_priv_wl com_thrd[HCLGE_TC_NUM_ONE_DESC];
};

struct hclge_rx_com_wl {
	struct hclge_priv_wl com_wl;
};

struct hclge_waterline {
	u32 low;
	u32 high;
};

struct hclge_tc_thrd {
	u32 low;
	u32 high;
};

struct hclge_priv_buf {
	struct hclge_waterline wl;	/* Waterline for low and high*/
	u32 buf_size;	/* TC private buffer size */
	u32 tx_buf_size;
	u32 enable;	/* Enable TC private buffer or not */
};

struct hclge_shared_buf {
	struct hclge_waterline self;
	struct hclge_tc_thrd tc_thrd[HCLGE_MAX_TC_NUM];
	u32 buf_size;
};

struct hclge_pkt_buf_alloc {
	struct hclge_priv_buf priv_buf[HCLGE_MAX_TC_NUM];
	struct hclge_shared_buf s_buf;
};

#define HCLGE_RX_COM_WL_EN_B	15
struct hclge_rx_com_wl_buf_cmd {
	__le16 high_wl;
	__le16 low_wl;
	u8 rsv[20];
};

#define HCLGE_RX_PKT_EN_B	15
struct hclge_rx_pkt_buf_cmd {
	__le16 high_pkt;
	__le16 low_pkt;
	u8 rsv[20];
};

#define HCLGE_PF_STATE_DONE_B	0
#define HCLGE_PF_STATE_MAIN_B	1
#define HCLGE_PF_STATE_BOND_B	2
#define HCLGE_PF_STATE_MAC_N_B	6
#define HCLGE_PF_MAC_NUM_MASK	0x3
#define HCLGE_PF_STATE_MAIN	BIT(HCLGE_PF_STATE_MAIN_B)
#define HCLGE_PF_STATE_DONE	BIT(HCLGE_PF_STATE_DONE_B)
struct hclge_func_status_cmd {
	__le32  vf_rst_state[4];
	u8 pf_state;
	u8 mac_id;
	u8 rsv1;
	u8 pf_cnt_in_mac;
	u8 pf_num;
	u8 vf_num;
	u8 rsv[2];
};

struct hclge_pf_res_cmd {
	__le16 tqp_num;
	__le16 buf_size;
	__le16 msixcap_localid_ba_nic;
	__le16 msixcap_localid_ba_rocee;
#define HCLGE_MSIX_OFT_ROCEE_S		0
#define HCLGE_MSIX_OFT_ROCEE_M		GENMASK(15, 0)
#define HCLGE_PF_VEC_NUM_S		0
#define HCLGE_PF_VEC_NUM_M		GENMASK(7, 0)
	__le16 pf_intr_vector_number;
	__le16 pf_own_fun_number;
	__le16 tx_buf_size;
	__le16 dv_buf_size;
	__le32 rsv[2];
};

#define HCLGE_CFG_OFFSET_S	0
#define HCLGE_CFG_OFFSET_M	GENMASK(19, 0)
#define HCLGE_CFG_RD_LEN_S	24
#define HCLGE_CFG_RD_LEN_M	GENMASK(27, 24)
#define HCLGE_CFG_RD_LEN_BYTES	16
#define HCLGE_CFG_RD_LEN_UNIT	4

#define HCLGE_CFG_VMDQ_S	0
#define HCLGE_CFG_VMDQ_M	GENMASK(7, 0)
#define HCLGE_CFG_TC_NUM_S	8
#define HCLGE_CFG_TC_NUM_M	GENMASK(15, 8)
#define HCLGE_CFG_TQP_DESC_N_S	16
#define HCLGE_CFG_TQP_DESC_N_M	GENMASK(31, 16)
#define HCLGE_CFG_PHY_ADDR_S	0
#define HCLGE_CFG_PHY_ADDR_M	GENMASK(7, 0)
#define HCLGE_CFG_MEDIA_TP_S	8
#define HCLGE_CFG_MEDIA_TP_M	GENMASK(15, 8)
#define HCLGE_CFG_RX_BUF_LEN_S	16
#define HCLGE_CFG_RX_BUF_LEN_M	GENMASK(31, 16)
#define HCLGE_CFG_MAC_ADDR_H_S	0
#define HCLGE_CFG_MAC_ADDR_H_M	GENMASK(15, 0)
#define HCLGE_CFG_DEFAULT_SPEED_S	16
#define HCLGE_CFG_DEFAULT_SPEED_M	GENMASK(23, 16)
#define HCLGE_CFG_RSS_SIZE_S	24
#define HCLGE_CFG_RSS_SIZE_M	GENMASK(31, 24)
#define HCLGE_CFG_SPEED_ABILITY_S	0
#define HCLGE_CFG_SPEED_ABILITY_M	GENMASK(7, 0)
#define HCLGE_CFG_UMV_TBL_SPACE_S	16
#define HCLGE_CFG_UMV_TBL_SPACE_M	GENMASK(31, 16)

struct hclge_cfg_param_cmd {
	__le32 offset;
	__le32 rsv;
	__le32 param[4];
};

#define HCLGE_MAC_MODE		0x0
#define HCLGE_DESC_NUM		0x40

#define HCLGE_ALLOC_VALID_B	0
struct hclge_vf_num_cmd {
	u8 alloc_valid;
	u8 rsv[23];
};

#define HCLGE_RSS_DEFAULT_OUTPORT_B	4
#define HCLGE_RSS_HASH_KEY_OFFSET_B	4
#define HCLGE_RSS_HASH_KEY_NUM		16
struct hclge_rss_config_cmd {
	u8 hash_config;
	u8 rsv[7];
	u8 hash_key[HCLGE_RSS_HASH_KEY_NUM];
};

struct hclge_rss_input_tuple_cmd {
	u8 ipv4_tcp_en;
	u8 ipv4_udp_en;
	u8 ipv4_sctp_en;
	u8 ipv4_fragment_en;
	u8 ipv6_tcp_en;
	u8 ipv6_udp_en;
	u8 ipv6_sctp_en;
	u8 ipv6_fragment_en;
	u8 rsv[16];
};

#define HCLGE_RSS_CFG_TBL_SIZE	16

struct hclge_rss_indirection_table_cmd {
	__le16 start_table_index;
	__le16 rss_set_bitmap;
	u8 rsv[4];
	u8 rss_result[HCLGE_RSS_CFG_TBL_SIZE];
};

#define HCLGE_RSS_TC_OFFSET_S		0
#define HCLGE_RSS_TC_OFFSET_M		GENMASK(9, 0)
#define HCLGE_RSS_TC_SIZE_S		12
#define HCLGE_RSS_TC_SIZE_M		GENMASK(14, 12)
#define HCLGE_RSS_TC_VALID_B		15
struct hclge_rss_tc_mode_cmd {
	__le16 rss_tc_mode[HCLGE_MAX_TC_NUM];
	u8 rsv[8];
};

#define HCLGE_LINK_STATUS_UP_B	0
#define HCLGE_LINK_STATUS_UP_M	BIT(HCLGE_LINK_STATUS_UP_B)
struct hclge_link_status_cmd {
	u8 status;
	u8 rsv[23];
};

struct hclge_promisc_param {
	u8 vf_id;
	u8 enable;
};

#define HCLGE_PROMISC_TX_EN_B	BIT(4)
#define HCLGE_PROMISC_RX_EN_B	BIT(5)
#define HCLGE_PROMISC_EN_B	1
#define HCLGE_PROMISC_EN_ALL	0x7
#define HCLGE_PROMISC_EN_UC	0x1
#define HCLGE_PROMISC_EN_MC	0x2
#define HCLGE_PROMISC_EN_BC	0x4
struct hclge_promisc_cfg_cmd {
	u8 flag;
	u8 vf_id;
	__le16 rsv0;
	u8 rsv1[20];
};

enum hclge_promisc_type {
	HCLGE_UNICAST	= 1,
	HCLGE_MULTICAST	= 2,
	HCLGE_BROADCAST	= 3,
};

#define HCLGE_MAC_TX_EN_B	6
#define HCLGE_MAC_RX_EN_B	7
#define HCLGE_MAC_PAD_TX_B	11
#define HCLGE_MAC_PAD_RX_B	12
#define HCLGE_MAC_1588_TX_B	13
#define HCLGE_MAC_1588_RX_B	14
#define HCLGE_MAC_APP_LP_B	15
#define HCLGE_MAC_LINE_LP_B	16
#define HCLGE_MAC_FCS_TX_B	17
#define HCLGE_MAC_RX_OVERSIZE_TRUNCATE_B	18
#define HCLGE_MAC_RX_FCS_STRIP_B	19
#define HCLGE_MAC_RX_FCS_B	20
#define HCLGE_MAC_TX_UNDER_MIN_ERR_B		21
#define HCLGE_MAC_TX_OVERSIZE_TRUNCATE_B	22

struct hclge_config_mac_mode_cmd {
	__le32 txrx_pad_fcs_loop_en;
	u8 rsv[20];
};

struct hclge_pf_rst_sync_cmd {
#define HCLGE_PF_RST_ALL_VF_RDY_B	0
	u8 all_vf_ready;
	u8 rsv[23];
};

#define HCLGE_CFG_SPEED_S		0
#define HCLGE_CFG_SPEED_M		GENMASK(5, 0)

#define HCLGE_CFG_DUPLEX_B		7
#define HCLGE_CFG_DUPLEX_M		BIT(HCLGE_CFG_DUPLEX_B)

struct hclge_config_mac_speed_dup_cmd {
	u8 speed_dup;

#define HCLGE_CFG_MAC_SPEED_CHANGE_EN_B	0
	u8 mac_change_fec_en;
	u8 rsv[22];
};

#define HCLGE_RING_ID_MASK		GENMASK(9, 0)
#define HCLGE_TQP_ENABLE_B		0

#define HCLGE_MAC_CFG_AN_EN_B		0
#define HCLGE_MAC_CFG_AN_INT_EN_B	1
#define HCLGE_MAC_CFG_AN_INT_MSK_B	2
#define HCLGE_MAC_CFG_AN_INT_CLR_B	3
#define HCLGE_MAC_CFG_AN_RST_B		4

#define HCLGE_MAC_CFG_AN_EN	BIT(HCLGE_MAC_CFG_AN_EN_B)

struct hclge_config_auto_neg_cmd {
	__le32  cfg_an_cmd_flag;
	u8      rsv[20];
};

struct hclge_sfp_info_cmd {
	__le32 speed;
	u8 query_type; /* 0: sfp speed, 1: active speed */
	u8 active_fec;
	u8 autoneg; /* autoneg state */
	u8 autoneg_ability; /* whether support autoneg */
	__le32 speed_ability; /* speed ability for current media */
	__le32 module_type;
	u8 rsv[8];
};

#define HCLGE_MAC_CFG_FEC_AUTO_EN_B	0
#define HCLGE_MAC_CFG_FEC_MODE_S	1
#define HCLGE_MAC_CFG_FEC_MODE_M	GENMASK(3, 1)
#define HCLGE_MAC_CFG_FEC_SET_DEF_B	0
#define HCLGE_MAC_CFG_FEC_CLR_DEF_B	1

#define HCLGE_MAC_FEC_OFF		0
#define HCLGE_MAC_FEC_BASER		1
#define HCLGE_MAC_FEC_RS		2
struct hclge_config_fec_cmd {
	u8 fec_mode;
	u8 default_config;
	u8 rsv[22];
};

#define HCLGE_MAC_UPLINK_PORT		0x100

struct hclge_config_max_frm_size_cmd {
	__le16  max_frm_size;
	u8      min_frm_size;
	u8      rsv[21];
};

enum hclge_mac_vlan_tbl_opcode {
	HCLGE_MAC_VLAN_ADD,	/* Add new or modify mac_vlan */
	HCLGE_MAC_VLAN_UPDATE,  /* Modify other fields of this table */
	HCLGE_MAC_VLAN_REMOVE,  /* Remove a entry through mac_vlan key */
	HCLGE_MAC_VLAN_LKUP,    /* Lookup a entry through mac_vlan key */
};

enum hclge_mac_vlan_add_resp_code {
	HCLGE_ADD_UC_OVERFLOW = 2,	/* ADD failed for UC overflow */
	HCLGE_ADD_MC_OVERFLOW,		/* ADD failed for MC overflow */
};

#define HCLGE_MAC_VLAN_BIT0_EN_B	0
#define HCLGE_MAC_VLAN_BIT1_EN_B	1
#define HCLGE_MAC_EPORT_SW_EN_B		12
#define HCLGE_MAC_EPORT_TYPE_B		11
#define HCLGE_MAC_EPORT_VFID_S		3
#define HCLGE_MAC_EPORT_VFID_M		GENMASK(10, 3)
#define HCLGE_MAC_EPORT_PFID_S		0
#define HCLGE_MAC_EPORT_PFID_M		GENMASK(2, 0)
struct hclge_mac_vlan_tbl_entry_cmd {
	u8	flags;
	u8      resp_code;
	__le16  vlan_tag;
	__le32  mac_addr_hi32;
	__le16  mac_addr_lo16;
	__le16  rsv1;
	u8      entry_type;
	u8      mc_mac_en;
	__le16  egress_port;
	__le16  egress_queue;
	u8      rsv2[6];
};

#define HCLGE_UMV_SPC_ALC_B	0
struct hclge_umv_spc_alc_cmd {
	u8 allocate;
	u8 rsv1[3];
	__le32 space_size;
	u8 rsv2[16];
};

#define HCLGE_MAC_MGR_MASK_VLAN_B		BIT(0)
#define HCLGE_MAC_MGR_MASK_MAC_B		BIT(1)
#define HCLGE_MAC_MGR_MASK_ETHERTYPE_B		BIT(2)

struct hclge_mac_mgr_tbl_entry_cmd {
	u8      flags;
	u8      resp_code;
	__le16  vlan_tag;
	u8      mac_addr[ETH_ALEN];
	__le16  rsv1;
	__le16  ethter_type;
	__le16  egress_port;
	__le16  egress_queue;
	u8      sw_port_id_aware;
	u8      rsv2;
	u8      i_port_bitmap;
	u8      i_port_direction;
	u8      rsv3[2];
};

struct hclge_mac_vlan_add_cmd {
	__le16  flags;
	__le16  mac_addr_hi16;
	__le32  mac_addr_lo32;
	__le32  mac_addr_msk_hi32;
	__le16  mac_addr_msk_lo16;
	__le16  vlan_tag;
	__le16  ingress_port;
	__le16  egress_port;
	u8      rsv[4];
};

#define HNS3_MAC_VLAN_CFG_FLAG_BIT 0
struct hclge_mac_vlan_remove_cmd {
	__le16  flags;
	__le16  mac_addr_hi16;
	__le32  mac_addr_lo32;
	__le32  mac_addr_msk_hi32;
	__le16  mac_addr_msk_lo16;
	__le16  vlan_tag;
	__le16  ingress_port;
	__le16  egress_port;
	u8      rsv[4];
};

struct hclge_vlan_filter_ctrl_cmd {
	u8 vlan_type;
	u8 vlan_fe;
	u8 rsv1[2];
	u8 vf_id;
	u8 rsv2[19];
};

struct hclge_vlan_filter_pf_cfg_cmd {
	u8 vlan_offset;
	u8 vlan_cfg;
	u8 rsv[2];
	u8 vlan_offset_bitmap[20];
};

struct hclge_vlan_filter_vf_cfg_cmd {
	__le16 vlan_id;
	u8  resp_code;
	u8  rsv;
	u8  vlan_cfg;
	u8  rsv1[3];
	u8  vf_bitmap[16];
};

#define HCLGE_SWITCH_ANTI_SPOOF_B	0U
#define HCLGE_SWITCH_ALW_LPBK_B		1U
#define HCLGE_SWITCH_ALW_LCL_LPBK_B	2U
#define HCLGE_SWITCH_ALW_DST_OVRD_B	3U
#define HCLGE_SWITCH_NO_MASK		0x0
#define HCLGE_SWITCH_ANTI_SPOOF_MASK	0xFE
#define HCLGE_SWITCH_ALW_LPBK_MASK	0xFD
#define HCLGE_SWITCH_ALW_LCL_LPBK_MASK	0xFB
#define HCLGE_SWITCH_LW_DST_OVRD_MASK	0xF7

struct hclge_mac_vlan_switch_cmd {
	u8 roce_sel;
	u8 rsv1[3];
	__le32 func_id;
	u8 switch_param;
	u8 rsv2[3];
	u8 param_mask;
	u8 rsv3[11];
};

enum hclge_mac_vlan_cfg_sel {
	HCLGE_MAC_VLAN_NIC_SEL = 0,
	HCLGE_MAC_VLAN_ROCE_SEL,
};

#define HCLGE_ACCEPT_TAG1_B		0
#define HCLGE_ACCEPT_UNTAG1_B		1
#define HCLGE_PORT_INS_TAG1_EN_B	2
#define HCLGE_PORT_INS_TAG2_EN_B	3
#define HCLGE_CFG_NIC_ROCE_SEL_B	4
#define HCLGE_ACCEPT_TAG2_B		5
#define HCLGE_ACCEPT_UNTAG2_B		6

struct hclge_vport_vtag_tx_cfg_cmd {
	u8 vport_vlan_cfg;
	u8 vf_offset;
	u8 rsv1[2];
	__le16 def_vlan_tag1;
	__le16 def_vlan_tag2;
	u8 vf_bitmap[8];
	u8 rsv2[8];
};

#define HCLGE_REM_TAG1_EN_B		0
#define HCLGE_REM_TAG2_EN_B		1
#define HCLGE_SHOW_TAG1_EN_B		2
#define HCLGE_SHOW_TAG2_EN_B		3
struct hclge_vport_vtag_rx_cfg_cmd {
	u8 vport_vlan_cfg;
	u8 vf_offset;
	u8 rsv1[6];
	u8 vf_bitmap[8];
	u8 rsv2[8];
};

struct hclge_tx_vlan_type_cfg_cmd {
	__le16 ot_vlan_type;
	__le16 in_vlan_type;
	u8 rsv[20];
};

struct hclge_rx_vlan_type_cfg_cmd {
	__le16 ot_fst_vlan_type;
	__le16 ot_sec_vlan_type;
	__le16 in_fst_vlan_type;
	__le16 in_sec_vlan_type;
	u8 rsv[16];
};

struct hclge_cfg_com_tqp_queue_cmd {
	__le16 tqp_id;
	__le16 stream_id;
	u8 enable;
	u8 rsv[19];
};

struct hclge_cfg_tx_queue_pointer_cmd {
	__le16 tqp_id;
	__le16 tx_tail;
	__le16 tx_head;
	__le16 fbd_num;
	__le16 ring_offset;
	u8 rsv[14];
};

#pragma pack(1)
struct hclge_mac_ethertype_idx_rd_cmd {
	u8	flags;
	u8	resp_code;
	__le16  vlan_tag;
	u8      mac_addr[6];
	__le16  index;
	__le16	ethter_type;
	__le16  egress_port;
	__le16  egress_queue;
	__le16  rev0;
	u8	i_port_bitmap;
	u8	i_port_direction;
	u8	rev1[2];
};

#pragma pack()

#define HCLGE_TSO_MSS_MIN_S	0
#define HCLGE_TSO_MSS_MIN_M	GENMASK(13, 0)

#define HCLGE_TSO_MSS_MAX_S	16
#define HCLGE_TSO_MSS_MAX_M	GENMASK(29, 16)

struct hclge_cfg_tso_status_cmd {
	__le16 tso_mss_min;
	__le16 tso_mss_max;
	u8 rsv[20];
};

#define HCLGE_GRO_EN_B		0
struct hclge_cfg_gro_status_cmd {
	__le16 gro_en;
	u8 rsv[22];
};

#define HCLGE_TSO_MSS_MIN	256
#define HCLGE_TSO_MSS_MAX	9668

#define HCLGE_TQP_RESET_B	0
struct hclge_reset_tqp_queue_cmd {
	__le16 tqp_id;
	u8 reset_req;
	u8 ready_to_reset;
	u8 rsv[20];
};

#define HCLGE_CFG_RESET_MAC_B		3
#define HCLGE_CFG_RESET_FUNC_B		7
struct hclge_reset_cmd {
	u8 mac_func_reset;
	u8 fun_reset_vfid;
	u8 rsv[22];
};

#define HCLGE_PF_RESET_DONE_BIT		BIT(0)

struct hclge_pf_rst_done_cmd {
	u8 pf_rst_done;
	u8 rsv[23];
};

#define HCLGE_CMD_SERDES_SERIAL_INNER_LOOP_B	BIT(0)
#define HCLGE_CMD_SERDES_PARALLEL_INNER_LOOP_B	BIT(2)
#define HCLGE_CMD_SERDES_DONE_B			BIT(0)
#define HCLGE_CMD_SERDES_SUCCESS_B		BIT(1)
struct hclge_serdes_lb_cmd {
	u8 mask;
	u8 enable;
	u8 result;
	u8 rsv[21];
};

#define HCLGE_DEFAULT_TX_BUF		0x4000	 /* 16k  bytes */
#define HCLGE_TOTAL_PKT_BUF		0x108000 /* 1.03125M bytes */
#define HCLGE_DEFAULT_DV		0xA000	 /* 40k byte */
#define HCLGE_DEFAULT_NON_DCB_DV	0x7800	/* 30K byte */
#define HCLGE_NON_DCB_ADDITIONAL_BUF	0x1400	/* 5120 byte */

#define HCLGE_TYPE_CRQ			0
#define HCLGE_TYPE_CSQ			1
#define HCLGE_NIC_CSQ_BASEADDR_L_REG	0x27000
#define HCLGE_NIC_CSQ_BASEADDR_H_REG	0x27004
#define HCLGE_NIC_CSQ_DEPTH_REG		0x27008
#define HCLGE_NIC_CSQ_TAIL_REG		0x27010
#define HCLGE_NIC_CSQ_HEAD_REG		0x27014
#define HCLGE_NIC_CRQ_BASEADDR_L_REG	0x27018
#define HCLGE_NIC_CRQ_BASEADDR_H_REG	0x2701c
#define HCLGE_NIC_CRQ_DEPTH_REG		0x27020
#define HCLGE_NIC_CRQ_TAIL_REG		0x27024
#define HCLGE_NIC_CRQ_HEAD_REG		0x27028

/* this bit indicates that the driver is ready for hardware reset */
#define HCLGE_NIC_SW_RST_RDY_B		16
#define HCLGE_NIC_SW_RST_RDY		BIT(HCLGE_NIC_SW_RST_RDY_B)

#define HCLGE_NIC_CMQ_DESC_NUM		1024
#define HCLGE_NIC_CMQ_DESC_NUM_S	3

#define HCLGE_LED_LOCATE_STATE_S	0
#define HCLGE_LED_LOCATE_STATE_M	GENMASK(1, 0)

struct hclge_set_led_state_cmd {
	u8 rsv1[3];
	u8 locate_led_config;
	u8 rsv2[20];
};

struct hclge_get_fd_mode_cmd {
	u8 mode;
	u8 enable;
	u8 rsv[22];
};

struct hclge_get_fd_allocation_cmd {
	__le32 stage1_entry_num;
	__le32 stage2_entry_num;
	__le16 stage1_counter_num;
	__le16 stage2_counter_num;
	u8 rsv[12];
};

struct hclge_set_fd_key_config_cmd {
	u8 stage;
	u8 key_select;
	u8 inner_sipv6_word_en;
	u8 inner_dipv6_word_en;
	u8 outer_sipv6_word_en;
	u8 outer_dipv6_word_en;
	u8 rsv1[2];
	__le32 tuple_mask;
	__le32 meta_data_mask;
	u8 rsv2[8];
};

#define HCLGE_FD_EPORT_SW_EN_B		0
struct hclge_fd_tcam_config_1_cmd {
	u8 stage;
	u8 xy_sel;
	u8 port_info;
	u8 rsv1[1];
	__le32 index;
	u8 entry_vld;
	u8 rsv2[7];
	u8 tcam_data[8];
};

struct hclge_fd_tcam_config_2_cmd {
	u8 tcam_data[24];
};

struct hclge_fd_tcam_config_3_cmd {
	u8 tcam_data[20];
	u8 rsv[4];
};

#define HCLGE_FD_AD_DROP_B		0
#define HCLGE_FD_AD_DIRECT_QID_B	1
#define HCLGE_FD_AD_QID_S		2
#define HCLGE_FD_AD_QID_M		GENMASK(12, 2)
#define HCLGE_FD_AD_USE_COUNTER_B	12
#define HCLGE_FD_AD_COUNTER_NUM_S	13
#define HCLGE_FD_AD_COUNTER_NUM_M	GENMASK(20, 13)
#define HCLGE_FD_AD_NXT_STEP_B		20
#define HCLGE_FD_AD_NXT_KEY_S		21
#define HCLGE_FD_AD_NXT_KEY_M		GENMASK(26, 21)
#define HCLGE_FD_AD_WR_RULE_ID_B	0
#define HCLGE_FD_AD_RULE_ID_S		1
#define HCLGE_FD_AD_RULE_ID_M		GENMASK(13, 1)

struct hclge_fd_ad_config_cmd {
	u8 stage;
	u8 rsv1[3];
	__le32 index;
	__le64 ad_data;
	u8 rsv2[8];
};

struct hclge_get_m7_bd_cmd {
	__le32 bd_num;
	u8 rsv[20];
};

struct hclge_query_ppu_pf_other_int_dfx_cmd {
	__le16 over_8bd_no_fe_qid;
	__le16 over_8bd_no_fe_vf_id;
	__le16 tso_mss_cmp_min_err_qid;
	__le16 tso_mss_cmp_min_err_vf_id;
	__le16 tso_mss_cmp_max_err_qid;
	__le16 tso_mss_cmp_max_err_vf_id;
	__le16 tx_rd_fbd_poison_qid;
	__le16 tx_rd_fbd_poison_vf_id;
	__le16 rx_rd_fbd_poison_qid;
	__le16 rx_rd_fbd_poison_vf_id;
	u8 rsv[4];
};

#define HCLGE_LINK_EVENT_REPORT_EN_B	0
#define HCLGE_NCSI_ERROR_REPORT_EN_B	1
struct hclge_firmware_compat_cmd {
	__le32 compat;
	u8 rsv[20];
};

int hclge_cmd_init(struct hclge_dev *hdev);
static inline void hclge_write_reg(void __iomem *base, u32 reg, u32 value)
{
	writel(value, base + reg);
}

#define hclge_write_dev(a, reg, value) \
	hclge_write_reg((a)->io_base, (reg), (value))
#define hclge_read_dev(a, reg) \
	hclge_read_reg((a)->io_base, (reg))

static inline u32 hclge_read_reg(u8 __iomem *base, u32 reg)
{
	u8 __iomem *reg_addr = READ_ONCE(base);

	return readl(reg_addr + reg);
}

#define HCLGE_SEND_SYNC(flag) \
	((flag) & HCLGE_CMD_FLAG_NO_INTR)

struct hclge_hw;
int hclge_cmd_send(struct hclge_hw *hw, struct hclge_desc *desc, int num);
void hclge_cmd_setup_basic_desc(struct hclge_desc *desc,
				enum hclge_opcode_type opcode, bool is_read);
void hclge_cmd_reuse_desc(struct hclge_desc *desc, bool is_read);

enum hclge_cmd_status hclge_cmd_mdio_write(struct hclge_hw *hw,
					   struct hclge_desc *desc);
enum hclge_cmd_status hclge_cmd_mdio_read(struct hclge_hw *hw,
					  struct hclge_desc *desc);

void hclge_cmd_uninit(struct hclge_dev *hdev);
int hclge_cmd_queue_init(struct hclge_dev *hdev);
#endif
