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
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/suspend.h>

#include <linux/firmware/xlnx-zynqmp.h>
#include <linux/firmware/xlnx-event-manager.h>
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

/**
 * struct zynqmp_pm_event_info - event related information
 * @cb_fun:	Function pointer to store the callback function.
 * @cb_type:	Type of callback from pm_api_cb_id,
 *			PM_NOTIFY_CB - for Error Events,
 *			PM_INIT_SUSPEND_CB - for suspend callback.
 * @node_id:	Node-Id related to event.
 * @event:	Event Mask for the Error Event.
 * @wake:	Flag specifying whether the subsystem should be woken upon
 *		event notification.
 */
struct zynqmp_pm_event_info {
	event_cb_func_t cb_fun;
	enum pm_api_cb_id cb_type;
	u32 node_id;
	u32 event;
	bool wake;
};

static struct zynqmp_pm_work_struct *zynqmp_pm_init_suspend_work, *zynqmp_pm_init_restart_work;
static struct mbox_chan *rx_chan;

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

static void zynqmp_pm_get_callback_data(u32 *buf)
{
	zynqmp_pm_invoke_fn(GET_CALLBACK_DATA, buf, 0);
}

static void subsystem_restart_event_callback(const u32 *payload, void *data)
{
	/* First element is callback API ID, others are callback arguments */
	if (work_pending(&zynqmp_pm_init_restart_work->callback_work))
		return;

	/* Copy callback arguments into work's structure */
	memcpy(zynqmp_pm_init_restart_work->args, &payload[0],
	       sizeof(zynqmp_pm_init_restart_work->args));

	queue_work(system_unbound_wq, &zynqmp_pm_init_restart_work->callback_work);
}

static void suspend_event_callback(const u32 *payload, void *data)
{
	/* First element is callback API ID, others are callback arguments */
	if (work_pending(&zynqmp_pm_init_suspend_work->callback_work))
		return;

	/* Copy callback arguments into work's structure */
	memcpy(zynqmp_pm_init_suspend_work->args, &payload[1],
	       sizeof(zynqmp_pm_init_suspend_work->args));

	queue_work(system_unbound_wq, &zynqmp_pm_init_suspend_work->callback_work);
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
			pr_err("%s Unsupported InitSuspendCb reason code %d\n",
			       __func__, payload[1]);
		}
	} else {
		pr_err("%s() Unsupported Callback %d\n", __func__, payload[0]);
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
 * zynqmp_pm_subsystem_restart_work_fn - Initiate Subsystem restart
 * @work:	Pointer to work_struct
 *
 * Bottom-half of PM callback IRQ handler.
 */
static void zynqmp_pm_subsystem_restart_work_fn(struct work_struct *work)
{
	int ret;
	struct zynqmp_pm_work_struct *pm_work = container_of(work, struct zynqmp_pm_work_struct,
							     callback_work);

	/* First element is callback API ID, others are callback arguments */
	if (pm_work->args[0] == PM_NOTIFY_CB) {
		if (pm_work->args[2] == EVENT_SUBSYSTEM_RESTART) {
			ret = zynqmp_pm_system_shutdown(ZYNQMP_PM_SHUTDOWN_TYPE_SETSCOPE_ONLY,
							ZYNQMP_PM_SHUTDOWN_SUBTYPE_SUBSYSTEM);
			if (ret) {
				pr_err("unable to set shutdown scope\n");
				return;
			}

			kernel_restart(NULL);
		} else {
			pr_err("%s Unsupported Event - %d\n", __func__, pm_work->args[2]);
		}
	} else {
		pr_err("%s() Unsupported Callback %d\n", __func__, pm_work->args[0]);
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

	for (md = PM_SUSPEND_MODE_FIRST; md < ARRAY_SIZE(suspend_modes); md++)
		if (suspend_modes[md] &&
		    sysfs_streq(suspend_modes[md], buf)) {
			ret = 0;
			break;
		}

	if (!ret && md != suspend_mode) {
		ret = zynqmp_pm_set_suspend_mode(md);
		if (likely(!ret))
			suspend_mode = md;
	}

	return ret ? ret : count;
}

static DEVICE_ATTR_RW(suspend_mode);

static void unregister_event(struct device *dev, void *res)
{
	struct zynqmp_pm_event_info *event_info = res;

	xlnx_unregister_event(event_info->cb_type, event_info->node_id,
			      event_info->event, event_info->cb_fun, NULL);
}

static int register_event(struct device *dev, const enum pm_api_cb_id cb_type, const u32 node_id,
			  const u32 event, const bool wake, event_cb_func_t cb_fun)
{
	int ret;
	struct zynqmp_pm_event_info *event_info;

	event_info = devres_alloc(unregister_event, sizeof(struct zynqmp_pm_event_info),
				  GFP_KERNEL);
	if (!event_info)
		return -ENOMEM;

	event_info->cb_type = cb_type;
	event_info->node_id = node_id;
	event_info->event = event;
	event_info->wake = wake;
	event_info->cb_fun = cb_fun;

	ret = xlnx_register_event(event_info->cb_type, event_info->node_id,
				  event_info->event, event_info->wake, event_info->cb_fun, NULL);
	if (ret) {
		devres_free(event_info);
		return ret;
	}

	devres_add(dev, event_info);
	return 0;
}

static int zynqmp_pm_probe(struct platform_device *pdev)
{
	int ret, irq;
	u32 pm_api_version, pm_family_code, pm_sub_family_code, node_id;
	struct mbox_client *client;

	ret = zynqmp_pm_get_api_version(&pm_api_version);
	if (ret)
		return ret;

	/* Check PM API version number */
	if (pm_api_version < ZYNQMP_PM_VERSION)
		return -ENODEV;

	/*
	 * First try to use Xilinx Event Manager by registering suspend_event_callback
	 * for suspend/shutdown event.
	 * If xlnx_register_event() returns -EACCES (Xilinx Event Manager
	 * is not available to use) or -ENODEV(Xilinx Event Manager not compiled),
	 * then use ipi-mailbox or interrupt method.
	 */
	ret = register_event(&pdev->dev, PM_INIT_SUSPEND_CB, 0, 0, false,
			     suspend_event_callback);
	if (!ret) {
		zynqmp_pm_init_suspend_work = devm_kzalloc(&pdev->dev,
							   sizeof(struct zynqmp_pm_work_struct),
							   GFP_KERNEL);
		if (!zynqmp_pm_init_suspend_work)
			return -ENOMEM;

		INIT_WORK(&zynqmp_pm_init_suspend_work->callback_work,
			  zynqmp_pm_init_suspend_work_fn);

		ret = zynqmp_pm_get_family_info(&pm_family_code, &pm_sub_family_code);
		if (ret < 0)
			return ret;

		if (pm_sub_family_code == VERSALNET_SUB_FAMILY_CODE)
			node_id = PM_DEV_ACPU_0_0;
		else
			node_id = PM_DEV_ACPU_0;

		ret = register_event(&pdev->dev, PM_NOTIFY_CB, node_id, EVENT_SUBSYSTEM_RESTART,
				     false, subsystem_restart_event_callback);
		if (ret) {
			dev_err(&pdev->dev, "Failed to Register with Xilinx Event manager %d\n",
				ret);
			return ret;
		}

		zynqmp_pm_init_restart_work = devm_kzalloc(&pdev->dev,
							   sizeof(struct zynqmp_pm_work_struct),
							   GFP_KERNEL);
		if (!zynqmp_pm_init_restart_work)
			return -ENOMEM;

		INIT_WORK(&zynqmp_pm_init_restart_work->callback_work,
			  zynqmp_pm_subsystem_restart_work_fn);
	} else if (ret != -EACCES && ret != -ENODEV) {
		dev_err(&pdev->dev, "Failed to Register with Xilinx Event manager %d\n", ret);
		return ret;
	} else if (of_property_present(pdev->dev.of_node, "mboxes")) {
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
			return PTR_ERR(rx_chan);
		}
	} else if (of_property_present(pdev->dev.of_node, "interrupts")) {
		irq = platform_get_irq(pdev, 0);
		if (irq < 0)
			return irq;

		ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
						zynqmp_pm_isr,
						IRQF_NO_SUSPEND | IRQF_ONESHOT,
						dev_name(&pdev->dev),
						&pdev->dev);
		if (ret) {
			dev_err(&pdev->dev, "devm_request_threaded_irq '%d' failed with %d\n",
				irq, ret);
			return ret;
		}
	} else {
		dev_err(&pdev->dev, "Required property not found in DT node\n");
		return -ENOENT;
	}

	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_suspend_mode.attr);
	if (ret)
		return ret;

	return 0;
}

static void zynqmp_pm_remove(struct platform_device *pdev)
{
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_suspend_mode.attr);

	if (!rx_chan)
		mbox_free_channel(rx_chan);
}

static const struct of_device_id pm_of_match[] = {
	{ .compatible = "xlnx,zynqmp-power", },
	{ /* end of table */ },
};
MODULE_DEVICE_TABLE(of, pm_of_match);

static struct platform_driver zynqmp_pm_platform_driver = {
	.probe = zynqmp_pm_probe,
	.remove_new = zynqmp_pm_remove,
	.driver = {
		.name = "zynqmp_power",
		.of_match_table = pm_of_match,
	},
};
module_platform_driver(zynqmp_pm_platform_driver);
