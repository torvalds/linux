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

#include "airoha_npu.h"

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
			int ppe_type;
			int wan_mode;
			int wan_sel;
		} init_info;
		struct {
			int func_id;
			u32 size;
			u32 data;
		} set_info;
	};
};

static int airoha_npu_send_msg(struct airoha_npu *npu, int func_id,
			       void *p, int size)
{
	u16 core = 0; /* FIXME */
	u32 val, offset = core << 4;
	dma_addr_t dma_addr;
	void *addr;
	int ret;

	addr = kmemdup(p, size, GFP_ATOMIC);
	if (!addr)
		return -ENOMEM;

	dma_addr = dma_map_single(npu->dev, addr, size, DMA_TO_DEVICE);
	ret = dma_mapping_error(npu->dev, dma_addr);
	if (ret)
		goto out;

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
out:
	kfree(addr);

	return ret;
}

static int airoha_npu_run_firmware(struct device *dev, void __iomem *base,
				   struct reserved_mem *rmem)
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

	addr = devm_ioremap(dev, rmem->base, rmem->size);
	if (!addr) {
		ret = -ENOMEM;
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
	struct ppe_mbox_data ppe_data = {
		.func_type = NPU_OP_SET,
		.func_id = PPE_FUNC_SET_WAIT_HWNAT_INIT,
		.init_info = {
			.ppe_type = PPE_TYPE_L2B_IPV4_IPV6,
			.wan_mode = QDMA_WAN_ETHER,
		},
	};

	return airoha_npu_send_msg(npu, NPU_FUNC_PPE, &ppe_data,
				   sizeof(struct ppe_mbox_data));
}

static int airoha_npu_ppe_deinit(struct airoha_npu *npu)
{
	struct ppe_mbox_data ppe_data = {
		.func_type = NPU_OP_SET,
		.func_id = PPE_FUNC_SET_WAIT_HWNAT_DEINIT,
	};

	return airoha_npu_send_msg(npu, NPU_FUNC_PPE, &ppe_data,
				   sizeof(struct ppe_mbox_data));
}

static int airoha_npu_ppe_flush_sram_entries(struct airoha_npu *npu,
					     dma_addr_t foe_addr,
					     int sram_num_entries)
{
	struct ppe_mbox_data ppe_data = {
		.func_type = NPU_OP_SET,
		.func_id = PPE_FUNC_SET_WAIT_API,
		.set_info = {
			.func_id = PPE_SRAM_RESET_VAL,
			.data = foe_addr,
			.size = sram_num_entries,
		},
	};

	return airoha_npu_send_msg(npu, NPU_FUNC_PPE, &ppe_data,
				   sizeof(struct ppe_mbox_data));
}

static int airoha_npu_foe_commit_entry(struct airoha_npu *npu,
				       dma_addr_t foe_addr,
				       u32 entry_size, u32 hash, bool ppe2)
{
	struct ppe_mbox_data ppe_data = {
		.func_type = NPU_OP_SET,
		.func_id = PPE_FUNC_SET_WAIT_API,
		.set_info = {
			.data = foe_addr,
			.size = entry_size,
		},
	};
	int err;

	ppe_data.set_info.func_id = ppe2 ? PPE2_SRAM_SET_ENTRY
					 : PPE_SRAM_SET_ENTRY;

	err = airoha_npu_send_msg(npu, NPU_FUNC_PPE, &ppe_data,
				  sizeof(struct ppe_mbox_data));
	if (err)
		return err;

	ppe_data.set_info.func_id = PPE_SRAM_SET_VAL;
	ppe_data.set_info.data = hash;
	ppe_data.set_info.size = sizeof(u32);

	return airoha_npu_send_msg(npu, NPU_FUNC_PPE, &ppe_data,
				   sizeof(struct ppe_mbox_data));
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
	of_node_put(np);

	if (!pdev) {
		dev_err(dev, "cannot find device node %s\n", np->name);
		return ERR_PTR(-ENODEV);
	}

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
	struct reserved_mem *rmem;
	struct airoha_npu *npu;
	struct device_node *np;
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
	npu->ops.ppe_flush_sram_entries = airoha_npu_ppe_flush_sram_entries;
	npu->ops.ppe_foe_commit_entry = airoha_npu_foe_commit_entry;

	npu->regmap = devm_regmap_init_mmio(dev, base, &regmap_config);
	if (IS_ERR(npu->regmap))
		return PTR_ERR(npu->regmap);

	np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!np)
		return -ENODEV;

	rmem = of_reserved_mem_lookup(np);
	of_node_put(np);

	if (!rmem)
		return -ENODEV;

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

	err = dma_set_coherent_mask(dev, DMA_BIT_MASK(32));
	if (err)
		return err;

	err = airoha_npu_run_firmware(dev, base, rmem);
	if (err)
		return dev_err_probe(dev, err, "failed to run npu firmware\n");

	regmap_write(npu->regmap, REG_CR_NPU_MIB(10),
		     rmem->base + NPU_EN7581_FIRMWARE_RV32_MAX_SIZE);
	regmap_write(npu->regmap, REG_CR_NPU_MIB(11), 0x40000); /* SRAM 256K */
	regmap_write(npu->regmap, REG_CR_NPU_MIB(12), 0);
	regmap_write(npu->regmap, REG_CR_NPU_MIB(21), 1);
	msleep(100);

	/* setting booting address */
	for (i = 0; i < NPU_NUM_CORES; i++)
		regmap_write(npu->regmap, REG_CR_BOOT_BASE(i), rmem->base);
	usleep_range(1000, 2000);

	/* enable NPU cores */
	/* do not start core3 since it is used for WiFi offloading */
	regmap_write(npu->regmap, REG_CR_BOOT_CONFIG, 0xf7);
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

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lorenzo Bianconi <lorenzo@kernel.org>");
MODULE_DESCRIPTION("Airoha Network Processor Unit driver");
