// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2019 NXP.
 */

#include <linux/err.h>
#include <linux/device.h>
#include <linux/firmware/imx/sci.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>

#define DEBOUNCE_TIME				30
#define REPEAT_INTERVAL				60

#define SC_IRQ_BUTTON				1
#define SC_IRQ_GROUP_WAKE			3

#define IMX_SC_MISC_FUNC_GET_BUTTON_STATUS	18

struct imx_key_drv_data {
	u32 keycode;
	bool keystate;  /* true: pressed, false: released */
	struct delayed_work check_work;
	struct input_dev *input;
	struct imx_sc_ipc *key_ipc_handle;
	struct notifier_block key_notifier;
};

struct imx_sc_msg_key {
	struct imx_sc_rpc_msg hdr;
	u8 state;
};

static int imx_sc_key_notify(struct notifier_block *nb,
			     unsigned long event, void *group)
{
	struct imx_key_drv_data *priv =
				 container_of(nb,
					      struct imx_key_drv_data,
					      key_notifier);

	if ((event & SC_IRQ_BUTTON) && (*(u8 *)group == SC_IRQ_GROUP_WAKE)) {
		schedule_delayed_work(&priv->check_work,
				      msecs_to_jiffies(DEBOUNCE_TIME));
		pm_wakeup_event(priv->input->dev.parent, 0);
	}

	return 0;
}

static void imx_sc_check_for_events(struct work_struct *work)
{
	struct imx_key_drv_data *priv =
				 container_of(work,
					      struct imx_key_drv_data,
					      check_work.work);
	struct input_dev *input = priv->input;
	struct imx_sc_msg_key msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	bool state;
	int error;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_MISC;
	hdr->func = IMX_SC_MISC_FUNC_GET_BUTTON_STATUS;
	hdr->size = 1;

	error = imx_scu_call_rpc(priv->key_ipc_handle, &msg, true);
	if (error) {
		dev_err(&input->dev, "read imx sc key failed, error %d\n", error);
		return;
	}

	state = (bool)msg.state;

	if (state ^ priv->keystate) {
		priv->keystate = state;
		input_event(input, EV_KEY, priv->keycode, state);
		input_sync(input);
		if (!priv->keystate)
			pm_relax(priv->input->dev.parent);
	}

	if (state)
		schedule_delayed_work(&priv->check_work,
				      msecs_to_jiffies(REPEAT_INTERVAL));
}

static int imx_sc_key_probe(struct platform_device *pdev)
{
	struct imx_key_drv_data *priv;
	struct input_dev *input;
	int error;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	error = imx_scu_get_handle(&priv->key_ipc_handle);
	if (error)
		return error;

	if (device_property_read_u32(&pdev->dev, "linux,keycodes",
				     &priv->keycode)) {
		dev_err(&pdev->dev, "missing linux,keycodes property\n");
		return -EINVAL;
	}

	INIT_DELAYED_WORK(&priv->check_work, imx_sc_check_for_events);

	input = devm_input_allocate_device(&pdev->dev);
	if (!input) {
		dev_err(&pdev->dev, "failed to allocate the input device\n");
		return -ENOMEM;
	}

	input->name = pdev->name;
	input->phys = "imx-sc-key/input0";
	input->id.bustype = BUS_HOST;

	input_set_capability(input, EV_KEY, priv->keycode);

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		return error;
	}

	priv->input = input;
	platform_set_drvdata(pdev, priv);

	error = imx_scu_irq_group_enable(SC_IRQ_GROUP_WAKE, SC_IRQ_BUTTON,
					 true);
	if (error) {
		dev_err(&pdev->dev, "failed to enable scu group irq\n");
		return error;
	}

	priv->key_notifier.notifier_call = imx_sc_key_notify;
	error = imx_scu_irq_register_notifier(&priv->key_notifier);
	if (error) {
		imx_scu_irq_group_enable(SC_IRQ_GROUP_WAKE, SC_IRQ_BUTTON,
					 false);
		dev_err(&pdev->dev, "failed to register scu notifier\n");
		return error;
	}

	return 0;
}

static int imx_sc_key_remove(struct platform_device *pdev)
{
	struct imx_key_drv_data *priv = platform_get_drvdata(pdev);

	imx_scu_irq_group_enable(SC_IRQ_GROUP_WAKE, SC_IRQ_BUTTON, false);
	imx_scu_irq_unregister_notifier(&priv->key_notifier);
	cancel_delayed_work_sync(&priv->check_work);

	return 0;
}

static const struct of_device_id imx_sc_key_ids[] = {
	{ .compatible = "fsl,imx-sc-key" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_sc_key_ids);

static struct platform_driver imx_sc_key_driver = {
	.driver = {
		.name = "imx-sc-key",
		.of_match_table = imx_sc_key_ids,
	},
	.probe = imx_sc_key_probe,
	.remove = imx_sc_key_remove,
};
module_platform_driver(imx_sc_key_driver);

MODULE_AUTHOR("Anson Huang <Anson.Huang@nxp.com>");
MODULE_DESCRIPTION("i.MX System Controller Key Driver");
MODULE_LICENSE("GPL v2");
