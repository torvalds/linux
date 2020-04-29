// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/mfd/si476x-i2c.c -- Core device driver for si476x MFD
 * device
 *
 * Copyright (C) 2012 Innovative Converged Devices(ICD)
 * Copyright (C) 2013 Andrey Smirnov
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 */
#include <linux/module.h>

#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c.h>
#include <linux/err.h>

#include <linux/mfd/si476x-core.h>

#define SI476X_MAX_IO_ERRORS		10
#define SI476X_DRIVER_RDS_FIFO_DEPTH	128

/**
 * si476x_core_config_pinmux() - pin function configuration function
 *
 * @core: Core device structure
 *
 * Configure the functions of the pins of the radio chip.
 *
 * The function returns zero in case of succes or negative error code
 * otherwise.
 */
static int si476x_core_config_pinmux(struct si476x_core *core)
{
	int err;
	dev_dbg(&core->client->dev, "Configuring pinmux\n");
	err = si476x_core_cmd_dig_audio_pin_cfg(core,
						core->pinmux.dclk,
						core->pinmux.dfs,
						core->pinmux.dout,
						core->pinmux.xout);
	if (err < 0) {
		dev_err(&core->client->dev,
			"Failed to configure digital audio pins(err = %d)\n",
			err);
		return err;
	}

	err = si476x_core_cmd_zif_pin_cfg(core,
					  core->pinmux.iqclk,
					  core->pinmux.iqfs,
					  core->pinmux.iout,
					  core->pinmux.qout);
	if (err < 0) {
		dev_err(&core->client->dev,
			"Failed to configure ZIF pins(err = %d)\n",
			err);
		return err;
	}

	err = si476x_core_cmd_ic_link_gpo_ctl_pin_cfg(core,
						      core->pinmux.icin,
						      core->pinmux.icip,
						      core->pinmux.icon,
						      core->pinmux.icop);
	if (err < 0) {
		dev_err(&core->client->dev,
			"Failed to configure IC-Link/GPO pins(err = %d)\n",
			err);
		return err;
	}

	err = si476x_core_cmd_ana_audio_pin_cfg(core,
						core->pinmux.lrout);
	if (err < 0) {
		dev_err(&core->client->dev,
			"Failed to configure analog audio pins(err = %d)\n",
			err);
		return err;
	}

	err = si476x_core_cmd_intb_pin_cfg(core,
					   core->pinmux.intb,
					   core->pinmux.a1);
	if (err < 0) {
		dev_err(&core->client->dev,
			"Failed to configure interrupt pins(err = %d)\n",
			err);
		return err;
	}

	return 0;
}

static inline void si476x_core_schedule_polling_work(struct si476x_core *core)
{
	schedule_delayed_work(&core->status_monitor,
			      usecs_to_jiffies(SI476X_STATUS_POLL_US));
}

/**
 * si476x_core_start() - early chip startup function
 * @core: Core device structure
 * @soft: When set, this flag forces "soft" startup, where "soft"
 * power down is the one done by sending appropriate command instead
 * of using reset pin of the tuner
 *
 * Perform required startup sequence to correctly power
 * up the chip and perform initial configuration. It does the
 * following sequence of actions:
 *       1. Claims and enables the power supplies VD and VIO1 required
 *          for I2C interface of the chip operation.
 *       2. Waits for 100us, pulls the reset line up, enables irq,
 *          waits for another 100us as it is specified by the
 *          datasheet.
 *       3. Sends 'POWER_UP' command to the device with all provided
 *          information about power-up parameters.
 *       4. Configures, pin multiplexor, disables digital audio and
 *          configures interrupt sources.
 *
 * The function returns zero in case of succes or negative error code
 * otherwise.
 */
int si476x_core_start(struct si476x_core *core, bool soft)
{
	struct i2c_client *client = core->client;
	int err;

	if (!soft) {
		if (gpio_is_valid(core->gpio_reset))
			gpio_set_value_cansleep(core->gpio_reset, 1);

		if (client->irq)
			enable_irq(client->irq);

		udelay(100);

		if (!client->irq) {
			atomic_set(&core->is_alive, 1);
			si476x_core_schedule_polling_work(core);
		}
	} else {
		if (client->irq)
			enable_irq(client->irq);
		else {
			atomic_set(&core->is_alive, 1);
			si476x_core_schedule_polling_work(core);
		}
	}

	err = si476x_core_cmd_power_up(core,
				       &core->power_up_parameters);

	if (err < 0) {
		dev_err(&core->client->dev,
			"Power up failure(err = %d)\n",
			err);
		goto disable_irq;
	}

	if (client->irq)
		atomic_set(&core->is_alive, 1);

	err = si476x_core_config_pinmux(core);
	if (err < 0) {
		dev_err(&core->client->dev,
			"Failed to configure pinmux(err = %d)\n",
			err);
		goto disable_irq;
	}

	if (client->irq) {
		err = regmap_write(core->regmap,
				   SI476X_PROP_INT_CTL_ENABLE,
				   SI476X_RDSIEN |
				   SI476X_STCIEN |
				   SI476X_CTSIEN);
		if (err < 0) {
			dev_err(&core->client->dev,
				"Failed to configure interrupt sources"
				"(err = %d)\n", err);
			goto disable_irq;
		}
	}

	return 0;

disable_irq:
	if (err == -ENODEV)
		atomic_set(&core->is_alive, 0);

	if (client->irq)
		disable_irq(client->irq);
	else
		cancel_delayed_work_sync(&core->status_monitor);

	if (gpio_is_valid(core->gpio_reset))
		gpio_set_value_cansleep(core->gpio_reset, 0);

	return err;
}
EXPORT_SYMBOL_GPL(si476x_core_start);

/**
 * si476x_core_stop() - chip power-down function
 * @core: Core device structure
 * @soft: When set, function sends a POWER_DOWN command instead of
 * bringing reset line low
 *
 * Power down the chip by performing following actions:
 * 1. Disable IRQ or stop the polling worker
 * 2. Send the POWER_DOWN command if the power down is soft or bring
 *    reset line low if not.
 *
 * The function returns zero in case of succes or negative error code
 * otherwise.
 */
int si476x_core_stop(struct si476x_core *core, bool soft)
{
	int err = 0;
	atomic_set(&core->is_alive, 0);

	if (soft) {
		/* TODO: This probably shoud be a configurable option,
		 * so it is possible to have the chips keep their
		 * oscillators running
		 */
		struct si476x_power_down_args args = {
			.xosc = false,
		};
		err = si476x_core_cmd_power_down(core, &args);
	}

	/* We couldn't disable those before
	 * 'si476x_core_cmd_power_down' since we expect to get CTS
	 * interrupt */
	if (core->client->irq)
		disable_irq(core->client->irq);
	else
		cancel_delayed_work_sync(&core->status_monitor);

	if (!soft) {
		if (gpio_is_valid(core->gpio_reset))
			gpio_set_value_cansleep(core->gpio_reset, 0);
	}
	return err;
}
EXPORT_SYMBOL_GPL(si476x_core_stop);

/**
 * si476x_core_set_power_state() - set the level at which the power is
 * supplied for the chip.
 * @core: Core device structure
 * @next_state: enum si476x_power_state describing power state to
 *              switch to.
 *
 * Switch on all the required power supplies
 *
 * This function returns 0 in case of suvccess and negative error code
 * otherwise.
 */
int si476x_core_set_power_state(struct si476x_core *core,
				enum si476x_power_state next_state)
{
	/*
	   It is not clear form the datasheet if it is possible to
	   work with device if not all power domains are operational.
	   So for now the power-up policy is "power-up all the things!"
	 */
	int err = 0;

	if (core->power_state == SI476X_POWER_INCONSISTENT) {
		dev_err(&core->client->dev,
			"The device in inconsistent power state\n");
		return -EINVAL;
	}

	if (next_state != core->power_state) {
		switch (next_state) {
		case SI476X_POWER_UP_FULL:
			err = regulator_bulk_enable(ARRAY_SIZE(core->supplies),
						    core->supplies);
			if (err < 0) {
				core->power_state = SI476X_POWER_INCONSISTENT;
				break;
			}
			/*
			 * Startup timing diagram recommends to have a
			 * 100 us delay between enabling of the power
			 * supplies and turning the tuner on.
			 */
			udelay(100);

			err = si476x_core_start(core, false);
			if (err < 0)
				goto disable_regulators;

			core->power_state = next_state;
			break;

		case SI476X_POWER_DOWN:
			core->power_state = next_state;
			err = si476x_core_stop(core, false);
			if (err < 0)
				core->power_state = SI476X_POWER_INCONSISTENT;
disable_regulators:
			err = regulator_bulk_disable(ARRAY_SIZE(core->supplies),
						     core->supplies);
			if (err < 0)
				core->power_state = SI476X_POWER_INCONSISTENT;
			break;
		default:
			BUG();
		}
	}

	return err;
}
EXPORT_SYMBOL_GPL(si476x_core_set_power_state);

/**
 * si476x_core_report_drainer_stop() - mark the completion of the RDS
 * buffer drain porcess by the worker.
 *
 * @core: Core device structure
 */
static inline void si476x_core_report_drainer_stop(struct si476x_core *core)
{
	mutex_lock(&core->rds_drainer_status_lock);
	core->rds_drainer_is_working = false;
	mutex_unlock(&core->rds_drainer_status_lock);
}

/**
 * si476x_core_start_rds_drainer_once() - start RDS drainer worker if
 * ther is none working, do nothing otherwise
 *
 * @core: Datastructure corresponding to the chip.
 */
static inline void si476x_core_start_rds_drainer_once(struct si476x_core *core)
{
	mutex_lock(&core->rds_drainer_status_lock);
	if (!core->rds_drainer_is_working) {
		core->rds_drainer_is_working = true;
		schedule_work(&core->rds_fifo_drainer);
	}
	mutex_unlock(&core->rds_drainer_status_lock);
}
/**
 * si476x_drain_rds_fifo() - RDS buffer drainer.
 * @work: struct work_struct being ppassed to the function by the
 * kernel.
 *
 * Drain the contents of the RDS FIFO of
 */
static void si476x_core_drain_rds_fifo(struct work_struct *work)
{
	int err;

	struct si476x_core *core = container_of(work, struct si476x_core,
						rds_fifo_drainer);

	struct si476x_rds_status_report report;

	si476x_core_lock(core);
	err = si476x_core_cmd_fm_rds_status(core, true, false, false, &report);
	if (!err) {
		int i = report.rdsfifoused;
		dev_dbg(&core->client->dev,
			"%d elements in RDS FIFO. Draining.\n", i);
		for (; i > 0; --i) {
			err = si476x_core_cmd_fm_rds_status(core, false, false,
							    (i == 1), &report);
			if (err < 0)
				goto unlock;

			kfifo_in(&core->rds_fifo, report.rds,
				 sizeof(report.rds));
			dev_dbg(&core->client->dev, "RDS data:\n %*ph\n",
				(int)sizeof(report.rds), report.rds);
		}
		dev_dbg(&core->client->dev, "Drrrrained!\n");
		wake_up_interruptible(&core->rds_read_queue);
	}

unlock:
	si476x_core_unlock(core);
	si476x_core_report_drainer_stop(core);
}

/**
 * si476x_core_pronounce_dead()
 *
 * @core: Core device structure
 *
 * Mark the device as being dead and wake up all potentially waiting
 * threads of execution.
 *
 */
static void si476x_core_pronounce_dead(struct si476x_core *core)
{
	dev_info(&core->client->dev, "Core device is dead.\n");

	atomic_set(&core->is_alive, 0);

	/* Wake up al possible waiting processes */
	wake_up_interruptible(&core->rds_read_queue);

	atomic_set(&core->cts, 1);
	wake_up(&core->command);

	atomic_set(&core->stc, 1);
	wake_up(&core->tuning);
}

/**
 * si476x_core_i2c_xfer()
 *
 * @core: Core device structure
 * @type: Transfer type
 * @buf: Transfer buffer for/with data
 * @count: Transfer buffer size
 *
 * Perfrom and I2C transfer(either read or write) and keep a counter
 * of I/O errors. If the error counter rises above the threshold
 * pronounce device dead.
 *
 * The function returns zero on succes or negative error code on
 * failure.
 */
int si476x_core_i2c_xfer(struct si476x_core *core,
		    enum si476x_i2c_type type,
		    char *buf, int count)
{
	static int io_errors_count;
	int err;
	if (type == SI476X_I2C_SEND)
		err = i2c_master_send(core->client, buf, count);
	else
		err = i2c_master_recv(core->client, buf, count);

	if (err < 0) {
		if (io_errors_count++ > SI476X_MAX_IO_ERRORS)
			si476x_core_pronounce_dead(core);
	} else {
		io_errors_count = 0;
	}

	return err;
}
EXPORT_SYMBOL_GPL(si476x_core_i2c_xfer);

/**
 * si476x_get_status()
 * @core: Core device structure
 *
 * Get the status byte of the core device by berforming one byte I2C
 * read.
 *
 * The function returns a status value or a negative error code on
 * error.
 */
static int si476x_core_get_status(struct si476x_core *core)
{
	u8 response;
	int err = si476x_core_i2c_xfer(core, SI476X_I2C_RECV,
				  &response, sizeof(response));

	return (err < 0) ? err : response;
}

/**
 * si476x_get_and_signal_status() - IRQ dispatcher
 * @core: Core device structure
 *
 * Dispatch the arrived interrupt request based on the value of the
 * status byte reported by the tuner.
 *
 */
static void si476x_core_get_and_signal_status(struct si476x_core *core)
{
	int status = si476x_core_get_status(core);
	if (status < 0) {
		dev_err(&core->client->dev, "Failed to get status\n");
		return;
	}

	if (status & SI476X_CTS) {
		/* Unfortunately completions could not be used for
		 * signalling CTS since this flag cannot be cleared
		 * in status byte, and therefore once it becomes true
		 * multiple calls to 'complete' would cause the
		 * commands following the current one to be completed
		 * before they actually are */
		dev_dbg(&core->client->dev, "[interrupt] CTSINT\n");
		atomic_set(&core->cts, 1);
		wake_up(&core->command);
	}

	if (status & SI476X_FM_RDS_INT) {
		dev_dbg(&core->client->dev, "[interrupt] RDSINT\n");
		si476x_core_start_rds_drainer_once(core);
	}

	if (status & SI476X_STC_INT) {
		dev_dbg(&core->client->dev, "[interrupt] STCINT\n");
		atomic_set(&core->stc, 1);
		wake_up(&core->tuning);
	}
}

static void si476x_core_poll_loop(struct work_struct *work)
{
	struct si476x_core *core = SI476X_WORK_TO_CORE(work);

	si476x_core_get_and_signal_status(core);

	if (atomic_read(&core->is_alive))
		si476x_core_schedule_polling_work(core);
}

static irqreturn_t si476x_core_interrupt(int irq, void *dev)
{
	struct si476x_core *core = dev;

	si476x_core_get_and_signal_status(core);

	return IRQ_HANDLED;
}

/**
 * si476x_firmware_version_to_revision()
 * @core: Core device structure
 * @major:  Firmware major number
 * @minor1: Firmware first minor number
 * @minor2: Firmware second minor number
 *
 * Convert a chip's firmware version number into an offset that later
 * will be used to as offset in "vtable" of tuner functions
 *
 * This function returns a positive offset in case of success and a -1
 * in case of failure.
 */
static int si476x_core_fwver_to_revision(struct si476x_core *core,
					 int func, int major,
					 int minor1, int minor2)
{
	switch (func) {
	case SI476X_FUNC_FM_RECEIVER:
		switch (major) {
		case 5:
			return SI476X_REVISION_A10;
		case 8:
			return SI476X_REVISION_A20;
		case 10:
			return SI476X_REVISION_A30;
		default:
			goto unknown_revision;
		}
	case SI476X_FUNC_AM_RECEIVER:
		switch (major) {
		case 5:
			return SI476X_REVISION_A10;
		case 7:
			return SI476X_REVISION_A20;
		case 9:
			return SI476X_REVISION_A30;
		default:
			goto unknown_revision;
		}
	case SI476X_FUNC_WB_RECEIVER:
		switch (major) {
		case 3:
			return SI476X_REVISION_A10;
		case 5:
			return SI476X_REVISION_A20;
		case 7:
			return SI476X_REVISION_A30;
		default:
			goto unknown_revision;
		}
	case SI476X_FUNC_BOOTLOADER:
	default:		/* FALLTHROUG */
		BUG();
		return -1;
	}

unknown_revision:
	dev_err(&core->client->dev,
		"Unsupported version of the firmware: %d.%d.%d, "
		"reverting to A10 compatible functions\n",
		major, minor1, minor2);

	return SI476X_REVISION_A10;
}

/**
 * si476x_get_revision_info()
 * @core: Core device structure
 *
 * Get the firmware version number of the device. It is done in
 * following three steps:
 *    1. Power-up the device
 *    2. Send the 'FUNC_INFO' command
 *    3. Powering the device down.
 *
 * The function return zero on success and a negative error code on
 * failure.
 */
static int si476x_core_get_revision_info(struct si476x_core *core)
{
	int rval;
	struct si476x_func_info info;

	si476x_core_lock(core);
	rval = si476x_core_set_power_state(core, SI476X_POWER_UP_FULL);
	if (rval < 0)
		goto exit;

	rval = si476x_core_cmd_func_info(core, &info);
	if (rval < 0)
		goto power_down;

	core->revision = si476x_core_fwver_to_revision(core, info.func,
						       info.firmware.major,
						       info.firmware.minor[0],
						       info.firmware.minor[1]);
power_down:
	si476x_core_set_power_state(core, SI476X_POWER_DOWN);
exit:
	si476x_core_unlock(core);

	return rval;
}

bool si476x_core_has_am(struct si476x_core *core)
{
	return core->chip_id == SI476X_CHIP_SI4761 ||
		core->chip_id == SI476X_CHIP_SI4764;
}
EXPORT_SYMBOL_GPL(si476x_core_has_am);

bool si476x_core_has_diversity(struct si476x_core *core)
{
	return core->chip_id == SI476X_CHIP_SI4764;
}
EXPORT_SYMBOL_GPL(si476x_core_has_diversity);

bool si476x_core_is_a_secondary_tuner(struct si476x_core *core)
{
	return si476x_core_has_diversity(core) &&
		(core->diversity_mode == SI476X_PHDIV_SECONDARY_ANTENNA ||
		 core->diversity_mode == SI476X_PHDIV_SECONDARY_COMBINING);
}
EXPORT_SYMBOL_GPL(si476x_core_is_a_secondary_tuner);

bool si476x_core_is_a_primary_tuner(struct si476x_core *core)
{
	return si476x_core_has_diversity(core) &&
		(core->diversity_mode == SI476X_PHDIV_PRIMARY_ANTENNA ||
		 core->diversity_mode == SI476X_PHDIV_PRIMARY_COMBINING);
}
EXPORT_SYMBOL_GPL(si476x_core_is_a_primary_tuner);

bool si476x_core_is_in_am_receiver_mode(struct si476x_core *core)
{
	return si476x_core_has_am(core) &&
		(core->power_up_parameters.func == SI476X_FUNC_AM_RECEIVER);
}
EXPORT_SYMBOL_GPL(si476x_core_is_in_am_receiver_mode);

bool si476x_core_is_powered_up(struct si476x_core *core)
{
	return core->power_state == SI476X_POWER_UP_FULL;
}
EXPORT_SYMBOL_GPL(si476x_core_is_powered_up);

static int si476x_core_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	int rval;
	struct si476x_core          *core;
	struct si476x_platform_data *pdata;
	struct mfd_cell *cell;
	int              cell_num;

	core = devm_kzalloc(&client->dev, sizeof(*core), GFP_KERNEL);
	if (!core)
		return -ENOMEM;

	core->client = client;

	core->regmap = devm_regmap_init_si476x(core);
	if (IS_ERR(core->regmap)) {
		rval = PTR_ERR(core->regmap);
		dev_err(&client->dev,
			"Failed to allocate register map: %d\n",
			rval);
		return rval;
	}

	i2c_set_clientdata(client, core);

	atomic_set(&core->is_alive, 0);
	core->power_state = SI476X_POWER_DOWN;

	pdata = dev_get_platdata(&client->dev);
	if (pdata) {
		memcpy(&core->power_up_parameters,
		       &pdata->power_up_parameters,
		       sizeof(core->power_up_parameters));

		core->gpio_reset = -1;
		if (gpio_is_valid(pdata->gpio_reset)) {
			rval = gpio_request(pdata->gpio_reset, "si476x reset");
			if (rval) {
				dev_err(&client->dev,
					"Failed to request gpio: %d\n", rval);
				return rval;
			}
			core->gpio_reset = pdata->gpio_reset;
			gpio_direction_output(core->gpio_reset, 0);
		}

		core->diversity_mode = pdata->diversity_mode;
		memcpy(&core->pinmux, &pdata->pinmux,
		       sizeof(struct si476x_pinmux));
	} else {
		dev_err(&client->dev, "No platform data provided\n");
		return -EINVAL;
	}

	core->supplies[0].supply = "vd";
	core->supplies[1].supply = "va";
	core->supplies[2].supply = "vio1";
	core->supplies[3].supply = "vio2";

	rval = devm_regulator_bulk_get(&client->dev,
				       ARRAY_SIZE(core->supplies),
				       core->supplies);
	if (rval) {
		dev_err(&client->dev, "Failed to get all of the regulators\n");
		goto free_gpio;
	}

	mutex_init(&core->cmd_lock);
	init_waitqueue_head(&core->command);
	init_waitqueue_head(&core->tuning);

	rval = kfifo_alloc(&core->rds_fifo,
			   SI476X_DRIVER_RDS_FIFO_DEPTH *
			   sizeof(struct v4l2_rds_data),
			   GFP_KERNEL);
	if (rval) {
		dev_err(&client->dev, "Could not allocate the FIFO\n");
		goto free_gpio;
	}
	mutex_init(&core->rds_drainer_status_lock);
	init_waitqueue_head(&core->rds_read_queue);
	INIT_WORK(&core->rds_fifo_drainer, si476x_core_drain_rds_fifo);

	if (client->irq) {
		rval = devm_request_threaded_irq(&client->dev,
						 client->irq, NULL,
						 si476x_core_interrupt,
						 IRQF_TRIGGER_FALLING |
						 IRQF_ONESHOT,
						 client->name, core);
		if (rval < 0) {
			dev_err(&client->dev, "Could not request IRQ %d\n",
				client->irq);
			goto free_kfifo;
		}
		disable_irq(client->irq);
		dev_dbg(&client->dev, "IRQ requested.\n");

		core->rds_fifo_depth = 20;
	} else {
		INIT_DELAYED_WORK(&core->status_monitor,
				  si476x_core_poll_loop);
		dev_info(&client->dev,
			 "No IRQ number specified, will use polling\n");

		core->rds_fifo_depth = 5;
	}

	core->chip_id = id->driver_data;

	rval = si476x_core_get_revision_info(core);
	if (rval < 0) {
		rval = -ENODEV;
		goto free_kfifo;
	}

	cell_num = 0;

	cell = &core->cells[SI476X_RADIO_CELL];
	cell->name = "si476x-radio";
	cell_num++;

#ifdef CONFIG_SND_SOC_SI476X
	if ((core->chip_id == SI476X_CHIP_SI4761 ||
	     core->chip_id == SI476X_CHIP_SI4764)	&&
	    core->pinmux.dclk == SI476X_DCLK_DAUDIO     &&
	    core->pinmux.dfs  == SI476X_DFS_DAUDIO      &&
	    core->pinmux.dout == SI476X_DOUT_I2S_OUTPUT &&
	    core->pinmux.xout == SI476X_XOUT_TRISTATE) {
		cell = &core->cells[SI476X_CODEC_CELL];
		cell->name          = "si476x-codec";
		cell_num++;
	}
#endif
	rval = mfd_add_devices(&client->dev,
			       (client->adapter->nr << 8) + client->addr,
			       core->cells, cell_num,
			       NULL, 0, NULL);
	if (!rval)
		return 0;

free_kfifo:
	kfifo_free(&core->rds_fifo);

free_gpio:
	if (gpio_is_valid(core->gpio_reset))
		gpio_free(core->gpio_reset);

	return rval;
}

static int si476x_core_remove(struct i2c_client *client)
{
	struct si476x_core *core = i2c_get_clientdata(client);

	si476x_core_pronounce_dead(core);
	mfd_remove_devices(&client->dev);

	if (client->irq)
		disable_irq(client->irq);
	else
		cancel_delayed_work_sync(&core->status_monitor);

	kfifo_free(&core->rds_fifo);

	if (gpio_is_valid(core->gpio_reset))
		gpio_free(core->gpio_reset);

	return 0;
}


static const struct i2c_device_id si476x_id[] = {
	{ "si4761", SI476X_CHIP_SI4761 },
	{ "si4764", SI476X_CHIP_SI4764 },
	{ "si4768", SI476X_CHIP_SI4768 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, si476x_id);

static struct i2c_driver si476x_core_driver = {
	.driver		= {
		.name	= "si476x-core",
	},
	.probe		= si476x_core_probe,
	.remove         = si476x_core_remove,
	.id_table       = si476x_id,
};
module_i2c_driver(si476x_core_driver);


MODULE_AUTHOR("Andrey Smirnov <andrew.smirnov@gmail.com>");
MODULE_DESCRIPTION("Si4761/64/68 AM/FM MFD core device driver");
MODULE_LICENSE("GPL");
