// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel La Jolla Cove Adapter USB driver
 *
 * Copyright (c) 2023, Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/auxiliary_bus.h>
#include <linux/dev_printk.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/usb/ljca.h>

#include <asm/unaligned.h>

/* command flags */
#define LJCA_ACK_FLAG			BIT(0)
#define LJCA_RESP_FLAG			BIT(1)
#define LJCA_CMPL_FLAG			BIT(2)

#define LJCA_MAX_PACKET_SIZE		64u
#define LJCA_MAX_PAYLOAD_SIZE		\
		(LJCA_MAX_PACKET_SIZE - sizeof(struct ljca_msg))

#define LJCA_WRITE_TIMEOUT_MS		200
#define LJCA_WRITE_ACK_TIMEOUT_MS	500
#define LJCA_ENUM_CLIENT_TIMEOUT_MS	20

/* ljca client type */
enum ljca_client_type {
	LJCA_CLIENT_MNG = 1,
	LJCA_CLIENT_GPIO = 3,
	LJCA_CLIENT_I2C = 4,
	LJCA_CLIENT_SPI = 5,
};

/* MNG client commands */
enum ljca_mng_cmd {
	LJCA_MNG_RESET = 2,
	LJCA_MNG_ENUM_GPIO = 4,
	LJCA_MNG_ENUM_I2C = 5,
	LJCA_MNG_ENUM_SPI = 8,
};

/* ljca client acpi _ADR */
enum ljca_client_acpi_adr {
	LJCA_GPIO_ACPI_ADR,
	LJCA_I2C1_ACPI_ADR,
	LJCA_I2C2_ACPI_ADR,
	LJCA_SPI1_ACPI_ADR,
	LJCA_SPI2_ACPI_ADR,
	LJCA_CLIENT_ACPI_ADR_MAX,
};

/* ljca cmd message structure */
struct ljca_msg {
	u8 type;
	u8 cmd;
	u8 flags;
	u8 len;
	u8 data[] __counted_by(len);
} __packed;

struct ljca_i2c_ctr_info {
	u8 id;
	u8 capacity;
	u8 intr_pin;
} __packed;

struct ljca_i2c_descriptor {
	u8 num;
	struct ljca_i2c_ctr_info info[] __counted_by(num);
} __packed;

struct ljca_spi_ctr_info {
	u8 id;
	u8 capacity;
	u8 intr_pin;
} __packed;

struct ljca_spi_descriptor {
	u8 num;
	struct ljca_spi_ctr_info info[] __counted_by(num);
} __packed;

struct ljca_bank_descriptor {
	u8 bank_id;
	u8 pin_num;

	/* 1 bit for each gpio, 1 means valid */
	__le32 valid_pins;
} __packed;

struct ljca_gpio_descriptor {
	u8 pins_per_bank;
	u8 bank_num;
	struct ljca_bank_descriptor bank_desc[] __counted_by(bank_num);
} __packed;

/**
 * struct ljca_adapter - represent a ljca adapter
 *
 * @intf: the usb interface for this ljca adapter
 * @usb_dev: the usb device for this ljca adapter
 * @dev: the specific device info of the usb interface
 * @rx_pipe: bulk in pipe for receive data from firmware
 * @tx_pipe: bulk out pipe for send data to firmware
 * @rx_urb: urb used for the bulk in pipe
 * @rx_buf: buffer used to receive command response and event
 * @rx_len: length of rx buffer
 * @ex_buf: external buffer to save command response
 * @ex_buf_len: length of external buffer
 * @actual_length: actual length of data copied to external buffer
 * @tx_buf: buffer used to download command to firmware
 * @tx_buf_len: length of tx buffer
 * @lock: spinlock to protect tx_buf and ex_buf
 * @cmd_completion: completion object as the command receives ack
 * @mutex: mutex to avoid command download concurrently
 * @client_list: client device list
 * @disconnect: usb disconnect ongoing or not
 * @reset_id: used to reset firmware
 */
struct ljca_adapter {
	struct usb_interface *intf;
	struct usb_device *usb_dev;
	struct device *dev;

	unsigned int rx_pipe;
	unsigned int tx_pipe;

	struct urb *rx_urb;
	void *rx_buf;
	unsigned int rx_len;

	u8 *ex_buf;
	u8 ex_buf_len;
	u8 actual_length;

	void *tx_buf;
	u8 tx_buf_len;

	spinlock_t lock;

	struct completion cmd_completion;
	struct mutex mutex;

	struct list_head client_list;

	bool disconnect;

	u32 reset_id;
};

struct ljca_match_ids_walk_data {
	const struct acpi_device_id *ids;
	const char *uid;
	struct acpi_device *adev;
};

static const struct acpi_device_id ljca_gpio_hids[] = {
	{ "INTC1074" },
	{ "INTC1096" },
	{ "INTC100B" },
	{ "INTC10D1" },
	{},
};

static const struct acpi_device_id ljca_i2c_hids[] = {
	{ "INTC1075" },
	{ "INTC1097" },
	{ "INTC100C" },
	{ "INTC10D2" },
	{},
};

static const struct acpi_device_id ljca_spi_hids[] = {
	{ "INTC1091" },
	{ "INTC1098" },
	{ "INTC100D" },
	{ "INTC10D3" },
	{},
};

static void ljca_handle_event(struct ljca_adapter *adap,
			      struct ljca_msg *header)
{
	struct ljca_client *client;

	list_for_each_entry(client, &adap->client_list, link) {
		/*
		 * Currently only GPIO register event callback, but
		 * firmware message structure should include id when
		 * multiple same type clients register event callback.
		 */
		if (client->type == header->type) {
			unsigned long flags;

			spin_lock_irqsave(&client->event_cb_lock, flags);
			client->event_cb(client->context, header->cmd,
					 header->data, header->len);
			spin_unlock_irqrestore(&client->event_cb_lock, flags);

			break;
		}
	}
}

/* process command ack and received data if available */
static void ljca_handle_cmd_ack(struct ljca_adapter *adap, struct ljca_msg *header)
{
	struct ljca_msg *tx_header = adap->tx_buf;
	u8 ibuf_len, actual_len = 0;
	unsigned long flags;
	u8 *ibuf;

	spin_lock_irqsave(&adap->lock, flags);

	if (tx_header->type != header->type || tx_header->cmd != header->cmd) {
		spin_unlock_irqrestore(&adap->lock, flags);
		dev_err(adap->dev, "cmd ack mismatch error\n");
		return;
	}

	ibuf_len = adap->ex_buf_len;
	ibuf = adap->ex_buf;

	if (ibuf && ibuf_len) {
		actual_len = min(header->len, ibuf_len);

		/* copy received data to external buffer */
		memcpy(ibuf, header->data, actual_len);
	}
	/* update copied data length */
	adap->actual_length = actual_len;

	spin_unlock_irqrestore(&adap->lock, flags);

	complete(&adap->cmd_completion);
}

static void ljca_recv(struct urb *urb)
{
	struct ljca_msg *header = urb->transfer_buffer;
	struct ljca_adapter *adap = urb->context;
	int ret;

	switch (urb->status) {
	case 0:
		/* success */
		break;
	case -ENOENT:
		/*
		 * directly complete the possible ongoing transfer
		 * during disconnect
		 */
		if (adap->disconnect)
			complete(&adap->cmd_completion);
		return;
	case -ECONNRESET:
	case -ESHUTDOWN:
	case -EPIPE:
		/* rx urb is terminated */
		dev_dbg(adap->dev, "rx urb terminated with status: %d\n",
			urb->status);
		return;
	default:
		dev_dbg(adap->dev, "rx urb error: %d\n", urb->status);
		goto resubmit;
	}

	if (header->len + sizeof(*header) != urb->actual_length)
		goto resubmit;

	if (header->flags & LJCA_ACK_FLAG)
		ljca_handle_cmd_ack(adap, header);
	else
		ljca_handle_event(adap, header);

resubmit:
	ret = usb_submit_urb(urb, GFP_ATOMIC);
	if (ret && ret != -EPERM)
		dev_err(adap->dev, "resubmit rx urb error %d\n", ret);
}

static int ljca_send(struct ljca_adapter *adap, u8 type, u8 cmd,
		     const u8 *obuf, u8 obuf_len, u8 *ibuf, u8 ibuf_len,
		     bool ack, unsigned long timeout)
{
	unsigned int msg_len = sizeof(struct ljca_msg) + obuf_len;
	struct ljca_msg *header = adap->tx_buf;
	unsigned int transferred;
	unsigned long flags;
	int ret;

	if (adap->disconnect)
		return -ENODEV;

	if (msg_len > adap->tx_buf_len)
		return -EINVAL;

	mutex_lock(&adap->mutex);

	spin_lock_irqsave(&adap->lock, flags);

	header->type = type;
	header->cmd = cmd;
	header->len = obuf_len;
	if (obuf)
		memcpy(header->data, obuf, obuf_len);

	header->flags = LJCA_CMPL_FLAG | (ack ? LJCA_ACK_FLAG : 0);

	adap->ex_buf = ibuf;
	adap->ex_buf_len = ibuf_len;
	adap->actual_length = 0;

	spin_unlock_irqrestore(&adap->lock, flags);

	reinit_completion(&adap->cmd_completion);

	ret = usb_autopm_get_interface(adap->intf);
	if (ret < 0)
		goto out;

	ret = usb_bulk_msg(adap->usb_dev, adap->tx_pipe, header,
			   msg_len, &transferred, LJCA_WRITE_TIMEOUT_MS);

	usb_autopm_put_interface(adap->intf);

	if (ret < 0)
		goto out;
	if (transferred != msg_len) {
		ret = -EIO;
		goto out;
	}

	if (ack) {
		ret = wait_for_completion_timeout(&adap->cmd_completion,
						  timeout);
		if (!ret) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}
	ret = adap->actual_length;

out:
	spin_lock_irqsave(&adap->lock, flags);
	adap->ex_buf = NULL;
	adap->ex_buf_len = 0;

	memset(header, 0, sizeof(*header));
	spin_unlock_irqrestore(&adap->lock, flags);

	mutex_unlock(&adap->mutex);

	return ret;
}

int ljca_transfer(struct ljca_client *client, u8 cmd, const u8 *obuf,
		  u8 obuf_len, u8 *ibuf, u8 ibuf_len)
{
	return ljca_send(client->adapter, client->type, cmd,
			 obuf, obuf_len, ibuf, ibuf_len, true,
			 LJCA_WRITE_ACK_TIMEOUT_MS);
}
EXPORT_SYMBOL_NS_GPL(ljca_transfer, LJCA);

int ljca_transfer_noack(struct ljca_client *client, u8 cmd, const u8 *obuf,
			u8 obuf_len)
{
	return ljca_send(client->adapter, client->type, cmd, obuf,
			 obuf_len, NULL, 0, false, LJCA_WRITE_ACK_TIMEOUT_MS);
}
EXPORT_SYMBOL_NS_GPL(ljca_transfer_noack, LJCA);

int ljca_register_event_cb(struct ljca_client *client, ljca_event_cb_t event_cb,
			   void *context)
{
	unsigned long flags;

	if (!event_cb)
		return -EINVAL;

	spin_lock_irqsave(&client->event_cb_lock, flags);

	if (client->event_cb) {
		spin_unlock_irqrestore(&client->event_cb_lock, flags);
		return -EALREADY;
	}

	client->event_cb = event_cb;
	client->context = context;

	spin_unlock_irqrestore(&client->event_cb_lock, flags);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(ljca_register_event_cb, LJCA);

void ljca_unregister_event_cb(struct ljca_client *client)
{
	unsigned long flags;

	spin_lock_irqsave(&client->event_cb_lock, flags);

	client->event_cb = NULL;
	client->context = NULL;

	spin_unlock_irqrestore(&client->event_cb_lock, flags);
}
EXPORT_SYMBOL_NS_GPL(ljca_unregister_event_cb, LJCA);

static int ljca_match_device_ids(struct acpi_device *adev, void *data)
{
	struct ljca_match_ids_walk_data *wd = data;
	const char *uid = acpi_device_uid(adev);

	if (acpi_match_device_ids(adev, wd->ids))
		return 0;

	if (!wd->uid)
		goto match;

	if (!uid)
		/*
		 * Some DSDTs have only one ACPI companion for the two I2C
		 * controllers and they don't set a UID at all (e.g. Dell
		 * Latitude 9420). On these platforms only the first I2C
		 * controller is used, so if a HID match has no UID we use
		 * "0" as the UID and assign ACPI companion to the first
		 * I2C controller.
		 */
		uid = "0";
	else
		uid = strchr(uid, wd->uid[0]);

	if (!uid || strcmp(uid, wd->uid))
		return 0;

match:
	wd->adev = adev;

	return 1;
}

/* bind auxiliary device to acpi device */
static void ljca_auxdev_acpi_bind(struct ljca_adapter *adap,
				  struct auxiliary_device *auxdev,
				  u64 adr, u8 id)
{
	struct ljca_match_ids_walk_data wd = { 0 };
	struct device *dev = adap->dev;
	struct acpi_device *parent;
	char uid[4];

	parent = ACPI_COMPANION(dev);
	if (!parent)
		return;

	/*
	 * Currently LJCA hw doesn't use _ADR instead the shipped
	 * platforms use _HID to distinguish children devices.
	 */
	switch (adr) {
	case LJCA_GPIO_ACPI_ADR:
		wd.ids = ljca_gpio_hids;
		break;
	case LJCA_I2C1_ACPI_ADR:
	case LJCA_I2C2_ACPI_ADR:
		snprintf(uid, sizeof(uid), "%d", id);
		wd.uid = uid;
		wd.ids = ljca_i2c_hids;
		break;
	case LJCA_SPI1_ACPI_ADR:
	case LJCA_SPI2_ACPI_ADR:
		wd.ids = ljca_spi_hids;
		break;
	default:
		dev_warn(dev, "unsupported _ADR\n");
		return;
	}

	acpi_dev_for_each_child(parent, ljca_match_device_ids, &wd);
	if (wd.adev) {
		ACPI_COMPANION_SET(&auxdev->dev, wd.adev);
		return;
	}

	parent = ACPI_COMPANION(dev->parent->parent);
	if (!parent)
		return;

	acpi_dev_for_each_child(parent, ljca_match_device_ids, &wd);
	if (wd.adev)
		ACPI_COMPANION_SET(&auxdev->dev, wd.adev);
}

static void ljca_auxdev_release(struct device *dev)
{
	struct auxiliary_device *auxdev = to_auxiliary_dev(dev);

	kfree(auxdev->dev.platform_data);
}

static int ljca_new_client_device(struct ljca_adapter *adap, u8 type, u8 id,
				  char *name, void *data, u64 adr)
{
	struct auxiliary_device *auxdev;
	struct ljca_client *client;
	int ret;

	client = kzalloc(sizeof *client, GFP_KERNEL);
	if (!client) {
		kfree(data);
		return -ENOMEM;
	}

	client->type = type;
	client->id = id;
	client->adapter = adap;
	spin_lock_init(&client->event_cb_lock);

	auxdev = &client->auxdev;
	auxdev->name = name;
	auxdev->id = id;

	auxdev->dev.parent = adap->dev;
	auxdev->dev.platform_data = data;
	auxdev->dev.release = ljca_auxdev_release;

	ret = auxiliary_device_init(auxdev);
	if (ret) {
		kfree(data);
		goto err_free;
	}

	ljca_auxdev_acpi_bind(adap, auxdev, adr, id);

	ret = auxiliary_device_add(auxdev);
	if (ret)
		goto err_uninit;

	list_add_tail(&client->link, &adap->client_list);

	return 0;

err_uninit:
	auxiliary_device_uninit(auxdev);

err_free:
	kfree(client);

	return ret;
}

static int ljca_enumerate_gpio(struct ljca_adapter *adap)
{
	u32 valid_pin[LJCA_MAX_GPIO_NUM / BITS_PER_TYPE(u32)];
	struct ljca_gpio_descriptor *desc;
	struct ljca_gpio_info *gpio_info;
	u8 buf[LJCA_MAX_PAYLOAD_SIZE];
	int ret, gpio_num;
	unsigned int i;

	ret = ljca_send(adap, LJCA_CLIENT_MNG, LJCA_MNG_ENUM_GPIO, NULL, 0, buf,
			sizeof(buf), true, LJCA_ENUM_CLIENT_TIMEOUT_MS);
	if (ret < 0)
		return ret;

	/* check firmware response */
	desc = (struct ljca_gpio_descriptor *)buf;
	if (ret != struct_size(desc, bank_desc, desc->bank_num))
		return -EINVAL;

	gpio_num = desc->pins_per_bank * desc->bank_num;
	if (gpio_num > LJCA_MAX_GPIO_NUM)
		return -EINVAL;

	/* construct platform data */
	gpio_info = kzalloc(sizeof *gpio_info, GFP_KERNEL);
	if (!gpio_info)
		return -ENOMEM;
	gpio_info->num = gpio_num;

	for (i = 0; i < desc->bank_num; i++)
		valid_pin[i] = get_unaligned_le32(&desc->bank_desc[i].valid_pins);
	bitmap_from_arr32(gpio_info->valid_pin_map, valid_pin, gpio_num);

	return ljca_new_client_device(adap, LJCA_CLIENT_GPIO, 0, "ljca-gpio",
				     gpio_info, LJCA_GPIO_ACPI_ADR);
}

static int ljca_enumerate_i2c(struct ljca_adapter *adap)
{
	struct ljca_i2c_descriptor *desc;
	struct ljca_i2c_info *i2c_info;
	u8 buf[LJCA_MAX_PAYLOAD_SIZE];
	unsigned int i;
	int ret;

	ret = ljca_send(adap, LJCA_CLIENT_MNG, LJCA_MNG_ENUM_I2C, NULL, 0, buf,
			sizeof(buf), true, LJCA_ENUM_CLIENT_TIMEOUT_MS);
	if (ret < 0)
		return ret;

	/* check firmware response */
	desc = (struct ljca_i2c_descriptor *)buf;
	if (ret != struct_size(desc, info, desc->num))
		return -EINVAL;

	for (i = 0; i < desc->num; i++) {
		/* construct platform data */
		i2c_info = kzalloc(sizeof *i2c_info, GFP_KERNEL);
		if (!i2c_info)
			return -ENOMEM;

		i2c_info->id = desc->info[i].id;
		i2c_info->capacity = desc->info[i].capacity;
		i2c_info->intr_pin = desc->info[i].intr_pin;

		ret = ljca_new_client_device(adap, LJCA_CLIENT_I2C, i,
					     "ljca-i2c", i2c_info,
					     LJCA_I2C1_ACPI_ADR + i);
		if (ret)
			return ret;
	}

	return 0;
}

static int ljca_enumerate_spi(struct ljca_adapter *adap)
{
	struct ljca_spi_descriptor *desc;
	struct ljca_spi_info *spi_info;
	u8 buf[LJCA_MAX_PAYLOAD_SIZE];
	unsigned int i;
	int ret;

	/* Not all LJCA chips implement SPI, a timeout reading the descriptors is normal */
	ret = ljca_send(adap, LJCA_CLIENT_MNG, LJCA_MNG_ENUM_SPI, NULL, 0, buf,
			sizeof(buf), true, LJCA_ENUM_CLIENT_TIMEOUT_MS);
	if (ret < 0)
		return (ret == -ETIMEDOUT) ? 0 : ret;

	/* check firmware response */
	desc = (struct ljca_spi_descriptor *)buf;
	if (ret != struct_size(desc, info, desc->num))
		return -EINVAL;

	for (i = 0; i < desc->num; i++) {
		/* construct platform data */
		spi_info = kzalloc(sizeof *spi_info, GFP_KERNEL);
		if (!spi_info)
			return -ENOMEM;

		spi_info->id = desc->info[i].id;
		spi_info->capacity = desc->info[i].capacity;

		ret = ljca_new_client_device(adap, LJCA_CLIENT_SPI, i,
					     "ljca-spi", spi_info,
					     LJCA_SPI1_ACPI_ADR + i);
		if (ret)
			return ret;
	}

	return 0;
}

static int ljca_reset_handshake(struct ljca_adapter *adap)
{
	__le32 reset_id = cpu_to_le32(adap->reset_id);
	__le32 reset_id_ret = 0;
	int ret;

	adap->reset_id++;

	ret = ljca_send(adap, LJCA_CLIENT_MNG, LJCA_MNG_RESET, (u8 *)&reset_id,
			sizeof(__le32), (u8 *)&reset_id_ret, sizeof(__le32),
			true, LJCA_WRITE_ACK_TIMEOUT_MS);
	if (ret < 0)
		return ret;

	if (reset_id_ret != reset_id)
		return -EINVAL;

	return 0;
}

static int ljca_enumerate_clients(struct ljca_adapter *adap)
{
	struct ljca_client *client, *next;
	int ret;

	ret = ljca_reset_handshake(adap);
	if (ret)
		goto err_kill;

	ret = ljca_enumerate_gpio(adap);
	if (ret) {
		dev_err(adap->dev, "enumerate GPIO error\n");
		goto err_kill;
	}

	ret = ljca_enumerate_i2c(adap);
	if (ret) {
		dev_err(adap->dev, "enumerate I2C error\n");
		goto err_kill;
	}

	ret = ljca_enumerate_spi(adap);
	if (ret) {
		dev_err(adap->dev, "enumerate SPI error\n");
		goto err_kill;
	}

	return 0;

err_kill:
	adap->disconnect = true;

	usb_kill_urb(adap->rx_urb);

	list_for_each_entry_safe_reverse(client, next, &adap->client_list, link) {
		auxiliary_device_delete(&client->auxdev);
		auxiliary_device_uninit(&client->auxdev);

		list_del_init(&client->link);
		kfree(client);
	}

	return ret;
}

static int ljca_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	struct usb_device *usb_dev = interface_to_usbdev(interface);
	struct usb_host_interface *alt = interface->cur_altsetting;
	struct usb_endpoint_descriptor *ep_in, *ep_out;
	struct device *dev = &interface->dev;
	struct ljca_adapter *adap;
	int ret;

	adap = devm_kzalloc(dev, sizeof(*adap), GFP_KERNEL);
	if (!adap)
		return -ENOMEM;

	/* separate tx buffer allocation for alignment */
	adap->tx_buf = devm_kzalloc(dev, LJCA_MAX_PACKET_SIZE, GFP_KERNEL);
	if (!adap->tx_buf)
		return -ENOMEM;
	adap->tx_buf_len = LJCA_MAX_PACKET_SIZE;

	mutex_init(&adap->mutex);
	spin_lock_init(&adap->lock);
	init_completion(&adap->cmd_completion);
	INIT_LIST_HEAD(&adap->client_list);

	adap->intf = usb_get_intf(interface);
	adap->usb_dev = usb_dev;
	adap->dev = dev;

	/*
	 * find the first bulk in and out endpoints.
	 * ignore any others.
	 */
	ret = usb_find_common_endpoints(alt, &ep_in, &ep_out, NULL, NULL);
	if (ret) {
		dev_err(dev, "bulk endpoints not found\n");
		goto err_put;
	}
	adap->rx_pipe = usb_rcvbulkpipe(usb_dev, usb_endpoint_num(ep_in));
	adap->tx_pipe = usb_sndbulkpipe(usb_dev, usb_endpoint_num(ep_out));

	/* setup rx buffer */
	adap->rx_len = usb_endpoint_maxp(ep_in);
	adap->rx_buf = devm_kzalloc(dev, adap->rx_len, GFP_KERNEL);
	if (!adap->rx_buf) {
		ret = -ENOMEM;
		goto err_put;
	}

	/* alloc rx urb */
	adap->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!adap->rx_urb) {
		ret = -ENOMEM;
		goto err_put;
	}
	usb_fill_bulk_urb(adap->rx_urb, usb_dev, adap->rx_pipe,
			  adap->rx_buf, adap->rx_len, ljca_recv, adap);

	usb_set_intfdata(interface, adap);

	/* submit rx urb before enumerate clients */
	ret = usb_submit_urb(adap->rx_urb, GFP_KERNEL);
	if (ret) {
		dev_err(dev, "submit rx urb failed: %d\n", ret);
		goto err_free;
	}

	ret = ljca_enumerate_clients(adap);
	if (ret)
		goto err_free;

	usb_enable_autosuspend(usb_dev);

	return 0;

err_free:
	usb_free_urb(adap->rx_urb);

err_put:
	usb_put_intf(adap->intf);

	mutex_destroy(&adap->mutex);

	return ret;
}

static void ljca_disconnect(struct usb_interface *interface)
{
	struct ljca_adapter *adap = usb_get_intfdata(interface);
	struct ljca_client *client, *next;

	adap->disconnect = true;

	usb_kill_urb(adap->rx_urb);

	list_for_each_entry_safe_reverse(client, next, &adap->client_list, link) {
		auxiliary_device_delete(&client->auxdev);
		auxiliary_device_uninit(&client->auxdev);

		list_del_init(&client->link);
		kfree(client);
	}

	usb_free_urb(adap->rx_urb);

	usb_put_intf(adap->intf);

	mutex_destroy(&adap->mutex);
}

static int ljca_suspend(struct usb_interface *interface, pm_message_t message)
{
	struct ljca_adapter *adap = usb_get_intfdata(interface);

	usb_kill_urb(adap->rx_urb);

	return 0;
}

static int ljca_resume(struct usb_interface *interface)
{
	struct ljca_adapter *adap = usb_get_intfdata(interface);

	return usb_submit_urb(adap->rx_urb, GFP_KERNEL);
}

static const struct usb_device_id ljca_table[] = {
	{ USB_DEVICE(0x8086, 0x0b63) },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(usb, ljca_table);

static struct usb_driver ljca_driver = {
	.name = "ljca",
	.id_table = ljca_table,
	.probe = ljca_probe,
	.disconnect = ljca_disconnect,
	.suspend = ljca_suspend,
	.resume = ljca_resume,
	.supports_autosuspend = 1,
};
module_usb_driver(ljca_driver);

MODULE_AUTHOR("Wentong Wu <wentong.wu@intel.com>");
MODULE_AUTHOR("Zhifeng Wang <zhifeng.wang@intel.com>");
MODULE_AUTHOR("Lixu Zhang <lixu.zhang@intel.com>");
MODULE_DESCRIPTION("Intel La Jolla Cove Adapter USB driver");
MODULE_LICENSE("GPL");
