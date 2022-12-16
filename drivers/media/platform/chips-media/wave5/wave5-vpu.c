// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Wave5 series multi-standard codec IP - platform driver
 *
 * Copyright (C) 2021 CHIPS&MEDIA INC
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include "wave5-vpu.h"
#include "wave5-regdefine.h"
#include "wave5-vpuconfig.h"
#include "wave5.h"

#define VPU_PLATFORM_DEVICE_NAME "vdec"
#define VPU_CLK_NAME "vcodec"

#define WAVE5_IS_ENC BIT(0)
#define WAVE5_IS_DEC BIT(1)

struct wave5_match_data {
	int flags;
	const char *fw_name;
};

int wave5_vpu_wait_interrupt(struct vpu_instance *inst, unsigned int timeout)
{
	int ret;

	ret = wait_for_completion_timeout(&inst->irq_done,
					  msecs_to_jiffies(timeout));
	if (!ret)
		return -ETIMEDOUT;

	reinit_completion(&inst->irq_done);

	return 0;
}

static void wave5_vpu_get_interrupt_for_inst(struct vpu_instance *inst, u32 status)
{
	struct vpu_device *dev = inst->dev;
	u32 seq_done;
	u32 cmd_done;
	int val;

	seq_done = wave5_vdi_readl(dev, W5_RET_SEQ_DONE_INSTANCE_INFO);
	cmd_done = wave5_vdi_readl(dev, W5_RET_QUEUE_CMD_DONE_INST);

	if (status & BIT(INT_WAVE5_INIT_SEQ)) {
		if (seq_done & BIT(inst->id)) {
			seq_done &= ~BIT(inst->id);
			wave5_vdi_write_register(dev, W5_RET_SEQ_DONE_INSTANCE_INFO, seq_done);
			val = BIT(INT_WAVE5_INIT_SEQ);
			kfifo_in(&inst->irq_status, &val, sizeof(int));
		}
	}
	if (status & BIT(INT_WAVE5_ENC_SET_PARAM)) {
		if (seq_done & BIT(inst->id)) {
			seq_done &= ~BIT(inst->id);
			wave5_vdi_write_register(dev, W5_RET_SEQ_DONE_INSTANCE_INFO, seq_done);
			val = BIT(INT_WAVE5_ENC_SET_PARAM);
			kfifo_in(&inst->irq_status, &val, sizeof(int));
		}
	}
	if (status & BIT(INT_WAVE5_DEC_PIC) ||
	    status & BIT(INT_WAVE5_ENC_PIC)) {
		if (cmd_done & BIT(inst->id)) {
			cmd_done &= ~BIT(inst->id);
			wave5_vdi_write_register(dev, W5_RET_QUEUE_CMD_DONE_INST, cmd_done);
			val = BIT(INT_WAVE5_DEC_PIC);
			kfifo_in(&inst->irq_status, &val, sizeof(int));
		}
	}
}

static irqreturn_t wave5_vpu_irq(int irq, void *dev_id)
{
	struct vpu_device *dev = dev_id;

	if (wave5_vdi_readl(dev, W5_VPU_VPU_INT_STS)) {
		struct vpu_instance *inst;
		u32 irq_status = wave5_vdi_readl(dev, W5_VPU_VINT_REASON);

		list_for_each_entry(inst, &dev->instances, list) {
			wave5_vpu_get_interrupt_for_inst(inst, irq_status);
		}

		wave5_vdi_write_register(dev, W5_VPU_VINT_REASON_CLR, irq_status);
		wave5_vdi_write_register(dev, W5_VPU_VINT_CLEAR, 0x1);

		return IRQ_WAKE_THREAD;
	}

	return IRQ_HANDLED;
}

static irqreturn_t wave5_vpu_irq_thread(int irq, void *dev_id)
{
	struct vpu_device *dev = dev_id;
	struct vpu_instance *inst;
	int irq_status, ret;
	u32 val;

	list_for_each_entry(inst, &dev->instances, list) {
		while (kfifo_len(&inst->irq_status)) {
			struct vpu_instance *curr;

			curr = v4l2_m2m_get_curr_priv(inst->v4l2_m2m_dev);
			if (curr) {
				inst->ops->finish_process(inst);
			} else {
				ret = kfifo_out(&inst->irq_status, &irq_status, sizeof(int));
				if (!ret)
					break;

				val = wave5_vdi_readl(dev, W5_VPU_VINT_REASON_USR);
				val &= ~irq_status;
				wave5_vdi_write_register(dev, W5_VPU_VINT_REASON_USR, val);
				complete(&inst->irq_done);
			}
		}
	}

	return IRQ_HANDLED;
}

static int wave5_vpu_load_firmware(struct device *dev, const char *fw_name)
{
	const struct firmware *fw;
	int ret;
	u32 revision;
	unsigned int product_id;

	ret = request_firmware(&fw, fw_name, dev);
	if (ret) {
		dev_err(dev, "request_firmware, fail: %d\n", ret);
		return ret;
	}

	ret = wave5_vpu_init_with_bitcode(dev, (u8 *)fw->data, fw->size);
	if (ret) {
		dev_err(dev, "vpu_init_with_bitcode, fail: %d\n", ret);
		goto release_fw;
	}
	release_firmware(fw);

	ret = wave5_vpu_get_version_info(dev, &revision, &product_id);
	if (ret) {
		dev_err(dev, "vpu_get_version_info fail: %d\n", ret);
		goto err_without_release;
	}

	dev_dbg(dev, "%s: enum product_id: %08x, fw revision: %u\n",
		__func__, product_id, revision);

	return 0;

release_fw:
	release_firmware(fw);
err_without_release:
	return ret;
}

static int wave5_vpu_probe(struct platform_device *pdev)
{
	int ret;
	struct vpu_device *dev;
	struct device_node *np;
	const struct wave5_match_data *match_data;
	struct resource sram;

	match_data = device_get_match_data(&pdev->dev);
	if (!match_data) {
		dev_err(&pdev->dev, "missing device match data\n");
		return -EINVAL;
	}

	/* physical addresses limited to 32 bits */
	dma_set_mask(&pdev->dev, DMA_BIT_MASK(32));
	dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));

	dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	dev->vdb_register = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(dev->vdb_register))
		return PTR_ERR(dev->vdb_register);
	ida_init(&dev->inst_ida);

	mutex_init(&dev->dev_lock);
	mutex_init(&dev->hw_lock);
	dev_set_drvdata(&pdev->dev, dev);
	dev->dev = &pdev->dev;

	ret = devm_clk_bulk_get_all(&pdev->dev, &dev->clks);

	/* continue without clock, assume externally managed */
	if (ret < 0) {
		dev_warn(&pdev->dev, "Getting clocks, fail: %d\n", ret);
		ret = 0;
	}
	dev->num_clks = ret;

	ret = clk_bulk_prepare_enable(dev->num_clks, dev->clks);
	if (ret) {
		dev_err(&pdev->dev, "Enabling clocks, fail: %d\n", ret);
		return ret;
	}

	np = of_parse_phandle(pdev->dev.of_node, "sram", 0);
	if (!np) {
		dev_warn(&pdev->dev, "sram node not found\n");
	} else {
		ret = of_address_to_resource(np, 0, &sram);
		if (ret) {
			dev_err(&pdev->dev, "sram resource not available\n");
			goto err_put_node;
		}
		dev->sram_buf.daddr = sram.start;
		dev->sram_buf.size = resource_size(&sram);
		dev_dbg(&pdev->dev, "%s: sram daddr: %pad, size: 0x%lx\n",
			__func__, &dev->sram_buf.daddr, dev->sram_buf.size);
	}

	dev->product_code = wave5_vdi_readl(dev, VPU_PRODUCT_CODE_REGISTER);
	ret = wave5_vdi_init(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "wave5_vdi_init, fail: %d\n", ret);
		goto err_clk_dis;
	}
	dev->product = wave5_vpu_get_product_id(dev);

	INIT_LIST_HEAD(&dev->instances);
	ret = v4l2_device_register(&pdev->dev, &dev->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "v4l2_device_register, fail: %d\n", ret);
		goto err_vdi_release;
	}

	if (match_data->flags & WAVE5_IS_DEC) {
		ret = wave5_vpu_dec_register_device(dev);
		if (ret) {
			dev_err(&pdev->dev, "wave5_vpu_dec_register_device, fail: %d\n", ret);
			goto err_v4l2_unregister;
		}
	}
	if (match_data->flags & WAVE5_IS_ENC) {
		ret = wave5_vpu_enc_register_device(dev);
		if (ret) {
			dev_err(&pdev->dev, "wave5_vpu_enc_register_device, fail: %d\n", ret);
			goto err_dec_unreg;
		}
	}

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0) {
		dev_err(&pdev->dev, "failed to get irq resource\n");
		ret = -ENXIO;
		goto err_enc_unreg;
	}

	ret = devm_request_threaded_irq(&pdev->dev, dev->irq, wave5_vpu_irq,
					wave5_vpu_irq_thread, 0, "vpu_irq", dev);
	if (ret) {
		dev_err(&pdev->dev, "Register interrupt handler, fail: %d\n", ret);
		goto err_enc_unreg;
	}

	ret = wave5_vpu_load_firmware(&pdev->dev, match_data->fw_name);
	if (ret) {
		dev_err(&pdev->dev, "wave5_vpu_load_firmware, fail: %d\n", ret);
		goto err_enc_unreg;
	}

	dev_dbg(&pdev->dev, "Added wave5 driver with caps: %s %s and product code: 0x%x\n",
		(match_data->flags & WAVE5_IS_ENC) ? "'ENCODE'" : "",
		(match_data->flags & WAVE5_IS_DEC) ? "'DECODE'" : "",
		dev->product_code);
	return 0;

err_enc_unreg:
	if (match_data->flags & WAVE5_IS_ENC)
		wave5_vpu_enc_unregister_device(dev);
err_dec_unreg:
	if (match_data->flags & WAVE5_IS_DEC)
		wave5_vpu_dec_unregister_device(dev);
err_v4l2_unregister:
	v4l2_device_unregister(&dev->v4l2_dev);
err_vdi_release:
	wave5_vdi_release(&pdev->dev);
err_clk_dis:
	clk_bulk_disable_unprepare(dev->num_clks, dev->clks);
err_put_node:
	of_node_put(np);

	return ret;
}

static int wave5_vpu_remove(struct platform_device *pdev)
{
	struct vpu_device *dev = dev_get_drvdata(&pdev->dev);

	clk_bulk_disable_unprepare(dev->num_clks, dev->clks);
	wave5_vpu_enc_unregister_device(dev);
	wave5_vpu_dec_unregister_device(dev);
	v4l2_device_unregister(&dev->v4l2_dev);
	wave5_vdi_release(&pdev->dev);
	ida_destroy(&dev->inst_ida);

	return 0;
}

static const struct wave5_match_data wave511_data = {
	.flags = WAVE5_IS_DEC,
	.fw_name = "wave511_dec_fw.bin",
};

static const struct wave5_match_data wave521_data = {
	.flags = WAVE5_IS_ENC,
	.fw_name = "wave521_enc_fw.bin",
};

static const struct wave5_match_data wave521c_data = {
	.flags = WAVE5_IS_ENC | WAVE5_IS_DEC,
	.fw_name = "wave521c_codec_fw.bin",
};

static const struct wave5_match_data default_match_data = {
	.flags = WAVE5_IS_ENC | WAVE5_IS_DEC,
	.fw_name = "chagall.bin",
};

static const struct of_device_id wave5_dt_ids[] = {
	{ .compatible = "cnm,cm511-vpu", .data = &wave511_data },
	{ .compatible = "cnm,cm517-vpu", .data = &default_match_data },
	{ .compatible = "cnm,cm521-vpu", .data = &wave521_data },
	{ .compatible = "cnm,cm521c-vpu", .data = &wave521c_data },
	{ .compatible = "cnm,cm521c-dual-vpu", .data = &wave521c_data },
	{ .compatible = "cnm,cm521e1-vpu", .data = &default_match_data },
	{ .compatible = "cnm,cm537-vpu", .data = &default_match_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wave5_dt_ids);

static struct platform_driver wave5_vpu_driver = {
	.driver = {
		.name = VPU_PLATFORM_DEVICE_NAME,
		.of_match_table = of_match_ptr(wave5_dt_ids),
		},
	.probe = wave5_vpu_probe,
	.remove = wave5_vpu_remove,
};

module_platform_driver(wave5_vpu_driver);
MODULE_DESCRIPTION("chips&media VPU V4L2 driver");
MODULE_LICENSE("Dual BSD/GPL");
