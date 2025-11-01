// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 AIROHA Inc
 * Author: Lorenzo Bianconi <lorenzo@kernel.org>
 */

#include <linux/devcoredump.h>
#include <linux/firmware.h>
#include <linux/platform_device.h>
#include <linux/of_net.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/regmap.h>

#include "airoha_eth.h"

#define NPU_EN7581_FIRMWARE_DATA		"airoha/en7581_npu_data.bin"
#define NPU_EN7581_FIRMWARE_RV32		"airoha/en7581_npu_rv32.bin"
#define NPU_EN7581_FIRMWARE_RV32_MAX_SIZE	0x200000
#define NPU_EN7581_FIRMWARE_DATA_MAX_SIZE	0x10000
#define NPU_DUMP_SIZE				512

#define REG_NPU_LOCAL_SRAM		0x0

#define NPU_PC_BASE_ADDR		0x305000
#define REG_PC_DBG(_n)			(0x305000 + ((_n) * 0x100))

#define NPU_CLUSTER_BASE_ADDR		0x306000

#define REG_CR_BOOT_TRIGGER		(NPU_CLUSTER_BASE_ADDR + 0x000)
#define REG_CR_BOOT_CONFIG		(NPU_CLUSTER_BASE_ADDR + 0x004)
#define REG_CR_BOOT_BASE(_n)		(NPU_CLUSTER_BASE_ADDR + 0x020 + ((_n) << 2))

#define NPU_MBOX_BASE_ADDR		0x30c000

#define REG_CR_MBOX_INT_STATUS		(NPU_MBOX_BASE_ADDR + 0x000)
#define MBOX_INT_STATUS_MASK		BIT(8)

#define REG_CR_MBOX_INT_MASK(_n)	(NPU_MBOX_BASE_ADDR + 0x004 + ((_n) << 2))
#define REG_CR_MBQ0_CTRL(_n)		(NPU_MBOX_BASE_ADDR + 0x030 + ((_n) << 2))
#define REG_CR_MBQ8_CTRL(_n)		(NPU_MBOX_BASE_ADDR + 0x0b0 + ((_n) << 2))
#define REG_CR_NPU_MIB(_n)		(NPU_MBOX_BASE_ADDR + 0x140 + ((_n) << 2))

#define NPU_WLAN_BASE_ADDR		0x30d000

#define REG_IRQ_STATUS			(NPU_WLAN_BASE_ADDR + 0x030)
#define REG_IRQ_RXDONE(_n)		(NPU_WLAN_BASE_ADDR + ((_n) << 2) + 0x034)
#define NPU_IRQ_RX_MASK(_n)		((_n) == 1 ? BIT(17) : BIT(16))

#define REG_TX_BASE(_n)			(NPU_WLAN_BASE_ADDR + ((_n) << 4) + 0x080)
#define REG_TX_DSCP_NUM(_n)		(NPU_WLAN_BASE_ADDR + ((_n) << 4) + 0x084)
#define REG_TX_CPU_IDX(_n)		(NPU_WLAN_BASE_ADDR + ((_n) << 4) + 0x088)
#define REG_TX_DMA_IDX(_n)		(NPU_WLAN_BASE_ADDR + ((_n) << 4) + 0x08c)

#define REG_RX_BASE(_n)			(NPU_WLAN_BASE_ADDR + ((_n) << 4) + 0x180)
#define REG_RX_DSCP_NUM(_n)		(NPU_WLAN_BASE_ADDR + ((_n) << 4) + 0x184)
#define REG_RX_CPU_IDX(_n)		(NPU_WLAN_BASE_ADDR + ((_n) << 4) + 0x188)
#define REG_RX_DMA_IDX(_n)		(NPU_WLAN_BASE_ADDR + ((_n) << 4) + 0x18c)

#define NPU_TIMER_BASE_ADDR		0x310100
#define REG_WDT_TIMER_CTRL(_n)		(NPU_TIMER_BASE_ADDR + ((_n) * 0x100))
#define WDT_EN_MASK			BIT(25)
#define WDT_INTR_MASK			BIT(21)

enum {
	NPU_OP_SET = 1,
	NPU_OP_SET_NO_WAIT,
	NPU_OP_GET,
	NPU_OP_GET_NO_WAIT,
};

enum {
	NPU_FUNC_WIFI,
	NPU_FUNC_TUNNEL,
	NPU_FUNC_NOTIFY,
	NPU_FUNC_DBA,
	NPU_FUNC_TR471,
	NPU_FUNC_PPE,
};

enum {
	NPU_MBOX_ERROR,
	NPU_MBOX_SUCCESS,
};

enum {
	PPE_FUNC_SET_WAIT,
	PPE_FUNC_SET_WAIT_HWNAT_INIT,
	PPE_FUNC_SET_WAIT_HWNAT_DEINIT,
	PPE_FUNC_SET_WAIT_API,
	PPE_FUNC_SET_WAIT_FLOW_STATS_SETUP,
};

enum {
	PPE2_SRAM_SET_ENTRY,
	PPE_SRAM_SET_ENTRY,
	PPE_SRAM_SET_VAL,
	PPE_SRAM_RESET_VAL,
};

enum {
	QDMA_WAN_ETHER = 1,
	QDMA_WAN_PON_XDSL,
};

#define MBOX_MSG_FUNC_ID	GENMASK(14, 11)
#define MBOX_MSG_STATIC_BUF	BIT(5)
#define MBOX_MSG_STATUS		GENMASK(4, 2)
#define MBOX_MSG_DONE		BIT(1)
#define MBOX_MSG_WAIT_RSP	BIT(0)

#define PPE_TYPE_L2B_IPV4	2
#define PPE_TYPE_L2B_IPV4_IPV6	3

struct ppe_mbox_data {
	u32 func_type;
	u32 func_id;
	union {
		struct {
			u8 cds;
			u8 xpon_hal_api;
			u8 wan_xsi;
			u8 ct_joyme4;
			u8 max_packet;
			u8 rsv[3];
			u32 ppe_type;
			u32 wan_mode;
			u32 wan_sel;
		} init_info;
		struct {
			u32 func_id;
			u32 size;
			u32 data;
		} set_info;
		struct {
			u32 npu_stats_addr;
			u32 foe_stats_addr;
		} stats_info;
	};
};

struct wlan_mbox_data {
	u32 ifindex:4;
	u32 func_type:4;
	u32 func_id;
	DECLARE_FLEX_ARRAY(u8, d);
};

static int airoha_npu_send_msg(struct airoha_npu *npu, int func_id,
			       void *p, int size)
{
	u16 core = 0; /* FIXME */
	u32 val, offset = core << 4;
	dma_addr_t dma_addr;
	int ret;

	dma_addr = dma_map_single(npu->dev, p, size, DMA_TO_DEVICE);
	ret = dma_mapping_error(npu->dev, dma_addr);
	if (ret)
		return ret;

	spin_lock_bh(&npu->cores[core].lock);

	regmap_write(npu->regmap, REG_CR_MBQ0_CTRL(0) + offset, dma_addr);
	regmap_write(npu->regmap, REG_CR_MBQ0_CTRL(1) + offset, size);
	regmap_read(npu->regmap, REG_CR_MBQ0_CTRL(2) + offset, &val);
	regmap_write(npu->regmap, REG_CR_MBQ0_CTRL(2) + offset, val + 1);
	val = FIELD_PREP(MBOX_MSG_FUNC_ID, func_id) | MBOX_MSG_WAIT_RSP;
	regmap_write(npu->regmap, REG_CR_MBQ0_CTRL(3) + offset, val);

	ret = regmap_read_poll_timeout_atomic(npu->regmap,
					      REG_CR_MBQ0_CTRL(3) + offset,
					      val, (val & MBOX_MSG_DONE),
					      100, 100 * MSEC_PER_SEC);
	if (!ret && FIELD_GET(MBOX_MSG_STATUS, val) != NPU_MBOX_SUCCESS)
		ret = -EINVAL;

	spin_unlock_bh(&npu->cores[core].lock);

	dma_unmap_single(npu->dev, dma_addr, size, DMA_TO_DEVICE);

	return ret;
}

static int airoha_npu_run_firmware(struct device *dev, void __iomem *base,
				   struct resource *res)
{
	const struct firmware *fw;
	void __iomem *addr;
	int ret;

	ret = request_firmware(&fw, NPU_EN7581_FIRMWARE_RV32, dev);
	if (ret)
		return ret == -ENOENT ? -EPROBE_DEFER : ret;

	if (fw->size > NPU_EN7581_FIRMWARE_RV32_MAX_SIZE) {
		dev_err(dev, "%s: fw size too overlimit (%zu)\n",
			NPU_EN7581_FIRMWARE_RV32, fw->size);
		ret = -E2BIG;
		goto out;
	}

	addr = devm_ioremap_resource(dev, res);
	if (IS_ERR(addr)) {
		ret = PTR_ERR(addr);
		goto out;
	}

	memcpy_toio(addr, fw->data, fw->size);
	release_firmware(fw);

	ret = request_firmware(&fw, NPU_EN7581_FIRMWARE_DATA, dev);
	if (ret)
		return ret == -ENOENT ? -EPROBE_DEFER : ret;

	if (fw->size > NPU_EN7581_FIRMWARE_DATA_MAX_SIZE) {
		dev_err(dev, "%s: fw size too overlimit (%zu)\n",
			NPU_EN7581_FIRMWARE_DATA, fw->size);
		ret = -E2BIG;
		goto out;
	}

	memcpy_toio(base + REG_NPU_LOCAL_SRAM, fw->data, fw->size);
out:
	release_firmware(fw);

	return ret;
}

static irqreturn_t airoha_npu_mbox_handler(int irq, void *npu_instance)
{
	struct airoha_npu *npu = npu_instance;

	/* clear mbox interrupt status */
	regmap_write(npu->regmap, REG_CR_MBOX_INT_STATUS,
		     MBOX_INT_STATUS_MASK);

	/* acknowledge npu */
	regmap_update_bits(npu->regmap, REG_CR_MBQ8_CTRL(3),
			   MBOX_MSG_STATUS | MBOX_MSG_DONE, MBOX_MSG_DONE);

	return IRQ_HANDLED;
}

static void airoha_npu_wdt_work(struct work_struct *work)
{
	struct airoha_npu_core *core;
	struct airoha_npu *npu;
	void *dump;
	u32 val[3];
	int c;

	core = container_of(work, struct airoha_npu_core, wdt_work);
	npu = core->npu;

	dump = vzalloc(NPU_DUMP_SIZE);
	if (!dump)
		return;

	c = core - &npu->cores[0];
	regmap_bulk_read(npu->regmap, REG_PC_DBG(c), val, ARRAY_SIZE(val));
	snprintf(dump, NPU_DUMP_SIZE, "PC: %08x SP: %08x LR: %08x\n",
		 val[0], val[1], val[2]);

	dev_coredumpv(npu->dev, dump, NPU_DUMP_SIZE, GFP_KERNEL);
}

static irqreturn_t airoha_npu_wdt_handler(int irq, void *core_instance)
{
	struct airoha_npu_core *core = core_instance;
	struct airoha_npu *npu = core->npu;
	int c = core - &npu->cores[0];
	u32 val;

	regmap_set_bits(npu->regmap, REG_WDT_TIMER_CTRL(c), WDT_INTR_MASK);
	if (!regmap_read(npu->regmap, REG_WDT_TIMER_CTRL(c), &val) &&
	    FIELD_GET(WDT_EN_MASK, val))
		schedule_work(&core->wdt_work);

	return IRQ_HANDLED;
}

static int airoha_npu_ppe_init(struct airoha_npu *npu)
{
	struct ppe_mbox_data *ppe_data;
	int err;

	ppe_data = kzalloc(sizeof(*ppe_data), GFP_KERNEL);
	if (!ppe_data)
		return -ENOMEM;

	ppe_data->func_type = NPU_OP_SET;
	ppe_data->func_id = PPE_FUNC_SET_WAIT_HWNAT_INIT;
	ppe_data->init_info.ppe_type = PPE_TYPE_L2B_IPV4_IPV6;
	ppe_data->init_info.wan_mode = QDMA_WAN_ETHER;

	err = airoha_npu_send_msg(npu, NPU_FUNC_PPE, ppe_data,
				  sizeof(*ppe_data));
	kfree(ppe_data);

	return err;
}

static int airoha_npu_ppe_deinit(struct airoha_npu *npu)
{
	struct ppe_mbox_data *ppe_data;
	int err;

	ppe_data = kzalloc(sizeof(*ppe_data), GFP_KERNEL);
	if (!ppe_data)
		return -ENOMEM;

	ppe_data->func_type = NPU_OP_SET;
	ppe_data->func_id = PPE_FUNC_SET_WAIT_HWNAT_DEINIT;

	err = airoha_npu_send_msg(npu, NPU_FUNC_PPE, ppe_data,
				  sizeof(*ppe_data));
	kfree(ppe_data);

	return err;
}

static int airoha_npu_ppe_flush_sram_entries(struct airoha_npu *npu,
					     dma_addr_t foe_addr,
					     int sram_num_entries)
{
	struct ppe_mbox_data *ppe_data;
	int err;

	ppe_data = kzalloc(sizeof(*ppe_data), GFP_KERNEL);
	if (!ppe_data)
		return -ENOMEM;

	ppe_data->func_type = NPU_OP_SET;
	ppe_data->func_id = PPE_FUNC_SET_WAIT_API;
	ppe_data->set_info.func_id = PPE_SRAM_RESET_VAL;
	ppe_data->set_info.data = foe_addr;
	ppe_data->set_info.size = sram_num_entries;

	err = airoha_npu_send_msg(npu, NPU_FUNC_PPE, ppe_data,
				  sizeof(*ppe_data));
	kfree(ppe_data);

	return err;
}

static int airoha_npu_foe_commit_entry(struct airoha_npu *npu,
				       dma_addr_t foe_addr,
				       u32 entry_size, u32 hash, bool ppe2)
{
	struct ppe_mbox_data *ppe_data;
	int err;

	ppe_data = kzalloc(sizeof(*ppe_data), GFP_ATOMIC);
	if (!ppe_data)
		return -ENOMEM;

	ppe_data->func_type = NPU_OP_SET;
	ppe_data->func_id = PPE_FUNC_SET_WAIT_API;
	ppe_data->set_info.data = foe_addr;
	ppe_data->set_info.size = entry_size;
	ppe_data->set_info.func_id = ppe2 ? PPE2_SRAM_SET_ENTRY
					  : PPE_SRAM_SET_ENTRY;

	err = airoha_npu_send_msg(npu, NPU_FUNC_PPE, ppe_data,
				  sizeof(*ppe_data));
	if (err)
		goto out;

	ppe_data->set_info.func_id = PPE_SRAM_SET_VAL;
	ppe_data->set_info.data = hash;
	ppe_data->set_info.size = sizeof(u32);

	err = airoha_npu_send_msg(npu, NPU_FUNC_PPE, ppe_data,
				  sizeof(*ppe_data));
out:
	kfree(ppe_data);

	return err;
}

static int airoha_npu_ppe_stats_setup(struct airoha_npu *npu,
				      dma_addr_t foe_stats_addr,
				      u32 num_stats_entries)
{
	int err, size = num_stats_entries * sizeof(*npu->stats);
	struct ppe_mbox_data *ppe_data;

	ppe_data = kzalloc(sizeof(*ppe_data), GFP_ATOMIC);
	if (!ppe_data)
		return -ENOMEM;

	ppe_data->func_type = NPU_OP_SET;
	ppe_data->func_id = PPE_FUNC_SET_WAIT_FLOW_STATS_SETUP;
	ppe_data->stats_info.foe_stats_addr = foe_stats_addr;

	err = airoha_npu_send_msg(npu, NPU_FUNC_PPE, ppe_data,
				  sizeof(*ppe_data));
	if (err)
		goto out;

	npu->stats = devm_ioremap(npu->dev,
				  ppe_data->stats_info.npu_stats_addr,
				  size);
	if (!npu->stats)
		err = -ENOMEM;
out:
	kfree(ppe_data);

	return err;
}

static int airoha_npu_wlan_msg_send(struct airoha_npu *npu, int ifindex,
				    enum airoha_npu_wlan_set_cmd func_id,
				    void *data, int data_len, gfp_t gfp)
{
	struct wlan_mbox_data *wlan_data;
	int err, len;

	len = sizeof(*wlan_data) + data_len;
	wlan_data = kzalloc(len, gfp);
	if (!wlan_data)
		return -ENOMEM;

	wlan_data->ifindex = ifindex;
	wlan_data->func_type = NPU_OP_SET;
	wlan_data->func_id = func_id;
	memcpy(wlan_data->d, data, data_len);

	err = airoha_npu_send_msg(npu, NPU_FUNC_WIFI, wlan_data, len);
	kfree(wlan_data);

	return err;
}

static int airoha_npu_wlan_msg_get(struct airoha_npu *npu, int ifindex,
				   enum airoha_npu_wlan_get_cmd func_id,
				   void *data, int data_len, gfp_t gfp)
{
	struct wlan_mbox_data *wlan_data;
	int err, len;

	len = sizeof(*wlan_data) + data_len;
	wlan_data = kzalloc(len, gfp);
	if (!wlan_data)
		return -ENOMEM;

	wlan_data->ifindex = ifindex;
	wlan_data->func_type = NPU_OP_GET;
	wlan_data->func_id = func_id;

	err = airoha_npu_send_msg(npu, NPU_FUNC_WIFI, wlan_data, len);
	if (!err)
		memcpy(data, wlan_data->d, data_len);
	kfree(wlan_data);

	return err;
}

static int
airoha_npu_wlan_set_reserved_memory(struct airoha_npu *npu,
				    int ifindex, const char *name,
				    enum airoha_npu_wlan_set_cmd func_id)
{
	struct device *dev = npu->dev;
	struct resource res;
	int err;
	u32 val;

	err = of_reserved_mem_region_to_resource_byname(dev->of_node, name,
							&res);
	if (err)
		return err;

	val = res.start;
	return airoha_npu_wlan_msg_send(npu, ifindex, func_id, &val,
					sizeof(val), GFP_KERNEL);
}

static int airoha_npu_wlan_init_memory(struct airoha_npu *npu)
{
	enum airoha_npu_wlan_set_cmd cmd = WLAN_FUNC_SET_WAIT_NPU_BAND0_ONCPU;
	u32 val = 0;
	int err;

	err = airoha_npu_wlan_msg_send(npu, 1, cmd, &val, sizeof(val),
				       GFP_KERNEL);
	if (err)
		return err;

	cmd = WLAN_FUNC_SET_WAIT_TX_BUF_CHECK_ADDR;
	err = airoha_npu_wlan_set_reserved_memory(npu, 0, "tx-bufid", cmd);
	if (err)
		return err;

	cmd = WLAN_FUNC_SET_WAIT_PKT_BUF_ADDR;
	err = airoha_npu_wlan_set_reserved_memory(npu, 0, "pkt", cmd);
	if (err)
		return err;

	cmd = WLAN_FUNC_SET_WAIT_TX_PKT_BUF_ADDR;
	err = airoha_npu_wlan_set_reserved_memory(npu, 0, "tx-pkt", cmd);
	if (err)
		return err;

	cmd = WLAN_FUNC_SET_WAIT_IS_FORCE_TO_CPU;
	return airoha_npu_wlan_msg_send(npu, 0, cmd, &val, sizeof(val),
					GFP_KERNEL);
}

static u32 airoha_npu_wlan_queue_addr_get(struct airoha_npu *npu, int qid,
					  bool xmit)
{
	if (xmit)
		return REG_TX_BASE(qid + 2);

	return REG_RX_BASE(qid);
}

static void airoha_npu_wlan_irq_status_set(struct airoha_npu *npu, u32 val)
{
	regmap_write(npu->regmap, REG_IRQ_STATUS, val);
}

static u32 airoha_npu_wlan_irq_status_get(struct airoha_npu *npu, int q)
{
	u32 val;

	regmap_read(npu->regmap, REG_IRQ_STATUS, &val);
	return val;
}

static void airoha_npu_wlan_irq_enable(struct airoha_npu *npu, int q)
{
	regmap_set_bits(npu->regmap, REG_IRQ_RXDONE(q), NPU_IRQ_RX_MASK(q));
}

static void airoha_npu_wlan_irq_disable(struct airoha_npu *npu, int q)
{
	regmap_clear_bits(npu->regmap, REG_IRQ_RXDONE(q), NPU_IRQ_RX_MASK(q));
}

struct airoha_npu *airoha_npu_get(struct device *dev)
{
	struct platform_device *pdev;
	struct device_node *np;
	struct airoha_npu *npu;

	np = of_parse_phandle(dev->of_node, "airoha,npu", 0);
	if (!np)
		return ERR_PTR(-ENODEV);

	pdev = of_find_device_by_node(np);

	if (!pdev) {
		dev_err(dev, "cannot find device node %s\n", np->name);
		of_node_put(np);
		return ERR_PTR(-ENODEV);
	}
	of_node_put(np);

	if (!try_module_get(THIS_MODULE)) {
		dev_err(dev, "failed to get the device driver module\n");
		npu = ERR_PTR(-ENODEV);
		goto error_pdev_put;
	}

	npu = platform_get_drvdata(pdev);
	if (!npu) {
		npu = ERR_PTR(-ENODEV);
		goto error_module_put;
	}

	if (!device_link_add(dev, &pdev->dev, DL_FLAG_AUTOREMOVE_SUPPLIER)) {
		dev_err(&pdev->dev,
			"failed to create device link to consumer %s\n",
			dev_name(dev));
		npu = ERR_PTR(-EINVAL);
		goto error_module_put;
	}

	return npu;

error_module_put:
	module_put(THIS_MODULE);
error_pdev_put:
	platform_device_put(pdev);

	return npu;
}
EXPORT_SYMBOL_GPL(airoha_npu_get);

void airoha_npu_put(struct airoha_npu *npu)
{
	module_put(THIS_MODULE);
	put_device(npu->dev);
}
EXPORT_SYMBOL_GPL(airoha_npu_put);

static const struct of_device_id of_airoha_npu_match[] = {
	{ .compatible = "airoha,en7581-npu" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_airoha_npu_match);

static const struct regmap_config regmap_config = {
	.name			= "npu",
	.reg_bits		= 32,
	.val_bits		= 32,
	.reg_stride		= 4,
	.disable_locking	= true,
};

static int airoha_npu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct airoha_npu *npu;
	struct resource res;
	void __iomem *base;
	int i, irq, err;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	npu = devm_kzalloc(dev, sizeof(*npu), GFP_KERNEL);
	if (!npu)
		return -ENOMEM;

	npu->dev = dev;
	npu->ops.ppe_init = airoha_npu_ppe_init;
	npu->ops.ppe_deinit = airoha_npu_ppe_deinit;
	npu->ops.ppe_init_stats = airoha_npu_ppe_stats_setup;
	npu->ops.ppe_flush_sram_entries = airoha_npu_ppe_flush_sram_entries;
	npu->ops.ppe_foe_commit_entry = airoha_npu_foe_commit_entry;
	npu->ops.wlan_init_reserved_memory = airoha_npu_wlan_init_memory;
	npu->ops.wlan_send_msg = airoha_npu_wlan_msg_send;
	npu->ops.wlan_get_msg = airoha_npu_wlan_msg_get;
	npu->ops.wlan_get_queue_addr = airoha_npu_wlan_queue_addr_get;
	npu->ops.wlan_set_irq_status = airoha_npu_wlan_irq_status_set;
	npu->ops.wlan_get_irq_status = airoha_npu_wlan_irq_status_get;
	npu->ops.wlan_enable_irq = airoha_npu_wlan_irq_enable;
	npu->ops.wlan_disable_irq = airoha_npu_wlan_irq_disable;

	npu->regmap = devm_regmap_init_mmio(dev, base, &regmap_config);
	if (IS_ERR(npu->regmap))
		return PTR_ERR(npu->regmap);

	err = of_reserved_mem_region_to_resource(dev->of_node, 0, &res);
	if (err)
		return err;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	err = devm_request_irq(dev, irq, airoha_npu_mbox_handler,
			       IRQF_SHARED, "airoha-npu-mbox", npu);
	if (err)
		return err;

	for (i = 0; i < ARRAY_SIZE(npu->cores); i++) {
		struct airoha_npu_core *core = &npu->cores[i];

		spin_lock_init(&core->lock);
		core->npu = npu;

		irq = platform_get_irq(pdev, i + 1);
		if (irq < 0)
			return irq;

		err = devm_request_irq(dev, irq, airoha_npu_wdt_handler,
				       IRQF_SHARED, "airoha-npu-wdt", core);
		if (err)
			return err;

		INIT_WORK(&core->wdt_work, airoha_npu_wdt_work);
	}

	/* wlan IRQ lines */
	for (i = 0; i < ARRAY_SIZE(npu->irqs); i++) {
		irq = platform_get_irq(pdev, i + ARRAY_SIZE(npu->cores) + 1);
		if (irq < 0)
			return irq;

		npu->irqs[i] = irq;
	}

	err = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	err = airoha_npu_run_firmware(dev, base, &res);
	if (err)
		return dev_err_probe(dev, err, "failed to run npu firmware\n");

	regmap_write(npu->regmap, REG_CR_NPU_MIB(10),
		     res.start + NPU_EN7581_FIRMWARE_RV32_MAX_SIZE);
	regmap_write(npu->regmap, REG_CR_NPU_MIB(11), 0x40000); /* SRAM 256K */
	regmap_write(npu->regmap, REG_CR_NPU_MIB(12), 0);
	regmap_write(npu->regmap, REG_CR_NPU_MIB(21), 1);
	msleep(100);

	/* setting booting address */
	for (i = 0; i < NPU_NUM_CORES; i++)
		regmap_write(npu->regmap, REG_CR_BOOT_BASE(i), res.start);
	usleep_range(1000, 2000);

	/* enable NPU cores */
	regmap_write(npu->regmap, REG_CR_BOOT_CONFIG, 0xff);
	regmap_write(npu->regmap, REG_CR_BOOT_TRIGGER, 0x1);
	msleep(100);

	platform_set_drvdata(pdev, npu);

	return 0;
}

static void airoha_npu_remove(struct platform_device *pdev)
{
	struct airoha_npu *npu = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < ARRAY_SIZE(npu->cores); i++)
		cancel_work_sync(&npu->cores[i].wdt_work);
}

static struct platform_driver airoha_npu_driver = {
	.probe = airoha_npu_probe,
	.remove = airoha_npu_remove,
	.driver = {
		.name = "airoha-npu",
		.of_match_table = of_airoha_npu_match,
	},
};
module_platform_driver(airoha_npu_driver);

MODULE_FIRMWARE(NPU_EN7581_FIRMWARE_DATA);
MODULE_FIRMWARE(NPU_EN7581_FIRMWARE_RV32);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_DESCRIPTION("Airoha Network Processor Unit driver");
