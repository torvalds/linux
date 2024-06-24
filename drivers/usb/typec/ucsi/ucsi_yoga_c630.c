// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024, Linaro Ltd
 * Authors:
 *  Bjorn Andersson
 *  Dmitry Baryshkov
 */
#include <linux/auxiliary_bus.h>
#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/string.h>
#include <linux/platform_data/lenovo-yoga-c630.h>

#include "ucsi.h"

struct yoga_c630_ucsi {
	struct yoga_c630_ec *ec;
	struct ucsi *ucsi;
	struct notifier_block nb;
	struct completion complete;
	unsigned long flags;
#define UCSI_C630_COMMAND_PENDING	0
#define UCSI_C630_ACK_PENDING		1
	u16 version;
};

static int yoga_c630_ucsi_read(struct ucsi *ucsi, unsigned int offset,
			       void *val, size_t val_len)
{
	struct yoga_c630_ucsi *uec = ucsi_get_drvdata(ucsi);
	u8 buf[YOGA_C630_UCSI_READ_SIZE];
	int ret;

	ret = yoga_c630_ec_ucsi_read(uec->ec, buf);
	if (ret)
		return ret;

	if (offset == UCSI_VERSION) {
		memcpy(val, &uec->version, min(val_len, sizeof(uec->version)));
		return 0;
	}

	switch (offset) {
	case UCSI_CCI:
		memcpy(val, buf, min(val_len, YOGA_C630_UCSI_CCI_SIZE));
		return 0;
	case UCSI_MESSAGE_IN:
		memcpy(val, buf + YOGA_C630_UCSI_CCI_SIZE,
		       min(val_len, YOGA_C630_UCSI_DATA_SIZE));
		return 0;
	default:
		return -EINVAL;
	}
}

static int yoga_c630_ucsi_async_write(struct ucsi *ucsi, unsigned int offset,
				      const void *val, size_t val_len)
{
	struct yoga_c630_ucsi *uec = ucsi_get_drvdata(ucsi);

	if (offset != UCSI_CONTROL ||
	    val_len != YOGA_C630_UCSI_WRITE_SIZE)
		return -EINVAL;

	return yoga_c630_ec_ucsi_write(uec->ec, val);
}

static int yoga_c630_ucsi_sync_write(struct ucsi *ucsi, unsigned int offset,
				     const void *val, size_t val_len)
{
	struct yoga_c630_ucsi *uec = ucsi_get_drvdata(ucsi);
	bool ack = UCSI_COMMAND(*(u64 *)val) == UCSI_ACK_CC_CI;
	int ret;

	if (ack)
		set_bit(UCSI_C630_ACK_PENDING, &uec->flags);
	else
		set_bit(UCSI_C630_COMMAND_PENDING, &uec->flags);

	reinit_completion(&uec->complete);

	ret = yoga_c630_ucsi_async_write(ucsi, offset, val, val_len);
	if (ret)
		goto out_clear_bit;

	if (!wait_for_completion_timeout(&uec->complete, 5 * HZ))
		ret = -ETIMEDOUT;

out_clear_bit:
	if (ack)
		clear_bit(UCSI_C630_ACK_PENDING, &uec->flags);
	else
		clear_bit(UCSI_C630_COMMAND_PENDING, &uec->flags);

	return ret;
}

const struct ucsi_operations yoga_c630_ucsi_ops = {
	.read = yoga_c630_ucsi_read,
	.sync_write = yoga_c630_ucsi_sync_write,
	.async_write = yoga_c630_ucsi_async_write,
};

static void yoga_c630_ucsi_notify_ucsi(struct yoga_c630_ucsi *uec, u32 cci)
{
	if (UCSI_CCI_CONNECTOR(cci))
		ucsi_connector_change(uec->ucsi, UCSI_CCI_CONNECTOR(cci));

	if (cci & UCSI_CCI_ACK_COMPLETE &&
	    test_bit(UCSI_C630_ACK_PENDING, &uec->flags))
		complete(&uec->complete);

	if (cci & UCSI_CCI_COMMAND_COMPLETE &&
	    test_bit(UCSI_C630_COMMAND_PENDING, &uec->flags))
		complete(&uec->complete);
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
		ucsi_connector_change(uec->ucsi, 1);
		return NOTIFY_OK;

	case LENOVO_EC_EVENT_UCSI:
		ret = uec->ucsi->ops->read(uec->ucsi, UCSI_CCI, &cci, sizeof(cci));
		if (ret)
			return NOTIFY_DONE;

		yoga_c630_ucsi_notify_ucsi(uec, cci);

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
	init_completion(&uec->complete);
	uec->nb.notifier_call = yoga_c630_ucsi_notify;

	uec->ucsi = ucsi_create(&adev->dev, &yoga_c630_ucsi_ops);
	if (IS_ERR(uec->ucsi))
		return PTR_ERR(uec->ucsi);

	ucsi_set_drvdata(uec->ucsi, uec);

	uec->version = yoga_c630_ec_ucsi_get_version(uec->ec);

	auxiliary_set_drvdata(adev, uec);

	ret = yoga_c630_ec_register_notify(ec, &uec->nb);
	if (ret)
		return ret;

	return ucsi_register(uec->ucsi);
}

static void yoga_c630_ucsi_remove(struct auxiliary_device *adev)
{
	struct yoga_c630_ucsi *uec = auxiliary_get_drvdata(adev);

	yoga_c630_ec_unregister_notify(uec->ec, &uec->nb);
	ucsi_unregister(uec->ucsi);
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
