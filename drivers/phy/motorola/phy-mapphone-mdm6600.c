// SPDX-License-Identifier: GPL-2.0
/*
 * Motorola Mapphone MDM6600 modem GPIO controlled USB PHY driver
 * Copyright (C) 2018 Tony Lindgren <tony@atomide.com>
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/gpio/consumer.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>
#include <linux/pinctrl/consumer.h>

#define PHY_MDM6600_PHY_DELAY_MS	4000	/* PHY enable 2.2s to 3.5s */
#define PHY_MDM6600_ENABLED_DELAY_MS	8000	/* 8s more total for MDM6600 */
#define PHY_MDM6600_WAKE_KICK_MS	600	/* time on after GPIO toggle */
#define MDM6600_MODEM_IDLE_DELAY_MS	1000	/* modem after USB suspend */
#define MDM6600_MODEM_WAKE_DELAY_MS	200	/* modem response after idle */

enum phy_mdm6600_ctrl_lines {
	PHY_MDM6600_ENABLE,			/* USB PHY enable */
	PHY_MDM6600_POWER,			/* Device power */
	PHY_MDM6600_RESET,			/* Device reset */
	PHY_MDM6600_NR_CTRL_LINES,
};

enum phy_mdm6600_bootmode_lines {
	PHY_MDM6600_MODE0,			/* out USB mode0 and OOB wake */
	PHY_MDM6600_MODE1,			/* out USB mode1, in OOB wake */
	PHY_MDM6600_NR_MODE_LINES,
};

enum phy_mdm6600_cmd_lines {
	PHY_MDM6600_CMD0,
	PHY_MDM6600_CMD1,
	PHY_MDM6600_CMD2,
	PHY_MDM6600_NR_CMD_LINES,
};

enum phy_mdm6600_status_lines {
	PHY_MDM6600_STATUS0,
	PHY_MDM6600_STATUS1,
	PHY_MDM6600_STATUS2,
	PHY_MDM6600_NR_STATUS_LINES,
};

/*
 * MDM6600 command codes. These are based on Motorola Mapphone Linux
 * kernel tree.
 */
enum phy_mdm6600_cmd {
	PHY_MDM6600_CMD_BP_PANIC_ACK,
	PHY_MDM6600_CMD_DATA_ONLY_BYPASS,	/* Reroute USB to CPCAP PHY */
	PHY_MDM6600_CMD_FULL_BYPASS,		/* Reroute USB to CPCAP PHY */
	PHY_MDM6600_CMD_NO_BYPASS,		/* Request normal USB mode */
	PHY_MDM6600_CMD_BP_SHUTDOWN_REQ,	/* Request device power off */
	PHY_MDM6600_CMD_BP_UNKNOWN_5,
	PHY_MDM6600_CMD_BP_UNKNOWN_6,
	PHY_MDM6600_CMD_UNDEFINED,
};

/*
 * MDM6600 status codes. These are based on Motorola Mapphone Linux
 * kernel tree.
 */
enum phy_mdm6600_status {
	PHY_MDM6600_STATUS_PANIC,		/* Seems to be really off */
	PHY_MDM6600_STATUS_PANIC_BUSY_WAIT,
	PHY_MDM6600_STATUS_QC_DLOAD,
	PHY_MDM6600_STATUS_RAM_DOWNLOADER,	/* MDM6600 USB flashing mode */
	PHY_MDM6600_STATUS_PHONE_CODE_AWAKE,	/* MDM6600 normal USB mode */
	PHY_MDM6600_STATUS_PHONE_CODE_ASLEEP,
	PHY_MDM6600_STATUS_SHUTDOWN_ACK,
	PHY_MDM6600_STATUS_UNDEFINED,
};

static const char * const
phy_mdm6600_status_name[] = {
	"off", "busy", "qc_dl", "ram_dl", "awake",
	"asleep", "shutdown", "undefined",
};

struct phy_mdm6600 {
	struct device *dev;
	struct phy *generic_phy;
	struct phy_provider *phy_provider;
	struct gpio_desc *ctrl_gpios[PHY_MDM6600_NR_CTRL_LINES];
	struct gpio_descs *mode_gpios;
	struct gpio_descs *status_gpios;
	struct gpio_descs *cmd_gpios;
	struct delayed_work bootup_work;
	struct delayed_work status_work;
	struct delayed_work modem_wake_work;
	struct completion ack;
	bool enabled;				/* mdm6600 phy enabled */
	bool running;				/* mdm6600 boot done */
	bool awake;				/* mdm6600 respnds on n_gsm */
	int status;
};

static int phy_mdm6600_init(struct phy *x)
{
	struct phy_mdm6600 *ddata = phy_get_drvdata(x);
	struct gpio_desc *enable_gpio = ddata->ctrl_gpios[PHY_MDM6600_ENABLE];

	if (!ddata->enabled)
		return -EPROBE_DEFER;

	gpiod_set_value_cansleep(enable_gpio, 0);

	return 0;
}

static int phy_mdm6600_power_on(struct phy *x)
{
	struct phy_mdm6600 *ddata = phy_get_drvdata(x);
	struct gpio_desc *enable_gpio = ddata->ctrl_gpios[PHY_MDM6600_ENABLE];
	int error;

	if (!ddata->enabled)
		return -ENODEV;

	error = pinctrl_pm_select_default_state(ddata->dev);
	if (error)
		dev_warn(ddata->dev, "%s: error with default_state: %i\n",
			 __func__, error);

	gpiod_set_value_cansleep(enable_gpio, 1);

	/* Allow aggressive PM for USB, it's only needed for n_gsm port */
	if (pm_runtime_enabled(&x->dev))
		phy_pm_runtime_put(x);

	return 0;
}

static int phy_mdm6600_power_off(struct phy *x)
{
	struct phy_mdm6600 *ddata = phy_get_drvdata(x);
	struct gpio_desc *enable_gpio = ddata->ctrl_gpios[PHY_MDM6600_ENABLE];
	int error;

	if (!ddata->enabled)
		return -ENODEV;

	/* Paired with phy_pm_runtime_put() in phy_mdm6600_power_on() */
	if (pm_runtime_enabled(&x->dev)) {
		error = phy_pm_runtime_get(x);
		if (error < 0 && error != -EINPROGRESS)
			dev_warn(ddata->dev, "%s: phy_pm_runtime_get: %i\n",
				 __func__, error);
	}

	gpiod_set_value_cansleep(enable_gpio, 0);

	error = pinctrl_pm_select_sleep_state(ddata->dev);
	if (error)
		dev_warn(ddata->dev, "%s: error with sleep_state: %i\n",
			 __func__, error);

	return 0;
}

static const struct phy_ops gpio_usb_ops = {
	.init = phy_mdm6600_init,
	.power_on = phy_mdm6600_power_on,
	.power_off = phy_mdm6600_power_off,
	.owner = THIS_MODULE,
};

/**
 * phy_mdm6600_cmd() - send a command request to mdm6600
 * @ddata: device driver data
 * @val: value of cmd to be set
 *
 * Configures the three command request GPIOs to the specified value.
 */
static void phy_mdm6600_cmd(struct phy_mdm6600 *ddata, int val)
{
	DECLARE_BITMAP(values, PHY_MDM6600_NR_CMD_LINES);

	values[0] = val;

	gpiod_set_array_value_cansleep(PHY_MDM6600_NR_CMD_LINES,
				       ddata->cmd_gpios->desc,
				       ddata->cmd_gpios->info, values);
}

/**
 * phy_mdm6600_status() - read mdm6600 status lines
 * @work: work structure
 */
static void phy_mdm6600_status(struct work_struct *work)
{
	struct phy_mdm6600 *ddata;
	struct device *dev;
	DECLARE_BITMAP(values, PHY_MDM6600_NR_STATUS_LINES);
	int error;

	ddata = container_of(work, struct phy_mdm6600, status_work.work);
	dev = ddata->dev;

	error = gpiod_get_array_value_cansleep(PHY_MDM6600_NR_STATUS_LINES,
					       ddata->status_gpios->desc,
					       ddata->status_gpios->info,
					       values);
	if (error)
		return;

	ddata->status = values[0] & ((1 << PHY_MDM6600_NR_STATUS_LINES) - 1);

	dev_info(dev, "modem status: %i %s\n",
		 ddata->status,
		 phy_mdm6600_status_name[ddata->status]);
	complete(&ddata->ack);
}

static irqreturn_t phy_mdm6600_irq_thread(int irq, void *data)
{
	struct phy_mdm6600 *ddata = data;

	schedule_delayed_work(&ddata->status_work, msecs_to_jiffies(10));

	return IRQ_HANDLED;
}

/**
 * phy_mdm6600_wakeirq_thread - handle mode1 line OOB wake after booting
 * @irq: interrupt
 * @data: interrupt handler data
 *
 * GPIO mode1 is used initially as output to configure the USB boot
 * mode for mdm6600. After booting it is used as input for OOB wake
 * signal from mdm6600 to the SoC. Just use it for debug info only
 * for now.
 */
static irqreturn_t phy_mdm6600_wakeirq_thread(int irq, void *data)
{
	struct phy_mdm6600 *ddata = data;
	struct gpio_desc *mode_gpio1;
	int error, wakeup;

	mode_gpio1 = ddata->mode_gpios->desc[PHY_MDM6600_MODE1];
	wakeup = gpiod_get_value(mode_gpio1);
	if (!wakeup)
		return IRQ_NONE;

	dev_dbg(ddata->dev, "OOB wake on mode_gpio1: %i\n", wakeup);
	error = pm_runtime_get_sync(ddata->dev);
	if (error < 0) {
		pm_runtime_put_noidle(ddata->dev);

		return IRQ_NONE;
	}

	/* Just wake-up and kick the autosuspend timer */
	pm_runtime_mark_last_busy(ddata->dev);
	pm_runtime_put_autosuspend(ddata->dev);

	return IRQ_HANDLED;
}

/**
 * phy_mdm6600_init_irq() - initialize mdm6600 status IRQ lines
 * @ddata: device driver data
 */
static void phy_mdm6600_init_irq(struct phy_mdm6600 *ddata)
{
	struct device *dev = ddata->dev;
	int i, error, irq;

	for (i = PHY_MDM6600_STATUS0;
	     i <= PHY_MDM6600_STATUS2; i++) {
		struct gpio_desc *gpio = ddata->status_gpios->desc[i];

		irq = gpiod_to_irq(gpio);
		if (irq <= 0)
			continue;

		error = devm_request_threaded_irq(dev, irq, NULL,
					phy_mdm6600_irq_thread,
					IRQF_TRIGGER_RISING |
					IRQF_TRIGGER_FALLING |
					IRQF_ONESHOT,
					"mdm6600",
					ddata);
		if (error)
			dev_warn(dev, "no modem status irq%i: %i\n",
				 irq, error);
	}
}

struct phy_mdm6600_map {
	const char *name;
	int direction;
};

static const struct phy_mdm6600_map
phy_mdm6600_ctrl_gpio_map[PHY_MDM6600_NR_CTRL_LINES] = {
	{ "enable", GPIOD_OUT_LOW, },		/* low = phy disabled */
	{ "power", GPIOD_OUT_LOW, },		/* low = off */
	{ "reset", GPIOD_OUT_HIGH, },		/* high = reset */
};

/**
 * phy_mdm6600_init_lines() - initialize mdm6600 GPIO lines
 * @ddata: device driver data
 */
static int phy_mdm6600_init_lines(struct phy_mdm6600 *ddata)
{
	struct device *dev = ddata->dev;
	int i;

	/* MDM6600 control lines */
	for (i = 0; i < ARRAY_SIZE(phy_mdm6600_ctrl_gpio_map); i++) {
		const struct phy_mdm6600_map *map =
			&phy_mdm6600_ctrl_gpio_map[i];
		struct gpio_desc **gpio = &ddata->ctrl_gpios[i];

		*gpio = devm_gpiod_get(dev, map->name, map->direction);
		if (IS_ERR(*gpio)) {
			dev_info(dev, "gpio %s error %li\n",
				 map->name, PTR_ERR(*gpio));
			return PTR_ERR(*gpio);
		}
	}

	/* MDM6600 USB start-up mode output lines */
	ddata->mode_gpios = devm_gpiod_get_array(dev, "motorola,mode",
						 GPIOD_OUT_LOW);
	if (IS_ERR(ddata->mode_gpios))
		return PTR_ERR(ddata->mode_gpios);

	if (ddata->mode_gpios->ndescs != PHY_MDM6600_NR_MODE_LINES)
		return -EINVAL;

	/* MDM6600 status input lines */
	ddata->status_gpios = devm_gpiod_get_array(dev, "motorola,status",
						   GPIOD_IN);
	if (IS_ERR(ddata->status_gpios))
		return PTR_ERR(ddata->status_gpios);

	if (ddata->status_gpios->ndescs != PHY_MDM6600_NR_STATUS_LINES)
		return -EINVAL;

	/* MDM6600 cmd output lines */
	ddata->cmd_gpios = devm_gpiod_get_array(dev, "motorola,cmd",
						GPIOD_OUT_LOW);
	if (IS_ERR(ddata->cmd_gpios))
		return PTR_ERR(ddata->cmd_gpios);

	if (ddata->cmd_gpios->ndescs != PHY_MDM6600_NR_CMD_LINES)
		return -EINVAL;

	return 0;
}

/**
 * phy_mdm6600_device_power_on() - power on mdm6600 device
 * @ddata: device driver data
 *
 * To get the integrated USB phy in MDM6600 takes some hoops. We must ensure
 * the shared USB bootmode GPIOs are configured, then request modem start-up,
 * reset and power-up.. And then we need to recycle the shared USB bootmode
 * GPIOs as they are also used for Out of Band (OOB) wake for the USB and
 * TS 27.010 serial mux.
 */
static int phy_mdm6600_device_power_on(struct phy_mdm6600 *ddata)
{
	struct gpio_desc *mode_gpio0, *mode_gpio1, *reset_gpio, *power_gpio;
	int error = 0, wakeirq;

	mode_gpio0 = ddata->mode_gpios->desc[PHY_MDM6600_MODE0];
	mode_gpio1 = ddata->mode_gpios->desc[PHY_MDM6600_MODE1];
	reset_gpio = ddata->ctrl_gpios[PHY_MDM6600_RESET];
	power_gpio = ddata->ctrl_gpios[PHY_MDM6600_POWER];

	/*
	 * Shared GPIOs must be low for normal USB mode. After booting
	 * they are used for OOB wake signaling. These can be also used
	 * to configure USB flashing mode later on based on a module
	 * parameter.
	 */
	gpiod_set_value_cansleep(mode_gpio0, 0);
	gpiod_set_value_cansleep(mode_gpio1, 0);

	/* Request start-up mode */
	phy_mdm6600_cmd(ddata, PHY_MDM6600_CMD_NO_BYPASS);

	/* Request a reset first */
	gpiod_set_value_cansleep(reset_gpio, 0);
	msleep(100);

	/* Toggle power GPIO to request mdm6600 to start */
	gpiod_set_value_cansleep(power_gpio, 1);
	msleep(100);
	gpiod_set_value_cansleep(power_gpio, 0);

	/*
	 * Looks like the USB PHY needs between 2.2 to 4 seconds.
	 * If we try to use it before that, we will get L3 errors
	 * from omap-usb-host trying to access the PHY. See also
	 * phy_mdm6600_init() for -EPROBE_DEFER.
	 */
	msleep(PHY_MDM6600_PHY_DELAY_MS);
	ddata->enabled = true;

	/* Booting up the rest of MDM6600 will take total about 8 seconds */
	dev_info(ddata->dev, "Waiting for power up request to complete..\n");
	if (wait_for_completion_timeout(&ddata->ack,
			msecs_to_jiffies(PHY_MDM6600_ENABLED_DELAY_MS))) {
		if (ddata->status > PHY_MDM6600_STATUS_PANIC &&
		    ddata->status < PHY_MDM6600_STATUS_SHUTDOWN_ACK)
			dev_info(ddata->dev, "Powered up OK\n");
	} else {
		ddata->enabled = false;
		error = -ETIMEDOUT;
		dev_err(ddata->dev, "Timed out powering up\n");
	}

	/* Reconfigure mode1 GPIO as input for OOB wake */
	gpiod_direction_input(mode_gpio1);

	wakeirq = gpiod_to_irq(mode_gpio1);
	if (wakeirq <= 0)
		return wakeirq;

	error = devm_request_threaded_irq(ddata->dev, wakeirq, NULL,
					  phy_mdm6600_wakeirq_thread,
					  IRQF_TRIGGER_RISING |
					  IRQF_TRIGGER_FALLING |
					  IRQF_ONESHOT,
					  "mdm6600-wake",
					  ddata);
	if (error)
		dev_warn(ddata->dev, "no modem wakeirq irq%i: %i\n",
			 wakeirq, error);

	ddata->running = true;

	return error;
}

/**
 * phy_mdm6600_device_power_off() - power off mdm6600 device
 * @ddata: device driver data
 */
static void phy_mdm6600_device_power_off(struct phy_mdm6600 *ddata)
{
	struct gpio_desc *reset_gpio =
		ddata->ctrl_gpios[PHY_MDM6600_RESET];

	ddata->enabled = false;
	phy_mdm6600_cmd(ddata, PHY_MDM6600_CMD_BP_SHUTDOWN_REQ);
	msleep(100);

	gpiod_set_value_cansleep(reset_gpio, 1);

	dev_info(ddata->dev, "Waiting for power down request to complete.. ");
	if (wait_for_completion_timeout(&ddata->ack,
					msecs_to_jiffies(5000))) {
		if (ddata->status == PHY_MDM6600_STATUS_PANIC)
			dev_info(ddata->dev, "Powered down OK\n");
	} else {
		dev_err(ddata->dev, "Timed out powering down\n");
	}
}

static void phy_mdm6600_deferred_power_on(struct work_struct *work)
{
	struct phy_mdm6600 *ddata;
	int error;

	ddata = container_of(work, struct phy_mdm6600, bootup_work.work);

	error = phy_mdm6600_device_power_on(ddata);
	if (error)
		dev_err(ddata->dev, "Device not functional\n");
}

/*
 * USB suspend puts mdm6600 into low power mode. For any n_gsm using apps,
 * we need to keep the modem awake by kicking it's mode0 GPIO. This will
 * keep the modem awake for about 1.2 seconds. When no n_gsm apps are using
 * the modem, runtime PM auto mode can be enabled so modem can enter low
 * power mode.
 */
static void phy_mdm6600_wake_modem(struct phy_mdm6600 *ddata)
{
	struct gpio_desc *mode_gpio0;

	mode_gpio0 = ddata->mode_gpios->desc[PHY_MDM6600_MODE0];
	gpiod_set_value_cansleep(mode_gpio0, 1);
	usleep_range(5, 15);
	gpiod_set_value_cansleep(mode_gpio0, 0);
	if (ddata->awake)
		usleep_range(5, 15);
	else
		msleep(MDM6600_MODEM_WAKE_DELAY_MS);
}

static void phy_mdm6600_modem_wake(struct work_struct *work)
{
	struct phy_mdm6600 *ddata;

	ddata = container_of(work, struct phy_mdm6600, modem_wake_work.work);
	phy_mdm6600_wake_modem(ddata);

	/*
	 * The modem does not always stay awake 1.2 seconds after toggling
	 * the wake GPIO, and sometimes it idles after about some 600 ms
	 * making writes time out.
	 */
	schedule_delayed_work(&ddata->modem_wake_work,
			      msecs_to_jiffies(PHY_MDM6600_WAKE_KICK_MS));
}

static int __maybe_unused phy_mdm6600_runtime_suspend(struct device *dev)
{
	struct phy_mdm6600 *ddata = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&ddata->modem_wake_work);
	ddata->awake = false;

	return 0;
}

static int __maybe_unused phy_mdm6600_runtime_resume(struct device *dev)
{
	struct phy_mdm6600 *ddata = dev_get_drvdata(dev);

	phy_mdm6600_modem_wake(&ddata->modem_wake_work.work);
	ddata->awake = true;

	return 0;
}

static const struct dev_pm_ops phy_mdm6600_pm_ops = {
	SET_RUNTIME_PM_OPS(phy_mdm6600_runtime_suspend,
			   phy_mdm6600_runtime_resume, NULL)
};

static const struct of_device_id phy_mdm6600_id_table[] = {
	{ .compatible = "motorola,mapphone-mdm6600", },
	{},
};
MODULE_DEVICE_TABLE(of, phy_mdm6600_id_table);

static int phy_mdm6600_probe(struct platform_device *pdev)
{
	struct phy_mdm6600 *ddata;
	int error;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	INIT_DELAYED_WORK(&ddata->bootup_work,
			  phy_mdm6600_deferred_power_on);
	INIT_DELAYED_WORK(&ddata->status_work, phy_mdm6600_status);
	INIT_DELAYED_WORK(&ddata->modem_wake_work, phy_mdm6600_modem_wake);
	init_completion(&ddata->ack);

	ddata->dev = &pdev->dev;
	platform_set_drvdata(pdev, ddata);

	/* Active state selected in phy_mdm6600_power_on() */
	error = pinctrl_pm_select_sleep_state(ddata->dev);
	if (error)
		dev_warn(ddata->dev, "%s: error with sleep_state: %i\n",
			 __func__, error);

	error = phy_mdm6600_init_lines(ddata);
	if (error)
		return error;

	phy_mdm6600_init_irq(ddata);
	schedule_delayed_work(&ddata->bootup_work, 0);

	/*
	 * See phy_mdm6600_device_power_on(). We should be able
	 * to remove this eventually when ohci-platform can deal
	 * with -EPROBE_DEFER.
	 */
	msleep(PHY_MDM6600_PHY_DELAY_MS + 500);

	/*
	 * Enable PM runtime only after PHY has been powered up properly.
	 * It is currently only needed after USB suspends mdm6600 and n_gsm
	 * needs to access the device. We don't want to do this earlier as
	 * gpio mode0 pin doubles as mdm6600 wake-up gpio.
	 */
	pm_runtime_use_autosuspend(ddata->dev);
	pm_runtime_set_autosuspend_delay(ddata->dev,
					 MDM6600_MODEM_IDLE_DELAY_MS);
	pm_runtime_enable(ddata->dev);
	error = pm_runtime_get_sync(ddata->dev);
	if (error < 0) {
		dev_warn(ddata->dev, "failed to wake modem: %i\n", error);
		pm_runtime_put_noidle(ddata->dev);
		goto cleanup;
	}

	ddata->generic_phy = devm_phy_create(ddata->dev, NULL, &gpio_usb_ops);
	if (IS_ERR(ddata->generic_phy)) {
		error = PTR_ERR(ddata->generic_phy);
		goto idle;
	}

	phy_set_drvdata(ddata->generic_phy, ddata);

	ddata->phy_provider =
		devm_of_phy_provider_register(ddata->dev,
					      of_phy_simple_xlate);
	if (IS_ERR(ddata->phy_provider))
		error = PTR_ERR(ddata->phy_provider);

idle:
	pm_runtime_mark_last_busy(ddata->dev);
	pm_runtime_put_autosuspend(ddata->dev);

cleanup:
	if (error < 0) {
		phy_mdm6600_device_power_off(ddata);
		pm_runtime_disable(ddata->dev);
		pm_runtime_dont_use_autosuspend(ddata->dev);
	}

	return error;
}

static int phy_mdm6600_remove(struct platform_device *pdev)
{
	struct phy_mdm6600 *ddata = platform_get_drvdata(pdev);
	struct gpio_desc *reset_gpio = ddata->ctrl_gpios[PHY_MDM6600_RESET];

	pm_runtime_dont_use_autosuspend(ddata->dev);
	pm_runtime_put_sync(ddata->dev);
	pm_runtime_disable(ddata->dev);

	if (!ddata->running)
		wait_for_completion_timeout(&ddata->ack,
			msecs_to_jiffies(PHY_MDM6600_ENABLED_DELAY_MS));

	gpiod_set_value_cansleep(reset_gpio, 1);
	phy_mdm6600_device_power_off(ddata);

	cancel_delayed_work_sync(&ddata->modem_wake_work);
	cancel_delayed_work_sync(&ddata->bootup_work);
	cancel_delayed_work_sync(&ddata->status_work);

	return 0;
}

static struct platform_driver phy_mdm6600_driver = {
	.probe = phy_mdm6600_probe,
	.remove = phy_mdm6600_remove,
	.driver = {
		.name = "phy-mapphone-mdm6600",
		.pm = &phy_mdm6600_pm_ops,
		.of_match_table = of_match_ptr(phy_mdm6600_id_table),
	},
};

module_platform_driver(phy_mdm6600_driver);

MODULE_ALIAS("platform:gpio_usb");
MODULE_AUTHOR("Tony Lindgren <tony@atomide.com>");
MODULE_DESCRIPTION("mdm6600 gpio usb phy driver");
MODULE_LICENSE("GPL v2");
