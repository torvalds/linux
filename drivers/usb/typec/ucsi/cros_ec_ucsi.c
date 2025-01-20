// SPDX-License-Identifier: GPL-2.0
/*
 * UCSI driver for ChromeOS EC
 *
 * Copyright 2024 Google LLC.
 */

#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/jiffies.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_data/cros_ec_commands.h>
#include <linux/platform_data/cros_usbpd_notify.h>
#include <linux/platform_data/cros_ec_proto.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include "ucsi.h"

/*
 * Maximum size in bytes of a UCSI message between AP and EC
 */
#define MAX_EC_DATA_SIZE	256

/*
 * Maximum time in milliseconds the cros_ec_ucsi driver
 * will wait for a response to a command or and ack.
 */
#define WRITE_TMO_MS		5000

/* Number of times to attempt recovery from a write timeout before giving up. */
#define WRITE_TMO_CTR_MAX	5

struct cros_ucsi_data {
	struct device *dev;
	struct ucsi *ucsi;

	struct cros_ec_device *ec;
	struct notifier_block nb;
	struct work_struct work;
	struct delayed_work write_tmo;
	int tmo_counter;

	struct completion complete;
	unsigned long flags;
};

static int cros_ucsi_read(struct ucsi *ucsi, unsigned int offset, void *val,
			  size_t val_len)
{
	struct cros_ucsi_data *udata = ucsi_get_drvdata(ucsi);
	struct ec_params_ucsi_ppm_get req = {
		.offset = offset,
		.size = val_len,
	};
	int ret;

	if (val_len > MAX_EC_DATA_SIZE) {
		dev_err(udata->dev, "Can't read %zu bytes. Too big.\n", val_len);
		return -EINVAL;
	}

	ret = cros_ec_cmd(udata->ec, 0, EC_CMD_UCSI_PPM_GET,
			  &req, sizeof(req), val, val_len);
	if (ret < 0) {
		dev_warn(udata->dev, "Failed to send EC message UCSI_PPM_GET: error=%d\n", ret);
		return ret;
	}
	return 0;
}

static int cros_ucsi_read_version(struct ucsi *ucsi, u16 *version)
{
	return cros_ucsi_read(ucsi, UCSI_VERSION, version, sizeof(*version));
}

static int cros_ucsi_read_cci(struct ucsi *ucsi, u32 *cci)
{
	return cros_ucsi_read(ucsi, UCSI_CCI, cci, sizeof(*cci));
}

static int cros_ucsi_read_message_in(struct ucsi *ucsi, void *val,
				     size_t val_len)
{
	return cros_ucsi_read(ucsi, UCSI_MESSAGE_IN, val, val_len);
}

static int cros_ucsi_async_control(struct ucsi *ucsi, u64 cmd)
{
	struct cros_ucsi_data *udata = ucsi_get_drvdata(ucsi);
	u8 ec_buf[sizeof(struct ec_params_ucsi_ppm_set) + sizeof(cmd)];
	struct ec_params_ucsi_ppm_set *req = (struct ec_params_ucsi_ppm_set *) ec_buf;
	int ret;

	req->offset = UCSI_CONTROL;
	memcpy(req->data, &cmd, sizeof(cmd));
	ret = cros_ec_cmd(udata->ec, 0, EC_CMD_UCSI_PPM_SET,
			  req, sizeof(ec_buf), NULL, 0);
	if (ret < 0) {
		dev_warn(udata->dev, "Failed to send EC message UCSI_PPM_SET: error=%d\n", ret);
		return ret;
	}
	return 0;
}

static int cros_ucsi_sync_control(struct ucsi *ucsi, u64 cmd, u32 *cci,
				  void *data, size_t size)
{
	struct cros_ucsi_data *udata = ucsi_get_drvdata(ucsi);
	int ret;

	ret = ucsi_sync_control_common(ucsi, cmd, cci, data, size);
	switch (ret) {
	case -EBUSY:
		/* EC may return -EBUSY if CCI.busy is set.
		 * Convert this to a timeout.
		 */
	case -ETIMEDOUT:
		/* Schedule recovery attempt when we timeout
		 * or tried to send a command while still busy.
		 */
		cancel_delayed_work_sync(&udata->write_tmo);
		schedule_delayed_work(&udata->write_tmo,
				      msecs_to_jiffies(WRITE_TMO_MS));
		break;
	case 0:
		/* Successful write. Cancel any pending recovery work. */
		cancel_delayed_work_sync(&udata->write_tmo);
		break;
	}

	return ret;
}

static const struct ucsi_operations cros_ucsi_ops = {
	.read_version = cros_ucsi_read_version,
	.read_cci = cros_ucsi_read_cci,
	.read_message_in = cros_ucsi_read_message_in,
	.async_control = cros_ucsi_async_control,
	.sync_control = cros_ucsi_sync_control,
};

static void cros_ucsi_work(struct work_struct *work)
{
	struct cros_ucsi_data *udata = container_of(work, struct cros_ucsi_data, work);
	u32 cci;

	if (cros_ucsi_read_cci(udata->ucsi, &cci))
		return;

	ucsi_notify_common(udata->ucsi, cci);
}

static void cros_ucsi_write_timeout(struct work_struct *work)
{
	struct cros_ucsi_data *udata =
		container_of(work, struct cros_ucsi_data, write_tmo.work);
	u32 cci;
	u64 cmd;

	if (cros_ucsi_read(udata->ucsi, UCSI_CCI, &cci, sizeof(cci))) {
		dev_err(udata->dev,
			"Reading CCI failed; no write timeout recovery possible.\n");
		return;
	}

	if (cci & UCSI_CCI_BUSY) {
		udata->tmo_counter++;

		if (udata->tmo_counter <= WRITE_TMO_CTR_MAX)
			schedule_delayed_work(&udata->write_tmo,
					      msecs_to_jiffies(WRITE_TMO_MS));
		else
			dev_err(udata->dev,
				"PPM unresponsive - too many write timeouts.\n");

		return;
	}

	/* No longer busy means we can reset our timeout counter. */
	udata->tmo_counter = 0;

	/* Need to ack previous command which may have timed out. */
	if (cci & UCSI_CCI_COMMAND_COMPLETE) {
		cmd = UCSI_ACK_CC_CI | UCSI_ACK_COMMAND_COMPLETE;
		cros_ucsi_async_control(udata->ucsi, cmd);

		/* Check again after a few seconds that the system has
		 * recovered to make sure our async write above was successful.
		 */
		schedule_delayed_work(&udata->write_tmo,
				      msecs_to_jiffies(WRITE_TMO_MS));
		return;
	}

	/* We recovered from a previous timeout. Treat this as a recovery from
	 * suspend and call resume.
	 */
	ucsi_resume(udata->ucsi);
}

static int cros_ucsi_event(struct notifier_block *nb,
			   unsigned long host_event, void *_notify)
{
	struct cros_ucsi_data *udata = container_of(nb, struct cros_ucsi_data, nb);

	if (!(host_event & PD_EVENT_PPM))
		return NOTIFY_OK;

	dev_dbg(udata->dev, "UCSI notification received\n");
	flush_work(&udata->work);
	schedule_work(&udata->work);

	return NOTIFY_OK;
}

static void cros_ucsi_destroy(struct cros_ucsi_data *udata)
{
	cros_usbpd_unregister_notify(&udata->nb);
	cancel_delayed_work_sync(&udata->write_tmo);
	cancel_work_sync(&udata->work);
	ucsi_destroy(udata->ucsi);
}

static int cros_ucsi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cros_ec_dev *ec_data = dev_get_drvdata(dev->parent);
	struct cros_ucsi_data *udata;
	int ret;

	udata = devm_kzalloc(dev, sizeof(*udata), GFP_KERNEL);
	if (!udata)
		return -ENOMEM;

	udata->dev = dev;

	udata->ec = ec_data->ec_dev;
	if (!udata->ec)
		return dev_err_probe(dev, -ENODEV, "couldn't find parent EC device\n");

	platform_set_drvdata(pdev, udata);

	INIT_WORK(&udata->work, cros_ucsi_work);
	INIT_DELAYED_WORK(&udata->write_tmo, cros_ucsi_write_timeout);
	init_completion(&udata->complete);

	udata->ucsi = ucsi_create(dev, &cros_ucsi_ops);
	if (IS_ERR(udata->ucsi))
		return dev_err_probe(dev, PTR_ERR(udata->ucsi), "failed to allocate UCSI instance\n");

	ucsi_set_drvdata(udata->ucsi, udata);

	udata->nb.notifier_call = cros_ucsi_event;
	ret = cros_usbpd_register_notify(&udata->nb);
	if (ret) {
		dev_err_probe(dev, ret, "failed to register notifier\n");
		ucsi_destroy(udata->ucsi);
		return ret;
	}

	ret = ucsi_register(udata->ucsi);
	if (ret) {
		dev_err_probe(dev, ret, "failed to register UCSI\n");
		cros_ucsi_destroy(udata);
		return ret;
	}

	return 0;
}

static void cros_ucsi_remove(struct platform_device *dev)
{
	struct cros_ucsi_data *udata = platform_get_drvdata(dev);

	ucsi_unregister(udata->ucsi);
	cros_ucsi_destroy(udata);
}

static int __maybe_unused cros_ucsi_suspend(struct device *dev)
{
	struct cros_ucsi_data *udata = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&udata->write_tmo);
	cancel_work_sync(&udata->work);

	return 0;
}

static void __maybe_unused cros_ucsi_complete(struct device *dev)
{
	struct cros_ucsi_data *udata = dev_get_drvdata(dev);

	ucsi_resume(udata->ucsi);
}

/*
 * UCSI protocol is also used on ChromeOS platforms which reply on
 * cros_ec_lpc.c driver for communication with embedded controller (EC).
 * On such platforms communication with the EC is not available until
 * the .complete() callback of the cros_ec_lpc driver is executed.
 * For this reason we delay ucsi_resume() until the .complete() stage
 * otherwise UCSI SET_NOTIFICATION_ENABLE command will fail and we won't
 * receive any UCSI notifications from the EC where PPM is implemented.
 */
static const struct dev_pm_ops cros_ucsi_pm_ops = {
#ifdef CONFIG_PM_SLEEP
	.suspend = cros_ucsi_suspend,
	.complete = cros_ucsi_complete,
#endif
};

static const struct platform_device_id cros_ucsi_id[] = {
	{ KBUILD_MODNAME, 0 },
	{}
};
MODULE_DEVICE_TABLE(platform, cros_ucsi_id);

static struct platform_driver cros_ucsi_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.pm = &cros_ucsi_pm_ops,
	},
	.id_table = cros_ucsi_id,
	.probe = cros_ucsi_probe,
	.remove = cros_ucsi_remove,
};

module_platform_driver(cros_ucsi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("UCSI driver for ChromeOS EC");
