// SPDX-License-Identifier: GPL-2.0-only

#include <linux/atomic.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/if_rmnet.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/remoteproc.h>
#include <linux/remoteproc/qcom_rproc.h>
#include <linux/spinlock.h>
#include <linux/sysfs.h>

#include "ipa-hw.h"
#include "ipa.h"

#define IPA_FIFO_NUM_DESC		BIT(8)
#define IPA_FIFO_IDX_MASK		(IPA_FIFO_NUM_DESC - 1)
#define IPA_FIFO_SIZE			(IPA_FIFO_NUM_DESC * sizeof(struct fifo_desc))
#define IPA_FIFO_NEXT_IDX(idx)		(((idx) + 1) & IPA_FIFO_IDX_MASK)
#define IPA_NUM_PIPES			(20)
#define IPA_RX_LEN			(2048)
#define IPA_TX_STOP_FREE_THRESH		(0)
#define IPA_PIPE_IRQ_MASK		(P_PRCSD_DESC_EN | P_ERR_EN | P_TRNSFR_END_EN)
#define EP_DMA_DIR(ep)			((ep)->is_rx ? DMA_FROM_DEVICE : DMA_TO_DEVICE)

static bool test_mode;
module_param(test_mode, bool, 0644);

static bool dump;
module_param(dump, bool, 0644);

union ipa_cmd {
	struct ipa_hw_imm_cmd_dma_shared_mem dma_smem;
	struct ipa_ip_packet_init ip_pkt_init;
	struct ipa_ip_v4_rule_init rule_v4_init;
	struct ipa_ip_v6_rule_init rule_v6_init;
	struct ipa_hdr_init_local hdr_local_init;
	struct ipa_hdr_init_system hdr_system_init;
};

struct ipa_dma_obj {
	dma_addr_t addr;
	u32 size;
	void *virt;
	struct device *dev;
};

#define DEF_ACTION(func, arg, ...) \
	static void action_##func(void *ptr) \
	{ \
		arg = ptr; \
		if (ptr) \
			func(__VA_ARGS__); \
	}

struct ipa_ep {
	atomic_t free_descs;
	spinlock_t lock; /* transmit only */
	struct fifo_desc *fifo;
	struct ipa *ipa;
	struct ipa_dma_obj fifo_obj;
	struct napi_struct *napi;
	struct sk_buff **skbs;
	u32 has_status	    :1;
	u32 id		    :8;
	u32 is_rx	    :1;
	u32 head, tail;
	void __iomem *reg_rd_off, *reg_wr_off;
};

struct ipa {
	atomic_t uc_cmd_busy;
	bool test_mode;
	struct clk *clk;
	struct device *dev;
	struct ipa_ep ep[EP_NUM];
	struct ipa_partition layout[MEM_END + 1];
	struct ipa_qmi *qmi;
	struct net_device *modem, *lan, *loopback;
	struct notifier_block ssr_nb;
	struct wait_queue_head uc_cmd_wq;
	u32 *smem_uc_loaded;
	u32 version, smem_size, smem_restr_bytes;
	void *ssr_cookie;
	void __iomem *mmio;
	struct ipa_dma_obj system_hdr;
};

struct ipa_ndev {
	struct ipa_ep *rx, *tx;
	struct napi_struct napi_rx;
	struct napi_struct napi_tx[];
};

#define FT4_EP0_OFF (2 + 3 * 0)
#define FT4_EP4_OFF (2 + 3 * 1)
#define RT4_EP0_OFF (2 + 3 * 2)
#define RT4_EP4_OFF (2 + 3 * 3)

static const u32 ipa_rules[] = {
	/* Default (zero) rules */
	0, 0,
	/* Rules for loopback */
	/* EP0 filter: dummy range16, routing index 1 */
	[FT4_EP0_OFF] = BIT(4) | (1 << 21),
	[FT4_EP0_OFF + 1] = 0xffff00,
	/* EP4 filter: dummy range16, routing index 2 */
	[FT4_EP4_OFF] = BIT(4) | (2 << 21),
	[FT4_EP4_OFF + 1] = 0xffff00,
	/* EP0 route: dummy range16, dest pipe 5, system hdr */
	[RT4_EP0_OFF] = BIT(21) | BIT(4) | (5 << 16),
	[RT4_EP0_OFF + 1] = 0xffff00,
	/* EP4 route: dummy range16, dest pipe 1, system hdr */
	[RT4_EP4_OFF] = BIT(21) | BIT(4) | (1 << 16),
	[RT4_EP4_OFF + 1] = 0xffff00,
	[RT4_EP4_OFF + 2] = 0,
};

static inline void rmw32(void __iomem *reg, u32 mask, u32 val)
{
	iowrite32((ioread32(reg) & ~mask) | (val & mask), reg);
}

static void ipa_dma_free(struct ipa_dma_obj *obj)
{
	if (obj->size)
		dma_free_coherent(obj->dev, obj->size, obj->virt, obj->addr);
	obj->size = 0;
}

DEF_ACTION(ipa_dma_free, struct ipa_dma_obj *obj, obj);

static int ipa_dma_alloc(struct ipa *ipa, struct ipa_dma_obj *obj, u32 size)
{
	if (WARN_ON(!size))
		return -EINVAL;

	obj->virt = dma_alloc_coherent(ipa->dev, size + 8, &obj->addr, GFP_KERNEL);
	if (!obj->virt)
		return -ENOMEM;

	obj->size = size;
	obj->dev = ipa->dev;
	return 0;
}

static int devm_ipa_dma_alloc(struct ipa *ipa, struct ipa_dma_obj *obj, u32 size)
{
	int ret = ipa_dma_alloc(ipa, obj, size);

	if (!ret)
		ret = devm_add_action_or_reset(ipa->dev, action_ipa_dma_free, obj);

	return ret;
}

static void ipa_reset_hw(struct ipa *ipa)
{
	iowrite32(1, ipa->mmio + REG_IPA_COMP_SW_RESET_OFST);
	iowrite32(0, ipa->mmio + REG_IPA_COMP_SW_RESET_OFST);
	iowrite32(1, ipa->mmio + REG_IPA_COMP_CFG_OFST);
	if (ipa->version >= 25)
		iowrite32(0x1fff7f, ipa->mmio + REG_IPA_BCR_OFST);

	iowrite32(BAM_SW_RST, ipa->mmio + REG_BAM_CTRL);
	iowrite32(0, ipa->mmio + REG_BAM_CTRL);

	iowrite32(0x10, ipa->mmio + REG_BAM_DESC_CNT_TRSHLD);
	iowrite32((u32)~BIT(11), ipa->mmio + REG_BAM_CNFG_BITS);
	rmw32(ipa->mmio + REG_BAM_CTRL, BAM_EN, BAM_EN);

	iowrite32(BAM_ERROR_EN | BAM_HRESP_ERR_EN, ipa->mmio + REG_BAM_IRQ_EN);
	iowrite32(BAM_IRQ, ipa->mmio + REG_BAM_IRQ_SRCS_MSK_EE0);
}

static inline u32 ipa_fifo_offset(void __iomem *reg)
{
	u32 off = readl_relaxed(reg) & 0xffff;

	off /= sizeof(struct fifo_desc);

	WARN_ON(off & ~IPA_FIFO_IDX_MASK);

	return off & IPA_FIFO_IDX_MASK;
}

static void ipa_bam_reset_pipe(struct ipa_ep *ep)
{
	void *mmio = ep->ipa->mmio;
	u32 val, id = ep->id;

	atomic_set(&ep->free_descs, IPA_FIFO_NUM_DESC - 1);

	iowrite32(ep->id != EP_CMD, mmio + REG_IPA_EP_CTRL(id));
	iowrite32(ep->is_rx ? 1 : 0, mmio + REG_IPA_EP_HOL_BLOCK_EN(id));

	iowrite32(0, mmio + REG_BAM_P_CTRL(id));
	iowrite32(1, mmio + REG_BAM_P_RST(id));
	iowrite32(0, mmio + REG_BAM_P_RST(id));

	ep->head = 0;
	ep->tail = 0;

	iowrite32(ep->head * sizeof(struct fifo_desc), ep->reg_rd_off);
	iowrite32(ep->tail * sizeof(struct fifo_desc), ep->reg_wr_off);
	iowrite32(ALIGN(ep->fifo_obj.addr, 8),
		  mmio + REG_BAM_P_DESC_FIFO_ADDR(id));
	iowrite32(IPA_FIFO_SIZE, mmio + REG_BAM_P_FIFO_SIZES(id));
	iowrite32(0, mmio + REG_BAM_P_IRQ_EN(id));

	val = ep->is_rx ? P_DIRECTION : 0;

	iowrite32(P_SYS_MODE | P_EN | val, mmio + REG_BAM_P_CTRL(id));

	WARN_ON(ipa_fifo_offset(ep->reg_rd_off) != ipa_fifo_offset(ep->reg_wr_off));
}

static int ipa_setup_ep(struct ipa *ipa, enum ipa_ep_id id)
{
	struct ipa_ep *ep = &ipa->ep[id];
	int ret;

	ret = devm_ipa_dma_alloc(ipa, &ep->fifo_obj, IPA_FIFO_SIZE + 8);
	if (ret)
		return ret;

	spin_lock_init(&ep->lock);

	ep->id = id;
	ep->ipa = ipa;
	ep->is_rx = EP_ID_IS_RX(id);
	ep->fifo = PTR_ALIGN(ep->fifo_obj.virt, 8);
	ep->reg_rd_off = ipa->mmio + REG_BAM_P_RD_OFF_REG(id);
	ep->reg_wr_off = ipa->mmio + REG_BAM_P_WR_OFF_REG(id);

	ipa_bam_reset_pipe(ep);

	rmw32(ipa->mmio + REG_BAM_IRQ_SRCS_MSK_EE0, BIT(id), BIT(id));

	switch (id) {
	case EP_LAN_RX:
		ep->has_status = 1;
		iowrite32(0x00000002, ipa->mmio + REG_IPA_EP_HDR(id));
		iowrite32(0x00000803, ipa->mmio + REG_IPA_EP_HDR_EXT(id));
		iowrite32(0x00000000, ipa->mmio + REG_IPA_EP_HDR_METADATA_MASK(id));
		iowrite32(0x00000001, ipa->mmio + REG_IPA_EP_STATUS(id));
		break;
	case EP_TX:
	case EP_TEST_TX:
		iowrite32(ipa->test_mode ? 0xc4 : 0x44, ipa->mmio + REG_IPA_EP_HDR(id));
		iowrite32(0x00000001, ipa->mmio + REG_IPA_EP_HDR_EXT(id));
		iowrite32(0x00000005, ipa->mmio + REG_IPA_EP_STATUS(id));
		iowrite32(0x00000007, ipa->mmio + REG_IPA_EP_ROUTE(id));
		iowrite32(0x00000020, ipa->mmio + REG_IPA_EP_MODE(id));
		break;
	case EP_RX:
	case EP_TEST_RX:
		iowrite32(0x002800c4, ipa->mmio + REG_IPA_EP_HDR(id));
		iowrite32(0x0000000b, ipa->mmio + REG_IPA_EP_HDR_EXT(id));
		iowrite32(0xff000000, ipa->mmio + REG_IPA_EP_HDR_METADATA_MASK(id));
	default:
		break;
	}

	return 0;
}

static inline void ipa_release_descs(struct ipa_ep *ep, int reserved)
{
	atomic_add(reserved, &ep->free_descs);
}

static inline int ipa_reserve_descs(struct ipa_ep *ep, int count)
{
	if (unlikely(count <= 0 && atomic_read(&ep->free_descs) < count))
		return 0;

	if (atomic_sub_return(count, &ep->free_descs) < 0)
		atomic_add(count, &ep->free_descs);
	else
		return count;
	return 0;
}

static int ipa_enqueue_descs(struct ipa_ep *ep, struct fifo_desc *descs,
			     int num_descs, struct sk_buff **skbs)
{
	u32 next, head, tail, first_idx;
	unsigned long flags;

	spin_lock_irqsave(&ep->lock, flags);

	first_idx = tail = ep->tail;
	head = ep->head;

	while (num_descs) {
		next = IPA_FIFO_NEXT_IDX(tail);
		if (WARN_ON(next == head)) {
			spin_unlock_irqrestore(&ep->lock, flags);
			return -EINVAL;
		}
		ep->fifo[tail] = *(descs++);
		if (likely(skbs))
			ep->skbs[tail] = *(skbs++);
		tail = next;
		num_descs--;
	}

	ep->tail = tail;

	/* Ensure descriptor write completes before updating tail pointer */
	wmb();

	iowrite32(tail * sizeof(struct fifo_desc), ep->reg_wr_off);

	spin_unlock_irqrestore(&ep->lock, flags);

	return first_idx;
}

static int
ipa_submit_sync(struct ipa_ep *ep, struct fifo_desc *descs, int num_descs)
{
	int timeout = 100;
	u32 idx, end_idx;

	idx = ipa_enqueue_descs(ep, descs, num_descs, NULL);
	if (idx < 0)
		return idx;

	while (idx != ep->head && --timeout > 0)
		usleep_range(200, 300);

	if (idx != ep->head)
		return -ETIMEDOUT;

	end_idx = (idx + num_descs) & IPA_FIFO_IDX_MASK;

	do {
		while (idx != end_idx && idx != ipa_fifo_offset(ep->reg_rd_off))
			idx = IPA_FIFO_NEXT_IDX(idx);

		ep->head = idx;
		if (idx == end_idx)
			return 0;

		usleep_range(200, 300);
	} while (--timeout > 0);

	return -ETIMEDOUT;
}

static int ipa_uc_send_cmd(struct ipa *ipa, u8 cmd_op, u32 cmd_param, u32 resp_status)
{
	unsigned long timeout = msecs_to_jiffies(1000);
	int val, ret;

	ret = wait_event_timeout(ipa->uc_cmd_wq,
			!(atomic_fetch_or(1, &ipa->uc_cmd_busy) & 1), timeout);
	if (ret <= 0)
		return -ETIMEDOUT;

	timeout = ret;

	iowrite32(cmd_op, ipa->mmio + REG_IPA_UC_CMD);
	iowrite32(cmd_param, ipa->mmio + REG_IPA_UC_CMD_PARAM);
	iowrite32(0, ipa->mmio + REG_IPA_UC_RESP);
	iowrite32(0, ipa->mmio + REG_IPA_UC_RESP_PARAM);

	iowrite32(1, ipa->mmio + REG_IPA_IRQ_UC_EE0);

	ret = wait_event_timeout(ipa->uc_cmd_wq,
		(val = FIELD_GET(IPA_UC_RESP_OP_MASK, ioread32(ipa->mmio + REG_IPA_UC_RESP))) ==
		IPA_UC_RESPONSE_CMD_COMPLETED,
		timeout);

	atomic_set(&ipa->uc_cmd_busy, 0);
	wake_up_all(&ipa->uc_cmd_wq);

	if (val != IPA_UC_RESPONSE_CMD_COMPLETED)
		return -ETIMEDOUT;

	val = FIELD_GET(IPA_UC_RESP_OP_PARAM_STATUS_MASK,
			ioread32(ipa->mmio + REG_IPA_UC_RESP_PARAM));
	if (val != resp_status) {
		dev_err(ipa->dev, "cmd %d returned unexpected status: %d\n",
			cmd_op, val);
		return -EINVAL;
	}

	return 0;
}

static void ipa_reset_modem_pipes(struct ipa *ipa)
{
	/* For 2.5+ */
	u8 pipes[] = { 6, 7, 11, 13, 8, 9, 12, 14 };
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(pipes); i++) {
		ret = ipa_uc_send_cmd(ipa, IPA_UC_CMD_RESET_PIPE,
				      IPA_UC_CMD_RESET_PIPE_PARAM(pipes[i], i >= 4), 0);
		if (ret)
			dev_err(ipa->dev, "failed to reset %d pipe: %d\n",
				pipes[i], ret);
	}
}

static void ipa_partition_put(struct ipa *ipa, u32 *offset,
			      enum ipa_part_id id, u32 size_words, u32 align_words)
{
	u32 __iomem *ptr = ipa->mmio + REG_IPA_SRAM_SW_FIRST_v2_5 +
		(ipa->version < 25 ? ipa->smem_restr_bytes : 0) + *offset;
	bool first_canary = true;
	u32 canary = 0xdeadbeaf;

	if (id == MEM_DRV) {
		/* Keep uc_loaded status in SRAM and don't override it */
		ipa->smem_uc_loaded = ptr;
		if (*ptr == 0x10ADEDFF)
			canary = 0x10ADEDFF;
	}

	while ((first_canary || ALIGN(*offset, 4 * align_words) != *offset) &&
	       *offset < ipa->smem_size) {
		*(ptr++) = canary;
		*offset += 4;
		first_canary = false;
	}

	ipa->layout[id].offset = *offset + ipa->smem_restr_bytes;
	ipa->layout[id].size = size_words * 4;

	*offset = *offset + size_words * 4;
}

static int ipa_partition_mem(struct ipa *ipa)
{
	u32 offset, val;

	val = ioread32(ipa->mmio + REG_IPA_SHARED_MEM);

	ipa->smem_restr_bytes = FIELD_GET(IPA_SHARED_MEM_BADDR_BMSK, val);
	ipa->smem_size = FIELD_GET(IPA_SHARED_MEM_SIZE_BMSK, val);

	if (WARN_ON(ipa->smem_restr_bytes > ipa->smem_size ||
		    (ipa->smem_restr_bytes & 3) || ipa->smem_size & 3))
		return -EINVAL;

	ipa->smem_size -= ipa->smem_restr_bytes;
	offset = 0x280;

	ipa_partition_put(ipa, &offset, MEM_FT_V4, IPA_NUM_PIPES + 2, 2);
	ipa_partition_put(ipa, &offset, MEM_FT_V6, IPA_NUM_PIPES + 2, 2);
	ipa_partition_put(ipa, &offset, MEM_RT_V4, 7, 2);
	ipa_partition_put(ipa, &offset, MEM_RT_V6, 7, 2);
	ipa_partition_put(ipa, &offset, MEM_MDM_HDR, 80, 2);
	ipa_partition_put(ipa, &offset, MEM_DRV, sizeof(ipa_rules) / 4, 1);

	if (ipa->version == 25)
		ipa_partition_put(ipa, &offset, MEM_MDM_HDR_PCTX, 128, 2);
	else if (ipa->version == 26)
		ipa_partition_put(ipa, &offset, MEM_MDM_COMP, 128, 2);

	ipa_partition_put(ipa, &offset, MEM_MDM,
			  (ipa->smem_size - offset) / 4 - 2, 1);
	ipa_partition_put(ipa, &offset, MEM_END, 0, 2);

	return 0;
}

static void ipa_setup_cmd_desc(struct fifo_desc *desc, enum ipa_cmd_opcode opcode,
			       struct ipa_dma_obj *cmd_args_obj, void *cmd_args_ptr)
{
	desc->addr = cmd_args_ptr - cmd_args_obj->virt + cmd_args_obj->addr;
	desc->flags = DESC_FLAG_IMMCMD | DESC_FLAG_EOT;
	desc->opcode = opcode;
}

static int ipa_init_sram_part(struct ipa *ipa, enum ipa_part_id mem_id)
{
	u32 part_offset, *payload, *end, val;
	struct ipa_partition *part = ipa->layout + mem_id;
	struct ipa_dma_obj pld, cmd_args;
	struct ipa_ep *ep = ipa->ep + EP_CMD;
	struct fifo_desc descs[3];
	struct fifo_desc *desc = descs;
	int ret, reserved;
	union ipa_cmd *cmd;

	if (!part->size)
		return 0;

	reserved = ipa_reserve_descs(ep, ARRAY_SIZE(descs));
	if (!reserved)
		return -EBUSY;

	ret = ipa_dma_alloc(ipa, &cmd_args, sizeof(*cmd) * ARRAY_SIZE(descs));
	if (ret)
		goto release_descs;

	ret = ipa_dma_alloc(ipa, &pld, part->size);
	if (ret)
		goto free_cmd_args;

	part_offset = part->offset;
	payload = pld.virt;
	cmd = cmd_args.virt;

	switch (mem_id) {
	case MEM_DRV:
		memcpy(payload, ipa_rules, sizeof(ipa_rules));
		break;
	case MEM_FT_V4:
	case MEM_FT_V6:
		*(payload++) = 0x1fffff;
		fallthrough;
	case MEM_RT_V4:
	case MEM_RT_V6:
		end = pld.virt + pld.size;
		val = ipa->layout[MEM_DRV].offset - part_offset;

		while (payload <= end)
			*(payload++) = val | 1;
	default:
		break;
	}

	if (ipa->test_mode) {
		payload = pld.virt;
		val = ipa->layout[MEM_DRV].offset - part_offset + 1;
		if (mem_id == MEM_FT_V4) {
			payload[2 + 0] = val + FT4_EP0_OFF * 4;
			payload[2 + 4] = val + FT4_EP4_OFF * 4;
		} else if (mem_id == MEM_RT_V4) {
			payload[1] = val + RT4_EP0_OFF * 4;
			payload[2] = val + RT4_EP4_OFF * 4;
		}
	}

	switch (mem_id) {
	case MEM_MDM_HDR:
		ret = devm_ipa_dma_alloc(ipa, &ipa->system_hdr, 2048);
		if (ret)
			goto free_pld;

		cmd->hdr_system_init.hdr_table_addr = ipa->system_hdr.addr;
		ipa_setup_cmd_desc(desc++, IPA_CMD_HDR_SYSTEM_INIT, &cmd_args, cmd++);

		cmd->hdr_local_init.hdr_table_src_addr = pld.addr;
		cmd->hdr_local_init.hdr_table_dst_addr = part_offset;
		cmd->hdr_local_init.size_hdr_table = part->size;
		ipa_setup_cmd_desc(desc++, IPA_CMD_HDR_LOCAL_INIT, &cmd_args, cmd++);

		fallthrough;
	case MEM_MDM_COMP:
	case MEM_MDM:
	case MEM_DRV:
		cmd->dma_smem.system_addr = pld.addr;
		cmd->dma_smem.local_addr = part_offset;
		cmd->dma_smem.size = part->size;
		ipa_setup_cmd_desc(desc++, IPA_CMD_DMA_SHARED_MEM, &cmd_args, cmd++);
		break;
	case MEM_RT_V4:
	case MEM_FT_V4:
		cmd->rule_v4_init.ipv4_addr = part_offset;
		cmd->rule_v4_init.size_ipv4_rules = part->size;
		cmd->rule_v4_init.ipv4_rules_addr = pld.addr;
		ipa_setup_cmd_desc(desc++, (mem_id == MEM_RT_V4) ?
				   IPA_CMD_RT_V4_INIT : IPA_CMD_FT_V4_INIT,
				   &cmd_args, cmd++);
		break;
	case MEM_RT_V6:
	case MEM_FT_V6:
		cmd->rule_v6_init.ipv6_addr = part_offset;
		cmd->rule_v6_init.size_ipv6_rules = part->size;
		cmd->rule_v6_init.ipv6_rules_addr = pld.addr;
		ipa_setup_cmd_desc(desc++, (mem_id == MEM_RT_V6) ?
				   IPA_CMD_RT_V6_INIT : IPA_CMD_FT_V6_INIT,
				   &cmd_args, cmd++);
		break;
	default:
		WARN_ON(1);
		ret = -EINVAL;
		goto free_pld;
	}

	ret = ipa_submit_sync(ep, descs, desc - &descs[0]);

free_pld:
	ipa_dma_free(&pld);

free_cmd_args:
	ipa_dma_free(&cmd_args);

release_descs:
	ipa_release_descs(ep, reserved);

	return ret;
}

static int ipa_init_sram(struct ipa *ipa)
{
	enum ipa_part_id part;
	int ret;

	for (part = 0; part < MEM_END; part++) {
		ret = ipa_init_sram_part(ipa, part);
		if (ret)
			return ret;
	}

	return 0;
}

static void ipa_reset_flush_ep(struct ipa_ep *ep)
{
	u32 head = ep->head, tail = ep->tail;
	struct ipa *ipa = ep->ipa;
	struct sk_buff *skb;
	struct fifo_desc desc;

	ipa_bam_reset_pipe(ep);

	while (head != tail) {
		desc = ep->fifo[head];
		skb = ep->skbs[head];
		ep->fifo[head].size = 0;

		dma_unmap_single(ipa->dev, desc.addr,
				 ep->is_rx ? IPA_RX_LEN : skb->len,
				 EP_DMA_DIR(ep));
		if (skb)
			dev_kfree_skb_any(skb);

		ep->fifo[head].addr = 0;
		head = IPA_FIFO_NEXT_IDX(head);
	}
}

static int ipa_ssr_notifier(struct notifier_block *nb,
			    unsigned long action, void *data)
{
	struct ipa *ipa = container_of(nb, struct ipa, ssr_nb);

	if (action == QCOM_SSR_BEFORE_SHUTDOWN) {
		ipa_modem_set_present(ipa->dev, false);
	} else if (action == QCOM_SSR_AFTER_SHUTDOWN) {
		ipa_reset_modem_pipes(ipa);
		ipa_init_sram(ipa);
	} else {
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static irqreturn_t ipa_isr_thread(int irq, void *data)
{
	struct ipa *ipa = data;
	u32 val;

	val = ioread32(ipa->mmio + REG_IPA_IRQ_STTS_EE0);
	iowrite32(val, ipa->mmio + REG_IPA_IRQ_CLR_EE0);

	if (val & BIT(IPA_IRQ_UC_IRQ_1)) {
		val = ioread32(ipa->mmio + REG_IPA_UC_RESP);
		val &= IPA_UC_RESP_OP_MASK;
		if (ipa->qmi && val == IPA_UC_RESPONSE_INIT_COMPLETED) {
			ipa_qmi_uc_loaded(ipa->qmi);
			ipa->smem_uc_loaded[0] = 0x10ADEDFF;
		} else if (ipa->qmi && val == IPA_UC_RESPONSE_CMD_COMPLETED) {
			wake_up_all(&ipa->uc_cmd_wq);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t ipa_dma_isr(int irq, void *data)
{
	struct ipa *ipa = data;
	enum ipa_ep_id id;
	u32 srcs;

	srcs = ioread32(ipa->mmio + REG_BAM_IRQ_SRCS_EE0);

	if (srcs & BIT(31)) {
		u32 sts = ioread32(ipa->mmio + REG_BAM_IRQ_STTS);

		iowrite32(sts, ipa->mmio + REG_BAM_IRQ_CLR);
		srcs &= ~BIT(31);
	}

	for (id = 0; id < EP_NUM; id++) {
		struct ipa_ep *ep = ipa->ep + id;

		if (!(srcs & BIT(id)))
			continue;

		u32 val = ioread32(ipa->mmio + REG_BAM_P_IRQ_STTS(id));

		srcs &= ~BIT(id);

		if (unlikely(val & P_ERR_EN))
			dev_warn_ratelimited(ipa->dev, "error on BAM pipe %d\n", id);

		iowrite32(val, ipa->mmio + REG_BAM_P_IRQ_CLR(id));

		if (ep->napi && napi_schedule_prep(ep->napi)) {
			iowrite32(0, ipa->mmio + REG_BAM_P_IRQ_EN(id));
			__napi_schedule_irqoff(ep->napi);
		}
	}

	WARN_ON_ONCE(srcs);
	return IRQ_HANDLED;
}

static int ipa_poll_tx(struct napi_struct *napi, int budget)
{
	struct ipa_ep *ep = container_of(napi, struct ipa_ndev, napi_tx[0])->tx;
	struct net_device *ndev = napi->dev;
	struct ipa *ipa = ep->ipa;
	struct device *dev = ipa->dev;
	struct sk_buff *skb;
	struct fifo_desc desc;
	u32 packets = 0, bytes = 0;
	int done = 0;

	u32 off = ipa_fifo_offset(ep->reg_rd_off);

	while (done < budget && ep->head != off) {
		skb = ep->skbs[ep->head];
		desc = ep->fifo[ep->head];

		bytes += skb->len;
		packets++;

		dma_unmap_single(dev, desc.addr, skb->len, DMA_TO_DEVICE);
		dev_consume_skb_any(skb);
		atomic_inc(&ep->free_descs);

		ep->head = IPA_FIFO_NEXT_IDX(ep->head);
		done++;

		if (ep->head == off)
			off = ipa_fifo_offset(ep->reg_rd_off);
	}

	if (netif_queue_stopped(ndev) &&
	    atomic_read(&ep->free_descs) > IPA_TX_STOP_FREE_THRESH)
		netif_wake_queue(ndev);

	ndev->stats.tx_bytes += bytes;
	ndev->stats.tx_packets += packets;

	if (budget && done < budget && napi_complete_done(napi, done))
		iowrite32(IPA_PIPE_IRQ_MASK, ipa->mmio + REG_BAM_P_IRQ_EN(ep->id));

	return done;
}

static int ipa_poll_rx(struct napi_struct *napi, int budget)
{
	struct ipa_ep *ep = container_of(napi, struct ipa_ndev, napi_rx)->rx;
	struct net_device *ndev = napi->dev;
	struct ipa *ipa = ep->ipa;
	struct device *dev = ipa->dev;
	struct sk_buff *skb, *new_skb;
	struct fifo_desc desc;
	u32 packets = 0, bytes = 0;
	dma_addr_t addr;
	int done = 0;

	u32 off = ipa_fifo_offset(ep->reg_rd_off);

	while (done < budget && ep->head != off) {
		desc = ep->fifo[ep->head];
		skb = ep->skbs[ep->head];

		if (WARN_ON_ONCE(desc.size > IPA_RX_LEN))
			goto skip_rx;

		new_skb = netdev_alloc_skb(ndev, IPA_RX_LEN);
		if (unlikely(!new_skb))
			goto skip_rx;

		addr = dma_map_single(dev, new_skb->data, IPA_RX_LEN,
				      DMA_FROM_DEVICE);
		if (unlikely(dma_mapping_error(ipa->dev, addr))) {
			dev_kfree_skb_any(new_skb);
			goto skip_rx;
		}

		skb_put(skb, desc.size);
		skb->dev = ndev;
		skb->protocol = htons(ETH_P_MAP);

		dma_unmap_single(dev, desc.addr, IPA_RX_LEN, DMA_FROM_DEVICE);

		if (unlikely(dump)) {
			char prefix[8] = "RX EP  ";

			prefix[5] = '0' + ep->id;
			print_hex_dump(KERN_DEBUG, prefix, DUMP_PREFIX_OFFSET,
				       16, 1, skb->data, skb->len, true);
		}

		if (likely(!ep->has_status)) {
			packets++;
			bytes += skb->len;
			netif_receive_skb(skb);
		} else {
			dev_kfree_skb_any(skb);
		}

		skb = new_skb;
		desc.addr = addr;

skip_rx:
		desc.size = IPA_RX_LEN;
		desc.flags = DESC_FLAG_INT;
		ep->skbs[ep->tail] = skb;
		ep->fifo[ep->tail] = desc;
		ep->tail = IPA_FIFO_NEXT_IDX(ep->tail);

		/* Ensure descriptor write completes before updating tail pointer */
		wmb();

		iowrite32(ep->tail * sizeof(struct fifo_desc), ep->reg_wr_off);
		ep->head = IPA_FIFO_NEXT_IDX(ep->head);
		done++;

		if (ep->head == off)
			off = ipa_fifo_offset(ep->reg_rd_off);
	}

	ndev->stats.rx_bytes += bytes;
	ndev->stats.rx_packets += packets;
	ndev->stats.rx_dropped += done - packets;

	if (budget && done < budget && napi_complete_done(napi, done))
		iowrite32(IPA_PIPE_IRQ_MASK, ipa->mmio + REG_BAM_P_IRQ_EN(ep->id));

	return done;
}

static int ipa_enqueue_skb(struct sk_buff *skb, struct net_device *ndev, struct ipa_ep *ep)
{
	struct device *dev = ep->ipa->dev;
	struct fifo_desc desc;
	int ret, reserved;
	u32 len;

	len = ep->is_rx ? IPA_RX_LEN : skb->len;

	reserved = ipa_reserve_descs(ep, 1);
	if (WARN_ON(!reserved))
		return -EBUSY;

	if (ep->is_rx) {
		WARN_ON(skb);
		skb = netdev_alloc_skb(ndev, len);
		if (!skb)
			goto release_desc;
	} else if (unlikely(dump)) {
		char prefix[8] = "TX EP  ";

		prefix[5] = '0' + ep->id;
		print_hex_dump(KERN_DEBUG, prefix, DUMP_PREFIX_OFFSET,
			       16, 1, skb->data, len, true);
	}

	dma_addr_t addr = dma_map_single(dev, skb->data, len, EP_DMA_DIR(ep));

	if (dma_mapping_error(dev, addr))
		goto free_skb;

	desc.addr = addr;
	desc.size = len;
	desc.flags = ep->is_rx ? DESC_FLAG_INT : DESC_FLAG_EOT;

	ret = ipa_enqueue_descs(ep, &desc, 1, &skb);
	if (unlikely(ret < 0))
		goto unmap_skb;

	return atomic_read(&ep->free_descs);

unmap_skb:
	dma_unmap_single(dev, addr, len, EP_DMA_DIR(ep));

free_skb:
	dev_kfree_skb_any(skb);

release_desc:
	ipa_release_descs(ep, reserved);

	return ret;
}

static int ipa_ndev_open(struct net_device *ndev)
{
	struct ipa_ndev *ipa_ndev = netdev_priv(ndev);
	struct ipa *ipa = ipa_ndev->rx->ipa;
	int ret = 0;

	if (!ipa_ndev->rx->skbs) {
		ipa_ndev->rx->skbs = devm_kzalloc(ipa->dev,
				sizeof(struct sk_buff) * IPA_FIFO_NUM_DESC,
				GFP_KERNEL);
		if (!ipa_ndev->rx->skbs)
			return -ENOMEM;
	}

	if (ipa_ndev->tx && !ipa_ndev->tx->skbs) {
		ipa_ndev->tx->skbs = devm_kzalloc(ipa->dev,
				sizeof(struct sk_buff) * IPA_FIFO_NUM_DESC,
				GFP_KERNEL);

		if (!ipa_ndev->tx->skbs)
			return -ENOMEM;
	}

	pm_runtime_get_sync(ipa->dev);

	while (atomic_read(&ipa_ndev->rx->free_descs) > 0) {
		ret = ipa_enqueue_skb(NULL, ndev, ipa_ndev->rx);
		if (WARN_ON(ret < 0))
			goto fail;
	}

	napi_enable(ipa_ndev->rx->napi);
	rmw32(ipa->mmio + REG_BAM_P_CTRL(ipa_ndev->rx->id), P_EN, P_EN);
	iowrite32(0, ipa->mmio + REG_IPA_EP_HOL_BLOCK_EN(ipa_ndev->rx->id));
	iowrite32(0, ipa->mmio + REG_IPA_EP_CTRL(ipa_ndev->rx->id));

	iowrite32(IPA_PIPE_IRQ_MASK, ipa->mmio + REG_BAM_P_IRQ_EN(ipa_ndev->rx->id));

	if (ipa_ndev->tx) {
		rmw32(ipa->mmio + REG_BAM_P_CTRL(ipa_ndev->tx->id), P_EN, P_EN);

		iowrite32(0, ipa->mmio + REG_IPA_EP_CTRL(ipa_ndev->tx->id));
		napi_enable(ipa_ndev->tx->napi);

		iowrite32(IPA_PIPE_IRQ_MASK, ipa->mmio + REG_BAM_P_IRQ_EN(ipa_ndev->tx->id));
	}

	netif_start_queue(ndev);

	return 0;

fail:
	ipa_reset_flush_ep(ipa_ndev->rx);
	pm_runtime_put(ipa->dev);
	return ret;
}

static int ipa_ndev_stop(struct net_device *ndev)
{
	struct ipa_ndev *ipa_ndev = netdev_priv(ndev);
	struct ipa *ipa = ipa_ndev->rx->ipa;

	netif_stop_queue(ndev);

	iowrite32(0, ipa->mmio + REG_BAM_P_IRQ_EN(ipa_ndev->rx->id));

	napi_disable(ipa_ndev->rx->napi);
	ipa_reset_flush_ep(ipa_ndev->rx);
	if (ipa_ndev->tx) {
		iowrite32(0, ipa->mmio + REG_BAM_P_IRQ_EN(ipa_ndev->tx->id));
		napi_disable(ipa_ndev->tx->napi);
		ipa_reset_flush_ep(ipa_ndev->tx);
	}

	pm_runtime_put(ipa->dev);

	return 0;
}

static void ipa_ndev_suspend_resume(struct net_device *ndev, bool resume)
{
	if (!ndev || !netif_running(ndev))
		return;

	if (resume)
		ipa_ndev_open(ndev);
	else
		ipa_ndev_stop(ndev);
}

static netdev_tx_t ipa_ndev_start_xmit(struct sk_buff *skb,
				       struct net_device *ndev)
{
	struct ipa_ndev *ipa_ndev = netdev_priv(ndev);
	int ret;

	if (skb->protocol != htons(ETH_P_MAP) || skb_linearize(skb) || !ipa_ndev->tx)
		goto drop_tx;

	ret = ipa_enqueue_skb(skb, ndev, ipa_ndev->tx);
	if (ret == -EBUSY)
		return NETDEV_TX_BUSY;
	else if (ret < 0)
		goto drop_tx;
	else if (ret <= IPA_TX_STOP_FREE_THRESH)
		netif_stop_queue(ndev);

	return NETDEV_TX_OK;

drop_tx:
	ndev->stats.tx_dropped++;
	return NETDEV_TX_OK;
}

static const struct net_device_ops ipa_ndev_ops = {
	.ndo_open = ipa_ndev_open,
	.ndo_stop = ipa_ndev_stop,
	.ndo_start_xmit = ipa_ndev_start_xmit,
};

static void ipa_ndev_setup(struct net_device *ndev)
{
	ndev->netdev_ops = &ipa_ndev_ops;
	ndev->addr_len = 0;
	ndev->hard_header_len = 0;
	ndev->min_header_len = ETH_HLEN;
	ndev->needed_headroom = 4; /* QMAP_HDR */
	ndev->mtu = IPA_RX_LEN - 32 - 4; /* STATUS + QMAP_HDR */
	ndev->max_mtu = ndev->mtu;
	ndev->needed_tailroom = 0;
	ndev->priv_flags |= IFF_TX_SKB_SHARING;
	ndev->tx_queue_len = 1000;
	ndev->type = ARPHRD_RAWIP;
	ndev->watchdog_timeo = 1000;
	eth_broadcast_addr(ndev->broadcast);
}

DEF_ACTION(qcom_unregister_ssr_notifier, struct ipa *ipa,
	   ipa->ssr_cookie, &ipa->ssr_nb);
DEF_ACTION(ipa_qmi_teardown, struct ipa *ipa, ipa->qmi);

static void ipa_remove_netdev(void *data)
{
	struct net_device *ndev = data;
	struct ipa_ndev *ipa_ndev = netdev_priv(ndev);

	netif_napi_del(ipa_ndev->rx->napi);
	if (ipa_ndev->tx)
		netif_napi_del(ipa_ndev->tx->napi);
	unregister_netdev(ndev);
	free_netdev(ndev);
}

static struct net_device *
ipa_create_netdev(struct device *dev, const char *name,
			     struct ipa_ep *rx, struct ipa_ep *tx)
{
	struct ipa_ndev *ipa_ndev;
	struct net_device *ndev;
	int ret;

	ndev = alloc_netdev(struct_size(ipa_ndev, napi_tx, !!tx),
			    name, NET_NAME_UNKNOWN, ipa_ndev_setup);
	if (IS_ERR_OR_NULL(ndev))
		return ERR_PTR(-ENOMEM);

	if (rx->id == EP_RX)
		SET_NETDEV_DEV(ndev, dev);
	ipa_ndev = netdev_priv(ndev);
	ipa_ndev->rx = rx;
	ipa_ndev->tx = tx;
	rx->napi = &ipa_ndev->napi_rx;

	netif_napi_add(ndev, rx->napi, ipa_poll_rx);
	if (tx) {
		tx->napi = &ipa_ndev->napi_tx[0];
		netif_napi_add_tx(ndev, tx->napi, ipa_poll_tx);
	}

	ret = register_netdev(ndev);
	if (ret) {
		netif_napi_del(rx->napi);
		if (tx)
			netif_napi_del(tx->napi);
		free_netdev(ndev);
		return ERR_PTR(ret);
	}

	ret = devm_add_action_or_reset(dev, ipa_remove_netdev, ndev);
	if (ret)
		return ERR_PTR(ret);

	return ndev;
}

void ipa_modem_set_present(struct device *dev, bool present)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	(present ? netif_device_attach : netif_device_detach) (ipa->modem);
}

static int ipa_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const char *name = "rmnet_ipa0";
	struct ipa *ipa;
	int ep, ret;

	ipa = devm_kzalloc(dev, sizeof(*ipa), GFP_KERNEL);
	if (IS_ERR_OR_NULL(ipa))
		return -ENOMEM;

	ipa->version = (long)of_device_get_match_data(dev);
	ipa->dev = dev;
	ipa->ssr_nb.notifier_call = ipa_ssr_notifier;
	ipa->test_mode = test_mode;

	atomic_set(&ipa->uc_cmd_busy, 0);
	init_waitqueue_head(&ipa->uc_cmd_wq);
	platform_set_drvdata(pdev, ipa);

	ipa->mmio = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR_OR_NULL(ipa->mmio))
		return PTR_ERR(ipa->mmio) ?: -ENOMEM;

	ipa->clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(ipa->clk))
		return dev_err_probe(dev, PTR_ERR(ipa->clk),
				     "failed to get clock\n");

	clk_set_rate(ipa->clk, 40000000);

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));

	ipa_reset_hw(ipa);

	ret = ipa_partition_mem(ipa);
	if (ret)
		return ret;

	for (ep = 0; ep < EP_NUM; ep++) {
		ret = ipa_setup_ep(ipa, ep);
		if (ret)
			return ret;
	}

	iowrite32(0x00040044, ipa->mmio + REG_IPA_ROUTE_OFST);

	ret = ipa_init_sram(ipa);
	if (ret)
		return ret;

	rmw32(ipa->mmio + REG_IPA_IRQ_EN_EE0, BIT(IPA_IRQ_UC_IRQ_1), BIT(IPA_IRQ_UC_IRQ_1));

	ret = of_irq_get_byname(dev->of_node, "ipa");
	if (ret < 0)
		return ret;

	ret = devm_request_threaded_irq(dev, ret, NULL, ipa_isr_thread,
					IRQF_ONESHOT, "ipa", ipa);
	if (ret)
		return ret;

	ret = of_irq_get_byname(dev->of_node, "dma");
	if (ret < 0)
		return ret;

	ret = devm_request_irq(dev, ret, ipa_dma_isr, 0, "ipa-dma", ipa);
	if (ret)
		return ret;

	pm_runtime_set_active(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return ret;

	if (ipa->test_mode) {
		name = "ipa_lo%d";

		ipa->loopback = ipa_create_netdev(dev, name, ipa->ep + EP_TEST_RX,
						  ipa->ep + EP_TEST_TX);
		if (IS_ERR(ipa->loopback))
			return PTR_ERR(ipa->loopback);
	}

	ipa->modem = ipa_create_netdev(dev, name, ipa->ep + EP_RX,
				       ipa->ep + EP_TX);
	if (IS_ERR(ipa->modem))
		return PTR_ERR(ipa->modem);

	ipa->lan = ipa_create_netdev(dev, "ipa_lan%d", ipa->ep + EP_LAN_RX, NULL);
	if (IS_ERR(ipa->lan))
		return PTR_ERR(ipa->lan);

	if (ipa->test_mode)
		return 0;
	else
		ipa_modem_set_present(dev, false);

	ipa->ssr_cookie = qcom_register_ssr_notifier("mpss", &ipa->ssr_nb);
	if (IS_ERR(ipa->ssr_cookie))
		return dev_err_probe(dev, PTR_ERR(ipa->ssr_cookie),
				     "failed to register SSR notifier\n");

	ret = devm_add_action_or_reset(dev, action_qcom_unregister_ssr_notifier, ipa);
	if (ret)
		return ret;

	ipa->qmi = ipa_qmi_setup(dev, ipa->layout);
	if (IS_ERR(ipa->qmi))
		return PTR_ERR(ipa->qmi);

	if (ipa->smem_uc_loaded[0] == 0x10ADEDFF)
		ipa_qmi_uc_loaded(ipa->qmi);

	return devm_add_action_or_reset(dev, action_ipa_qmi_teardown, ipa);
}

static void ipa_remove(struct platform_device *pdev)
{
	struct ipa *ipa = platform_get_drvdata(pdev);
	struct device_node *np;
	struct rproc *rproc;

	if (!ipa->qmi || !ipa_qmi_is_modem_ready(ipa->qmi))
		return;

	np = of_parse_phandle(ipa->dev->of_node, "modem-remoteproc", 0);
	if (!np)
		return;

	rproc = rproc_get_by_phandle(np->phandle);
	of_node_put(np);
	if (!rproc)
		return;

	/* Should we bring it back up? */
	if (rproc->state == RPROC_RUNNING)
		rproc_shutdown(rproc);

	rproc_put(rproc);
}

static int ipa_runtime_resume(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	clk_set_rate(ipa->clk, 40000000);

	return 0;
}

static int ipa_runtime_suspend(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	clk_set_rate(ipa->clk, 9600000);

	return 0;
}

static int ipa_system_resume(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	ipa_ndev_suspend_resume(ipa->modem, true);
	ipa_ndev_suspend_resume(ipa->loopback, true);
	ipa_ndev_suspend_resume(ipa->lan, true);

	return 0;
}

static int ipa_system_suspend(struct device *dev)
{
	struct ipa *ipa = dev_get_drvdata(dev);

	ipa_ndev_suspend_resume(ipa->modem, false);
	ipa_ndev_suspend_resume(ipa->loopback, false);
	ipa_ndev_suspend_resume(ipa->lan, false);

	return 0;
}

static int ipa_modem_rx_id = EP_RX;
static int ipa_modem_tx_id = EP_TX;
static DEVICE_INT_ATTR(rx_endpoint_id, 0444, ipa_modem_rx_id);
static DEVICE_INT_ATTR(tx_endpoint_id, 0444, ipa_modem_tx_id);

static struct attribute *ipa_modem_attrs[] = {
	&dev_attr_rx_endpoint_id.attr.attr,
	&dev_attr_tx_endpoint_id.attr.attr,
	NULL
};

const struct attribute_group ipa_modem_group = {
	.name		= "modem",
	.attrs		= ipa_modem_attrs,
};

const struct attribute_group *ipa_groups[] = {
	&ipa_modem_group,
	NULL
};

static const struct of_device_id ipa_match[] = {
	{ .compatible	= "qcom,ipa-v2.5", (void *)25 },
	{ .compatible	= "qcom,ipa-lite-v2.6", (void *)26 },
	{ },
};

static const struct dev_pm_ops ipa_pm = {
	SET_RUNTIME_PM_OPS(ipa_runtime_suspend, ipa_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(ipa_system_suspend, ipa_system_resume)
};

static struct platform_driver ipa2_lite_driver = {
	.probe		= ipa_probe,
	.remove		= ipa_remove,
	.driver	= {
		.name		= "ipa",
		.dev_groups	= ipa_groups,
		.of_match_table	= ipa_match,
		.pm		= &ipa_pm
	},
};

module_platform_driver(ipa2_lite_driver);

MODULE_DEVICE_TABLE(of, ipa_match);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm IP Accelerator v2.X driver");
