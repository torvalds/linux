// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Zynq MPSoC Power Management
 *
 *  Copyright (C) 2014-2019 Xilinx, Inc.
 *
 *  Davorin Mista <davorin.mista@aggios.com>
 *  Jolly Shah <jollys@xilinx.com>
 *  Rajan Vaja <rajan.vaja@xilinx.com>
 */

#include <linux/mailbox_client.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/suspend.h>

#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/mailbox/zynqmp-ipi-message.h>

/**
 * struct zynqmp_pm_work_struct - Wrapper for struct work_struct
 * @callback_work:	Work structure
 * @args:		Callback arguments
 */
struct zynqmp_pm_work_struct {
	struct work_struct callback_work;
	u32 args[CB_ARG_CNT];
};

static struct zynqmp_pm_work_struct *zynqmp_pm_init_suspend_work;
static struct mbox_chan *rx_chan;
static const struct zynqmp_eemi_ops *eemi_ops;

enum pm_suspend_mode {
	PM_SUSPEND_MODE_FIRST = 0,
	PM_SUSPEND_MODE_STD = PM_SUSPEND_MODE_FIRST,
	PM_SUSPEND_MODE_POWER_OFF,
};

#define PM_SUSPEND_MODE_FIRST	PM_SUSPEND_MODE_STD

static const char *const suspend_modes[] = {
	[PM_SUSPEND_MODE_STD] = "standard",
	[PM_SUSPEND_MODE_POWER_OFF] = "power-off",
};

static enum pm_suspend_mode suspend_mode = PM_SUSPEND_MODE_STD;

enum pm_api_cb_id {
	PM_INIT_SUSPEND_CB = 30,
	PM_ACKNOWLEDGE_CB,
	PM_NOTIFY_CB,
};

static void zynqmp_pm_get_callback_data(u32 *buf)
{
	zynqmp_pm_invoke_fn(GET_CALLBACK_DATA, 0, 0, 0, 0, buf);
}

static irqreturn_t zynqmp_pm_isr(int irq, void *data)
{
	u32 payload[CB_PAYLOAD_SIZE];

	zynqmp_pm_get_callback_data(payload);

	/* First element is callback API ID, others are callback arguments */
	if (payload[0] == PM_INIT_SUSPEND_CB) {
		switch (payload[1]) {
		case SUSPEND_SYSTEM_SHUTDOWN:
			orderly_poweroff(true);
			break;
		case SUSPEND_POWER_REQUEST:
			pm_suspend(PM_SUSPEND_MEM);
			break;
		default:
			pr_err("%s Unsupported InitSuspendCb reason "
				"code %d\n", __func__, payload[1]);
		}
	}

	return IRQ_HANDLED;
}

static void ipi_receive_callback(struct mbox_client *cl, void *data)
{
	struct zynqmp_ipi_message *msg = (struct zynqmp_ipi_message *)data;
	u32 payload[CB_PAYLOAD_SIZE];
	int ret;

	memcpy(payload, msg->data, sizeof(msg->len));
	/* First element is callback API ID, others are callback arguments */
	if (payload[0] == PM_INIT_SUSPEND_CB) {
		if (work_pending(&zynqmp_pm_init_suspend_work->callback_work))
			return;

		/* Copy callback arguments into work's structure */
		memcpy(zynqmp_pm_init_suspend_work->args, &payload[1],
		       sizeof(zynqmp_pm_init_suspend_work->args));

		queue_work(system_unbound_wq,
			   &zynqmp_pm_init_suspend_work->callback_work);

		/* Send NULL message to mbox controller to ack the message */
		ret = mbox_send_message(rx_chan, NULL);
		if (ret)
			pr_err("IPI ack failed. Error %d\n", ret);
	}
}

/**
 * zynqmp_pm_init_suspend_work_fn - Initialize suspend
 * @work:	Pointer to work_struct
 *
 * Bottom-half of PM callback IRQ handler.
 */
static void zynqmp_pm_init_suspend_work_fn(struct work_struct *work)
{
	struct zynqmp_pm_work_struct *pm_work =
		container_of(work, struct zynqmp_pm_work_struct, callback_work);

	if (pm_work->args[0] == SUSPEND_SYSTEM_SHUTDOWN) {
		orderly_poweroff(true);
	} else if (pm_work->args[0] == SUSPEND_POWER_REQUEST) {
		pm_suspend(PM_SUSPEND_MEM);
	} else {
		pr_err("%s Unsupported InitSuspendCb reason code %d.\n",
		       __func__, pm_work->args[0]);
	}
}

static ssize_t suspend_mode_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	char *s = buf;
	int md;

	for (md = PM_SUSPEND_MODE_FIRST; md < ARRAY_SIZE(suspend_modes); md++)
		if (suspend_modes[md]) {
			if (md == suspend_mode)
				s += sprintf(s, "[%s] ", suspend_modes[md]);
			else
				s += sprintf(s, "%s ", suspend_modes[md]);
		}

	/* Convert last space to newline */
	if (s != buf)
		*(s - 1) = '\n';
	return (s - buf);
}

static ssize_t suspend_mode_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int md, ret = -EINVAL;

	if (!eemi_ops->set_suspend_mode)
		return ret;

	for (md = PM_SUSPEND_MODE_FIRST; md < ARRAY_SIZE(suspend_modes); md++)
		if (suspend_modes[md] &&
		    sysfs_streq(suspend_modes[md], buf)) {
			ret = 0;
			break;
		}

	if (!ret && md != suspend_mode) {
		ret = eemi_ops->set_suspend_mode(md);
		if (likely(!ret))
			suspend_mode = md;
	}

	return ret ? ret : count;
}

static DEVICE_ATTR_RW(suspend_mode);

static int zynqmp_pm_probe(struct platform_device *pdev)
{
	int ret, irq;
	u32 pm_api_version;
	struct mbox_client *client;

	eemi_ops = zynqmp_pm_get_eemi_ops();
	if (IS_ERR(eemi_ops))
		return PTR_ERR(eemi_ops);

	if (!eemi_ops->get_api_version || !eemi_ops->init_finalize)
		return -ENXIO;

	eemi_ops->init_finalize();
	eemi_ops->get_api_version(&pm_api_version);

	/* Check PM API version number */
	if (pm_api_version < ZYNQMP_PM_VERSION)
		return -ENODEV;

	if (of_find_property(pdev->dev.of_node, "mboxes", NULL)) {
		zynqmp_pm_init_suspend_work =
			devm_kzalloc(&pdev->dev,
				     sizeof(struct zynqmp_pm_work_struct),
				     GFP_KERNEL);
		if (!zynqmp_pm_init_suspend_work)
			return -ENOMEM;

		INIT_WORK(&zynqmp_pm_init_suspend_work->callback_work,
			  zynqmp_pm_init_suspend_work_fn);
		client = devm_kzalloc(&pdev->dev, sizeof(*client), GFP_KERNEL);
		if (!client)
			return -ENOMEM;

		client->dev = &pdev->dev;
		client->rx_callback = ipi_receive_callback;

		rx_chan = mbox_request_channel_byname(client, "rx");
		if (IS_ERR(rx_chan)) {
			dev_err(&pdev->dev, "Failed to request rx channel\n");
			return IS_ERR(rx_chan);
		}
	} else if (of_find_property(pdev->dev.of_node, "interrupts", NULL)) {
		irq = platform_get_irq(pdev, 0);
		if (irq <= 0)
			return -ENXIO;

		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						zynqmp_pm_isr,
						IRQF_NO_SUSPEND | IRQF_ONESHOT,
						dev_name(&pdev->dev),
						&pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "devm_request_threaded_irq '%d' "
					    "failed with %d\n", irq, ret);
			return ret;
		}
	} else {
		dev_err(&pdev->dev, "Required property not found in DT node\n");
		return -ENOENT;
	}

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_suspend_mode.attr);
	if (ret) {
		dev_err(&pdev->dev, "unable to create sysfs interface\n");
		return ret;
	}

	return 0;
}

static int zynqmp_pm_remove(struct platform_device *pdev)
{
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_suspend_mode.attr);

	if (!rx_chan)
		mbox_free_channel(rx_chan);

	return 0;
}

static const struct of_device_id pm_of_match[] = {
	{ .compatible = "xlnx,zynqmp-power", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, pm_of_match);

static struct platform_driver zynqmp_pm_platform_driver = {
	.probe = zynqmp_pm_probe,
	.remove = zynqmp_pm_remove,
	.driver = {
		.name = "zynqmp_power",
		.of_match_table = pm_of_match,
	},
};
module_platform_driver(zynqmp_pm_platform_driver);
