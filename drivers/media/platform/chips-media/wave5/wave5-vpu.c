// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Wave5 series multi-standard codec IP - platform driver
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/firmware.h>
#include <linux/interrupt.h>
#include <linux/reset.h>
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
	u32 sram_size;
};

static int vpu_poll_interval = 5;
module_param(vpu_poll_interval, int, 0644);

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

static void wave5_vpu_handle_irq(void *dev_id)
{
	u32 seq_done;
	u32 cmd_done;
	u32 irq_reason;
	struct vpu_instance *inst;
	struct vpu_device *dev = dev_id;

	irq_reason = wave5_vdi_read_register(dev, W5_VPU_VINT_REASON);
	wave5_vdi_write_register(dev, W5_VPU_VINT_REASON_CLR, irq_reason);
	wave5_vdi_write_register(dev, W5_VPU_VINT_CLEAR, 0x1);

	list_for_each_entry(inst, &dev->instances, list) {
		seq_done = wave5_vdi_read_register(dev, W5_RET_SEQ_DONE_INSTANCE_INFO);
		cmd_done = wave5_vdi_read_register(dev, W5_RET_QUEUE_CMD_DONE_INST);

		if (irq_reason & BIT(INT_WAVE5_INIT_SEQ) ||
		    irq_reason & BIT(INT_WAVE5_ENC_SET_PARAM)) {
			if (dev->product_code == WAVE515_CODE &&
			    (cmd_done & BIT(inst->id))) {
				cmd_done &= ~BIT(inst->id);
				wave5_vdi_write_register(dev, W5_RET_QUEUE_CMD_DONE_INST,
							 cmd_done);
				complete(&inst->irq_done);
			} else if (seq_done & BIT(inst->id)) {
				seq_done &= ~BIT(inst->id);
				wave5_vdi_write_register(dev, W5_RET_SEQ_DONE_INSTANCE_INFO,
							 seq_done);
				complete(&inst->irq_done);
			}
		}

		if (irq_reason & BIT(INT_WAVE5_DEC_PIC) ||
		    irq_reason & BIT(INT_WAVE5_ENC_PIC)) {
			if (cmd_done & BIT(inst->id)) {
				cmd_done &= ~BIT(inst->id);
				wave5_vdi_write_register(dev, W5_RET_QUEUE_CMD_DONE_INST,
							 cmd_done);
				inst->ops->finish_process(inst);
			}
		}

		wave5_vpu_clear_interrupt(inst, irq_reason);
	}
}

static irqreturn_t wave5_vpu_irq_thread(int irq, void *dev_id)
{
	struct vpu_device *dev = dev_id;

	if (wave5_vdi_read_register(dev, W5_VPU_VPU_INT_STS))
		wave5_vpu_handle_irq(dev);

	return IRQ_HANDLED;
}

static void wave5_vpu_irq_work_fn(struct kthread_work *work)
{
	struct vpu_device *dev = container_of(work, struct vpu_device, work);

	if (wave5_vdi_read_register(dev, W5_VPU_VPU_INT_STS))
		wave5_vpu_handle_irq(dev);
}

static enum hrtimer_restart wave5_vpu_timer_callback(struct hrtimer *timer)
{
	struct vpu_device *dev =
			container_of(timer, struct vpu_device, hrtimer);

	kthread_queue_work(dev->worker, &dev->work);
	hrtimer_forward_now(timer, ns_to_ktime(vpu_poll_interval * NSEC_PER_MSEC));

	return HRTIMER_RESTART;
}

static int wave5_vpu_load_firmware(struct device *dev, const char *fw_name,
				   u32 *revision)
{
	const struct firmware *fw;
	int ret;
	unsigned int product_id;

	ret = request_firmware(&fw, fw_name, dev);
	if (ret) {
		dev_err(dev, "request_firmware, fail: %d\n", ret);
		return ret;
	}

	ret = wave5_vpu_init_with_bitcode(dev, (u8 *)fw->data, fw->size);
	if (ret) {
		dev_err(dev, "vpu_init_with_bitcode, fail: %d\n", ret);
		release_firmware(fw);
		return ret;
	}
	release_firmware(fw);

	ret = wave5_vpu_get_version_info(dev, revision, &product_id);
	if (ret) {
		dev_err(dev, "vpu_get_version_info fail: %d\n", ret);
		return ret;
	}

	dev_dbg(dev, "%s: enum product_id: %08x, fw revision: %u\n",
		__func__, product_id, *revision);

	return 0;
}

static int wave5_vpu_probe(struct platform_device *pdev)
{
	int ret;
	struct vpu_device *dev;
	const struct wave5_match_data *match_data;
	u32 fw_revision;

	match_data = device_get_match_data(&pdev->dev);
	if (!match_data) {
		dev_err(&pdev->dev, "missing device match data\n");
		return -EINVAL;
	}

	/* physical addresses limited to 32 bits */
	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev, "Failed to set DMA mask: %d\n", ret);
		return ret;
	}

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

	dev->resets = devm_reset_control_array_get_optional_exclusive(&pdev->dev);
	if (IS_ERR(dev->resets)) {
		return dev_err_probe(&pdev->dev, PTR_ERR(dev->resets),
				     "Failed to get reset control\n");
	}

	ret = reset_control_deassert(dev->resets);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "Failed to deassert resets\n");

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
		goto err_reset_assert;
	}

	dev->sram_pool = of_gen_pool_get(pdev->dev.of_node, "sram", 0);
	if (!dev->sram_pool)
		dev_warn(&pdev->dev, "sram node not found\n");

	dev->sram_size = match_data->sram_size;

	dev->product_code = wave5_vdi_read_register(dev, VPU_PRODUCT_CODE_REGISTER);
	ret = wave5_vdi_init(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "wave5_vdi_init, fail: %d\n", ret);
		goto err_clk_dis;
	}
	dev->product = wave5_vpu_get_product_id(dev);

	dev->irq = platform_get_irq(pdev, 0);
	if (dev->irq < 0) {
		dev_err(&pdev->dev, "failed to get irq resource, falling back to polling\n");
		hrtimer_init(&dev->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_PINNED);
		dev->hrtimer.function = &wave5_vpu_timer_callback;
		dev->worker = kthread_create_worker(0, "vpu_irq_thread");
		if (IS_ERR(dev->worker)) {
			dev_err(&pdev->dev, "failed to create vpu irq worker\n");
			ret = PTR_ERR(dev->worker);
			goto err_vdi_release;
		}
		dev->vpu_poll_interval = vpu_poll_interval;
		kthread_init_work(&dev->work, wave5_vpu_irq_work_fn);
	} else {
		ret = devm_request_threaded_irq(&pdev->dev, dev->irq, NULL,
						wave5_vpu_irq_thread, IRQF_ONESHOT, "vpu_irq", dev);
		if (ret) {
			dev_err(&pdev->dev, "Register interrupt handler, fail: %d\n", ret);
			goto err_enc_unreg;
		}
	}

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

	ret = wave5_vpu_load_firmware(&pdev->dev, match_data->fw_name, &fw_revision);
	if (ret) {
		dev_err(&pdev->dev, "wave5_vpu_load_firmware, fail: %d\n", ret);
		goto err_enc_unreg;
	}

	dev_info(&pdev->dev, "Added wave5 driver with caps: %s %s\n",
		 (match_data->flags & WAVE5_IS_ENC) ? "'ENCODE'" : "",
		 (match_data->flags & WAVE5_IS_DEC) ? "'DECODE'" : "");
	dev_info(&pdev->dev, "Product Code:      0x%x\n", dev->product_code);
	dev_info(&pdev->dev, "Firmware Revision: %u\n", fw_revision);
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
err_reset_assert:
	reset_control_assert(dev->resets);

	return ret;
}

static void wave5_vpu_remove(struct platform_device *pdev)
{
	struct vpu_device *dev = dev_get_drvdata(&pdev->dev);

	if (dev->irq < 0) {
		kthread_destroy_worker(dev->worker);
		hrtimer_cancel(&dev->hrtimer);
	}

	mutex_destroy(&dev->dev_lock);
	mutex_destroy(&dev->hw_lock);
	reset_control_assert(dev->resets);
	clk_bulk_disable_unprepare(dev->num_clks, dev->clks);
	wave5_vpu_enc_unregister_device(dev);
	wave5_vpu_dec_unregister_device(dev);
	v4l2_device_unregister(&dev->v4l2_dev);
	wave5_vdi_release(&pdev->dev);
	ida_destroy(&dev->inst_ida);
}

static const struct wave5_match_data ti_wave521c_data = {
	.flags = WAVE5_IS_ENC | WAVE5_IS_DEC,
	.fw_name = "cnm/wave521c_k3_codec_fw.bin",
	.sram_size = (64 * 1024),
};

static const struct of_device_id wave5_dt_ids[] = {
	{ .compatible = "ti,j721s2-wave521c", .data = &ti_wave521c_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wave5_dt_ids);

static struct platform_driver wave5_vpu_driver = {
	.driver = {
		.name = VPU_PLATFORM_DEVICE_NAME,
		.of_match_table = of_match_ptr(wave5_dt_ids),
		},
	.probe = wave5_vpu_probe,
	.remove_new = wave5_vpu_remove,
};

module_platform_driver(wave5_vpu_driver);
MODULE_DESCRIPTION("chips&media VPU V4L2 driver");
MODULE_LICENSE("Dual BSD/GPL");
