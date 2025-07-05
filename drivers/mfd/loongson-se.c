// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 *
 * Author: Yinggang Gu <guyinggang@loongson.cn>
 * Author: Qunqin Zhao <zhaoqunqin@loongson.cn>
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/mfd/loongson-se.h>
#include <linux/module.h>
#include <linux/platform_device.h>

struct loongson_se {
	void __iomem *base;
	spinlock_t dev_lock;
	struct completion cmd_completion;

	void *dmam_base;
	int dmam_size;

	struct mutex engine_init_lock;
	struct loongson_se_engine engines[SE_ENGINE_MAX];
};

struct loongson_se_controller_cmd {
	u32 command_id;
	u32 info[7];
};

static int loongson_se_poll(struct loongson_se *se, u32 int_bit)
{
	u32 status;
	int err;

	spin_lock_irq(&se->dev_lock);

	/* Notify the controller that the engine needs to be started */
	writel(int_bit, se->base + SE_L2SINT_SET);

	/* Polling until the controller has forwarded the engine command */
	err = readl_relaxed_poll_timeout_atomic(se->base + SE_L2SINT_STAT, status,
						!(status & int_bit),
						1, LOONGSON_ENGINE_CMD_TIMEOUT_US);

	spin_unlock_irq(&se->dev_lock);

	return err;
}

static int loongson_se_send_controller_cmd(struct loongson_se *se,
					   struct loongson_se_controller_cmd *cmd)
{
	u32 *send_cmd = (u32 *)cmd;
	int err, i;

	for (i = 0; i < SE_SEND_CMD_REG_LEN; i++)
		writel(send_cmd[i], se->base + SE_SEND_CMD_REG + i * 4);

	err = loongson_se_poll(se, SE_INT_CONTROLLER);
	if (err)
		return err;

	return wait_for_completion_interruptible(&se->cmd_completion);
}

int loongson_se_send_engine_cmd(struct loongson_se_engine *engine)
{
	/*
	 * After engine initialization, the controller already knows
	 * where to obtain engine commands from. Now all we need to
	 * do is notify the controller that the engine needs to be started.
	 */
	int err = loongson_se_poll(engine->se, BIT(engine->id));

	if (err)
		return err;

	return wait_for_completion_interruptible(&engine->completion);
}
EXPORT_SYMBOL_GPL(loongson_se_send_engine_cmd);

struct loongson_se_engine *loongson_se_init_engine(struct device *dev, int id)
{
	struct loongson_se *se = dev_get_drvdata(dev);
	struct loongson_se_engine *engine = &se->engines[id];
	struct loongson_se_controller_cmd cmd;

	engine->se = se;
	engine->id = id;
	init_completion(&engine->completion);

	/* Divide DMA memory equally among all engines */
	engine->buffer_size = se->dmam_size / SE_ENGINE_MAX;
	engine->buffer_off = (se->dmam_size / SE_ENGINE_MAX) * id;
	engine->data_buffer = se->dmam_base + engine->buffer_off;

	/*
	 * There has no engine0, use its data buffer as command buffer for other
	 * engines. The DMA memory size is obtained from the ACPI table, which
	 * ensures that the data buffer size of engine0 is larger than the
	 * command buffer size of all engines.
	 */
	engine->command = se->dmam_base + id * (2 * SE_ENGINE_CMD_SIZE);
	engine->command_ret = engine->command + SE_ENGINE_CMD_SIZE;

	mutex_lock(&se->engine_init_lock);

	/* Tell the controller where to find engine command */
	cmd.command_id = SE_CMD_SET_ENGINE_CMDBUF;
	cmd.info[0] = id;
	cmd.info[1] = engine->command - se->dmam_base;
	cmd.info[2] = 2 * SE_ENGINE_CMD_SIZE;

	if (loongson_se_send_controller_cmd(se, &cmd))
		engine = NULL;

	mutex_unlock(&se->engine_init_lock);

	return engine;
}
EXPORT_SYMBOL_GPL(loongson_se_init_engine);

static irqreturn_t se_irq_handler(int irq, void *dev_id)
{
	struct loongson_se *se = dev_id;
	u32 int_status;
	int id;

	spin_lock(&se->dev_lock);

	int_status = readl(se->base + SE_S2LINT_STAT);

	/* For controller */
	if (int_status & SE_INT_CONTROLLER) {
		complete(&se->cmd_completion);
		int_status &= ~SE_INT_CONTROLLER;
		writel(SE_INT_CONTROLLER, se->base + SE_S2LINT_CL);
	}

	/* For engines */
	while (int_status) {
		id = __ffs(int_status);
		complete(&se->engines[id].completion);
		int_status &= ~BIT(id);
		writel(BIT(id), se->base + SE_S2LINT_CL);
	}

	spin_unlock(&se->dev_lock);

	return IRQ_HANDLED;
}

static int loongson_se_init(struct loongson_se *se, dma_addr_t addr, int size)
{
	struct loongson_se_controller_cmd cmd;
	int err;

	cmd.command_id = SE_CMD_START;
	err = loongson_se_send_controller_cmd(se, &cmd);
	if (err)
		return err;

	cmd.command_id = SE_CMD_SET_DMA;
	cmd.info[0] = lower_32_bits(addr);
	cmd.info[1] = upper_32_bits(addr);
	cmd.info[2] = size;

	return loongson_se_send_controller_cmd(se, &cmd);
}

static const struct mfd_cell engines[] = {
	{ .name = "loongson-rng" },
	{ .name = "tpm_loongson" },
};

static int loongson_se_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct loongson_se *se;
	int nr_irq, irq, err, i;
	dma_addr_t paddr;

	se = devm_kmalloc(dev, sizeof(*se), GFP_KERNEL);
	if (!se)
		return -ENOMEM;

	dev_set_drvdata(dev, se);
	init_completion(&se->cmd_completion);
	spin_lock_init(&se->dev_lock);
	mutex_init(&se->engine_init_lock);

	dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (device_property_read_u32(dev, "dmam_size", &se->dmam_size))
		return -ENODEV;

	se->dmam_base = dmam_alloc_coherent(dev, se->dmam_size, &paddr, GFP_KERNEL);
	if (!se->dmam_base)
		return -ENOMEM;

	se->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(se->base))
		return PTR_ERR(se->base);

	writel(SE_INT_ALL, se->base + SE_S2LINT_EN);

	nr_irq = platform_irq_count(pdev);
	if (nr_irq <= 0)
		return -ENODEV;

	for (i = 0; i < nr_irq; i++) {
		irq = platform_get_irq(pdev, i);
		err = devm_request_irq(dev, irq, se_irq_handler, 0, "loongson-se", se);
		if (err)
			dev_err(dev, "failed to request IRQ: %d\n", irq);
	}

	err = loongson_se_init(se, paddr, se->dmam_size);
	if (err)
		return err;

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, engines,
				    ARRAY_SIZE(engines), NULL, 0, NULL);
}

static const struct acpi_device_id loongson_se_acpi_match[] = {
	{ "LOON0011", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, loongson_se_acpi_match);

static struct platform_driver loongson_se_driver = {
	.probe   = loongson_se_probe,
	.driver  = {
		.name  = "loongson-se",
		.acpi_match_table = loongson_se_acpi_match,
	},
};
module_platform_driver(loongson_se_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yinggang Gu <guyinggang@loongson.cn>");
MODULE_AUTHOR("Qunqin Zhao <zhaoqunqin@loongson.cn>");
MODULE_DESCRIPTION("Loongson Security Engine chip controller driver");
