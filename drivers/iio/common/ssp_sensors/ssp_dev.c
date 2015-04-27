/*
 *  Copyright (C) 2014, Samsung Electronics Co. Ltd. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/iio/iio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include "ssp.h"

#define SSP_WDT_TIME			10000
#define SSP_LIMIT_RESET_CNT		20
#define SSP_LIMIT_TIMEOUT_CNT		3

/* It is possible that it is max clk rate for version 1.0 of bootcode */
#define SSP_BOOT_SPI_HZ	400000

/*
 * These fields can look enigmatic but this structure is used mainly to flat
 * some values and depends on command type.
 */
struct ssp_instruction {
	__le32 a;
	__le32 b;
	u8 c;
} __attribute__((__packed__));

static const u8 ssp_magnitude_table[] = {110, 85, 171, 71, 203, 195, 0, 67,
	208, 56, 175, 244, 206, 213, 0, 92, 250, 0, 55, 48, 189, 252, 171,
	243, 13, 45, 250};

static const struct ssp_sensorhub_info ssp_rinato_info = {
	.fw_name = "ssp_B2.fw",
	.fw_crashed_name = "ssp_crashed.fw",
	.fw_rev = 14052300,
	.mag_table = ssp_magnitude_table,
	.mag_length = ARRAY_SIZE(ssp_magnitude_table),
};

static const struct ssp_sensorhub_info ssp_thermostat_info = {
	.fw_name = "thermostat_B2.fw",
	.fw_crashed_name = "ssp_crashed.fw",
	.fw_rev = 14080600,
	.mag_table = ssp_magnitude_table,
	.mag_length = ARRAY_SIZE(ssp_magnitude_table),
};

static const struct mfd_cell sensorhub_sensor_devs[] = {
	{
		.name = "ssp-accelerometer",
	},
	{
		.name = "ssp-gyroscope",
	},
};

static void ssp_toggle_mcu_reset_gpio(struct ssp_data *data)
{
	gpio_set_value(data->mcu_reset_gpio, 0);
	usleep_range(1000, 1200);
	gpio_set_value(data->mcu_reset_gpio, 1);
	msleep(50);
}

static void ssp_sync_available_sensors(struct ssp_data *data)
{
	int i, ret;

	for (i = 0; i < SSP_SENSOR_MAX; ++i) {
		if (data->available_sensors & BIT(i)) {
			ret = ssp_enable_sensor(data, i, data->delay_buf[i]);
			if (ret < 0) {
				dev_err(&data->spi->dev,
					"Sync sensor nr: %d fail\n", i);
				continue;
			}
		}
	}

	ret = ssp_command(data, SSP_MSG2SSP_AP_MCU_SET_DUMPMODE,
			  data->mcu_dump_mode);
	if (ret < 0)
		dev_err(&data->spi->dev,
			"SSP_MSG2SSP_AP_MCU_SET_DUMPMODE failed\n");
}

static void ssp_enable_mcu(struct ssp_data *data, bool enable)
{
	dev_info(&data->spi->dev, "current shutdown = %d, old = %d\n", enable,
		 data->shut_down);

	if (enable && data->shut_down) {
		data->shut_down = false;
		enable_irq(data->spi->irq);
		enable_irq_wake(data->spi->irq);
	} else if (!enable && !data->shut_down) {
		data->shut_down = true;
		disable_irq(data->spi->irq);
		disable_irq_wake(data->spi->irq);
	} else {
		dev_warn(&data->spi->dev, "current shutdown = %d, old = %d\n",
			 enable, data->shut_down);
	}
}

/*
 * This function is the first one which communicates with the mcu so it is
 * possible that the first attempt will fail
 */
static int ssp_check_fwbl(struct ssp_data *data)
{
	int retries = 0;

	while (retries++ < 5) {
		data->cur_firm_rev = ssp_get_firmware_rev(data);
		if (data->cur_firm_rev == SSP_INVALID_REVISION ||
		    data->cur_firm_rev == SSP_INVALID_REVISION2) {
			dev_warn(&data->spi->dev,
				 "Invalid revision, trying %d time\n", retries);
		} else {
			break;
		}
	}

	if (data->cur_firm_rev == SSP_INVALID_REVISION ||
	    data->cur_firm_rev == SSP_INVALID_REVISION2) {
		dev_err(&data->spi->dev, "SSP_INVALID_REVISION\n");
		return SSP_FW_DL_STATE_NEED_TO_SCHEDULE;
	}

	dev_info(&data->spi->dev,
		 "MCU Firm Rev : Old = %8u, New = %8u\n",
		 data->cur_firm_rev,
		 data->sensorhub_info->fw_rev);

	if (data->cur_firm_rev != data->sensorhub_info->fw_rev)
		return SSP_FW_DL_STATE_NEED_TO_SCHEDULE;

	return SSP_FW_DL_STATE_NONE;
}

static void ssp_reset_mcu(struct ssp_data *data)
{
	ssp_enable_mcu(data, false);
	ssp_clean_pending_list(data);
	ssp_toggle_mcu_reset_gpio(data);
	ssp_enable_mcu(data, true);
}

static void ssp_wdt_work_func(struct work_struct *work)
{
	struct ssp_data *data = container_of(work, struct ssp_data, work_wdt);

	dev_err(&data->spi->dev, "%s - Sensor state: 0x%x, RC: %u, CC: %u\n",
		__func__, data->available_sensors, data->reset_cnt,
		data->com_fail_cnt);

	ssp_reset_mcu(data);
	data->com_fail_cnt = 0;
	data->timeout_cnt = 0;
}

static void ssp_wdt_timer_func(unsigned long ptr)
{
	struct ssp_data *data = (struct ssp_data *)ptr;

	switch (data->fw_dl_state) {
	case SSP_FW_DL_STATE_FAIL:
	case SSP_FW_DL_STATE_DOWNLOADING:
	case SSP_FW_DL_STATE_SYNC:
		goto _mod;
	}

	if (data->timeout_cnt > SSP_LIMIT_TIMEOUT_CNT ||
	    data->com_fail_cnt > SSP_LIMIT_RESET_CNT)
		queue_work(system_power_efficient_wq, &data->work_wdt);
_mod:
	mod_timer(&data->wdt_timer, jiffies + msecs_to_jiffies(SSP_WDT_TIME));
}

static void ssp_enable_wdt_timer(struct ssp_data *data)
{
	mod_timer(&data->wdt_timer, jiffies + msecs_to_jiffies(SSP_WDT_TIME));
}

static void ssp_disable_wdt_timer(struct ssp_data *data)
{
	del_timer_sync(&data->wdt_timer);
	cancel_work_sync(&data->work_wdt);
}

/**
 * ssp_get_sensor_delay() - gets sensor data acquisition period
 * @data:	sensorhub structure
 * @type:	SSP sensor type
 *
 * Returns acquisition period in ms
 */
u32 ssp_get_sensor_delay(struct ssp_data *data, enum ssp_sensor_type type)
{
	return data->delay_buf[type];
}
EXPORT_SYMBOL(ssp_get_sensor_delay);

/**
 * ssp_enable_sensor() - enables data acquisition for sensor
 * @data:	sensorhub structure
 * @type:	SSP sensor type
 * @delay:	delay in ms
 *
 * Returns 0 or negative value in case of error
 */
int ssp_enable_sensor(struct ssp_data *data, enum ssp_sensor_type type,
		      u32 delay)
{
	int ret;
	struct ssp_instruction to_send;

	to_send.a = cpu_to_le32(delay);
	to_send.b = cpu_to_le32(data->batch_latency_buf[type]);
	to_send.c = data->batch_opt_buf[type];

	switch (data->check_status[type]) {
	case SSP_INITIALIZATION_STATE:
		/* do calibration step, now just enable */
	case SSP_ADD_SENSOR_STATE:
		ret = ssp_send_instruction(data,
					   SSP_MSG2SSP_INST_BYPASS_SENSOR_ADD,
					   type,
					   (u8 *)&to_send, sizeof(to_send));
		if (ret < 0) {
			dev_err(&data->spi->dev, "Enabling sensor failed\n");
			data->check_status[type] = SSP_NO_SENSOR_STATE;
			goto derror;
		}

		data->sensor_enable |= BIT(type);
		data->check_status[type] = SSP_RUNNING_SENSOR_STATE;
		break;
	case SSP_RUNNING_SENSOR_STATE:
		ret = ssp_send_instruction(data,
					   SSP_MSG2SSP_INST_CHANGE_DELAY, type,
					   (u8 *)&to_send, sizeof(to_send));
		if (ret < 0) {
			dev_err(&data->spi->dev,
				"Changing sensor delay failed\n");
			goto derror;
		}
		break;
	default:
		data->check_status[type] = SSP_ADD_SENSOR_STATE;
		break;
	}

	data->delay_buf[type] = delay;

	if (atomic_inc_return(&data->enable_refcount) == 1)
		ssp_enable_wdt_timer(data);

	return 0;

derror:
	return ret;
}
EXPORT_SYMBOL(ssp_enable_sensor);

/**
 * ssp_change_delay() - changes data acquisition for sensor
 * @data:	sensorhub structure
 * @type:	SSP sensor type
 * @delay:	delay in ms
 *
 * Returns 0 or negative value in case of error
 */
int ssp_change_delay(struct ssp_data *data, enum ssp_sensor_type type,
		     u32 delay)
{
	int ret;
	struct ssp_instruction to_send;

	to_send.a = cpu_to_le32(delay);
	to_send.b = cpu_to_le32(data->batch_latency_buf[type]);
	to_send.c = data->batch_opt_buf[type];

	ret = ssp_send_instruction(data, SSP_MSG2SSP_INST_CHANGE_DELAY, type,
				   (u8 *)&to_send, sizeof(to_send));
	if (ret < 0) {
		dev_err(&data->spi->dev, "Changing sensor delay failed\n");
		return ret;
	}

	data->delay_buf[type] = delay;

	return 0;
}
EXPORT_SYMBOL(ssp_change_delay);

/**
 * ssp_disable_sensor() - disables sensor
 *
 * @data:	sensorhub structure
 * @type:	SSP sensor type
 *
 * Returns 0 or negative value in case of error
 */
int ssp_disable_sensor(struct ssp_data *data, enum ssp_sensor_type type)
{
	int ret;
	__le32 command;

	if (data->sensor_enable & BIT(type)) {
		command = cpu_to_le32(data->delay_buf[type]);

		ret = ssp_send_instruction(data,
					   SSP_MSG2SSP_INST_BYPASS_SENSOR_RM,
					   type, (u8 *)&command,
					   sizeof(command));
		if (ret < 0) {
			dev_err(&data->spi->dev, "Remove sensor fail\n");
			return ret;
		}

		data->sensor_enable &= ~BIT(type);
	}

	data->check_status[type] = SSP_ADD_SENSOR_STATE;

	if (atomic_dec_and_test(&data->enable_refcount))
		ssp_disable_wdt_timer(data);

	return 0;
}
EXPORT_SYMBOL(ssp_disable_sensor);

static irqreturn_t ssp_irq_thread_fn(int irq, void *dev_id)
{
	struct ssp_data *data = dev_id;

	/*
	 * This wrapper is done to preserve error path for ssp_irq_msg, also
	 * it is defined in different file.
	 */
	ssp_irq_msg(data);

	return IRQ_HANDLED;
}

static int ssp_initialize_mcu(struct ssp_data *data)
{
	int ret;

	ssp_clean_pending_list(data);

	ret = ssp_get_chipid(data);
	if (ret != SSP_DEVICE_ID) {
		dev_err(&data->spi->dev, "%s - MCU %s ret = %d\n", __func__,
			ret < 0 ? "is not working" : "identification failed",
			ret);
		return ret < 0 ? ret : -ENODEV;
	}

	dev_info(&data->spi->dev, "MCU device ID = %d\n", ret);

	/*
	 * needs clarification, for now do not want to export all transfer
	 * methods to sensors' drivers
	 */
	ret = ssp_set_magnetic_matrix(data);
	if (ret < 0) {
		dev_err(&data->spi->dev,
			"%s - ssp_set_magnetic_matrix failed\n", __func__);
		return ret;
	}

	data->available_sensors = ssp_get_sensor_scanning_info(data);
	if (data->available_sensors == 0) {
		dev_err(&data->spi->dev,
			"%s - ssp_get_sensor_scanning_info failed\n", __func__);
		return -EIO;
	}

	data->cur_firm_rev = ssp_get_firmware_rev(data);
	dev_info(&data->spi->dev, "MCU Firm Rev : New = %8u\n",
		 data->cur_firm_rev);

	return ssp_command(data, SSP_MSG2SSP_AP_MCU_DUMP_CHECK, 0);
}

/*
 * sensorhub can request its reinitialization as some brutal and rare error
 * handling. It can be requested from the MCU.
 */
static void ssp_refresh_task(struct work_struct *work)
{
	struct ssp_data *data = container_of((struct delayed_work *)work,
					     struct ssp_data, work_refresh);

	dev_info(&data->spi->dev, "refreshing\n");

	data->reset_cnt++;

	if (ssp_initialize_mcu(data) >= 0) {
		ssp_sync_available_sensors(data);
		if (data->last_ap_state != 0)
			ssp_command(data, data->last_ap_state, 0);

		if (data->last_resume_state != 0)
			ssp_command(data, data->last_resume_state, 0);

		data->timeout_cnt = 0;
		data->com_fail_cnt = 0;
	}
}

int ssp_queue_ssp_refresh_task(struct ssp_data *data, unsigned int delay)
{
	cancel_delayed_work_sync(&data->work_refresh);

	return queue_delayed_work(system_power_efficient_wq,
				  &data->work_refresh,
				  msecs_to_jiffies(delay));
}

#ifdef CONFIG_OF
static const struct of_device_id ssp_of_match[] = {
	{
		.compatible	= "samsung,sensorhub-rinato",
		.data		= &ssp_rinato_info,
	}, {
		.compatible	= "samsung,sensorhub-thermostat",
		.data		= &ssp_thermostat_info,
	},
	{},
};
MODULE_DEVICE_TABLE(of, ssp_of_match);

static struct ssp_data *ssp_parse_dt(struct device *dev)
{
	int ret;
	struct ssp_data *data;
	struct device_node *node = dev->of_node;
	const struct of_device_id *match;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->mcu_ap_gpio = of_get_named_gpio(node, "mcu-ap-gpios", 0);
	if (data->mcu_ap_gpio < 0)
		goto err_free_pd;

	data->ap_mcu_gpio = of_get_named_gpio(node, "ap-mcu-gpios", 0);
	if (data->ap_mcu_gpio < 0)
		goto err_free_pd;

	data->mcu_reset_gpio = of_get_named_gpio(node, "mcu-reset-gpios", 0);
	if (data->mcu_reset_gpio < 0)
		goto err_free_pd;

	ret = devm_gpio_request_one(dev, data->ap_mcu_gpio, GPIOF_OUT_INIT_HIGH,
				    "ap-mcu-gpios");
	if (ret)
		goto err_free_pd;

	ret = devm_gpio_request_one(dev, data->mcu_reset_gpio,
				    GPIOF_OUT_INIT_HIGH, "mcu-reset-gpios");
	if (ret)
		goto err_ap_mcu;

	match = of_match_node(ssp_of_match, node);
	if (!match)
		goto err_mcu_reset_gpio;

	data->sensorhub_info = (struct ssp_sensorhub_info *)match->data;

	dev_set_drvdata(dev, data);

	return data;

err_mcu_reset_gpio:
	devm_gpio_free(dev, data->mcu_reset_gpio);
err_ap_mcu:
	devm_gpio_free(dev, data->ap_mcu_gpio);
err_free_pd:
	devm_kfree(dev, data);
	return NULL;
}
#else
static struct ssp_data *ssp_parse_dt(struct device *pdev)
{
	return NULL;
}
#endif

/**
 * ssp_register_consumer() - registers iio consumer in ssp framework
 *
 * @indio_dev:	consumer iio device
 * @type:	ssp sensor type
 */
void ssp_register_consumer(struct iio_dev *indio_dev, enum ssp_sensor_type type)
{
	struct ssp_data *data = dev_get_drvdata(indio_dev->dev.parent->parent);

	data->sensor_devs[type] = indio_dev;
}
EXPORT_SYMBOL(ssp_register_consumer);

static int ssp_probe(struct spi_device *spi)
{
	int ret, i;
	struct ssp_data *data;

	data = ssp_parse_dt(&spi->dev);
	if (!data) {
		dev_err(&spi->dev, "Failed to find platform data\n");
		return -ENODEV;
	}

	ret = mfd_add_devices(&spi->dev, -1, sensorhub_sensor_devs,
			      ARRAY_SIZE(sensorhub_sensor_devs), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(&spi->dev, "mfd add devices fail\n");
		return ret;
	}

	spi->mode = SPI_MODE_1;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(&spi->dev, "Failed to setup spi\n");
		return ret;
	}

	data->fw_dl_state = SSP_FW_DL_STATE_NONE;
	data->spi = spi;
	spi_set_drvdata(spi, data);

	mutex_init(&data->comm_lock);

	for (i = 0; i < SSP_SENSOR_MAX; ++i) {
		data->delay_buf[i] = SSP_DEFAULT_POLLING_DELAY;
		data->batch_latency_buf[i] = 0;
		data->batch_opt_buf[i] = 0;
		data->check_status[i] = SSP_INITIALIZATION_STATE;
	}

	data->delay_buf[SSP_BIO_HRM_LIB] = 100;

	data->time_syncing = true;

	mutex_init(&data->pending_lock);
	INIT_LIST_HEAD(&data->pending_list);

	atomic_set(&data->enable_refcount, 0);

	INIT_WORK(&data->work_wdt, ssp_wdt_work_func);
	INIT_DELAYED_WORK(&data->work_refresh, ssp_refresh_task);

	setup_timer(&data->wdt_timer, ssp_wdt_timer_func, (unsigned long)data);

	ret = request_threaded_irq(data->spi->irq, NULL,
				   ssp_irq_thread_fn,
				   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				   "SSP_Int", data);
	if (ret < 0) {
		dev_err(&spi->dev, "Irq request fail\n");
		goto err_setup_irq;
	}

	/* Let's start with enabled one so irq balance could be ok */
	data->shut_down = false;

	/* just to avoid unbalanced irq set wake up */
	enable_irq_wake(data->spi->irq);

	data->fw_dl_state = ssp_check_fwbl(data);
	if (data->fw_dl_state == SSP_FW_DL_STATE_NONE) {
		ret = ssp_initialize_mcu(data);
		if (ret < 0) {
			dev_err(&spi->dev, "Initialize_mcu failed\n");
			goto err_read_reg;
		}
	} else {
		dev_err(&spi->dev, "Firmware version not supported\n");
		ret = -EPERM;
		goto err_read_reg;
	}

	return 0;

err_read_reg:
	free_irq(data->spi->irq, data);
err_setup_irq:
	mutex_destroy(&data->pending_lock);
	mutex_destroy(&data->comm_lock);

	dev_err(&spi->dev, "Probe failed!\n");

	return ret;
}

static int ssp_remove(struct spi_device *spi)
{
	struct ssp_data *data = spi_get_drvdata(spi);

	if (ssp_command(data, SSP_MSG2SSP_AP_STATUS_SHUTDOWN, 0) < 0)
		dev_err(&data->spi->dev,
			"SSP_MSG2SSP_AP_STATUS_SHUTDOWN failed\n");

	ssp_enable_mcu(data, false);
	ssp_disable_wdt_timer(data);

	ssp_clean_pending_list(data);

	free_irq(data->spi->irq, data);

	del_timer_sync(&data->wdt_timer);
	cancel_work_sync(&data->work_wdt);

	mutex_destroy(&data->comm_lock);
	mutex_destroy(&data->pending_lock);

	mfd_remove_devices(&spi->dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int ssp_suspend(struct device *dev)
{
	int ret;
	struct ssp_data *data = spi_get_drvdata(to_spi_device(dev));

	data->last_resume_state = SSP_MSG2SSP_AP_STATUS_SUSPEND;

	if (atomic_read(&data->enable_refcount) > 0)
		ssp_disable_wdt_timer(data);

	ret = ssp_command(data, SSP_MSG2SSP_AP_STATUS_SUSPEND, 0);
	if (ret < 0) {
		dev_err(&data->spi->dev,
			"%s SSP_MSG2SSP_AP_STATUS_SUSPEND failed\n", __func__);

		ssp_enable_wdt_timer(data);
		return ret;
	}

	data->time_syncing = false;
	disable_irq(data->spi->irq);

	return 0;
}

static int ssp_resume(struct device *dev)
{
	int ret;
	struct ssp_data *data = spi_get_drvdata(to_spi_device(dev));

	enable_irq(data->spi->irq);

	if (atomic_read(&data->enable_refcount) > 0)
		ssp_enable_wdt_timer(data);

	ret = ssp_command(data, SSP_MSG2SSP_AP_STATUS_RESUME, 0);
	if (ret < 0) {
		dev_err(&data->spi->dev,
			"%s SSP_MSG2SSP_AP_STATUS_RESUME failed\n", __func__);
		ssp_disable_wdt_timer(data);
		return ret;
	}

	/* timesyncing is set by MCU */
	data->last_resume_state = SSP_MSG2SSP_AP_STATUS_RESUME;

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops ssp_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ssp_suspend, ssp_resume)
};

static struct spi_driver ssp_driver = {
	.probe = ssp_probe,
	.remove = ssp_remove,
	.driver = {
		.pm = &ssp_pm_ops,
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ssp_of_match),
		.name = "sensorhub"
	},
};

module_spi_driver(ssp_driver);

MODULE_DESCRIPTION("ssp sensorhub driver");
MODULE_AUTHOR("Samsung Electronics");
MODULE_LICENSE("GPL");
