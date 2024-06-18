// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2024, Linaro Ltd
 * Authors:
 *    Bjorn Andersson
 *    Dmitry Baryshkov
 */
#include <linux/auxiliary_bus.h>
#include <linux/cleanup.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/irqreturn.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/platform_data/lenovo-yoga-c630.h>

#define LENOVO_EC_RESPONSE_REG		0x01
#define LENOVO_EC_REQUEST_REG		0x02

#define LENOVO_EC_UCSI_WRITE		0x20
#define LENOVO_EC_UCSI_READ		0x21

#define LENOVO_EC_READ_REG		0xb0
#define LENOVO_EC_REQUEST_NEXT_EVENT	0x84

#define LENOVO_EC_UCSI_VERSION		0x20

struct yoga_c630_ec {
	struct i2c_client *client;
	struct mutex lock;
	struct blocking_notifier_head notifier_list;
};

static int yoga_c630_ec_request(struct yoga_c630_ec *ec, u8 *req, size_t req_len,
				u8 *resp, size_t resp_len)
{
	int ret;

	lockdep_assert_held(&ec->lock);

	ret = i2c_smbus_write_i2c_block_data(ec->client, LENOVO_EC_REQUEST_REG,
					     req_len, req);
	if (ret < 0)
		return ret;

	return i2c_smbus_read_i2c_block_data(ec->client, LENOVO_EC_RESPONSE_REG,
					     resp_len, resp);
}

int yoga_c630_ec_read8(struct yoga_c630_ec *ec, u8 addr)
{
	u8 req[2] = { LENOVO_EC_READ_REG, };
	int ret;
	u8 val;

	guard(mutex)(&ec->lock);

	req[1] = addr;
	ret = yoga_c630_ec_request(ec, req, sizeof(req), &val, 1);
	if (ret < 0)
		return ret;

	return val;
}
EXPORT_SYMBOL_GPL(yoga_c630_ec_read8);

int yoga_c630_ec_read16(struct yoga_c630_ec *ec, u8 addr)
{
	u8 req[2] = { LENOVO_EC_READ_REG, };
	int ret;
	u8 msb;
	u8 lsb;

	/* don't overflow the address */
	if (addr == 0xff)
		return -EINVAL;

	guard(mutex)(&ec->lock);

	req[1] = addr;
	ret = yoga_c630_ec_request(ec, req, sizeof(req), &lsb, 1);
	if (ret < 0)
		return ret;

	req[1] = addr + 1;
	ret = yoga_c630_ec_request(ec, req, sizeof(req), &msb, 1);
	if (ret < 0)
		return ret;

	return msb << 8 | lsb;
}
EXPORT_SYMBOL_GPL(yoga_c630_ec_read16);

u16 yoga_c630_ec_ucsi_get_version(struct yoga_c630_ec *ec)
{
	u8 req[3] = { 0xb3, 0xf2, };
	int ret;
	u8 msb;
	u8 lsb;

	guard(mutex)(&ec->lock);

	req[2] = LENOVO_EC_UCSI_VERSION;
	ret = yoga_c630_ec_request(ec, req, sizeof(req), &lsb, 1);
	if (ret < 0)
		return ret;

	req[2] = LENOVO_EC_UCSI_VERSION + 1;
	ret = yoga_c630_ec_request(ec, req, sizeof(req), &msb, 1);
	if (ret < 0)
		return ret;

	return msb << 8 | lsb;
}
EXPORT_SYMBOL_GPL(yoga_c630_ec_ucsi_get_version);

int yoga_c630_ec_ucsi_write(struct yoga_c630_ec *ec,
			    const u8 req[YOGA_C630_UCSI_WRITE_SIZE])
{
	int ret;

	mutex_lock(&ec->lock);
	ret = i2c_smbus_write_i2c_block_data(ec->client, LENOVO_EC_UCSI_WRITE,
					     YOGA_C630_UCSI_WRITE_SIZE, req);
	mutex_unlock(&ec->lock);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(yoga_c630_ec_ucsi_write);

int yoga_c630_ec_ucsi_read(struct yoga_c630_ec *ec,
			   u8 resp[YOGA_C630_UCSI_READ_SIZE])
{
	int ret;

	mutex_lock(&ec->lock);
	ret = i2c_smbus_read_i2c_block_data(ec->client, LENOVO_EC_UCSI_READ,
					    YOGA_C630_UCSI_READ_SIZE, resp);
	mutex_unlock(&ec->lock);

	return ret < 0 ? ret : 0;
}
EXPORT_SYMBOL_GPL(yoga_c630_ec_ucsi_read);

static irqreturn_t yoga_c630_ec_thread_intr(int irq, void *data)
{
	u8 req[] = { LENOVO_EC_REQUEST_NEXT_EVENT };
	struct yoga_c630_ec *ec = data;
	u8 event;
	int ret;

	mutex_lock(&ec->lock);
	ret = yoga_c630_ec_request(ec, req, sizeof(req), &event, 1);
	mutex_unlock(&ec->lock);
	if (ret < 0)
		return IRQ_HANDLED;

	blocking_notifier_call_chain(&ec->notifier_list, event, ec);

	return IRQ_HANDLED;
}

/**
 * yoga_c630_ec_register_notify - Register a notifier callback for EC events.
 * @ec: Yoga C630 EC
 * @nb: Notifier block pointer to register
 *
 * Return: 0 on success or negative error code.
 */
int yoga_c630_ec_register_notify(struct yoga_c630_ec *ec, struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&ec->notifier_list, nb);
}
EXPORT_SYMBOL_GPL(yoga_c630_ec_register_notify);

/**
 * yoga_c630_ec_unregister_notify - Unregister notifier callback for EC events.
 * @ec: Yoga C630 EC
 * @nb: Notifier block pointer to unregister
 *
 * Unregister a notifier callback that was previously registered with
 * yoga_c630_ec_register_notify().
 */
void yoga_c630_ec_unregister_notify(struct yoga_c630_ec *ec, struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&ec->notifier_list, nb);
}
EXPORT_SYMBOL_GPL(yoga_c630_ec_unregister_notify);

static void yoga_c630_aux_release(struct device *dev)
{
	struct auxiliary_device *adev = to_auxiliary_dev(dev);

	kfree(adev);
}

static void yoga_c630_aux_remove(void *data)
{
	struct auxiliary_device *adev = data;

	auxiliary_device_delete(adev);
	auxiliary_device_uninit(adev);
}

static int yoga_c630_aux_init(struct device *parent, const char *name,
			      struct yoga_c630_ec *ec)
{
	struct auxiliary_device *adev;
	int ret;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;

	adev->name = name;
	adev->id = 0;
	adev->dev.parent = parent;
	adev->dev.release = yoga_c630_aux_release;
	adev->dev.platform_data = ec;

	ret = auxiliary_device_init(adev);
	if (ret) {
		kfree(adev);
		return ret;
	}

	ret = auxiliary_device_add(adev);
	if (ret) {
		auxiliary_device_uninit(adev);
		return ret;
	}

	return devm_add_action_or_reset(parent, yoga_c630_aux_remove, adev);
}

static int yoga_c630_ec_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct yoga_c630_ec *ec;
	int ret;

	ec = devm_kzalloc(dev, sizeof(*ec), GFP_KERNEL);
	if (!ec)
		return -ENOMEM;

	mutex_init(&ec->lock);
	ec->client = client;
	BLOCKING_INIT_NOTIFIER_HEAD(&ec->notifier_list);

	ret = devm_request_threaded_irq(dev, client->irq,
					NULL, yoga_c630_ec_thread_intr,
					IRQF_ONESHOT, "yoga_c630_ec", ec);
	if (ret < 0)
		return dev_err_probe(dev, ret, "unable to request irq\n");

	ret = yoga_c630_aux_init(dev, YOGA_C630_DEV_PSY, ec);
	if (ret)
		return ret;

	return yoga_c630_aux_init(dev, YOGA_C630_DEV_UCSI, ec);
}


static const struct of_device_id yoga_c630_ec_of_match[] = {
	{ .compatible = "lenovo,yoga-c630-ec" },
	{}
};
MODULE_DEVICE_TABLE(of, yoga_c630_ec_of_match);

static const struct i2c_device_id yoga_c630_ec_i2c_id_table[] = {
	{ "yoga-c630-ec", },
	{}
};
MODULE_DEVICE_TABLE(i2c, yoga_c630_ec_i2c_id_table);

static struct i2c_driver yoga_c630_ec_i2c_driver = {
	.driver = {
		.name = "yoga-c630-ec",
		.of_match_table = yoga_c630_ec_of_match
	},
	.probe = yoga_c630_ec_probe,
	.id_table = yoga_c630_ec_i2c_id_table,
};
module_i2c_driver(yoga_c630_ec_i2c_driver);

MODULE_DESCRIPTION("Lenovo Yoga C630 Embedded Controller");
MODULE_LICENSE("GPL");
