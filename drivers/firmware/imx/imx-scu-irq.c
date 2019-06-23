// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 NXP
 *
 * Implementation of the SCU IRQ functions using MU.
 *
 */

#include <dt-bindings/firmware/imx/rsrc.h>
#include <linux/firmware/imx/ipc.h>
#include <linux/mailbox_client.h>

#define IMX_SC_IRQ_FUNC_ENABLE	1
#define IMX_SC_IRQ_FUNC_STATUS	2
#define IMX_SC_IRQ_NUM_GROUP	4

static u32 mu_resource_id;

struct imx_sc_msg_irq_get_status {
	struct imx_sc_rpc_msg hdr;
	union {
		struct {
			u16 resource;
			u8 group;
			u8 reserved;
		} __packed req;
		struct {
			u32 status;
		} resp;
	} data;
};

struct imx_sc_msg_irq_enable {
	struct imx_sc_rpc_msg hdr;
	u32 mask;
	u16 resource;
	u8 group;
	u8 enable;
} __packed;

static struct imx_sc_ipc *imx_sc_irq_ipc_handle;
static struct work_struct imx_sc_irq_work;
static ATOMIC_NOTIFIER_HEAD(imx_scu_irq_notifier_chain);

int imx_scu_irq_register_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(
		&imx_scu_irq_notifier_chain, nb);
}
EXPORT_SYMBOL(imx_scu_irq_register_notifier);

int imx_scu_irq_unregister_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(
		&imx_scu_irq_notifier_chain, nb);
}
EXPORT_SYMBOL(imx_scu_irq_unregister_notifier);

static int imx_scu_irq_notifier_call_chain(unsigned long status, u8 *group)
{
	return atomic_notifier_call_chain(&imx_scu_irq_notifier_chain,
		status, (void *)group);
}

static void imx_scu_irq_work_handler(struct work_struct *work)
{
	struct imx_sc_msg_irq_get_status msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	u32 irq_status;
	int ret;
	u8 i;

	for (i = 0; i < IMX_SC_IRQ_NUM_GROUP; i++) {
		hdr->ver = IMX_SC_RPC_VERSION;
		hdr->svc = IMX_SC_RPC_SVC_IRQ;
		hdr->func = IMX_SC_IRQ_FUNC_STATUS;
		hdr->size = 2;

		msg.data.req.resource = mu_resource_id;
		msg.data.req.group = i;

		ret = imx_scu_call_rpc(imx_sc_irq_ipc_handle, &msg, true);
		if (ret) {
			pr_err("get irq group %d status failed, ret %d\n",
			       i, ret);
			return;
		}

		irq_status = msg.data.resp.status;
		if (!irq_status)
			continue;

		imx_scu_irq_notifier_call_chain(irq_status, &i);
	}
}

int imx_scu_irq_group_enable(u8 group, u32 mask, u8 enable)
{
	struct imx_sc_msg_irq_enable msg;
	struct imx_sc_rpc_msg *hdr = &msg.hdr;
	int ret;

	if (!imx_sc_irq_ipc_handle)
		return -EPROBE_DEFER;

	hdr->ver = IMX_SC_RPC_VERSION;
	hdr->svc = IMX_SC_RPC_SVC_IRQ;
	hdr->func = IMX_SC_IRQ_FUNC_ENABLE;
	hdr->size = 3;

	msg.resource = mu_resource_id;
	msg.group = group;
	msg.mask = mask;
	msg.enable = enable;

	ret = imx_scu_call_rpc(imx_sc_irq_ipc_handle, &msg, true);
	if (ret)
		pr_err("enable irq failed, group %d, mask %d, ret %d\n",
			group, mask, ret);

	return ret;
}
EXPORT_SYMBOL(imx_scu_irq_group_enable);

static void imx_scu_irq_callback(struct mbox_client *c, void *msg)
{
	schedule_work(&imx_sc_irq_work);
}

int imx_scu_enable_general_irq_channel(struct device *dev)
{
	struct of_phandle_args spec;
	struct mbox_client *cl;
	struct mbox_chan *ch;
	int ret = 0, i = 0;

	ret = imx_scu_get_handle(&imx_sc_irq_ipc_handle);
	if (ret)
		return ret;

	cl = devm_kzalloc(dev, sizeof(*cl), GFP_KERNEL);
	if (!cl)
		return -ENOMEM;

	cl->dev = dev;
	cl->rx_callback = imx_scu_irq_callback;

	/* SCU general IRQ uses general interrupt channel 3 */
	ch = mbox_request_channel_byname(cl, "gip3");
	if (IS_ERR(ch)) {
		ret = PTR_ERR(ch);
		dev_err(dev, "failed to request mbox chan gip3, ret %d\n", ret);
		devm_kfree(dev, cl);
		return ret;
	}

	INIT_WORK(&imx_sc_irq_work, imx_scu_irq_work_handler);

	if (!of_parse_phandle_with_args(dev->of_node, "mboxes",
				       "#mbox-cells", 0, &spec))
		i = of_alias_get_id(spec.np, "mu");

	/* use mu1 as general mu irq channel if failed */
	if (i < 0)
		i = 1;

	mu_resource_id = IMX_SC_R_MU_0A + i;

	return ret;
}
EXPORT_SYMBOL(imx_scu_enable_general_irq_channel);
