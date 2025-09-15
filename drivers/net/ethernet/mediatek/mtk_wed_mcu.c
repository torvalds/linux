// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2022 MediaTek Inc.
 *
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 *	   Sujuan Chen <sujuan.chen@mediatek.com>
 */

#include <linux/firmware.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/mfd/syscon.h>
#include <linux/soc/mediatek/mtk_wed.h>
#include <linux/unaligned.h>

#include "mtk_wed_regs.h"
#include "mtk_wed_wo.h"
#include "mtk_wed.h"

static struct mtk_wed_wo_memory_region mem_region[] = {
	[MTK_WED_WO_REGION_EMI] = {
		.name = "wo-emi",
	},
	[MTK_WED_WO_REGION_ILM] = {
		.name = "wo-ilm",
	},
	[MTK_WED_WO_REGION_DATA] = {
		.name = "wo-data",
		.shared = true,
	},
	[MTK_WED_WO_REGION_BOOT] = {
		.name = "wo-boot",
	},
};

static u32 wo_r32(u32 reg)
{
	return readl(mem_region[MTK_WED_WO_REGION_BOOT].addr + reg);
}

static void wo_w32(u32 reg, u32 val)
{
	writel(val, mem_region[MTK_WED_WO_REGION_BOOT].addr + reg);
}

static struct sk_buff *
mtk_wed_mcu_msg_alloc(const void *data, int data_len)
{
	int length = sizeof(struct mtk_wed_mcu_hdr) + data_len;
	struct sk_buff *skb;

	skb = alloc_skb(length, GFP_KERNEL);
	if (!skb)
		return NULL;

	memset(skb->head, 0, length);
	skb_reserve(skb, sizeof(struct mtk_wed_mcu_hdr));
	if (data && data_len)
		skb_put_data(skb, data, data_len);

	return skb;
}

static struct sk_buff *
mtk_wed_mcu_get_response(struct mtk_wed_wo *wo, unsigned long expires)
{
	if (!time_is_after_jiffies(expires))
		return NULL;

	wait_event_timeout(wo->mcu.wait, !skb_queue_empty(&wo->mcu.res_q),
			   expires - jiffies);
	return skb_dequeue(&wo->mcu.res_q);
}

void mtk_wed_mcu_rx_event(struct mtk_wed_wo *wo, struct sk_buff *skb)
{
	skb_queue_tail(&wo->mcu.res_q, skb);
	wake_up(&wo->mcu.wait);
}

static void
mtk_wed_update_rx_stats(struct mtk_wed_device *wed, struct sk_buff *skb)
{
	u32 count = get_unaligned_le32(skb->data);
	struct mtk_wed_wo_rx_stats *stats;
	int i;

	if (!wed->wlan.update_wo_rx_stats)
		return;

	if (count * sizeof(*stats) > skb->len - sizeof(u32))
		return;

	stats = (struct mtk_wed_wo_rx_stats *)(skb->data + sizeof(u32));
	for (i = 0 ; i < count ; i++)
		wed->wlan.update_wo_rx_stats(wed, &stats[i]);
}

void mtk_wed_mcu_rx_unsolicited_event(struct mtk_wed_wo *wo,
				      struct sk_buff *skb)
{
	struct mtk_wed_mcu_hdr *hdr = (struct mtk_wed_mcu_hdr *)skb->data;

	skb_pull(skb, sizeof(*hdr));

	switch (hdr->cmd) {
	case MTK_WED_WO_EVT_LOG_DUMP:
		dev_notice(wo->hw->dev, "%s\n", skb->data);
		break;
	case MTK_WED_WO_EVT_PROFILING: {
		struct mtk_wed_wo_log_info *info = (void *)skb->data;
		u32 count = skb->len / sizeof(*info);
		int i;

		for (i = 0 ; i < count ; i++)
			dev_notice(wo->hw->dev,
				   "SN:%u latency: total=%u, rro:%u, mod:%u\n",
				   le32_to_cpu(info[i].sn),
				   le32_to_cpu(info[i].total),
				   le32_to_cpu(info[i].rro),
				   le32_to_cpu(info[i].mod));
		break;
	}
	case MTK_WED_WO_EVT_RXCNT_INFO:
		mtk_wed_update_rx_stats(wo->hw->wed_dev, skb);
		break;
	default:
		break;
	}

	dev_kfree_skb(skb);
}

static int
mtk_wed_mcu_skb_send_msg(struct mtk_wed_wo *wo, struct sk_buff *skb,
			 int id, int cmd, u16 *wait_seq, bool wait_resp)
{
	struct mtk_wed_mcu_hdr *hdr;

	/* TODO: make it dynamic based on cmd */
	wo->mcu.timeout = 20 * HZ;

	hdr = (struct mtk_wed_mcu_hdr *)skb_push(skb, sizeof(*hdr));
	hdr->cmd = cmd;
	hdr->length = cpu_to_le16(skb->len);

	if (wait_resp && wait_seq) {
		u16 seq = ++wo->mcu.seq;

		if (!seq)
			seq = ++wo->mcu.seq;
		*wait_seq = seq;

		hdr->flag |= cpu_to_le16(MTK_WED_WARP_CMD_FLAG_NEED_RSP);
		hdr->seq = cpu_to_le16(seq);
	}
	if (id == MTK_WED_MODULE_ID_WO)
		hdr->flag |= cpu_to_le16(MTK_WED_WARP_CMD_FLAG_FROM_TO_WO);

	return mtk_wed_wo_queue_tx_skb(wo, &wo->q_tx, skb);
}

static int
mtk_wed_mcu_parse_response(struct mtk_wed_wo *wo, struct sk_buff *skb,
			   int cmd, int seq)
{
	struct mtk_wed_mcu_hdr *hdr;

	if (!skb) {
		dev_err(wo->hw->dev, "Message %08x (seq %d) timeout\n",
			cmd, seq);
		return -ETIMEDOUT;
	}

	hdr = (struct mtk_wed_mcu_hdr *)skb->data;
	if (le16_to_cpu(hdr->seq) != seq)
		return -EAGAIN;

	skb_pull(skb, sizeof(*hdr));
	switch (cmd) {
	case MTK_WED_WO_CMD_RXCNT_INFO:
		mtk_wed_update_rx_stats(wo->hw->wed_dev, skb);
		break;
	default:
		break;
	}

	return 0;
}

int mtk_wed_mcu_send_msg(struct mtk_wed_wo *wo, int id, int cmd,
			 const void *data, int len, bool wait_resp)
{
	unsigned long expires;
	struct sk_buff *skb;
	u16 seq;
	int ret;

	skb = mtk_wed_mcu_msg_alloc(data, len);
	if (!skb)
		return -ENOMEM;

	mutex_lock(&wo->mcu.mutex);

	ret = mtk_wed_mcu_skb_send_msg(wo, skb, id, cmd, &seq, wait_resp);
	if (ret || !wait_resp)
		goto unlock;

	expires = jiffies + wo->mcu.timeout;
	do {
		skb = mtk_wed_mcu_get_response(wo, expires);
		ret = mtk_wed_mcu_parse_response(wo, skb, cmd, seq);
		dev_kfree_skb(skb);
	} while (ret == -EAGAIN);

unlock:
	mutex_unlock(&wo->mcu.mutex);

	return ret;
}

int mtk_wed_mcu_msg_update(struct mtk_wed_device *dev, int id, void *data,
			   int len)
{
	struct mtk_wed_wo *wo = dev->hw->wed_wo;

	if (!mtk_wed_get_rx_capa(dev))
		return 0;

	if (WARN_ON(!wo))
		return -ENODEV;

	return mtk_wed_mcu_send_msg(wo, MTK_WED_MODULE_ID_WO, id, data, len,
				    true);
}

static int
mtk_wed_get_memory_region(struct mtk_wed_hw *hw, const char *name,
			  struct mtk_wed_wo_memory_region *region)
{
	struct resource res;
	int ret;

	ret = of_reserved_mem_region_to_resource_byname(hw->node, name, &res);
	if (ret)
		return 0;

	region->phy_addr = res.start;
	region->size = resource_size(&res);
	region->addr = devm_ioremap_resource(hw->dev, &res);
	if (IS_ERR(region->addr))
		return PTR_ERR(region->addr);

	return 0;
}

static int
mtk_wed_mcu_run_firmware(struct mtk_wed_wo *wo, const struct firmware *fw)
{
	const u8 *first_region_ptr, *region_ptr, *trailer_ptr, *ptr = fw->data;
	const struct mtk_wed_fw_trailer *trailer;
	const struct mtk_wed_fw_region *fw_region;

	trailer_ptr = fw->data + fw->size - sizeof(*trailer);
	trailer = (const struct mtk_wed_fw_trailer *)trailer_ptr;
	region_ptr = trailer_ptr - trailer->num_region * sizeof(*fw_region);
	first_region_ptr = region_ptr;

	while (region_ptr < trailer_ptr) {
		u32 length;
		int i;

		fw_region = (const struct mtk_wed_fw_region *)region_ptr;
		length = le32_to_cpu(fw_region->len);
		if (first_region_ptr < ptr + length)
			goto next;

		for (i = 0; i < ARRAY_SIZE(mem_region); i++) {
			struct mtk_wed_wo_memory_region *region;

			region = &mem_region[i];
			if (region->phy_addr != le32_to_cpu(fw_region->addr))
				continue;

			if (region->size < length)
				continue;

			if (region->shared && region->consumed)
				break;

			if (!region->shared || !region->consumed) {
				memcpy_toio(region->addr, ptr, length);
				region->consumed = true;
				break;
			}
		}

		if (i == ARRAY_SIZE(mem_region))
			return -EINVAL;
next:
		region_ptr += sizeof(*fw_region);
		ptr += length;
	}

	return 0;
}

static int
mtk_wed_mcu_load_firmware(struct mtk_wed_wo *wo)
{
	const struct mtk_wed_fw_trailer *trailer;
	const struct firmware *fw;
	const char *fw_name;
	u32 val, boot_cr;
	int ret, i;

	/* load firmware region metadata */
	for (i = 0; i < ARRAY_SIZE(mem_region); i++) {
		ret = mtk_wed_get_memory_region(wo->hw, mem_region[i].name, &mem_region[i]);
		if (ret)
			return ret;
	}

	/* set dummy cr */
	wed_w32(wo->hw->wed_dev, MTK_WED_SCR0 + 4 * MTK_WED_DUMMY_CR_FWDL,
		wo->hw->index + 1);

	/* load firmware */
	switch (wo->hw->version) {
	case 2:
		if (of_device_is_compatible(wo->hw->node,
					    "mediatek,mt7981-wed"))
			fw_name = MT7981_FIRMWARE_WO;
		else
			fw_name = wo->hw->index ? MT7986_FIRMWARE_WO1
						: MT7986_FIRMWARE_WO0;
		break;
	case 3:
		fw_name = wo->hw->index ? MT7988_FIRMWARE_WO1
					: MT7988_FIRMWARE_WO0;
		break;
	default:
		return -EINVAL;
	}

	ret = request_firmware(&fw, fw_name, wo->hw->dev);
	if (ret)
		return ret;

	trailer = (void *)(fw->data + fw->size -
			   sizeof(struct mtk_wed_fw_trailer));
	dev_info(wo->hw->dev,
		 "MTK WED WO Firmware Version: %.10s, Build Time: %.15s\n",
		 trailer->fw_ver, trailer->build_date);
	dev_info(wo->hw->dev, "MTK WED WO Chip ID %02x Region %d\n",
		 trailer->chip_id, trailer->num_region);

	ret = mtk_wed_mcu_run_firmware(wo, fw);
	if (ret)
		goto out;

	/* set the start address */
	if (!mtk_wed_is_v3_or_greater(wo->hw) && wo->hw->index)
		boot_cr = MTK_WO_MCU_CFG_LS_WA_BOOT_ADDR_ADDR;
	else
		boot_cr = MTK_WO_MCU_CFG_LS_WM_BOOT_ADDR_ADDR;
	wo_w32(boot_cr, mem_region[MTK_WED_WO_REGION_EMI].phy_addr >> 16);
	/* wo firmware reset */
	wo_w32(MTK_WO_MCU_CFG_LS_WF_MCCR_CLR_ADDR, 0xc00);

	val = wo_r32(MTK_WO_MCU_CFG_LS_WF_MCU_CFG_WM_WA_ADDR) |
	      MTK_WO_MCU_CFG_LS_WF_WM_WA_WM_CPU_RSTB_MASK;
	wo_w32(MTK_WO_MCU_CFG_LS_WF_MCU_CFG_WM_WA_ADDR, val);
out:
	release_firmware(fw);

	return ret;
}

static u32
mtk_wed_mcu_read_fw_dl(struct mtk_wed_wo *wo)
{
	return wed_r32(wo->hw->wed_dev,
		       MTK_WED_SCR0 + 4 * MTK_WED_DUMMY_CR_FWDL);
}

int mtk_wed_mcu_init(struct mtk_wed_wo *wo)
{
	u32 val;
	int ret;

	skb_queue_head_init(&wo->mcu.res_q);
	init_waitqueue_head(&wo->mcu.wait);
	mutex_init(&wo->mcu.mutex);

	ret = mtk_wed_mcu_load_firmware(wo);
	if (ret)
		return ret;

	return readx_poll_timeout(mtk_wed_mcu_read_fw_dl, wo, val, !val,
				  100, MTK_FW_DL_TIMEOUT);
}

MODULE_FIRMWARE(MT7981_FIRMWARE_WO);
MODULE_FIRMWARE(MT7986_FIRMWARE_WO0);
MODULE_FIRMWARE(MT7986_FIRMWARE_WO1);
MODULE_FIRMWARE(MT7988_FIRMWARE_WO0);
MODULE_FIRMWARE(MT7988_FIRMWARE_WO1);
