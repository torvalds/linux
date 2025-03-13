// SPDX-License-Identifier: GPL-2.0
/*
 * CS40L50 Advanced Haptic Driver with waveform memory,
 * integrated DSP, and closed-loop algorithms
 *
 * Copyright 2024 Cirrus Logic, Inc.
 *
 * Author: James Ogletree <james.ogletree@cirrus.com>
 */

#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/wmfw.h>
#include <linux/mfd/core.h>
#include <linux/mfd/cs40l50.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

static const struct mfd_cell cs40l50_devs[] = {
	{ .name = "cs40l50-codec", },
	{ .name = "cs40l50-vibra", },
};

const struct regmap_config cs40l50_regmap = {
	.reg_bits =		32,
	.reg_stride =		4,
	.val_bits =		32,
	.reg_format_endian =	REGMAP_ENDIAN_BIG,
	.val_format_endian =	REGMAP_ENDIAN_BIG,
};
EXPORT_SYMBOL_GPL(cs40l50_regmap);

static const char * const cs40l50_supplies[] = {
	"vdd-io",
};

static const struct regmap_irq cs40l50_reg_irqs[] = {
	REGMAP_IRQ_REG(CS40L50_DSP_QUEUE_IRQ, CS40L50_IRQ1_INT_2_OFFSET,
		       CS40L50_DSP_QUEUE_MASK),
	REGMAP_IRQ_REG(CS40L50_AMP_SHORT_IRQ, CS40L50_IRQ1_INT_1_OFFSET,
		       CS40L50_AMP_SHORT_MASK),
	REGMAP_IRQ_REG(CS40L50_TEMP_ERR_IRQ, CS40L50_IRQ1_INT_8_OFFSET,
		       CS40L50_TEMP_ERR_MASK),
	REGMAP_IRQ_REG(CS40L50_BST_UVP_IRQ, CS40L50_IRQ1_INT_9_OFFSET,
		       CS40L50_BST_UVP_MASK),
	REGMAP_IRQ_REG(CS40L50_BST_SHORT_IRQ, CS40L50_IRQ1_INT_9_OFFSET,
		       CS40L50_BST_SHORT_MASK),
	REGMAP_IRQ_REG(CS40L50_BST_ILIMIT_IRQ, CS40L50_IRQ1_INT_9_OFFSET,
		       CS40L50_BST_ILIMIT_MASK),
	REGMAP_IRQ_REG(CS40L50_UVLO_VDDBATT_IRQ, CS40L50_IRQ1_INT_10_OFFSET,
		       CS40L50_UVLO_VDDBATT_MASK),
	REGMAP_IRQ_REG(CS40L50_GLOBAL_ERROR_IRQ, CS40L50_IRQ1_INT_18_OFFSET,
		       CS40L50_GLOBAL_ERROR_MASK),
};

static struct regmap_irq_chip cs40l50_irq_chip = {
	.name =		"cs40l50",
	.status_base =	CS40L50_IRQ1_INT_1,
	.mask_base =	CS40L50_IRQ1_MASK_1,
	.ack_base =	CS40L50_IRQ1_INT_1,
	.num_regs =	22,
	.irqs =		cs40l50_reg_irqs,
	.num_irqs =	ARRAY_SIZE(cs40l50_reg_irqs),
	.runtime_pm =	true,
};

int cs40l50_dsp_write(struct device *dev, struct regmap *regmap, u32 val)
{
	int i, ret;
	u32 ack;

	/* Device NAKs if hibernating, so optionally retry */
	for (i = 0; i < CS40L50_DSP_TIMEOUT_COUNT; i++) {
		ret = regmap_write(regmap, CS40L50_DSP_QUEUE, val);
		if (!ret)
			break;

		usleep_range(CS40L50_DSP_POLL_US, CS40L50_DSP_POLL_US + 100);
	}

	/* If the write never took place, no need to check for the ACK */
	if (i == CS40L50_DSP_TIMEOUT_COUNT) {
		dev_err(dev, "Timed out writing %#X to DSP: %d\n", val, ret);
		return ret;
	}

	ret = regmap_read_poll_timeout(regmap, CS40L50_DSP_QUEUE, ack, !ack,
				       CS40L50_DSP_POLL_US,
				       CS40L50_DSP_POLL_US * CS40L50_DSP_TIMEOUT_COUNT);
	if (ret)
		dev_err(dev, "DSP failed to ACK %#X: %d\n", val, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(cs40l50_dsp_write);

static const struct cs_dsp_region cs40l50_dsp_regions[] = {
	{ .type = WMFW_HALO_PM_PACKED, .base = CS40L50_PMEM_0 },
	{ .type = WMFW_HALO_XM_PACKED, .base = CS40L50_XMEM_PACKED_0 },
	{ .type = WMFW_HALO_YM_PACKED, .base = CS40L50_YMEM_PACKED_0 },
	{ .type = WMFW_ADSP2_XM, .base = CS40L50_XMEM_UNPACKED24_0 },
	{ .type = WMFW_ADSP2_YM, .base = CS40L50_YMEM_UNPACKED24_0 },
};

static const struct reg_sequence cs40l50_internal_vamp_config[] = {
	{ CS40L50_BST_LPMODE_SEL, CS40L50_DCM_LOW_POWER },
	{ CS40L50_BLOCK_ENABLES2, CS40L50_OVERTEMP_WARN },
};

static const struct reg_sequence cs40l50_irq_mask_override[] = {
	{ CS40L50_IRQ1_MASK_2, CS40L50_IRQ_MASK_2_OVERRIDE },
	{ CS40L50_IRQ1_MASK_20, CS40L50_IRQ_MASK_20_OVERRIDE },
};

static int cs40l50_wseq_init(struct cs40l50 *cs40l50)
{
	struct cs_dsp *dsp = &cs40l50->dsp;

	cs40l50->wseqs[CS40L50_STANDBY].ctl = cs_dsp_get_ctl(dsp, "STANDBY_SEQUENCE",
							     WMFW_ADSP2_XM,
							     CS40L50_PM_ALGO);
	if (!cs40l50->wseqs[CS40L50_STANDBY].ctl) {
		dev_err(cs40l50->dev, "Control not found for standby sequence\n");
		return -ENOENT;
	}

	cs40l50->wseqs[CS40L50_ACTIVE].ctl = cs_dsp_get_ctl(dsp, "ACTIVE_SEQUENCE",
							    WMFW_ADSP2_XM,
							    CS40L50_PM_ALGO);
	if (!cs40l50->wseqs[CS40L50_ACTIVE].ctl) {
		dev_err(cs40l50->dev, "Control not found for active sequence\n");
		return -ENOENT;
	}

	cs40l50->wseqs[CS40L50_PWR_ON].ctl = cs_dsp_get_ctl(dsp, "PM_PWR_ON_SEQ",
							    WMFW_ADSP2_XM,
							    CS40L50_PM_ALGO);
	if (!cs40l50->wseqs[CS40L50_PWR_ON].ctl) {
		dev_err(cs40l50->dev, "Control not found for power-on sequence\n");
		return -ENOENT;
	}

	return cs_dsp_wseq_init(&cs40l50->dsp, cs40l50->wseqs, ARRAY_SIZE(cs40l50->wseqs));
}

static int cs40l50_dsp_config(struct cs40l50 *cs40l50)
{
	int ret;

	/* Configure internal V_AMP supply */
	ret = regmap_multi_reg_write(cs40l50->regmap, cs40l50_internal_vamp_config,
				     ARRAY_SIZE(cs40l50_internal_vamp_config));
	if (ret)
		return ret;

	ret = cs_dsp_wseq_multi_write(&cs40l50->dsp, &cs40l50->wseqs[CS40L50_PWR_ON],
				      cs40l50_internal_vamp_config, CS_DSP_WSEQ_FULL,
				      ARRAY_SIZE(cs40l50_internal_vamp_config), false);
	if (ret)
		return ret;

	/* Override firmware defaults for IRQ masks */
	ret = regmap_multi_reg_write(cs40l50->regmap, cs40l50_irq_mask_override,
				     ARRAY_SIZE(cs40l50_irq_mask_override));
	if (ret)
		return ret;

	return cs_dsp_wseq_multi_write(&cs40l50->dsp, &cs40l50->wseqs[CS40L50_PWR_ON],
				       cs40l50_irq_mask_override, CS_DSP_WSEQ_FULL,
				       ARRAY_SIZE(cs40l50_irq_mask_override), false);
}

static int cs40l50_dsp_post_run(struct cs_dsp *dsp)
{
	struct cs40l50 *cs40l50 = container_of(dsp, struct cs40l50, dsp);
	int ret;

	ret = cs40l50_wseq_init(cs40l50);
	if (ret)
		return ret;

	ret = cs40l50_dsp_config(cs40l50);
	if (ret) {
		dev_err(cs40l50->dev, "Failed to configure DSP: %d\n", ret);
		return ret;
	}

	ret = devm_mfd_add_devices(cs40l50->dev, PLATFORM_DEVID_NONE, cs40l50_devs,
				   ARRAY_SIZE(cs40l50_devs), NULL, 0, NULL);
	if (ret)
		dev_err(cs40l50->dev, "Failed to add child devices: %d\n", ret);

	return ret;
}

static const struct cs_dsp_client_ops client_ops = {
	.post_run = cs40l50_dsp_post_run,
};

static void cs40l50_dsp_remove(void *data)
{
	cs_dsp_remove(data);
}

static int cs40l50_dsp_init(struct cs40l50 *cs40l50)
{
	int ret;

	cs40l50->dsp.num = 1;
	cs40l50->dsp.type = WMFW_HALO;
	cs40l50->dsp.dev = cs40l50->dev;
	cs40l50->dsp.regmap = cs40l50->regmap;
	cs40l50->dsp.base = CS40L50_CORE_BASE;
	cs40l50->dsp.base_sysinfo = CS40L50_SYS_INFO_ID;
	cs40l50->dsp.mem = cs40l50_dsp_regions;
	cs40l50->dsp.num_mems = ARRAY_SIZE(cs40l50_dsp_regions);
	cs40l50->dsp.no_core_startstop = true;
	cs40l50->dsp.client_ops = &client_ops;

	ret = cs_dsp_halo_init(&cs40l50->dsp);
	if (ret)
		return ret;

	return devm_add_action_or_reset(cs40l50->dev, cs40l50_dsp_remove,
					&cs40l50->dsp);
}

static int cs40l50_reset_dsp(struct cs40l50 *cs40l50)
{
	int ret;

	mutex_lock(&cs40l50->lock);

	if (cs40l50->dsp.running)
		cs_dsp_stop(&cs40l50->dsp);

	if (cs40l50->dsp.booted)
		cs_dsp_power_down(&cs40l50->dsp);

	ret = cs40l50_dsp_write(cs40l50->dev, cs40l50->regmap, CS40L50_SHUTDOWN);
	if (ret)
		goto err_mutex;

	ret = cs_dsp_power_up(&cs40l50->dsp, cs40l50->fw, "cs40l50.wmfw",
			      cs40l50->bin, "cs40l50.bin", "cs40l50");
	if (ret)
		goto err_mutex;

	ret = cs40l50_dsp_write(cs40l50->dev, cs40l50->regmap, CS40L50_SYSTEM_RESET);
	if (ret)
		goto err_mutex;

	ret = cs40l50_dsp_write(cs40l50->dev, cs40l50->regmap, CS40L50_PREVENT_HIBER);
	if (ret)
		goto err_mutex;

	ret = cs_dsp_run(&cs40l50->dsp);
err_mutex:
	mutex_unlock(&cs40l50->lock);

	return ret;
}

static void cs40l50_dsp_power_down(void *data)
{
	cs_dsp_power_down(data);
}

static void cs40l50_dsp_stop(void *data)
{
	cs_dsp_stop(data);
}

static void cs40l50_dsp_bringup(const struct firmware *bin, void *context)
{
	struct cs40l50 *cs40l50 = context;
	u32 nwaves;
	int ret;

	/* Wavetable is optional; bringup DSP regardless */
	cs40l50->bin = bin;

	ret = cs40l50_reset_dsp(cs40l50);
	if (ret) {
		dev_err(cs40l50->dev, "Failed to reset DSP: %d\n", ret);
		goto err_fw;
	}

	ret = regmap_read(cs40l50->regmap, CS40L50_NUM_WAVES, &nwaves);
	if (ret)
		goto err_fw;

	dev_info(cs40l50->dev, "%u RAM effects loaded\n", nwaves);

	/* Add teardown actions for first-time bringup */
	ret = devm_add_action_or_reset(cs40l50->dev, cs40l50_dsp_power_down,
				       &cs40l50->dsp);
	if (ret) {
		dev_err(cs40l50->dev, "Failed to add power down action: %d\n", ret);
		goto err_fw;
	}

	ret = devm_add_action_or_reset(cs40l50->dev, cs40l50_dsp_stop, &cs40l50->dsp);
	if (ret)
		dev_err(cs40l50->dev, "Failed to add stop action: %d\n", ret);
err_fw:
	release_firmware(cs40l50->bin);
	release_firmware(cs40l50->fw);
}

static void cs40l50_request_firmware(const struct firmware *fw, void *context)
{
	struct cs40l50 *cs40l50 = context;
	int ret;

	if (!fw) {
		dev_err(cs40l50->dev, "No firmware file found\n");
		return;
	}

	cs40l50->fw = fw;

	ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT, CS40L50_WT,
				      cs40l50->dev, GFP_KERNEL, cs40l50,
				      cs40l50_dsp_bringup);
	if (ret) {
		dev_err(cs40l50->dev, "Failed to request %s: %d\n", CS40L50_WT, ret);
		release_firmware(cs40l50->fw);
	}
}

struct cs40l50_irq {
	const char *name;
	int virq;
};

static struct cs40l50_irq cs40l50_irqs[] = {
	{ "DSP", },
	{ "Global", },
	{ "Boost UVLO", },
	{ "Boost current limit", },
	{ "Boost short", },
	{ "Boost undervolt", },
	{ "Overtemp", },
	{ "Amp short", },
};

static const struct reg_sequence cs40l50_err_rls[] = {
	{ CS40L50_ERR_RLS, CS40L50_GLOBAL_ERR_RLS_SET },
	{ CS40L50_ERR_RLS, CS40L50_GLOBAL_ERR_RLS_CLEAR },
};

static irqreturn_t cs40l50_hw_err(int irq, void *data)
{
	struct cs40l50 *cs40l50 = data;
	int ret = 0, i;

	mutex_lock(&cs40l50->lock);

	/* Log hardware interrupt and execute error release sequence */
	for (i = 1; i < ARRAY_SIZE(cs40l50_irqs); i++) {
		if (cs40l50_irqs[i].virq == irq) {
			dev_err(cs40l50->dev, "%s error\n", cs40l50_irqs[i].name);
			ret = regmap_multi_reg_write(cs40l50->regmap, cs40l50_err_rls,
						     ARRAY_SIZE(cs40l50_err_rls));
			break;
		}
	}

	mutex_unlock(&cs40l50->lock);
	return IRQ_RETVAL(!ret);
}

static irqreturn_t cs40l50_dsp_queue(int irq, void *data)
{
	struct cs40l50 *cs40l50 = data;
	u32 rd_ptr, val, wt_ptr;
	int ret = 0;

	mutex_lock(&cs40l50->lock);

	/* Read from DSP queue, log, and update read pointer */
	while (!ret) {
		ret = regmap_read(cs40l50->regmap, CS40L50_DSP_QUEUE_WT, &wt_ptr);
		if (ret)
			break;

		ret = regmap_read(cs40l50->regmap, CS40L50_DSP_QUEUE_RD, &rd_ptr);
		if (ret)
			break;

		/* Check if queue is empty */
		if (wt_ptr == rd_ptr)
			break;

		ret = regmap_read(cs40l50->regmap, rd_ptr, &val);
		if (ret)
			break;

		dev_dbg(cs40l50->dev, "DSP payload: %#X", val);

		rd_ptr += sizeof(u32);

		if (rd_ptr > CS40L50_DSP_QUEUE_END)
			rd_ptr = CS40L50_DSP_QUEUE_BASE;

		ret = regmap_write(cs40l50->regmap, CS40L50_DSP_QUEUE_RD, rd_ptr);
	}

	mutex_unlock(&cs40l50->lock);

	return IRQ_RETVAL(!ret);
}

static int cs40l50_irq_init(struct cs40l50 *cs40l50)
{
	int ret, i, virq;

	ret = devm_regmap_add_irq_chip(cs40l50->dev, cs40l50->regmap, cs40l50->irq,
				       IRQF_ONESHOT | IRQF_SHARED, 0,
				       &cs40l50_irq_chip, &cs40l50->irq_data);
	if (ret) {
		dev_err(cs40l50->dev, "Failed adding IRQ chip\n");
		return ret;
	}

	for (i = 0; i < ARRAY_SIZE(cs40l50_irqs); i++) {
		virq = regmap_irq_get_virq(cs40l50->irq_data, i);
		if (virq < 0) {
			dev_err(cs40l50->dev, "Failed getting virq for %s\n",
				cs40l50_irqs[i].name);
			return virq;
		}

		cs40l50_irqs[i].virq = virq;

		/* Handle DSP and hardware interrupts separately */
		ret = devm_request_threaded_irq(cs40l50->dev, virq, NULL,
						i ? cs40l50_hw_err : cs40l50_dsp_queue,
						IRQF_ONESHOT | IRQF_SHARED,
						cs40l50_irqs[i].name, cs40l50);
		if (ret) {
			return dev_err_probe(cs40l50->dev, ret,
					     "Failed requesting %s IRQ\n",
					     cs40l50_irqs[i].name);
		}
	}

	return 0;
}

static int cs40l50_get_model(struct cs40l50 *cs40l50)
{
	int ret;

	ret = regmap_read(cs40l50->regmap, CS40L50_DEVID, &cs40l50->devid);
	if (ret)
		return ret;

	if (cs40l50->devid != CS40L50_DEVID_A)
		return -EINVAL;

	ret = regmap_read(cs40l50->regmap, CS40L50_REVID, &cs40l50->revid);
	if (ret)
		return ret;

	if (cs40l50->revid < CS40L50_REVID_B0)
		return -EINVAL;

	dev_dbg(cs40l50->dev, "Cirrus Logic CS40L50 rev. %02X\n", cs40l50->revid);

	return 0;
}

static int cs40l50_pm_runtime_setup(struct device *dev)
{
	int ret;

	pm_runtime_set_autosuspend_delay(dev, CS40L50_AUTOSUSPEND_MS);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_get_noresume(dev);
	ret = pm_runtime_set_active(dev);
	if (ret)
		return ret;

	return devm_pm_runtime_enable(dev);
}

int cs40l50_probe(struct cs40l50 *cs40l50)
{
	struct device *dev = cs40l50->dev;
	int ret;

	mutex_init(&cs40l50->lock);

	cs40l50->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(cs40l50->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(cs40l50->reset_gpio),
				     "Failed getting reset GPIO\n");

	ret = devm_regulator_bulk_get_enable(dev, ARRAY_SIZE(cs40l50_supplies),
					     cs40l50_supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed getting supplies\n");

	/* Ensure minimum reset pulse width */
	usleep_range(CS40L50_RESET_PULSE_US, CS40L50_RESET_PULSE_US + 100);

	gpiod_set_value_cansleep(cs40l50->reset_gpio, 0);

	/* Wait for control port to be ready */
	usleep_range(CS40L50_CP_READY_US, CS40L50_CP_READY_US + 100);

	ret = cs40l50_get_model(cs40l50);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get part number\n");

	ret = cs40l50_dsp_init(cs40l50);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize DSP\n");

	ret = cs40l50_pm_runtime_setup(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize runtime PM\n");

	ret = cs40l50_irq_init(cs40l50);
	if (ret)
		return ret;

	ret = request_firmware_nowait(THIS_MODULE, FW_ACTION_UEVENT, CS40L50_FW,
				      dev, GFP_KERNEL, cs40l50, cs40l50_request_firmware);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request %s\n", CS40L50_FW);

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(cs40l50_probe);

int cs40l50_remove(struct cs40l50 *cs40l50)
{
	gpiod_set_value_cansleep(cs40l50->reset_gpio, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(cs40l50_remove);

static int cs40l50_runtime_suspend(struct device *dev)
{
	struct cs40l50 *cs40l50 = dev_get_drvdata(dev);

	return regmap_write(cs40l50->regmap, CS40L50_DSP_QUEUE, CS40L50_ALLOW_HIBER);
}

static int cs40l50_runtime_resume(struct device *dev)
{
	struct cs40l50 *cs40l50 = dev_get_drvdata(dev);

	return cs40l50_dsp_write(dev, cs40l50->regmap, CS40L50_PREVENT_HIBER);
}

EXPORT_GPL_DEV_PM_OPS(cs40l50_pm_ops) = {
	RUNTIME_PM_OPS(cs40l50_runtime_suspend, cs40l50_runtime_resume, NULL)
};

MODULE_DESCRIPTION("CS40L50 Advanced Haptic Driver");
MODULE_AUTHOR("James Ogletree, Cirrus Logic Inc. <james.ogletree@cirrus.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("FW_CS_DSP");
