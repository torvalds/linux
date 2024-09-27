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

#define PMIC_GLINK_MAX_PORTS		3

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

	struct pmic_glink_client *client;

	struct ucsi *ucsi;
	struct completion read_ack;
	struct completion write_ack;
	struct mutex lock;	/* protects concurrent access to PMIC Glink interface */

	struct work_struct notify_work;
	struct work_struct register_work;
	spinlock_t state_lock;
	bool ucsi_registered;
	bool pd_running;

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

static int pmic_glink_ucsi_read_version(struct ucsi *ucsi, u16 *version)
{
	return pmic_glink_ucsi_read(ucsi, UCSI_VERSION, version, sizeof(*version));
}

static int pmic_glink_ucsi_read_cci(struct ucsi *ucsi, u32 *cci)
{
	return pmic_glink_ucsi_read(ucsi, UCSI_CCI, cci, sizeof(*cci));
}

static int pmic_glink_ucsi_read_message_in(struct ucsi *ucsi, void *val, size_t val_len)
{
	return pmic_glink_ucsi_read(ucsi, UCSI_MESSAGE_IN, val, val_len);
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

static int pmic_glink_ucsi_async_control(struct ucsi *__ucsi, u64 command)
{
	struct pmic_glink_ucsi *ucsi = ucsi_get_drvdata(__ucsi);
	int ret;

	mutex_lock(&ucsi->lock);
	ret = pmic_glink_ucsi_locked_write(ucsi, UCSI_CONTROL, &command, sizeof(command));
	mutex_unlock(&ucsi->lock);

	return ret;
}

static void pmic_glink_ucsi_update_connector(struct ucsi_connector *con)
{
	struct pmic_glink_ucsi *ucsi = ucsi_get_drvdata(con->ucsi);
	int i;

	for (i = 0; i < PMIC_GLINK_MAX_PORTS; i++) {
		if (ucsi->port_orientation[i])
			con->typec_cap.orientation_aware = true;
	}
}

static void pmic_glink_ucsi_connector_status(struct ucsi_connector *con)
{
	struct pmic_glink_ucsi *ucsi = ucsi_get_drvdata(con->ucsi);
	int orientation;

	if (con->num >= PMIC_GLINK_MAX_PORTS ||
	    !ucsi->port_orientation[con->num - 1])
		return;

	orientation = gpiod_get_value(ucsi->port_orientation[con->num - 1]);
	if (orientation >= 0) {
		typec_set_orientation(con->port,
				      orientation ?
				      TYPEC_ORIENTATION_REVERSE :
				      TYPEC_ORIENTATION_NORMAL);
	}
}

static const struct ucsi_operations pmic_glink_ucsi_ops = {
	.read_version = pmic_glink_ucsi_read_version,
	.read_cci = pmic_glink_ucsi_read_cci,
	.read_message_in = pmic_glink_ucsi_read_message_in,
	.sync_control = ucsi_sync_control_common,
	.async_control = pmic_glink_ucsi_async_control,
	.update_connector = pmic_glink_ucsi_update_connector,
	.connector_status = pmic_glink_ucsi_connector_status,
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

	complete(&ucsi->write_ack);
}

static void pmic_glink_ucsi_notify(struct work_struct *work)
{
	struct pmic_glink_ucsi *ucsi = container_of(work, struct pmic_glink_ucsi, notify_work);
	u32 cci;
	int ret;

	ret = pmic_glink_ucsi_read(ucsi->ucsi, UCSI_CCI, &cci, sizeof(cci));
	if (ret) {
		dev_err(ucsi->dev, "failed to read CCI on notification\n");
		return;
	}

	ucsi_notify_common(ucsi->ucsi, cci);
}

static void pmic_glink_ucsi_register(struct work_struct *work)
{
	struct pmic_glink_ucsi *ucsi = container_of(work, struct pmic_glink_ucsi, register_work);
	unsigned long flags;
	bool pd_running;

	spin_lock_irqsave(&ucsi->state_lock, flags);
	pd_running = ucsi->pd_running;
	spin_unlock_irqrestore(&ucsi->state_lock, flags);

	if (!ucsi->ucsi_registered && pd_running) {
		ucsi_register(ucsi->ucsi);
		ucsi->ucsi_registered = true;
	} else if (ucsi->ucsi_registered && !pd_running) {
		ucsi_unregister(ucsi->ucsi);
		ucsi->ucsi_registered = false;
	}
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
	}
}

static void pmic_glink_ucsi_pdr_notify(void *priv, int state)
{
	struct pmic_glink_ucsi *ucsi = priv;
	unsigned long flags;

	spin_lock_irqsave(&ucsi->state_lock, flags);
	ucsi->pd_running = (state == SERVREG_SERVICE_STATE_UP);
	spin_unlock_irqrestore(&ucsi->state_lock, flags);
	schedule_work(&ucsi->register_work);
}

static void pmic_glink_ucsi_destroy(void *data)
{
	struct pmic_glink_ucsi *ucsi = data;

	/* Protect to make sure we're not in a middle of a transaction from a glink callback */
	mutex_lock(&ucsi->lock);
	ucsi_destroy(ucsi->ucsi);
	mutex_unlock(&ucsi->lock);
}

static unsigned long quirk_sc8180x = UCSI_NO_PARTNER_PDOS;
static unsigned long quirk_sc8280xp = UCSI_NO_PARTNER_PDOS | UCSI_DELAY_DEVICE_PDOS;
static unsigned long quirk_sm8450 = UCSI_DELAY_DEVICE_PDOS;

static const struct of_device_id pmic_glink_ucsi_of_quirks[] = {
	{ .compatible = "qcom,qcm6490-pmic-glink", .data = &quirk_sc8280xp, },
	{ .compatible = "qcom,sc8180x-pmic-glink", .data = &quirk_sc8180x, },
	{ .compatible = "qcom,sc8280xp-pmic-glink", .data = &quirk_sc8280xp, },
	{ .compatible = "qcom,sm8350-pmic-glink", .data = &quirk_sc8180x, },
	{ .compatible = "qcom,sm8450-pmic-glink", .data = &quirk_sm8450, },
	{ .compatible = "qcom,sm8550-pmic-glink", .data = &quirk_sm8450, },
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
	spin_lock_init(&ucsi->state_lock);
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
		ucsi->ucsi->quirks = *(unsigned long *)match->data;

	ucsi_set_drvdata(ucsi->ucsi, ucsi);

	device_for_each_child_node(dev, fwnode) {
		struct gpio_desc *desc;
		u32 port;

		ret = fwnode_property_read_u32(fwnode, "reg", &port);
		if (ret < 0) {
			dev_err(dev, "missing reg property of %pOFn\n", fwnode);
			fwnode_handle_put(fwnode);
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

		if (IS_ERR(desc)) {
			fwnode_handle_put(fwnode);
			return dev_err_probe(dev, PTR_ERR(desc),
					     "unable to acquire orientation gpio\n");
		}
		ucsi->port_orientation[port] = desc;
	}

	ucsi->client = devm_pmic_glink_client_alloc(dev, PMIC_GLINK_OWNER_USBC,
						    pmic_glink_ucsi_callback,
						    pmic_glink_ucsi_pdr_notify,
						    ucsi);
	if (IS_ERR(ucsi->client))
		return PTR_ERR(ucsi->client);

	pmic_glink_client_register(ucsi->client);

	return 0;
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
