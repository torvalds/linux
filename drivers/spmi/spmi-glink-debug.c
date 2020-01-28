// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020, The Linux Foundation. All rights reserved. */

#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rpmsg.h>
#include <linux/slab.h>
#include <linux/spmi.h>
#include <linux/string.h>

#include <linux/soc/qcom/pmic_glink.h>

#define MSG_OWNER_REG_DUMP		32783
#define MSG_TYPE_REQ_RESP		1
#define REG_DUMP_REG_READ_REQ		0x37
#define REG_DUMP_REG_WRITE_REQ		0x38

#define REG_DUMP_WAIT_TIME_MS		1000

#define SPMI_GLINK_MAX_READ_BYTES	256
#define SPMI_GLINK_MAX_WRITE_BYTES	1

#define PERIPH_MASK			GENMASK(7, 0)

struct reg_dump_read_req_msg {
	struct pmic_glink_hdr		hdr;
	u32				spmi_bus_id;
	u32				pmic_sid;
	u32				address;
	u32				byte_count;
};

struct reg_dump_read_resp_msg {
	struct pmic_glink_hdr		hdr;
	u32				spmi_bus_id;
	u32				pmic_sid;
	u32				address;
	u32				byte_count;
	u8				data[SPMI_GLINK_MAX_READ_BYTES];
};

struct reg_dump_write_req_msg {
	struct pmic_glink_hdr		hdr;
	u32				spmi_bus_id;
	u32				pmic_sid;
	u32				address;
	u32				data;
};

struct reg_dump_write_resp_msg {
	struct pmic_glink_hdr		hdr;
	u32				return_status;
};

struct spmi_glink_ctrl;

struct spmi_glink_dev {
	struct pmic_glink_client	*client;
	struct device			*dev;
	struct mutex			lock;
	struct completion		ack;
	struct reg_dump_read_resp_msg	read_msg;
	struct spmi_glink_ctrl		**gctrl;
	int				bus_count;
};

struct spmi_glink_ctrl {
	struct spmi_glink_dev		*gd;
	struct spmi_controller		*ctrl;
	u32				bus_id;
};

static int spmi_glink_write(struct spmi_glink_ctrl *gctrl, void *data,
				size_t len)
{
	struct spmi_glink_dev *gd = gctrl->gd;
	int ret;

	reinit_completion(&gd->ack);
	ret = pmic_glink_write(gd->client, data, len);
	if (ret)
		return ret;

	ret = wait_for_completion_timeout(&gd->ack,
				msecs_to_jiffies(REG_DUMP_WAIT_TIME_MS));
	if (!ret) {
		dev_err(&gctrl->ctrl->dev, "Error, timed out sending message\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/* Non-data SPMI command */
static int spmi_glink_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid)
{
	return -EOPNOTSUPP;
}

static int spmi_glink_read_reg(struct spmi_glink_ctrl *gctrl, u8 sid, u16 addr,
				u8 *buf, size_t len)
{
	struct spmi_glink_dev *gd = gctrl->gd;
	struct reg_dump_read_req_msg msg = {{0}};
	int ret;

	if (len > SPMI_GLINK_MAX_READ_BYTES)
		return -EINVAL;

	msg.hdr.owner = MSG_OWNER_REG_DUMP;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = REG_DUMP_REG_READ_REQ;

	msg.spmi_bus_id = gctrl->bus_id;
	msg.pmic_sid = sid;
	msg.address = addr;
	msg.byte_count = len;

	ret = spmi_glink_write(gctrl, &msg, sizeof(msg));
	if (ret)
		return ret;

	if (gd->read_msg.byte_count != len)
		return -EINVAL;

	memcpy(buf, gd->read_msg.data, len);

	return 0;
}

static int spmi_glink_read_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
			     u16 addr, u8 *buf, size_t len)
{
	struct spmi_glink_ctrl *gctrl = spmi_controller_get_drvdata(ctrl);
	struct spmi_glink_dev *gd = gctrl->gd;
	int ret, count;

	mutex_lock(&gd->lock);
	do {
		count = min_t(size_t, len, SPMI_GLINK_MAX_READ_BYTES);
		/* Ensure transactions are divided across peripherals */
		if ((addr & PERIPH_MASK) + count > PERIPH_MASK + 1)
			count = PERIPH_MASK + 1 - (addr & PERIPH_MASK);

		ret = spmi_glink_read_reg(gctrl, sid, addr, buf, count);
		if (ret)
			goto done;

		/* Handle a transaction split across SIDs */
		if ((u16)(addr + count) < addr)
			sid++;
		addr += count;
		buf += count;
		len -= count;
	} while (len > 0);

done:
	mutex_unlock(&gd->lock);
	return ret;
}

static int spmi_glink_write_reg(struct spmi_glink_ctrl *gctrl, u8 sid, u16 addr,
				u8 val)
{
	struct reg_dump_write_req_msg msg = {{0}};

	msg.hdr.owner = MSG_OWNER_REG_DUMP;
	msg.hdr.type = MSG_TYPE_REQ_RESP;
	msg.hdr.opcode = REG_DUMP_REG_WRITE_REQ;

	msg.spmi_bus_id = gctrl->bus_id;
	msg.pmic_sid = sid;
	msg.address = addr;
	msg.data = val;

	return spmi_glink_write(gctrl, &msg, sizeof(msg));
}

static int spmi_glink_write_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
			     u16 addr, const u8 *buf, size_t len)
{
	struct spmi_glink_ctrl *gctrl = spmi_controller_get_drvdata(ctrl);
	struct spmi_glink_dev *gd = gctrl->gd;
	int ret, count;

	mutex_lock(&gd->lock);
	do {
		count = min_t(size_t, len, SPMI_GLINK_MAX_WRITE_BYTES);
		/* Ensure transactions are divided across peripherals */
		if ((addr & PERIPH_MASK) + count > PERIPH_MASK + 1)
			count = PERIPH_MASK + 1 - (addr & PERIPH_MASK);

		ret = spmi_glink_write_reg(gctrl, sid, addr, *buf);
		if (ret)
			goto done;

		/* Handle a transaction split across SIDs */
		if ((u16)(addr + count) < addr)
			sid++;
		addr += count;
		buf += count;
		len -= count;
	} while (len > 0);

done:
	mutex_unlock(&gd->lock);
	return ret;
}

static void spmi_glink_handle_read_resp(struct spmi_glink_dev *gd,
			struct reg_dump_read_resp_msg *read_resp, size_t len)
{
	if (len != sizeof(*read_resp)) {
		dev_err(gd->dev, "Invalid read response, glink packet size=%zu\n",
			len);
		return;
	}

	if ((int)read_resp->byte_count < 0) {
		dev_err(gd->dev, "glink read failed, ret=%d\n",
			(int)read_resp->byte_count);
		return;
	}

	memcpy(&gd->read_msg, read_resp, sizeof(gd->read_msg));

	complete(&gd->ack);
}

static void spmi_glink_handle_write_resp(struct spmi_glink_dev *gd,
			struct reg_dump_write_resp_msg *write_resp, size_t len)
{
	if (len != sizeof(*write_resp)) {
		dev_err(gd->dev, "Invalid write response, glink packet size=%zu\n",
			len);
		return;
	}

	if (write_resp->return_status) {
		dev_err(gd->dev, "glink write failed, ret=%d\n",
			(int)write_resp->return_status);
		return;
	}

	complete(&gd->ack);
}

static int spmi_glink_callback(void *priv, void *data, size_t len)
{
	struct spmi_glink_dev *gd = priv;
	struct pmic_glink_hdr *hdr = data;

	dev_dbg(gd->dev, "owner: %u type: %u opcode: %#x len: %zu\n",
		hdr->owner, hdr->type, hdr->opcode, len);

	switch (hdr->opcode) {
	case REG_DUMP_REG_READ_REQ:
		spmi_glink_handle_read_resp(gd, data, len);
		break;
	case REG_DUMP_REG_WRITE_REQ:
		spmi_glink_handle_write_resp(gd, data, len);
		break;
	default:
		dev_err(gd->dev, "Unknown opcode %u\n", hdr->opcode);
		break;
	}

	return 0;
}

static int spmi_glink_remove(struct platform_device *pdev)
{
	struct spmi_glink_dev *gd = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < gd->bus_count; i++) {
		if (gd->gctrl[i]) {
			spmi_controller_remove(gd->gctrl[i]->ctrl);
			spmi_controller_put(gd->gctrl[i]->ctrl);
		}
	}

	pmic_glink_unregister_client(gd->client);

	return 0;
}

static int spmi_glink_probe(struct platform_device *pdev)
{
	struct spmi_glink_dev *gd;
	struct spmi_controller *ctrl;
	struct pmic_glink_client_data client_data = { };
	struct spmi_glink_ctrl *gctrl;
	struct device_node *node;
	int ret, i;

	gd = devm_kzalloc(&pdev->dev, sizeof(*gd), GFP_KERNEL);
	if (!gd)
		return -ENOMEM;

	gd->dev = &pdev->dev;
	mutex_init(&gd->lock);
	init_completion(&gd->ack);
	platform_set_drvdata(pdev, gd);

	for_each_available_child_of_node(pdev->dev.of_node, node)
		gd->bus_count++;
	if (!gd->bus_count) {
		dev_err(&pdev->dev, "SPMI bus child nodes missing\n");
		return -ENODEV;
	}

	gd->gctrl = devm_kcalloc(&pdev->dev, gd->bus_count, sizeof(*gd->gctrl),
				GFP_KERNEL);
	if (!gd->gctrl)
		return -ENOMEM;

	client_data.id = MSG_OWNER_REG_DUMP;
	client_data.name = "spmi_register_debug";
	client_data.msg_cb = spmi_glink_callback;
	client_data.priv = gd;

	gd->client = pmic_glink_register_client(&pdev->dev, &client_data);
	if (IS_ERR(gd->client)) {
		ret = PTR_ERR(gd->client);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Error registering with pmic_glink, ret=%d\n",
				ret);
		return ret;
	}

	i = 0;
	for_each_available_child_of_node(pdev->dev.of_node, node) {
		ctrl = spmi_controller_alloc(&pdev->dev, sizeof(*gctrl));
		if (!ctrl) {
			ret = -ENOMEM;
			of_node_put(node);
			goto err_remove_ctrl;
		}

		gctrl = spmi_controller_get_drvdata(ctrl);
		gctrl->ctrl = ctrl;
		gctrl->gd = gd;
		ret = of_property_read_u32(node, "reg", &gctrl->bus_id);
		if (ret) {
			dev_err(&pdev->dev, "Could not find reg property, ret=%d\n",
				ret);
			spmi_controller_put(ctrl);
			of_node_put(node);
			goto err_remove_ctrl;
		}

		ctrl->cmd = spmi_glink_cmd;
		ctrl->read_cmd = spmi_glink_read_cmd;
		ctrl->write_cmd = spmi_glink_write_cmd;
		ctrl->dev.of_node = node;

		ret = spmi_controller_add(ctrl);
		if (ret) {
			spmi_controller_put(ctrl);
			of_node_put(node);
			goto err_remove_ctrl;
		}

		gd->gctrl[i++] = gctrl;
	}

	return 0;

err_remove_ctrl:
	spmi_glink_remove(pdev);

	return ret;
}

static const struct of_device_id spmi_glink_match_table[] = {
	{ .compatible = "qcom,spmi-glink-debug", },
	{},
};
MODULE_DEVICE_TABLE(of, spmi_glink_match_table);

static struct platform_driver spmi_glink_driver = {
	.driver = {
		.name = "spmi_glink",
		.of_match_table = spmi_glink_match_table,
	},
	.probe = spmi_glink_probe,
	.remove = spmi_glink_remove,
};
module_platform_driver(spmi_glink_driver);

MODULE_DESCRIPTION("SPMI Glink Debug Driver");
MODULE_LICENSE("GPL v2");
