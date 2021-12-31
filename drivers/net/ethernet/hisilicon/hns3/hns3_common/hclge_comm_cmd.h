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

enum hclge_comm_special_cmd {
	HCLGE_COMM_OPC_STATS_64_BIT		= 0x0030,
	HCLGE_COMM_OPC_STATS_32_BIT		= 0x0031,
	HCLGE_COMM_OPC_STATS_MAC		= 0x0032,
	HCLGE_COMM_OPC_STATS_MAC_ALL		= 0x0034,
	HCLGE_COMM_OPC_QUERY_32_BIT_REG		= 0x0041,
	HCLGE_COMM_OPC_QUERY_64_BIT_REG		= 0x0042,
	HCLGE_COMM_QUERY_CLEAR_MPF_RAS_INT	= 0x1511,
	HCLGE_COMM_QUERY_CLEAR_PF_RAS_INT	= 0x1512,
	HCLGE_COMM_QUERY_CLEAR_ALL_MPF_MSIX_INT	= 0x1514,
	HCLGE_COMM_QUERY_CLEAR_ALL_PF_MSIX_INT	= 0x1515,
	HCLGE_COMM_QUERY_ALL_ERR_INFO		= 0x1517,
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
};

enum HCLGE_COMM_API_CAP_BITS {
	HCLGE_COMM_API_CAP_FLEX_RSS_TBL_B,
};

enum hclge_comm_opcode_type {
	HCLGE_COMM_OPC_QUERY_FW_VER		= 0x0001,
	HCLGE_COMM_OPC_IMP_COMPAT_CFG		= 0x701A,
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
			int num, bool is_pf);
void hclge_comm_cmd_reuse_desc(struct hclge_desc *desc, bool is_read);
int hclge_comm_firmware_compat_config(struct hnae3_ae_dev *ae_dev, bool is_pf,
				      struct hclge_comm_hw *hw, bool en);
void hclge_comm_free_cmd_desc(struct hclge_comm_cmq_ring *ring);
void hclge_comm_cmd_setup_basic_desc(struct hclge_desc *desc,
				     enum hclge_comm_opcode_type opcode,
				     bool is_read);
void hclge_comm_cmd_uninit(struct hnae3_ae_dev *ae_dev, bool is_pf,
			   struct hclge_comm_hw *hw);
int hclge_comm_cmd_queue_init(struct pci_dev *pdev, struct hclge_comm_hw *hw);
int hclge_comm_cmd_init(struct hnae3_ae_dev *ae_dev, struct hclge_comm_hw *hw,
			u32 *fw_version, bool is_pf,
			unsigned long reset_pending);

#endif
