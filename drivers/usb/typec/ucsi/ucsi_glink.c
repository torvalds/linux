// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023, Linaro Ltd
 */
#include <linux/auxiliary_bus.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/soc/qcom/pdr.h>
#include <linux/usb/typec_mux.h>
#include <linux/gpio/consumer.h>
#include <linux/soc/qcom/pmic_glink.h>
#include "ucsi.h"

#define PMIC_GLINK_MAX_PORTS	2

#define UCSI_BUF_SIZE                   48

#define MSG_TYPE_REQ_RESP               1
#define UCSI_BUF_SIZE                   48

#define UC_NOTIFY_RECEIVER_UCSI         0x0
#define UC_UCSI_READ_BUF_REQ            0x11
#define UC_UCSI_WRITE_BUF_REQ           0x12
#define UC_UCSI_USBC_NOTIFY_IND         0x13

struct ucsi_read_buf_req_msg {
	struct pmic_glink_hdr   hdr;
};

struct ucsi_read_buf_resp_msg {
	struct pmic_glink_hdr   hdr;
	u8                      buf[UCSI_BUF_SIZE];
	u32                     ret_code;
};

struct ucsi_write_buf_req_msg {
	struct pmic_glink_hdr   hdr;
	u8                      buf[UCSI_BUF_SIZE];
	u32                     reserved;
};

struct ucsi_write_buf_resp_msg {
	struct pmic_glink_hdr   hdr;
	u32                     ret_code;
};

struct ucsi_notify_ind_msg {
	struct pmic_glink_hdr   hdr;
	u32                     notification;
	u32                     receiver;
	u32                     reserved;
};

struct pmic_glink_ucsi {
	struct device *dev;

	struct gpio_desc *port_orientation[PMIC_GLINK_MAX_PORTS];
	struct typec_switch *port_switch[PMIC_GLINK_MAX_PORTS];

	struct pmic_glink_client *client;

	struct ucsi *ucsi;
	struct completion read_ack;
	struct completion write_ack;
	struct completion sync_ack;
	bool sync_pending;
	struct mutex lock;	/* protects concurrent access to PMIC Glink interface */

	int sync_val;

	struct work_struct notify_work;
	struct work_struct register_work;

	u8 read_buf[UCSI_BUF_SIZE];
};

static int pmic_glink_ucsi_read(struct ucsi *__ucsi, unsigned int offset,
				void *val, size_t val_len)
{
	struct pmic_glink_ucsi *ucsi = ucsi_get_drvdata(__ucsi);
	struct ucsi_read_buf_req_msg req = {};
	unsigned long left;
	int ret;

	req.hdr.owner = PMIC_GLINK_OWNER_USBC;
	req.hdr.type = MSG_TYPE_REQ_RESP;
	req.hdr.opcode = UC_UCSI_READ_BUF_REQ;

	mutex_lock(&ucsi->lock);
	memset(ucsi->read_buf, 0, sizeof(ucsi->read_buf));
	reinit_completion(&ucsi->read_ack);

	ret = pmic_glink_send(ucsi->client, &req, sizeof(req));
	if (ret < 0) {
		dev_err(ucsi->dev, "failed to send UCSI read request: %d\n", ret);
		goto out_unlock;
	}

	left = wait_for_completion_timeout(&ucsi->read_ack, 5 * HZ);
	if (!left) {
		dev_err(ucsi->dev, "timeout waiting for UCSI read response\n");
		ret = -ETIMEDOUT;
		goto out_unlock;
	}

	memcpy(val, &ucsi->read_buf[offset], val_len);
	ret = 0;

out_unlock:
	mutex_unlock(&ucsi->lock);

	return ret;
}

static int pmic_glink_ucsi_locked_write(struct pmic_glink_ucsi *ucsi, unsigned int offset,
					const void *val, size_t val_len)
{
	struct ucsi_write_buf_req_msg req = {};
	unsigned long left;
	int ret;

	req.hdr.owner = PMIC_GLINK_OWNER_USBC;
	req.hdr.type = MSG_TYPE_REQ_RESP;
	req.hdr.opcode = UC_UCSI_WRITE_BUF_REQ;
	memcpy(&req.buf[offset], val, val_len);

	reinit_completion(&ucsi->write_ack);

	ret = pmic_glink_send(ucsi->client, &req, sizeof(req));
	if (ret < 0) {
		dev_err(ucsi->dev, "failed to send UCSI write request: %d\n", ret);
		return ret;
	}

	left = wait_for_completion_timeout(&ucsi->write_ack, 5 * HZ);
	if (!left) {
		dev_err(ucsi->dev, "timeout waiting for UCSI write response\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int pmic_glink_ucsi_async_write(struct ucsi *__ucsi, unsigned int offset,
				       const void *val, size_t val_len)
{
	struct pmic_glink_ucsi *ucsi = ucsi_get_drvdata(__ucsi);
	int ret;

	mutex_lock(&ucsi->lock);
	ret = pmic_glink_ucsi_locked_write(ucsi, offset, val, val_len);
	mutex_unlock(&ucsi->lock);

	return ret;
}

static int pmic_glink_ucsi_sync_write(struct ucsi *__ucsi, unsigned int offset,
				      const void *val, size_t val_len)
{
	struct pmic_glink_ucsi *ucsi = ucsi_get_drvdata(__ucsi);
	unsigned long left;
	int ret;

	/* TOFIX: Downstream forces recipient to CON when UCSI_GET_ALTERNATE_MODES command */

	mutex_lock(&ucsi->lock);
	ucsi->sync_val = 0;
	reinit_completion(&ucsi->sync_ack);
	ucsi->sync_pending = true;
	ret = pmic_glink_ucsi_locked_write(ucsi, offset, val, val_len);
	mutex_unlock(&ucsi->lock);

	left = wait_for_completion_timeout(&ucsi->sync_ack, 5 * HZ);
	if (!left) {
		dev_err(ucsi->dev, "timeout waiting for UCSI sync write response\n");
		ret = -ETIMEDOUT;
	} else if (ucsi->sync_val) {
		dev_err(ucsi->dev, "sync write returned: %d\n", ucsi->sync_val);
	}

	ucsi->sync_pending = false;

	return ret;
}

static const struct ucsi_operations pmic_glink_ucsi_ops = {
	.read = pmic_glink_ucsi_read,
	.sync_write = pmic_glink_ucsi_sync_write,
	.async_write = pmic_glink_ucsi_async_write
};

static void pmic_glink_ucsi_read_ack(struct pmic_glink_ucsi *ucsi, const void *data, int len)
{
	const struct ucsi_read_buf_resp_msg *resp = data;

	if (resp->ret_code)
		return;

	memcpy(ucsi->read_buf, resp->buf, UCSI_BUF_SIZE);
	complete(&ucsi->read_ack);
}

static void pmic_glink_ucsi_write_ack(struct pmic_glink_ucsi *ucsi, const void *data, int len)
{
	const struct ucsi_write_buf_resp_msg *resp = data;

	if (resp->ret_code)
		return;

	ucsi->sync_val = resp->ret_code;
	complete(&ucsi->write_ack);
}

static void pmic_glink_ucsi_notify(struct work_struct *work)
{
	struct pmic_glink_ucsi *ucsi = container_of(work, struct pmic_glink_ucsi, notify_work);
	unsigned int con_num;
	u32 cci;
	int ret;

	ret = pmic_glink_ucsi_read(ucsi->ucsi, UCSI_CCI, &cci, sizeof(cci));
	if (ret) {
		dev_err(ucsi->dev, "failed to read CCI on notification\n");
		return;
	}

	con_num = UCSI_CCI_CONNECTOR(cci);
	if (con_num) {
		if (con_num <= PMIC_GLINK_MAX_PORTS &&
		    ucsi->port_orientation[con_num - 1]) {
			int orientation = gpiod_get_value(ucsi->port_orientation[con_num - 1]);

			if (orientation >= 0) {
				typec_switch_set(ucsi->port_switch[con_num - 1],
						 orientation ? TYPEC_ORIENTATION_REVERSE
							     : TYPEC_ORIENTATION_NORMAL);
			}
		}

		ucsi_connector_change(ucsi->ucsi, con_num);
	}

	if (ucsi->sync_pending && cci & UCSI_CCI_BUSY) {
		ucsi->sync_val = -EBUSY;
		complete(&ucsi->sync_ack);
	} else if (ucsi->sync_pending &&
		   (cci & (UCSI_CCI_ACK_COMPLETE | UCSI_CCI_COMMAND_COMPLETE))) {
		complete(&ucsi->sync_ack);
	}
}

static void pmic_glink_ucsi_register(struct work_struct *work)
{
	struct pmic_glink_ucsi *ucsi = container_of(work, struct pmic_glink_ucsi, register_work);

	ucsi_register(ucsi->ucsi);
}

static void pmic_glink_ucsi_callback(const void *data, size_t len, void *priv)
{
	struct pmic_glink_ucsi *ucsi = priv;
	const struct pmic_glink_hdr *hdr = data;

	switch (le32_to_cpu(hdr->opcode)) {
	case UC_UCSI_READ_BUF_REQ:
		pmic_glink_ucsi_read_ack(ucsi, data, len);
		break;
	case UC_UCSI_WRITE_BUF_REQ:
		pmic_glink_ucsi_write_ack(ucsi, data, len);
		break;
	case UC_UCSI_USBC_NOTIFY_IND:
		schedule_work(&ucsi->notify_work);
		break;
	};
}

static void pmic_glink_ucsi_pdr_notify(void *priv, int state)
{
	struct pmic_glink_ucsi *ucsi = priv;

	if (state == SERVREG_SERVICE_STATE_UP)
		schedule_work(&ucsi->register_work);
	else if (state == SERVREG_SERVICE_STATE_DOWN)
		ucsi_unregister(ucsi->ucsi);
}

static void pmic_glink_ucsi_destroy(void *data)
{
	struct pmic_glink_ucsi *ucsi = data;

	/* Protect to make sure we're not in a middle of a transaction from a glink callback */
	mutex_lock(&ucsi->lock);
	ucsi_destroy(ucsi->ucsi);
	mutex_unlock(&ucsi->lock);
}

static const struct of_device_id pmic_glink_ucsi_of_quirks[] = {
	{ .compatible = "qcom,qcm6490-pmic-glink", .data = (void *)UCSI_NO_PARTNER_PDOS, },
	{ .compatible = "qcom,sc8180x-pmic-glink", .data = (void *)UCSI_NO_PARTNER_PDOS, },
	{ .compatible = "qcom,sc8280xp-pmic-glink", .data = (void *)UCSI_NO_PARTNER_PDOS, },
	{ .compatible = "qcom,sm8350-pmic-glink", .data = (void *)UCSI_NO_PARTNER_PDOS, },
	{ .compatible = "qcom,sm8550-pmic-glink", .data = (void *)UCSI_NO_PARTNER_PDOS, },
	{}
};

static int pmic_glink_ucsi_probe(struct auxiliary_device *adev,
				 const struct auxiliary_device_id *id)
{
	struct pmic_glink_ucsi *ucsi;
	struct device *dev = &adev->dev;
	const struct of_device_id *match;
	struct fwnode_handle *fwnode;
	int ret;

	ucsi = devm_kzalloc(dev, sizeof(*ucsi), GFP_KERNEL);
	if (!ucsi)
		return -ENOMEM;

	ucsi->dev = dev;
	dev_set_drvdata(dev, ucsi);

	INIT_WORK(&ucsi->notify_work, pmic_glink_ucsi_notify);
	INIT_WORK(&ucsi->register_work, pmic_glink_ucsi_register);
	init_completion(&ucsi->read_ack);
	init_completion(&ucsi->write_ack);
	init_completion(&ucsi->sync_ack);
	mutex_init(&ucsi->lock);

	ucsi->ucsi = ucsi_create(dev, &pmic_glink_ucsi_ops);
	if (IS_ERR(ucsi->ucsi))
		return PTR_ERR(ucsi->ucsi);

	/* Make sure we destroy *after* pmic_glink unregister */
	ret = devm_add_action_or_reset(dev, pmic_glink_ucsi_destroy, ucsi);
	if (ret)
		return ret;

	match = of_match_device(pmic_glink_ucsi_of_quirks, dev->parent);
	if (match)
		ucsi->ucsi->quirks = (unsigned long)match->data;

	ucsi_set_drvdata(ucsi->ucsi, ucsi);

	device_for_each_child_node(dev, fwnode) {
		struct gpio_desc *desc;
		u32 port;

		ret = fwnode_property_read_u32(fwnode, "reg", &port);
		if (ret < 0) {
			dev_err(dev, "missing reg property of %pOFn\n", fwnode);
			return ret;
		}

		if (port >= PMIC_GLINK_MAX_PORTS) {
			dev_warn(dev, "invalid connector number, ignoring\n");
			continue;
		}

		desc = devm_gpiod_get_index_optional(&adev->dev, "orientation", port, GPIOD_IN);

		/* If GPIO isn't found, continue */
		if (!desc)
			continue;

		if (IS_ERR(desc))
			return dev_err_probe(dev, PTR_ERR(desc),
					     "unable to acquire orientation gpio\n");
		ucsi->port_orientation[port] = desc;

		ucsi->port_switch[port] = fwnode_typec_switch_get(fwnode);
		if (IS_ERR(ucsi->port_switch[port]))
			return dev_err_probe(dev, PTR_ERR(ucsi->port_switch[port]),
					"failed to acquire orientation-switch\n");
	}

	ucsi->client = devm_pmic_glink_register_client(dev,
						       PMIC_GLINK_OWNER_USBC,
						       pmic_glink_ucsi_callback,
						       pmic_glink_ucsi_pdr_notify,
						       ucsi);
	return PTR_ERR_OR_ZERO(ucsi->client);
}

static void pmic_glink_ucsi_remove(struct auxiliary_device *adev)
{
	struct pmic_glink_ucsi *ucsi = dev_get_drvdata(&adev->dev);

	/* Unregister first to stop having read & writes */
	ucsi_unregister(ucsi->ucsi);
}

static const struct auxiliary_device_id pmic_glink_ucsi_id_table[] = {
	{ .name = "pmic_glink.ucsi", },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, pmic_glink_ucsi_id_table);

static struct auxiliary_driver pmic_glink_ucsi_driver = {
	.name = "pmic_glink_ucsi",
	.probe = pmic_glink_ucsi_probe,
	.remove = pmic_glink_ucsi_remove,
	.id_table = pmic_glink_ucsi_id_table,
};

module_auxiliary_driver(pmic_glink_ucsi_driver);

MODULE_DESCRIPTION("Qualcomm PMIC GLINK UCSI driver");
MODULE_LICENSE("GPL");
