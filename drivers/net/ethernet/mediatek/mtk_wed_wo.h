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

enum {
	MTK_WED_WO_REGION_EMI,
	MTK_WED_WO_REGION_ILM,
	MTK_WED_WO_REGION_DATA,
	MTK_WED_WO_REGION_BOOT,
	__MTK_WED_WO_REGION_MAX,
};

enum mtk_wed_dummy_cr_idx {
	MTK_WED_DUMMY_CR_FWDL,
	MTK_WED_DUMMY_CR_WO_STATUS,
};

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

struct mtk_wed_wo {
	struct mtk_wed_hw *hw;
	struct mtk_wed_wo_memory_region boot;

	struct {
		struct mutex mutex;
		int timeout;
		u16 seq;

		struct sk_buff_head res_q;
		wait_queue_head_t wait;
	} mcu;
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
int mtk_wed_mcu_init(struct mtk_wed_wo *wo);

#endif /* __MTK_WED_WO_H */
