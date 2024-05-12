/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2022 Lorenzo Bianconi <lorenzo@kernel.org>  */

#ifndef __MTK_WED_WO_H
#define __MTK_WED_WO_H

#include <linux/skbuff.h>
#include <linux/netdevice.h>

struct mtk_wed_hw;

struct mtk_wed_mcu_hdr {
	/* DW0 */
	u8 version;
	u8 cmd;
	__le16 length;

	/* DW1 */
	__le16 seq;
	__le16 flag;

	/* DW2 */
	__le32 status;

	/* DW3 */
	u8 rsv[20];
};

struct mtk_wed_wo_log_info {
	__le32 sn;
	__le32 total;
	__le32 rro;
	__le32 mod;
};

enum mtk_wed_wo_event {
	MTK_WED_WO_EVT_LOG_DUMP		= 0x1,
	MTK_WED_WO_EVT_PROFILING	= 0x2,
	MTK_WED_WO_EVT_RXCNT_INFO	= 0x3,
};

#define MTK_WED_MODULE_ID_WO		1
#define MTK_FW_DL_TIMEOUT		4000000 /* us */
#define MTK_WOCPU_TIMEOUT		2000000 /* us */

enum {
	MTK_WED_WARP_CMD_FLAG_RSP		= BIT(0),
	MTK_WED_WARP_CMD_FLAG_NEED_RSP		= BIT(1),
	MTK_WED_WARP_CMD_FLAG_FROM_TO_WO	= BIT(2),
};

#define MTK_WED_WO_CPU_MCUSYS_RESET_ADDR	0x15194050
#define MTK_WED_WO_CPU_WO0_MCUSYS_RESET_MASK	0x20
#define MTK_WED_WO_CPU_WO1_MCUSYS_RESET_MASK	0x1

enum {
	MTK_WED_WO_REGION_EMI,
	MTK_WED_WO_REGION_ILM,
	MTK_WED_WO_REGION_DATA,
	MTK_WED_WO_REGION_BOOT,
	__MTK_WED_WO_REGION_MAX,
};

enum mtk_wed_wo_state {
	MTK_WED_WO_STATE_UNDEFINED,
	MTK_WED_WO_STATE_INIT,
	MTK_WED_WO_STATE_ENABLE,
	MTK_WED_WO_STATE_DISABLE,
	MTK_WED_WO_STATE_HALT,
	MTK_WED_WO_STATE_GATING,
	MTK_WED_WO_STATE_SER_RESET,
	MTK_WED_WO_STATE_WF_RESET,
};

enum mtk_wed_wo_done_state {
	MTK_WED_WOIF_UNDEFINED,
	MTK_WED_WOIF_DISABLE_DONE,
	MTK_WED_WOIF_TRIGGER_ENABLE,
	MTK_WED_WOIF_ENABLE_DONE,
	MTK_WED_WOIF_TRIGGER_GATING,
	MTK_WED_WOIF_GATING_DONE,
	MTK_WED_WOIF_TRIGGER_HALT,
	MTK_WED_WOIF_HALT_DONE,
};

enum mtk_wed_dummy_cr_idx {
	MTK_WED_DUMMY_CR_FWDL,
	MTK_WED_DUMMY_CR_WO_STATUS,
};

#define MT7981_FIRMWARE_WO	"mediatek/mt7981_wo.bin"
#define MT7986_FIRMWARE_WO0	"mediatek/mt7986_wo_0.bin"
#define MT7986_FIRMWARE_WO1	"mediatek/mt7986_wo_1.bin"

#define MTK_WO_MCU_CFG_LS_BASE				0
#define MTK_WO_MCU_CFG_LS_HW_VER_ADDR			(MTK_WO_MCU_CFG_LS_BASE + 0x000)
#define MTK_WO_MCU_CFG_LS_FW_VER_ADDR			(MTK_WO_MCU_CFG_LS_BASE + 0x004)
#define MTK_WO_MCU_CFG_LS_CFG_DBG1_ADDR			(MTK_WO_MCU_CFG_LS_BASE + 0x00c)
#define MTK_WO_MCU_CFG_LS_CFG_DBG2_ADDR			(MTK_WO_MCU_CFG_LS_BASE + 0x010)
#define MTK_WO_MCU_CFG_LS_WF_MCCR_ADDR			(MTK_WO_MCU_CFG_LS_BASE + 0x014)
#define MTK_WO_MCU_CFG_LS_WF_MCCR_SET_ADDR		(MTK_WO_MCU_CFG_LS_BASE + 0x018)
#define MTK_WO_MCU_CFG_LS_WF_MCCR_CLR_ADDR		(MTK_WO_MCU_CFG_LS_BASE + 0x01c)
#define MTK_WO_MCU_CFG_LS_WF_MCU_CFG_WM_WA_ADDR		(MTK_WO_MCU_CFG_LS_BASE + 0x050)
#define MTK_WO_MCU_CFG_LS_WM_BOOT_ADDR_ADDR		(MTK_WO_MCU_CFG_LS_BASE + 0x060)
#define MTK_WO_MCU_CFG_LS_WA_BOOT_ADDR_ADDR		(MTK_WO_MCU_CFG_LS_BASE + 0x064)

#define MTK_WO_MCU_CFG_LS_WF_WM_WA_WM_CPU_RSTB_MASK	BIT(5)
#define MTK_WO_MCU_CFG_LS_WF_WM_WA_WA_CPU_RSTB_MASK	BIT(0)

#define MTK_WED_WO_RING_SIZE	256
#define MTK_WED_WO_CMD_LEN	1504

#define MTK_WED_WO_TXCH_NUM		0
#define MTK_WED_WO_RXCH_NUM		1
#define MTK_WED_WO_RXCH_WO_EXCEPTION	7

#define MTK_WED_WO_TXCH_INT_MASK	BIT(0)
#define MTK_WED_WO_RXCH_INT_MASK	BIT(1)
#define MTK_WED_WO_EXCEPTION_INT_MASK	BIT(7)
#define MTK_WED_WO_ALL_INT_MASK		(MTK_WED_WO_RXCH_INT_MASK | \
					 MTK_WED_WO_EXCEPTION_INT_MASK)

#define MTK_WED_WO_CCIF_BUSY		0x004
#define MTK_WED_WO_CCIF_START		0x008
#define MTK_WED_WO_CCIF_TCHNUM		0x00c
#define MTK_WED_WO_CCIF_RCHNUM		0x010
#define MTK_WED_WO_CCIF_RCHNUM_MASK	GENMASK(7, 0)

#define MTK_WED_WO_CCIF_ACK		0x014
#define MTK_WED_WO_CCIF_IRQ0_MASK	0x018
#define MTK_WED_WO_CCIF_IRQ1_MASK	0x01c
#define MTK_WED_WO_CCIF_DUMMY1		0x020
#define MTK_WED_WO_CCIF_DUMMY2		0x024
#define MTK_WED_WO_CCIF_DUMMY3		0x028
#define MTK_WED_WO_CCIF_DUMMY4		0x02c
#define MTK_WED_WO_CCIF_SHADOW1		0x030
#define MTK_WED_WO_CCIF_SHADOW2		0x034
#define MTK_WED_WO_CCIF_SHADOW3		0x038
#define MTK_WED_WO_CCIF_SHADOW4		0x03c
#define MTK_WED_WO_CCIF_DUMMY5		0x050
#define MTK_WED_WO_CCIF_DUMMY6		0x054
#define MTK_WED_WO_CCIF_DUMMY7		0x058
#define MTK_WED_WO_CCIF_DUMMY8		0x05c
#define MTK_WED_WO_CCIF_SHADOW5		0x060
#define MTK_WED_WO_CCIF_SHADOW6		0x064
#define MTK_WED_WO_CCIF_SHADOW7		0x068
#define MTK_WED_WO_CCIF_SHADOW8		0x06c

#define MTK_WED_WO_CTL_SD_LEN1		GENMASK(13, 0)
#define MTK_WED_WO_CTL_LAST_SEC1	BIT(14)
#define MTK_WED_WO_CTL_BURST		BIT(15)
#define MTK_WED_WO_CTL_SD_LEN0_SHIFT	16
#define MTK_WED_WO_CTL_SD_LEN0		GENMASK(29, 16)
#define MTK_WED_WO_CTL_LAST_SEC0	BIT(30)
#define MTK_WED_WO_CTL_DMA_DONE		BIT(31)
#define MTK_WED_WO_INFO_WINFO		GENMASK(15, 0)

struct mtk_wed_wo_memory_region {
	const char *name;
	void __iomem *addr;
	phys_addr_t phy_addr;
	u32 size;
	bool shared:1;
	bool consumed:1;
};

struct mtk_wed_fw_region {
	__le32 decomp_crc;
	__le32 decomp_len;
	__le32 decomp_blk_sz;
	u8 rsv0[4];
	__le32 addr;
	__le32 len;
	u8 feature_set;
	u8 rsv1[15];
} __packed;

struct mtk_wed_fw_trailer {
	u8 chip_id;
	u8 eco_code;
	u8 num_region;
	u8 format_ver;
	u8 format_flag;
	u8 rsv[2];
	char fw_ver[10];
	char build_date[15];
	u32 crc;
};

struct mtk_wed_wo_queue_regs {
	u32 desc_base;
	u32 ring_size;
	u32 cpu_idx;
	u32 dma_idx;
};

struct mtk_wed_wo_queue_desc {
	__le32 buf0;
	__le32 ctrl;
	__le32 buf1;
	__le32 info;
	__le32 reserved[4];
} __packed __aligned(32);

struct mtk_wed_wo_queue_entry {
	dma_addr_t addr;
	void *buf;
	u32 len;
};

struct mtk_wed_wo_queue {
	struct mtk_wed_wo_queue_regs regs;

	struct page_frag_cache cache;

	struct mtk_wed_wo_queue_desc *desc;
	dma_addr_t desc_dma;

	struct mtk_wed_wo_queue_entry *entry;

	u16 head;
	u16 tail;
	int n_desc;
	int queued;
	int buf_size;

};

struct mtk_wed_wo {
	struct mtk_wed_hw *hw;
	struct mtk_wed_wo_memory_region boot;

	struct mtk_wed_wo_queue q_tx;
	struct mtk_wed_wo_queue q_rx;

	struct {
		struct mutex mutex;
		int timeout;
		u16 seq;

		struct sk_buff_head res_q;
		wait_queue_head_t wait;
	} mcu;

	struct {
		struct regmap *regs;

		spinlock_t lock;
		struct tasklet_struct irq_tasklet;
		int irq;
		u32 irq_mask;
	} mmio;
};

static inline int
mtk_wed_mcu_check_msg(struct mtk_wed_wo *wo, struct sk_buff *skb)
{
	struct mtk_wed_mcu_hdr *hdr = (struct mtk_wed_mcu_hdr *)skb->data;

	if (hdr->version)
		return -EINVAL;

	if (skb->len < sizeof(*hdr) || skb->len != le16_to_cpu(hdr->length))
		return -EINVAL;

	return 0;
}

void mtk_wed_mcu_rx_event(struct mtk_wed_wo *wo, struct sk_buff *skb);
void mtk_wed_mcu_rx_unsolicited_event(struct mtk_wed_wo *wo,
				      struct sk_buff *skb);
int mtk_wed_mcu_send_msg(struct mtk_wed_wo *wo, int id, int cmd,
			 const void *data, int len, bool wait_resp);
int mtk_wed_mcu_msg_update(struct mtk_wed_device *dev, int id, void *data,
			   int len);
int mtk_wed_mcu_init(struct mtk_wed_wo *wo);
int mtk_wed_wo_init(struct mtk_wed_hw *hw);
void mtk_wed_wo_deinit(struct mtk_wed_hw *hw);
int mtk_wed_wo_queue_tx_skb(struct mtk_wed_wo *dev, struct mtk_wed_wo_queue *q,
			    struct sk_buff *skb);

#endif /* __MTK_WED_WO_H */
