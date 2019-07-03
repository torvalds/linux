// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 NVIDIA Corporation
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include <drm/drm_dp_helper.h>
#include <drm/drm_panel.h>

#include "dpaux.h"
#include "drm.h"
#include "trace.h"

static DEFINE_MUTEX(dpaux_lock);
static LIST_HEAD(dpaux_list);

struct tegra_dpaux {
	struct drm_dp_aux aux;
	struct device *dev;

	void __iomem *regs;
	int irq;

	struct tegra_output *output;

	struct reset_control *rst;
	struct clk *clk_parent;
	struct clk *clk;

	struct regulator *vdd;

	struct completion complete;
	struct work_struct work;
	struct list_head list;

#ifdef CONFIG_GENERIC_PINCONF
	struct pinctrl_dev *pinctrl;
	struct pinctrl_desc desc;
#endif
};

static inline struct tegra_dpaux *to_dpaux(struct drm_dp_aux *aux)
{
	return container_of(aux, struct tegra_dpaux, aux);
}

static inline struct tegra_dpaux *work_to_dpaux(struct work_struct *work)
{
	return container_of(work, struct tegra_dpaux, work);
}

static inline u32 tegra_dpaux_readl(struct tegra_dpaux *dpaux,
				    unsigned int offset)
{
	u32 value = readl(dpaux->regs + (offset << 2));

	trace_dpaux_readl(dpaux->dev, offset, value);

	return value;
}

static inline void tegra_dpaux_writel(struct tegra_dpaux *dpaux,
				      u32 value, unsigned int offset)
{
	trace_dpaux_writel(dpaux->dev, offset, value);
	writel(value, dpaux->regs + (offset << 2));
}

static void tegra_dpaux_write_fifo(struct tegra_dpaux *dpaux, const u8 *buffer,
				   size_t size)
{
	size_t i, j;

	for (i = 0; i < DIV_ROUND_UP(size, 4); i++) {
		size_t num = min_t(size_t, size - i * 4, 4);
		u32 value = 0;

		for (j = 0; j < num; j++)
			value |= buffer[i * 4 + j] << (j * 8);

		tegra_dpaux_writel(dpaux, value, DPAUX_DP_AUXDATA_WRITE(i));
	}
}

static void tegra_dpaux_read_fifo(struct tegra_dpaux *dpaux, u8 *buffer,
				  size_t size)
{
	size_t i, j;

	for (i = 0; i < DIV_ROUND_UP(size, 4); i++) {
		size_t num = min_t(size_t, size - i * 4, 4);
		u32 value;

		value = tegra_dpaux_readl(dpaux, DPAUX_DP_AUXDATA_READ(i));

		for (j = 0; j < num; j++)
			buffer[i * 4 + j] = value >> (j * 8);
	}
}

static ssize_t tegra_dpaux_transfer(struct drm_dp_aux *aux,
				    struct drm_dp_aux_msg *msg)
{
	unsigned long timeout = msecs_to_jiffies(250);
	struct tegra_dpaux *dpaux = to_dpaux(aux);
	unsigned long status;
	ssize_t ret = 0;
	u32 value;

	/* Tegra has 4x4 byte DP AUX transmit and receive FIFOs. */
	if (msg->size > 16)
		return -EINVAL;

	/*
	 * Allow zero-sized messages only for I2C, in which case they specify
	 * address-only transactions.
	 */
	if (msg->size < 1) {
		switch (msg->request & ~DP_AUX_I2C_MOT) {
		case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		case DP_AUX_I2C_WRITE:
		case DP_AUX_I2C_READ:
			value = DPAUX_DP_AUXCTL_CMD_ADDRESS_ONLY;
			break;

		default:
			return -EINVAL;
		}
	} else {
		/* For non-zero-sized messages, set the CMDLEN field. */
		value = DPAUX_DP_AUXCTL_CMDLEN(msg->size - 1);
	}

	switch (msg->request & ~DP_AUX_I2C_MOT) {
	case DP_AUX_I2C_WRITE:
		if (msg->request & DP_AUX_I2C_MOT)
			value |= DPAUX_DP_AUXCTL_CMD_MOT_WR;
		else
			value |= DPAUX_DP_AUXCTL_CMD_I2C_WR;

		break;

	case DP_AUX_I2C_READ:
		if (msg->request & DP_AUX_I2C_MOT)
			value |= DPAUX_DP_AUXCTL_CMD_MOT_RD;
		else
			value |= DPAUX_DP_AUXCTL_CMD_I2C_RD;

		break;

	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
		if (msg->request & DP_AUX_I2C_MOT)
			value |= DPAUX_DP_AUXCTL_CMD_MOT_RQ;
		else
			value |= DPAUX_DP_AUXCTL_CMD_I2C_RQ;

		break;

	case DP_AUX_NATIVE_WRITE:
		value |= DPAUX_DP_AUXCTL_CMD_AUX_WR;
		break;

	case DP_AUX_NATIVE_READ:
		value |= DPAUX_DP_AUXCTL_CMD_AUX_RD;
		break;

	default:
		return -EINVAL;
	}

	tegra_dpaux_writel(dpaux, msg->address, DPAUX_DP_AUXADDR);
	tegra_dpaux_writel(dpaux, value, DPAUX_DP_AUXCTL);

	if ((msg->request & DP_AUX_I2C_READ) == 0) {
		tegra_dpaux_write_fifo(dpaux, msg->buffer, msg->size);
		ret = msg->size;
	}

	/* start transaction */
	value = tegra_dpaux_readl(dpaux, DPAUX_DP_AUXCTL);
	value |= DPAUX_DP_AUXCTL_TRANSACTREQ;
	tegra_dpaux_writel(dpaux, value, DPAUX_DP_AUXCTL);

	status = wait_for_completion_timeout(&dpaux->complete, timeout);
	if (!status)
		return -ETIMEDOUT;

	/* read status and clear errors */
	value = tegra_dpaux_readl(dpaux, DPAUX_DP_AUXSTAT);
	tegra_dpaux_writel(dpaux, 0xf00, DPAUX_DP_AUXSTAT);

	if (value & DPAUX_DP_AUXSTAT_TIMEOUT_ERROR)
		return -ETIMEDOUT;

	if ((value & DPAUX_DP_AUXSTAT_RX_ERROR) ||
	    (value & DPAUX_DP_AUXSTAT_SINKSTAT_ERROR) ||
	    (value & DPAUX_DP_AUXSTAT_NO_STOP_ERROR))
		return -EIO;

	switch ((value & DPAUX_DP_AUXSTAT_REPLY_TYPE_MASK) >> 16) {
	case 0x00:
		msg->reply = DP_AUX_NATIVE_REPLY_ACK;
		break;

	case 0x01:
		msg->reply = DP_AUX_NATIVE_REPLY_NACK;
		break;

	case 0x02:
		msg->reply = DP_AUX_NATIVE_REPLY_DEFER;
		break;

	case 0x04:
		msg->reply = DP_AUX_I2C_REPLY_NACK;
		break;

	case 0x08:
		msg->reply = DP_AUX_I2C_REPLY_DEFER;
		break;
	}

	if ((msg->size > 0) && (msg->reply == DP_AUX_NATIVE_REPLY_ACK)) {
		if (msg->request & DP_AUX_I2C_READ) {
			size_t count = value & DPAUX_DP_AUXSTAT_REPLY_MASK;

			if (WARN_ON(count != msg->size))
				count = min_t(size_t, count, msg->size);

			tegra_dpaux_read_fifo(dpaux, msg->buffer, count);
			ret = count;
		}
	}

	return ret;
}

static void tegra_dpaux_hotplug(struct work_struct *work)
{
	struct tegra_dpaux *dpaux = work_to_dpaux(work);

	if (dpaux->output)
		drm_helper_hpd_irq_event(dpaux->output->connector.dev);
}

static irqreturn_t tegra_dpaux_irq(int irq, void *data)
{
	struct tegra_dpaux *dpaux = data;
	irqreturn_t ret = IRQ_HANDLED;
	u32 value;

	/* clear interrupts */
	value = tegra_dpaux_readl(dpaux, DPAUX_INTR_AUX);
	tegra_dpaux_writel(dpaux, value, DPAUX_INTR_AUX);

	if (value & (DPAUX_INTR_PLUG_EVENT | DPAUX_INTR_UNPLUG_EVENT))
		schedule_work(&dpaux->work);

	if (value & DPAUX_INTR_IRQ_EVENT) {
		/* TODO: handle this */
	}

	if (value & DPAUX_INTR_AUX_DONE)
		complete(&dpaux->complete);

	return ret;
}

enum tegra_dpaux_functions {
	DPAUX_PADCTL_FUNC_AUX,
	DPAUX_PADCTL_FUNC_I2C,
	DPAUX_PADCTL_FUNC_OFF,
};

static void tegra_dpaux_pad_power_down(struct tegra_dpaux *dpaux)
{
	u32 value = tegra_dpaux_readl(dpaux, DPAUX_HYBRID_SPARE);

	value |= DPAUX_HYBRID_SPARE_PAD_POWER_DOWN;

	tegra_dpaux_writel(dpaux, value, DPAUX_HYBRID_SPARE);
}

static void tegra_dpaux_pad_power_up(struct tegra_dpaux *dpaux)
{
	u32 value = tegra_dpaux_readl(dpaux, DPAUX_HYBRID_SPARE);

	value &= ~DPAUX_HYBRID_SPARE_PAD_POWER_DOWN;

	tegra_dpaux_writel(dpaux, value, DPAUX_HYBRID_SPARE);
}

static int tegra_dpaux_pad_config(struct tegra_dpaux *dpaux, unsigned function)
{
	u32 value;

	switch (function) {
	case DPAUX_PADCTL_FUNC_AUX:
		value = DPAUX_HYBRID_PADCTL_AUX_CMH(2) |
			DPAUX_HYBRID_PADCTL_AUX_DRVZ(4) |
			DPAUX_HYBRID_PADCTL_AUX_DRVI(0x18) |
			DPAUX_HYBRID_PADCTL_AUX_INPUT_RCV |
			DPAUX_HYBRID_PADCTL_MODE_AUX;
		break;

	case DPAUX_PADCTL_FUNC_I2C:
		value = DPAUX_HYBRID_PADCTL_I2C_SDA_INPUT_RCV |
			DPAUX_HYBRID_PADCTL_I2C_SCL_INPUT_RCV |
			DPAUX_HYBRID_PADCTL_AUX_CMH(2) |
			DPAUX_HYBRID_PADCTL_AUX_DRVZ(4) |
			DPAUX_HYBRID_PADCTL_AUX_DRVI(0x18) |
			DPAUX_HYBRID_PADCTL_MODE_I2C;
		break;

	case DPAUX_PADCTL_FUNC_OFF:
		tegra_dpaux_pad_power_down(dpaux);
		return 0;

	default:
		return -ENOTSUPP;
	}

	tegra_dpaux_writel(dpaux, value, DPAUX_HYBRID_PADCTL);
	tegra_dpaux_pad_power_up(dpaux);

	return 0;
}

#ifdef CONFIG_GENERIC_PINCONF
static const struct pinctrl_pin_desc tegra_dpaux_pins[] = {
	PINCTRL_PIN(0, "DP_AUX_CHx_P"),
	PINCTRL_PIN(1, "DP_AUX_CHx_N"),
};

static const unsigned tegra_dpaux_pin_numbers[] = { 0, 1 };

static const char * const tegra_dpaux_groups[] = {
	"dpaux-io",
};

static const char * const tegra_dpaux_functions[] = {
	"aux",
	"i2c",
	"off",
};

static int tegra_dpaux_get_groups_count(struct pinctrl_dev *pinctrl)
{
	return ARRAY_SIZE(tegra_dpaux_groups);
}

static const char *tegra_dpaux_get_group_name(struct pinctrl_dev *pinctrl,
					      unsigned int group)
{
	return tegra_dpaux_groups[group];
}

static int tegra_dpaux_get_group_pins(struct pinctrl_dev *pinctrl,
				      unsigned group, const unsigned **pins,
				      unsigned *num_pins)
{
	*pins = tegra_dpaux_pin_numbers;
	*num_pins = ARRAY_SIZE(tegra_dpaux_pin_numbers);

	return 0;
}

static const struct pinctrl_ops tegra_dpaux_pinctrl_ops = {
	.get_groups_count = tegra_dpaux_get_groups_count,
	.get_group_name = tegra_dpaux_get_group_name,
	.get_group_pins = tegra_dpaux_get_group_pins,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_group,
	.dt_free_map = pinconf_generic_dt_free_map,
};

static int tegra_dpaux_get_functions_count(struct pinctrl_dev *pinctrl)
{
	return ARRAY_SIZE(tegra_dpaux_functions);
}

static const char *tegra_dpaux_get_function_name(struct pinctrl_dev *pinctrl,
						 unsigned int function)
{
	return tegra_dpaux_functions[function];
}

static int tegra_dpaux_get_function_groups(struct pinctrl_dev *pinctrl,
					   unsigned int function,
					   const char * const **groups,
					   unsigned * const num_groups)
{
	*num_groups = ARRAY_SIZE(tegra_dpaux_groups);
	*groups = tegra_dpaux_groups;

	return 0;
}

static int tegra_dpaux_set_mux(struct pinctrl_dev *pinctrl,
			       unsigned int function, unsigned int group)
{
	struct tegra_dpaux *dpaux = pinctrl_dev_get_drvdata(pinctrl);

	return tegra_dpaux_pad_config(dpaux, function);
}

static const struct pinmux_ops tegra_dpaux_pinmux_ops = {
	.get_functions_count = tegra_dpaux_get_functions_count,
	.get_function_name = tegra_dpaux_get_function_name,
	.get_function_groups = tegra_dpaux_get_function_groups,
	.set_mux = tegra_dpaux_set_mux,
};
#endif

static int tegra_dpaux_probe(struct platform_device *pdev)
{
	struct tegra_dpaux *dpaux;
	struct resource *regs;
	u32 value;
	int err;

	dpaux = devm_kzalloc(&pdev->dev, sizeof(*dpaux), GFP_KERNEL);
	if (!dpaux)
		return -ENOMEM;

	INIT_WORK(&dpaux->work, tegra_dpaux_hotplug);
	init_completion(&dpaux->complete);
	INIT_LIST_HEAD(&dpaux->list);
	dpaux->dev = &pdev->dev;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	dpaux->regs = devm_ioremap_resource(&pdev->dev, regs);
	if (IS_ERR(dpaux->regs))
		return PTR_ERR(dpaux->regs);

	dpaux->irq = platform_get_irq(pdev, 0);
	if (dpaux->irq < 0) {
		dev_err(&pdev->dev, "failed to get IRQ\n");
		return -ENXIO;
	}

	if (!pdev->dev.pm_domain) {
		dpaux->rst = devm_reset_control_get(&pdev->dev, "dpaux");
		if (IS_ERR(dpaux->rst)) {
			dev_err(&pdev->dev,
				"failed to get reset control: %ld\n",
				PTR_ERR(dpaux->rst));
			return PTR_ERR(dpaux->rst);
		}
	}

	dpaux->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(dpaux->clk)) {
		dev_err(&pdev->dev, "failed to get module clock: %ld\n",
			PTR_ERR(dpaux->clk));
		return PTR_ERR(dpaux->clk);
	}

	dpaux->clk_parent = devm_clk_get(&pdev->dev, "parent");
	if (IS_ERR(dpaux->clk_parent)) {
		dev_err(&pdev->dev, "failed to get parent clock: %ld\n",
			PTR_ERR(dpaux->clk_parent));
		return PTR_ERR(dpaux->clk_parent);
	}

	err = clk_set_rate(dpaux->clk_parent, 270000000);
	if (err < 0) {
		dev_err(&pdev->dev, "failed to set clock to 270 MHz: %d\n",
			err);
		return err;
	}

	dpaux->vdd = devm_regulator_get(&pdev->dev, "vdd");
	if (IS_ERR(dpaux->vdd)) {
		dev_err(&pdev->dev, "failed to get VDD supply: %ld\n",
			PTR_ERR(dpaux->vdd));
		return PTR_ERR(dpaux->vdd);
	}

	platform_set_drvdata(pdev, dpaux);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	err = devm_request_irq(dpaux->dev, dpaux->irq, tegra_dpaux_irq, 0,
			       dev_name(dpaux->dev), dpaux);
	if (err < 0) {
		dev_err(dpaux->dev, "failed to request IRQ#%u: %d\n",
			dpaux->irq, err);
		return err;
	}

	disable_irq(dpaux->irq);

	dpaux->aux.transfer = tegra_dpaux_transfer;
	dpaux->aux.dev = &pdev->dev;

	err = drm_dp_aux_register(&dpaux->aux);
	if (err < 0)
		return err;

	/*
	 * Assume that by default the DPAUX/I2C pads will be used for HDMI,
	 * so power them up and configure them in I2C mode.
	 *
	 * The DPAUX code paths reconfigure the pads in AUX mode, but there
	 * is no possibility to perform the I2C mode configuration in the
	 * HDMI path.
	 */
	err = tegra_dpaux_pad_config(dpaux, DPAUX_PADCTL_FUNC_I2C);
	if (err < 0)
		return err;

#ifdef CONFIG_GENERIC_PINCONF
	dpaux->desc.name = dev_name(&pdev->dev);
	dpaux->desc.pins = tegra_dpaux_pins;
	dpaux->desc.npins = ARRAY_SIZE(tegra_dpaux_pins);
	dpaux->desc.pctlops = &tegra_dpaux_pinctrl_ops;
	dpaux->desc.pmxops = &tegra_dpaux_pinmux_ops;
	dpaux->desc.owner = THIS_MODULE;

	dpaux->pinctrl = devm_pinctrl_register(&pdev->dev, &dpaux->desc, dpaux);
	if (IS_ERR(dpaux->pinctrl)) {
		dev_err(&pdev->dev, "failed to register pincontrol\n");
		return PTR_ERR(dpaux->pinctrl);
	}
#endif
	/* enable and clear all interrupts */
	value = DPAUX_INTR_AUX_DONE | DPAUX_INTR_IRQ_EVENT |
		DPAUX_INTR_UNPLUG_EVENT | DPAUX_INTR_PLUG_EVENT;
	tegra_dpaux_writel(dpaux, value, DPAUX_INTR_EN_AUX);
	tegra_dpaux_writel(dpaux, value, DPAUX_INTR_AUX);

	mutex_lock(&dpaux_lock);
	list_add_tail(&dpaux->list, &dpaux_list);
	mutex_unlock(&dpaux_lock);

	return 0;
}

static int tegra_dpaux_remove(struct platform_device *pdev)
{
	struct tegra_dpaux *dpaux = platform_get_drvdata(pdev);

	cancel_work_sync(&dpaux->work);

	/* make sure pads are powered down when not in use */
	tegra_dpaux_pad_power_down(dpaux);

	pm_runtime_put(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	drm_dp_aux_unregister(&dpaux->aux);

	mutex_lock(&dpaux_lock);
	list_del(&dpaux->list);
	mutex_unlock(&dpaux_lock);

	return 0;
}

#ifdef CONFIG_PM
static int tegra_dpaux_suspend(struct device *dev)
{
	struct tegra_dpaux *dpaux = dev_get_drvdata(dev);
	int err = 0;

	if (dpaux->rst) {
		err = reset_control_assert(dpaux->rst);
		if (err < 0) {
			dev_err(dev, "failed to assert reset: %d\n", err);
			return err;
		}
	}

	usleep_range(1000, 2000);

	clk_disable_unprepare(dpaux->clk_parent);
	clk_disable_unprepare(dpaux->clk);

	return err;
}

static int tegra_dpaux_resume(struct device *dev)
{
	struct tegra_dpaux *dpaux = dev_get_drvdata(dev);
	int err;

	err = clk_prepare_enable(dpaux->clk);
	if (err < 0) {
		dev_err(dev, "failed to enable clock: %d\n", err);
		return err;
	}

	err = clk_prepare_enable(dpaux->clk_parent);
	if (err < 0) {
		dev_err(dev, "failed to enable parent clock: %d\n", err);
		goto disable_clk;
	}

	usleep_range(1000, 2000);

	if (dpaux->rst) {
		err = reset_control_deassert(dpaux->rst);
		if (err < 0) {
			dev_err(dev, "failed to deassert reset: %d\n", err);
			goto disable_parent;
		}

		usleep_range(1000, 2000);
	}

	return 0;

disable_parent:
	clk_disable_unprepare(dpaux->clk_parent);
disable_clk:
	clk_disable_unprepare(dpaux->clk);
	return err;
}
#endif

static const struct dev_pm_ops tegra_dpaux_pm_ops = {
	SET_RUNTIME_PM_OPS(tegra_dpaux_suspend, tegra_dpaux_resume, NULL)
};

static const struct of_device_id tegra_dpaux_of_match[] = {
	{ .compatible = "nvidia,tegra194-dpaux", },
	{ .compatible = "nvidia,tegra186-dpaux", },
	{ .compatible = "nvidia,tegra210-dpaux", },
	{ .compatible = "nvidia,tegra124-dpaux", },
	{ },
};
MODULE_DEVICE_TABLE(of, tegra_dpaux_of_match);

struct platform_driver tegra_dpaux_driver = {
	.driver = {
		.name = "tegra-dpaux",
		.of_match_table = tegra_dpaux_of_match,
		.pm = &tegra_dpaux_pm_ops,
	},
	.probe = tegra_dpaux_probe,
	.remove = tegra_dpaux_remove,
};

struct drm_dp_aux *drm_dp_aux_find_by_of_node(struct device_node *np)
{
	struct tegra_dpaux *dpaux;

	mutex_lock(&dpaux_lock);

	list_for_each_entry(dpaux, &dpaux_list, list)
		if (np == dpaux->dev->of_node) {
			mutex_unlock(&dpaux_lock);
			return &dpaux->aux;
		}

	mutex_unlock(&dpaux_lock);

	return NULL;
}

int drm_dp_aux_attach(struct drm_dp_aux *aux, struct tegra_output *output)
{
	struct tegra_dpaux *dpaux = to_dpaux(aux);
	unsigned long timeout;
	int err;

	output->connector.polled = DRM_CONNECTOR_POLL_HPD;
	dpaux->output = output;

	err = regulator_enable(dpaux->vdd);
	if (err < 0)
		return err;

	timeout = jiffies + msecs_to_jiffies(250);

	while (time_before(jiffies, timeout)) {
		enum drm_connector_status status;

		status = drm_dp_aux_detect(aux);
		if (status == connector_status_connected) {
			enable_irq(dpaux->irq);
			return 0;
		}

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

int drm_dp_aux_detach(struct drm_dp_aux *aux)
{
	struct tegra_dpaux *dpaux = to_dpaux(aux);
	unsigned long timeout;
	int err;

	disable_irq(dpaux->irq);

	err = regulator_disable(dpaux->vdd);
	if (err < 0)
		return err;

	timeout = jiffies + msecs_to_jiffies(250);

	while (time_before(jiffies, timeout)) {
		enum drm_connector_status status;

		status = drm_dp_aux_detect(aux);
		if (status == connector_status_disconnected) {
			dpaux->output = NULL;
			return 0;
		}

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

enum drm_connector_status drm_dp_aux_detect(struct drm_dp_aux *aux)
{
	struct tegra_dpaux *dpaux = to_dpaux(aux);
	u32 value;

	value = tegra_dpaux_readl(dpaux, DPAUX_DP_AUXSTAT);

	if (value & DPAUX_DP_AUXSTAT_HPD_STATUS)
		return connector_status_connected;

	return connector_status_disconnected;
}

int drm_dp_aux_enable(struct drm_dp_aux *aux)
{
	struct tegra_dpaux *dpaux = to_dpaux(aux);

	return tegra_dpaux_pad_config(dpaux, DPAUX_PADCTL_FUNC_AUX);
}

int drm_dp_aux_disable(struct drm_dp_aux *aux)
{
	struct tegra_dpaux *dpaux = to_dpaux(aux);

	tegra_dpaux_pad_power_down(dpaux);

	return 0;
}

int drm_dp_aux_prepare(struct drm_dp_aux *aux, u8 encoding)
{
	int err;

	err = drm_dp_dpcd_writeb(aux, DP_MAIN_LINK_CHANNEL_CODING_SET,
				 encoding);
	if (err < 0)
		return err;

	return 0;
}

int drm_dp_aux_train(struct drm_dp_aux *aux, struct drm_dp_link *link,
		     u8 pattern)
{
	u8 tp = pattern & DP_TRAINING_PATTERN_MASK;
	u8 status[DP_LINK_STATUS_SIZE], values[4];
	unsigned int i;
	int err;

	err = drm_dp_dpcd_writeb(aux, DP_TRAINING_PATTERN_SET, pattern);
	if (err < 0)
		return err;

	if (tp == DP_TRAINING_PATTERN_DISABLE)
		return 0;

	for (i = 0; i < link->num_lanes; i++)
		values[i] = DP_TRAIN_MAX_PRE_EMPHASIS_REACHED |
			    DP_TRAIN_PRE_EMPH_LEVEL_0 |
			    DP_TRAIN_MAX_SWING_REACHED |
			    DP_TRAIN_VOLTAGE_SWING_LEVEL_0;

	err = drm_dp_dpcd_write(aux, DP_TRAINING_LANE0_SET, values,
				link->num_lanes);
	if (err < 0)
		return err;

	usleep_range(500, 1000);

	err = drm_dp_dpcd_read_link_status(aux, status);
	if (err < 0)
		return err;

	switch (tp) {
	case DP_TRAINING_PATTERN_1:
		if (!drm_dp_clock_recovery_ok(status, link->num_lanes))
			return -EAGAIN;

		break;

	case DP_TRAINING_PATTERN_2:
		if (!drm_dp_channel_eq_ok(status, link->num_lanes))
			return -EAGAIN;

		break;

	default:
		dev_err(aux->dev, "unsupported training pattern %u\n", tp);
		return -EINVAL;
	}

	err = drm_dp_dpcd_writeb(aux, DP_EDP_CONFIGURATION_SET, 0);
	if (err < 0)
		return err;

	return 0;
}
