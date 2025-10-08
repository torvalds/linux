// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024, Linaro Ltd
 * Authors:
 *  Bjorn Andersson
 *  Dmitry Baryshkov
 */
#include <linux/auxiliary_bus.h>
#include <linux/bitops.h>
#include <linux/bitfield.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/string.h>
#include <linux/platform_data/lenovo-yoga-c630.h>
#include <linux/usb/typec_dp.h>

#include <drm/bridge/aux-bridge.h>

#include "ucsi.h"

#define LENOVO_EC_USB_MUX	0x08

#define USB_MUX_MUXC	GENMASK(1, 0)
#define USB_MUX_CCST	GENMASK(3, 2)
#define USB_MUX_DPPN	GENMASK(7, 4)
#define USB_MUX_HPDS	BIT(8)
#define USB_MUX_HSFL	GENMASK(11, 9)

struct yoga_c630_ucsi {
	struct yoga_c630_ec *ec;
	struct ucsi *ucsi;
	struct auxiliary_device *bridge;
	struct notifier_block nb;
	u16 version;
};

static int yoga_c630_ucsi_read_version(struct ucsi *ucsi, u16 *version)
{
	struct yoga_c630_ucsi *uec = ucsi_get_drvdata(ucsi);

	*version = uec->version;

	return 0;
}

static int yoga_c630_ucsi_read_cci(struct ucsi *ucsi, u32 *cci)
{
	struct yoga_c630_ucsi *uec = ucsi_get_drvdata(ucsi);
	u8 buf[YOGA_C630_UCSI_READ_SIZE];
	int ret;

	ret = yoga_c630_ec_ucsi_read(uec->ec, buf);
	if (ret)
		return ret;

	memcpy(cci, buf, sizeof(*cci));

	return 0;
}

static int yoga_c630_ucsi_read_message_in(struct ucsi *ucsi,
					  void *val, size_t val_len)
{
	struct yoga_c630_ucsi *uec = ucsi_get_drvdata(ucsi);
	u8 buf[YOGA_C630_UCSI_READ_SIZE];
	int ret;

	ret = yoga_c630_ec_ucsi_read(uec->ec, buf);
	if (ret)
		return ret;

	memcpy(val, buf + YOGA_C630_UCSI_CCI_SIZE,
	       min(val_len, YOGA_C630_UCSI_DATA_SIZE));

	return 0;
}

static int yoga_c630_ucsi_async_control(struct ucsi *ucsi, u64 command)
{
	struct yoga_c630_ucsi *uec = ucsi_get_drvdata(ucsi);

	return yoga_c630_ec_ucsi_write(uec->ec, (u8*)&command);
}

static int yoga_c630_ucsi_sync_control(struct ucsi *ucsi,
				       u64 command,
				       u32 *cci,
				       void *data, size_t size)
{
	int ret;

	/*
	 * EC doesn't return connector's DP mode even though it is supported.
	 * Fake it.
	 */
	if (UCSI_COMMAND(command) == UCSI_GET_ALTERNATE_MODES &&
	    UCSI_GET_ALTMODE_GET_CONNECTOR_NUMBER(command) == 1 &&
	    UCSI_ALTMODE_RECIPIENT(command) == UCSI_RECIPIENT_CON &&
	    UCSI_ALTMODE_OFFSET(command) == 0) {
		static const struct ucsi_altmode alt = {
			.svid = USB_TYPEC_DP_SID,
			.mid = USB_TYPEC_DP_MODE,
		};

		dev_dbg(ucsi->dev, "faking DP altmode for con1\n");
		memset(data, 0, size);
		memcpy(data, &alt, min(sizeof(alt), size));
		*cci = UCSI_CCI_COMMAND_COMPLETE | UCSI_SET_CCI_LENGTH(sizeof(alt));
		return 0;
	}

	/*
	 * EC can return AltModes present on CON1 (port0, right) for CON2
	 * (port1, left) too. Ignore all requests going to CON2 (it doesn't
	 * support DP anyway).
	 */
	if (UCSI_COMMAND(command) == UCSI_GET_ALTERNATE_MODES &&
	    UCSI_GET_ALTMODE_GET_CONNECTOR_NUMBER(command) == 2) {
		dev_dbg(ucsi->dev, "ignoring altmodes for con2\n");
		memset(data, 0, size);
		*cci = UCSI_CCI_COMMAND_COMPLETE;
		return 0;
	}

	ret = ucsi_sync_control_common(ucsi, command, cci, data, size);
	if (ret < 0)
		return ret;

	/* UCSI_GET_CURRENT_CAM is off-by-one on all ports */
	if (UCSI_COMMAND(command) == UCSI_GET_CURRENT_CAM && data)
		((u8 *)data)[0]--;

	return ret;
}

static bool yoga_c630_ucsi_update_altmodes(struct ucsi *ucsi,
					   u8 recipient,
					   struct ucsi_altmode *orig,
					   struct ucsi_altmode *updated)
{
	int i;

	if (orig[0].svid == 0 || recipient != UCSI_RECIPIENT_SOP)
		return false;

	/* EC is nice and repeats altmodes again and again. Ignore copies. */
	for (i = 1; i < UCSI_MAX_ALTMODES; i++) {
		if (orig[i].svid == orig[0].svid) {
			dev_dbg(ucsi->dev, "Found duplicate altmodes, starting from %d\n", i);
			memset(&orig[i], 0, (UCSI_MAX_ALTMODES - i) * sizeof(*orig));
			break;
		}
	}

	return false;
}

static void yoga_c630_ucsi_update_connector(struct ucsi_connector *con)
{
	if (con->num == 1)
		con->typec_cap.orientation_aware = true;
}

static const struct ucsi_operations yoga_c630_ucsi_ops = {
	.read_version = yoga_c630_ucsi_read_version,
	.read_cci = yoga_c630_ucsi_read_cci,
	.poll_cci = yoga_c630_ucsi_read_cci,
	.read_message_in = yoga_c630_ucsi_read_message_in,
	.sync_control = yoga_c630_ucsi_sync_control,
	.async_control = yoga_c630_ucsi_async_control,
	.update_altmodes = yoga_c630_ucsi_update_altmodes,
	.update_connector = yoga_c630_ucsi_update_connector,
};

static void yoga_c630_ucsi_read_port0_status(struct yoga_c630_ucsi *uec)
{
	int val;
	unsigned int muxc, ccst, dppn, hpds, hsfl;

	val = yoga_c630_ec_read16(uec->ec, LENOVO_EC_USB_MUX);

	muxc = FIELD_GET(USB_MUX_MUXC, val);
	ccst = FIELD_GET(USB_MUX_CCST, val);
	dppn = FIELD_GET(USB_MUX_DPPN, val);
	hpds = FIELD_GET(USB_MUX_HPDS, val);
	hsfl = FIELD_GET(USB_MUX_HSFL, val);

	dev_dbg(uec->ucsi->dev, " mux %04x (muxc %d ccst %d dppn %d hpds %d hsfl %d)\n",
		val,
		muxc, ccst, dppn, hpds, hsfl);

	if (uec->ucsi->connector && uec->ucsi->connector[0].port)
		typec_set_orientation(uec->ucsi->connector[0].port,
				      ccst == 1 ?
				      TYPEC_ORIENTATION_REVERSE :
				      TYPEC_ORIENTATION_NORMAL);

	if (uec->bridge)
		drm_aux_hpd_bridge_notify(&uec->bridge->dev,
					  dppn != 0 ?
					  connector_status_connected :
					  connector_status_disconnected);

}

static int yoga_c630_ucsi_notify(struct notifier_block *nb,
				 unsigned long action, void *data)
{
	struct yoga_c630_ucsi *uec = container_of(nb, struct yoga_c630_ucsi, nb);
	u32 cci;
	int ret;

	switch (action) {
	case LENOVO_EC_EVENT_USB:
	case LENOVO_EC_EVENT_HPD:
		yoga_c630_ucsi_read_port0_status(uec);
		ucsi_connector_change(uec->ucsi, 1);
		return NOTIFY_OK;

	case LENOVO_EC_EVENT_UCSI:
		ret = uec->ucsi->ops->read_cci(uec->ucsi, &cci);
		if (ret)
			return NOTIFY_DONE;

		ucsi_notify_common(uec->ucsi, cci);

		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}

static int yoga_c630_ucsi_probe(struct auxiliary_device *adev,
				const struct auxiliary_device_id *id)
{
	struct yoga_c630_ec *ec = adev->dev.platform_data;
	struct yoga_c630_ucsi *uec;
	int ret;

	uec = devm_kzalloc(&adev->dev, sizeof(*uec), GFP_KERNEL);
	if (!uec)
		return -ENOMEM;

	uec->ec = ec;
	uec->nb.notifier_call = yoga_c630_ucsi_notify;

	device_for_each_child_node_scoped(&adev->dev, fwnode) {
		u32 port;

		ret = fwnode_property_read_u32(fwnode, "reg", &port);
		if (ret < 0) {
			dev_err(&adev->dev, "missing reg property of %pfwP\n", fwnode);
			return ret;
		}

		/* DP is only on port0 */
		if (port != 0)
			continue;

		uec->bridge = devm_drm_dp_hpd_bridge_alloc(&adev->dev, to_of_node(fwnode));
		if (IS_ERR(uec->bridge))
			return PTR_ERR(uec->bridge);
	}

	uec->ucsi = ucsi_create(&adev->dev, &yoga_c630_ucsi_ops);
	if (IS_ERR(uec->ucsi))
		return PTR_ERR(uec->ucsi);

	ucsi_set_drvdata(uec->ucsi, uec);

	uec->version = yoga_c630_ec_ucsi_get_version(uec->ec);

	auxiliary_set_drvdata(adev, uec);

	ret = yoga_c630_ec_register_notify(ec, &uec->nb);
	if (ret)
		goto err_destroy;

	ret = ucsi_register(uec->ucsi);
	if (ret)
		goto err_unregister;

	if (uec->bridge) {
		ret = devm_drm_dp_hpd_bridge_add(&adev->dev, uec->bridge);
		if (ret)
			goto err_ucsi_unregister;
	}

	return 0;

err_ucsi_unregister:
	ucsi_unregister(uec->ucsi);

err_unregister:
	yoga_c630_ec_unregister_notify(uec->ec, &uec->nb);

err_destroy:
	ucsi_destroy(uec->ucsi);

	return ret;
}

static void yoga_c630_ucsi_remove(struct auxiliary_device *adev)
{
	struct yoga_c630_ucsi *uec = auxiliary_get_drvdata(adev);

	ucsi_unregister(uec->ucsi);
	yoga_c630_ec_unregister_notify(uec->ec, &uec->nb);
	ucsi_destroy(uec->ucsi);
}

static const struct auxiliary_device_id yoga_c630_ucsi_id_table[] = {
	{ .name = YOGA_C630_MOD_NAME "." YOGA_C630_DEV_UCSI, },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, yoga_c630_ucsi_id_table);

static struct auxiliary_driver yoga_c630_ucsi_driver = {
	.name = YOGA_C630_DEV_UCSI,
	.id_table = yoga_c630_ucsi_id_table,
	.probe = yoga_c630_ucsi_probe,
	.remove = yoga_c630_ucsi_remove,
};

module_auxiliary_driver(yoga_c630_ucsi_driver);

MODULE_DESCRIPTION("Lenovo Yoga C630 UCSI");
MODULE_LICENSE("GPL");
