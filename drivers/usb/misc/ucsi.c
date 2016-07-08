/*
 * USB Type-C Connector System Software Interface driver
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/acpi.h>

#include "ucsi.h"

/* Double the time defined by MIN_TIME_TO_RESPOND_WITH_BUSY */
#define UCSI_TIMEOUT_MS 20

enum ucsi_status {
	UCSI_IDLE = 0,
	UCSI_BUSY,
	UCSI_ERROR,
};

struct ucsi_connector {
	int num;
	struct ucsi *ucsi;
	struct work_struct work;
	struct ucsi_connector_capability cap;
};

struct ucsi {
	struct device *dev;
	struct ucsi_data __iomem *data;

	enum ucsi_status status;
	struct completion complete;
	struct ucsi_capability cap;
	struct ucsi_connector *connector;

	/* device lock */
	spinlock_t dev_lock;

	/* PPM Communication lock */
	struct mutex ppm_lock;

	/* PPM communication flags */
	unsigned long flags;
#define EVENT_PENDING	0
#define COMMAND_PENDING	1
};

static int ucsi_acpi_cmd(struct ucsi *ucsi, struct ucsi_control *ctrl)
{
	uuid_le uuid = UUID_LE(0x6f8398c2, 0x7ca4, 0x11e4,
			       0xad, 0x36, 0x63, 0x10, 0x42, 0xb5, 0x00, 0x8f);
	union acpi_object *obj;

	ucsi->data->ctrl.raw_cmd = ctrl->raw_cmd;

	obj = acpi_evaluate_dsm(ACPI_HANDLE(ucsi->dev), uuid.b, 1, 1, NULL);
	if (!obj) {
		dev_err(ucsi->dev, "%s: failed to evaluate _DSM\n", __func__);
		return -EIO;
	}

	ACPI_FREE(obj);
	return 0;
}

static void ucsi_acpi_notify(acpi_handle handle, u32 event, void *data)
{
	struct ucsi *ucsi = data;
	struct ucsi_cci *cci;

	spin_lock(&ucsi->dev_lock);

	ucsi->status = UCSI_IDLE;
	cci = &ucsi->data->cci;

	/*
	 * REVISIT: This is not documented behavior, but all known PPMs ACK
	 * asynchronous events by sending notification with cleared CCI.
	 */
	if (!ucsi->data->raw_cci) {
		if (test_bit(EVENT_PENDING, &ucsi->flags))
			complete(&ucsi->complete);
		else
			dev_WARN(ucsi->dev, "spurious notification\n");
		goto out_unlock;
	}

	if (test_bit(COMMAND_PENDING, &ucsi->flags)) {
		if (cci->busy) {
			ucsi->status = UCSI_BUSY;
			complete(&ucsi->complete);

			goto out_unlock;
		} else if (cci->ack_complete || cci->cmd_complete) {
			/* Error Indication is only valid with commands */
			if (cci->error && cci->cmd_complete)
				ucsi->status = UCSI_ERROR;

			ucsi->data->ctrl.raw_cmd = 0;
			complete(&ucsi->complete);
		}
	}

	if (cci->connector_change) {
		struct ucsi_connector *con;

		/*
		 * This is workaround for buggy PPMs that create asynchronous
		 * event notifications before OPM has enabled them.
		 */
		if (!ucsi->connector)
			goto out_unlock;

		con = ucsi->connector + (cci->connector_change - 1);

		/*
		 * PPM will not clear the connector specific bit in Connector
		 * Change Indication field of CCI until the driver has ACK it,
		 * and the driver can not ACK it before it has been processed.
		 * The PPM will not generate new events before the first has
		 * been acknowledged, even if they are for an other connector.
		 * So only one event at a time.
		 */
		if (!test_and_set_bit(EVENT_PENDING, &ucsi->flags))
			schedule_work(&con->work);
	}
out_unlock:
	spin_unlock(&ucsi->dev_lock);
}

static int ucsi_ack(struct ucsi *ucsi, u8 cmd)
{
	struct ucsi_control ctrl;
	int ret;

	ctrl.cmd.cmd = UCSI_ACK_CC_CI;
	ctrl.cmd.length = 0;
	ctrl.cmd.data = cmd;
	ret = ucsi_acpi_cmd(ucsi, &ctrl);
	if (ret)
		return ret;

	/* Waiting for ACK also with ACK CMD for now */
	ret = wait_for_completion_timeout(&ucsi->complete,
					  msecs_to_jiffies(UCSI_TIMEOUT_MS));
	if (!ret)
		return -ETIMEDOUT;
	return 0;
}

static int ucsi_run_cmd(struct ucsi *ucsi, struct ucsi_control *ctrl,
			void *data, size_t size)
{
	u16 err_value = 0;
	int ret;

	set_bit(COMMAND_PENDING, &ucsi->flags);

	ret = ucsi_acpi_cmd(ucsi, ctrl);
	if (ret)
		goto err_clear_flag;

	ret = wait_for_completion_timeout(&ucsi->complete,
					  msecs_to_jiffies(UCSI_TIMEOUT_MS));
	if (!ret) {
		ret = -ETIMEDOUT;
		goto err_clear_flag;
	}

	switch (ucsi->status) {
	case UCSI_IDLE:
		if (data)
			memcpy(data, ucsi->data->message_in, size);

		ret = ucsi_ack(ucsi, UCSI_ACK_CMD);
		break;
	case UCSI_BUSY:
		/* The caller decides whether to cancel or not */
		ret = -EBUSY;
		goto err_clear_flag;
	case UCSI_ERROR:
		ret = ucsi_ack(ucsi, UCSI_ACK_CMD);
		if (ret)
			goto err_clear_flag;

		ctrl->cmd.cmd = UCSI_GET_ERROR_STATUS;
		ctrl->cmd.length = 0;
		ctrl->cmd.data = 0;
		ret = ucsi_acpi_cmd(ucsi, ctrl);
		if (ret)
			goto err_clear_flag;

		ret = wait_for_completion_timeout(&ucsi->complete,
					msecs_to_jiffies(UCSI_TIMEOUT_MS));
		if (!ret) {
			ret = -ETIMEDOUT;
			goto err_clear_flag;
		}

		memcpy(&err_value, ucsi->data->message_in, sizeof(err_value));

		/* Something has really gone wrong */
		if (WARN_ON(ucsi->status == UCSI_ERROR)) {
			ret = -ENODEV;
			goto err_clear_flag;
		}

		ret = ucsi_ack(ucsi, UCSI_ACK_CMD);
		if (ret)
			goto err_clear_flag;

		switch (err_value) {
		case UCSI_ERROR_INCOMPATIBLE_PARTNER:
			ret = -EOPNOTSUPP;
			break;
		case UCSI_ERROR_CC_COMMUNICATION_ERR:
			ret = -ECOMM;
			break;
		case UCSI_ERROR_CONTRACT_NEGOTIATION_FAIL:
			ret = -EIO;
			break;
		case UCSI_ERROR_DEAD_BATTERY:
			dev_warn(ucsi->dev, "Dead battery condition!\n");
			ret = -EPERM;
			break;
		/* The following mean a bug in this driver */
		case UCSI_ERROR_INVALID_CON_NUM:
		case UCSI_ERROR_UNREGONIZED_CMD:
		case UCSI_ERROR_INVALID_CMD_ARGUMENT:
		default:
			dev_warn(ucsi->dev,
				 "%s: possible UCSI driver bug - error %hu\n",
				 __func__, err_value);
			ret = -EINVAL;
			break;
		}
		break;
	}
	ctrl->raw_cmd = 0;
err_clear_flag:
	clear_bit(COMMAND_PENDING, &ucsi->flags);
	return ret;
}

static void ucsi_connector_change(struct work_struct *work)
{
	struct ucsi_connector *con = container_of(work, struct ucsi_connector,
						  work);
	struct ucsi_connector_status constat;
	struct ucsi *ucsi = con->ucsi;
	struct ucsi_control ctrl;
	int ret;

	mutex_lock(&ucsi->ppm_lock);

	ctrl.cmd.cmd = UCSI_GET_CONNECTOR_STATUS;
	ctrl.cmd.length = 0;
	ctrl.cmd.data = con->num;
	ret = ucsi_run_cmd(con->ucsi, &ctrl, &constat, sizeof(constat));
	if (ret) {
		dev_err(ucsi->dev, "%s: failed to read connector status (%d)\n",
			__func__, ret);
		goto out_ack_event;
	}

	/* Ignoring disconnections and Alternate Modes */
	if (!constat.connected || !(constat.change &
	    (UCSI_CONSTAT_PARTNER_CHANGE | UCSI_CONSTAT_CONNECT_CHANGE)) ||
	    constat.partner_flags & UCSI_CONSTAT_PARTNER_FLAG_ALT_MODE)
		goto out_ack_event;

	/* If the partner got USB Host role, attempting swap */
	if (constat.partner_type & UCSI_CONSTAT_PARTNER_TYPE_DFP) {
		ctrl.uor.cmd = UCSI_SET_UOR;
		ctrl.uor.con_num = con->num;
		ctrl.uor.role = UCSI_UOR_ROLE_DFP;

		ret = ucsi_run_cmd(con->ucsi, &ctrl, NULL, 0);
		if (ret)
			dev_err(ucsi->dev, "%s: failed to swap role (%d)\n",
				__func__, ret);
	}
out_ack_event:
	ucsi_ack(ucsi, UCSI_ACK_EVENT);
	clear_bit(EVENT_PENDING, &ucsi->flags);
	mutex_unlock(&ucsi->ppm_lock);
}

static int ucsi_reset_ppm(struct ucsi *ucsi)
{
	int timeout = UCSI_TIMEOUT_MS;
	struct ucsi_control ctrl;
	int ret;

	memset(&ctrl, 0, sizeof(ctrl));
	ctrl.cmd.cmd = UCSI_PPM_RESET;
	ret = ucsi_acpi_cmd(ucsi, &ctrl);
	if (ret)
		return ret;

	/* There is no quarantee the PPM will ever set the RESET_COMPLETE bit */
	while (!ucsi->data->cci.reset_complete && timeout--)
		usleep_range(1000, 2000);
	return 0;
}

static int ucsi_init(struct ucsi *ucsi)
{
	struct ucsi_connector *con;
	struct ucsi_control ctrl;
	int ret;
	int i;

	init_completion(&ucsi->complete);
	spin_lock_init(&ucsi->dev_lock);
	mutex_init(&ucsi->ppm_lock);

	/* Reset the PPM */
	ret = ucsi_reset_ppm(ucsi);
	if (ret)
		return ret;

	/*
	 * REVISIT: Executing second reset to WA an issue seen on some of the
	 * Broxton based platforms, where the first reset puts the PPM into a
	 * state where it's unable to recognise some of the commands.
	 */
	ret = ucsi_reset_ppm(ucsi);
	if (ret)
		return ret;

	mutex_lock(&ucsi->ppm_lock);

	/* Enable basic notifications */
	ctrl.cmd.cmd = UCSI_SET_NOTIFICATION_ENABLE;
	ctrl.cmd.length = 0;
	ctrl.cmd.data = UCSI_ENABLE_NTFY_CMD_COMPLETE | UCSI_ENABLE_NTFY_ERROR;
	ret = ucsi_run_cmd(ucsi, &ctrl, NULL, 0);
	if (ret)
		goto err_reset;

	/* Get PPM capabilities */
	ctrl.cmd.cmd = UCSI_GET_CAPABILITY;
	ret = ucsi_run_cmd(ucsi, &ctrl, &ucsi->cap, sizeof(ucsi->cap));
	if (ret)
		goto err_reset;

	if (!ucsi->cap.num_connectors) {
		ret = -ENODEV;
		goto err_reset;
	}

	ucsi->connector = devm_kcalloc(ucsi->dev, ucsi->cap.num_connectors,
				       sizeof(*ucsi->connector), GFP_KERNEL);
	if (!ucsi->connector) {
		ret = -ENOMEM;
		goto err_reset;
	}

	for (i = 1, con = ucsi->connector; i < ucsi->cap.num_connectors + 1;
	     i++, con++) {
		/* Get connector capability */
		ctrl.cmd.cmd = UCSI_GET_CONNECTOR_CAPABILITY;
		ctrl.cmd.data = i;
		ret = ucsi_run_cmd(ucsi, &ctrl, &con->cap, sizeof(con->cap));
		if (ret)
			goto err_reset;

		con->num = i;
		con->ucsi = ucsi;
		INIT_WORK(&con->work, ucsi_connector_change);
	}

	/* Enable all notifications */
	ctrl.cmd.cmd = UCSI_SET_NOTIFICATION_ENABLE;
	ctrl.cmd.data = UCSI_ENABLE_NTFY_ALL;
	ret = ucsi_run_cmd(ucsi, &ctrl, NULL, 0);
	if (ret < 0)
		goto err_reset;

	mutex_unlock(&ucsi->ppm_lock);
	return 0;
err_reset:
	ucsi_reset_ppm(ucsi);
	mutex_unlock(&ucsi->ppm_lock);
	return ret;
}

static int ucsi_acpi_probe(struct platform_device *pdev)
{
	struct resource *res;
	acpi_status status;
	struct ucsi *ucsi;
	int ret;

	ucsi = devm_kzalloc(&pdev->dev, sizeof(*ucsi), GFP_KERNEL);
	if (!ucsi)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "missing memory resource\n");
		return -ENODEV;
	}

	/*
	 * NOTE: ACPI has claimed the memory region as it's also an Operation
	 * Region. It's not possible to request it in the driver.
	 */
	ucsi->data = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!ucsi->data)
		return -ENOMEM;

	ucsi->dev = &pdev->dev;

	status = acpi_install_notify_handler(ACPI_HANDLE(&pdev->dev),
					     ACPI_ALL_NOTIFY,
					     ucsi_acpi_notify, ucsi);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	ret = ucsi_init(ucsi);
	if (ret) {
		acpi_remove_notify_handler(ACPI_HANDLE(&pdev->dev),
					   ACPI_ALL_NOTIFY,
					   ucsi_acpi_notify);
		return ret;
	}

	platform_set_drvdata(pdev, ucsi);
	return 0;
}

static int ucsi_acpi_remove(struct platform_device *pdev)
{
	struct ucsi *ucsi = platform_get_drvdata(pdev);

	acpi_remove_notify_handler(ACPI_HANDLE(&pdev->dev),
				   ACPI_ALL_NOTIFY, ucsi_acpi_notify);

	/* Make sure there are no events in the middle of being processed */
	if (wait_on_bit_timeout(&ucsi->flags, EVENT_PENDING,
				TASK_UNINTERRUPTIBLE,
				msecs_to_jiffies(UCSI_TIMEOUT_MS)))
		dev_WARN(ucsi->dev, "%s: Events still pending\n", __func__);

	ucsi_reset_ppm(ucsi);
	return 0;
}

static const struct acpi_device_id ucsi_acpi_match[] = {
	{ "PNP0CA0", 0 },
	{ },
};
MODULE_DEVICE_TABLE(acpi, ucsi_acpi_match);

static struct platform_driver ucsi_acpi_platform_driver = {
	.driver = {
		.name = "ucsi_acpi",
		.acpi_match_table = ACPI_PTR(ucsi_acpi_match),
	},
	.probe = ucsi_acpi_probe,
	.remove = ucsi_acpi_remove,
};

module_platform_driver(ucsi_acpi_platform_driver);

MODULE_AUTHOR("Heikki Krogerus <heikki.krogerus@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("USB Type-C System Software Interface (UCSI) driver");
