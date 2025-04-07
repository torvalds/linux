// SPDX-License-Identifier: GPL-2.0
/*
 * Apple Z2 touchscreen driver
 *
 * Copyright (C) The Asahi Linux Contributors
 */

#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/unaligned.h>

#define APPLE_Z2_NUM_FINGERS_OFFSET      16
#define APPLE_Z2_FINGERS_OFFSET          24
#define APPLE_Z2_TOUCH_STARTED           3
#define APPLE_Z2_TOUCH_MOVED             4
#define APPLE_Z2_CMD_READ_INTERRUPT_DATA 0xEB
#define APPLE_Z2_HBPP_CMD_BLOB           0x3001
#define APPLE_Z2_FW_MAGIC                0x5746325A
#define LOAD_COMMAND_INIT_PAYLOAD        0
#define LOAD_COMMAND_SEND_BLOB           1
#define LOAD_COMMAND_SEND_CALIBRATION    2
#define CAL_PROP_NAME                    "apple,z2-cal-blob"

struct apple_z2 {
	struct spi_device *spidev;
	struct gpio_desc *reset_gpio;
	struct input_dev *input_dev;
	struct completion boot_irq;
	bool booted;
	int index_parity;
	struct touchscreen_properties props;
	const char *fw_name;
	u8 *tx_buf;
	u8 *rx_buf;
};

struct apple_z2_finger {
	u8 finger;
	u8 state;
	__le16 unknown2;
	__le16 abs_x;
	__le16 abs_y;
	__le16 rel_x;
	__le16 rel_y;
	__le16 tool_major;
	__le16 tool_minor;
	__le16 orientation;
	__le16 touch_major;
	__le16 touch_minor;
	__le16 unused[2];
	__le16 pressure;
	__le16 multi;
} __packed;

struct apple_z2_hbpp_blob_hdr {
	__le16 cmd;
	__le16 len;
	__le32 addr;
	__le16 checksum;
};

struct apple_z2_fw_hdr {
	__le32 magic;
	__le32 version;
};

struct apple_z2_read_interrupt_cmd {
	u8 cmd;
	u8 counter;
	u8 unused[12];
	__le16 checksum;
};

static void apple_z2_parse_touches(struct apple_z2 *z2,
				   const u8 *msg, size_t msg_len)
{
	int i;
	int nfingers;
	int slot;
	int slot_valid;
	struct apple_z2_finger *fingers;

	if (msg_len <= APPLE_Z2_NUM_FINGERS_OFFSET)
		return;
	nfingers = msg[APPLE_Z2_NUM_FINGERS_OFFSET];
	fingers = (struct apple_z2_finger *)(msg + APPLE_Z2_FINGERS_OFFSET);
	for (i = 0; i < nfingers; i++) {
		slot = input_mt_get_slot_by_key(z2->input_dev, fingers[i].finger);
		if (slot < 0) {
			dev_warn(&z2->spidev->dev, "unable to get slot for finger\n");
			continue;
		}
		slot_valid = fingers[i].state == APPLE_Z2_TOUCH_STARTED ||
			     fingers[i].state == APPLE_Z2_TOUCH_MOVED;
		input_mt_slot(z2->input_dev, slot);
		if (!input_mt_report_slot_state(z2->input_dev, MT_TOOL_FINGER, slot_valid))
			continue;
		touchscreen_report_pos(z2->input_dev, &z2->props,
				       le16_to_cpu(fingers[i].abs_x),
				       le16_to_cpu(fingers[i].abs_y),
				       true);
		input_report_abs(z2->input_dev, ABS_MT_WIDTH_MAJOR,
				 le16_to_cpu(fingers[i].tool_major));
		input_report_abs(z2->input_dev, ABS_MT_WIDTH_MINOR,
				 le16_to_cpu(fingers[i].tool_minor));
		input_report_abs(z2->input_dev, ABS_MT_ORIENTATION,
				 le16_to_cpu(fingers[i].orientation));
		input_report_abs(z2->input_dev, ABS_MT_TOUCH_MAJOR,
				 le16_to_cpu(fingers[i].touch_major));
		input_report_abs(z2->input_dev, ABS_MT_TOUCH_MINOR,
				 le16_to_cpu(fingers[i].touch_minor));
	}
	input_mt_sync_frame(z2->input_dev);
	input_sync(z2->input_dev);
}

static int apple_z2_read_packet(struct apple_z2 *z2)
{
	struct apple_z2_read_interrupt_cmd *len_cmd = (void *)z2->tx_buf;
	struct spi_transfer xfer;
	int error;
	size_t pkt_len;

	memset(&xfer, 0, sizeof(xfer));
	len_cmd->cmd = APPLE_Z2_CMD_READ_INTERRUPT_DATA;
	len_cmd->counter = z2->index_parity + 1;
	len_cmd->checksum =
		cpu_to_le16(APPLE_Z2_CMD_READ_INTERRUPT_DATA + len_cmd->counter);
	z2->index_parity = !z2->index_parity;
	xfer.tx_buf = z2->tx_buf;
	xfer.rx_buf = z2->rx_buf;
	xfer.len = sizeof(*len_cmd);

	error = spi_sync_transfer(z2->spidev, &xfer, 1);
	if (error)
		return error;

	pkt_len = (get_unaligned_le16(z2->rx_buf + 1) + 8) & 0xfffffffc;

	error = spi_read(z2->spidev, z2->rx_buf, pkt_len);
	if (error)
		return error;

	apple_z2_parse_touches(z2, z2->rx_buf + 5, pkt_len - 5);

	return 0;
}

static irqreturn_t apple_z2_irq(int irq, void *data)
{
	struct apple_z2 *z2 = data;

	if (unlikely(!z2->booted))
		complete(&z2->boot_irq);
	else
		apple_z2_read_packet(z2);

	return IRQ_HANDLED;
}

/* Build calibration blob, caller is responsible for freeing the blob data. */
static const u8 *apple_z2_build_cal_blob(struct apple_z2 *z2,
					 u32 address, size_t *size)
{
	u8 *cal_data;
	int cal_size;
	size_t blob_size;
	u32 checksum;
	u16 checksum_hdr;
	int i;
	struct apple_z2_hbpp_blob_hdr *hdr;
	int error;

	if (!device_property_present(&z2->spidev->dev, CAL_PROP_NAME))
		return NULL;

	cal_size = device_property_count_u8(&z2->spidev->dev, CAL_PROP_NAME);
	if (cal_size < 0)
		return ERR_PTR(cal_size);

	blob_size = sizeof(struct apple_z2_hbpp_blob_hdr) + cal_size + sizeof(__le32);
	u8 *blob_data __free(kfree) = kzalloc(blob_size, GFP_KERNEL);
	if (!blob_data)
		return ERR_PTR(-ENOMEM);

	hdr = (struct apple_z2_hbpp_blob_hdr *)blob_data;
	hdr->cmd = cpu_to_le16(APPLE_Z2_HBPP_CMD_BLOB);
	hdr->len = cpu_to_le16(round_up(cal_size, 4) / 4);
	hdr->addr = cpu_to_le32(address);

	checksum_hdr = 0;
	for (i = 2; i < 8; i++)
		checksum_hdr += blob_data[i];
	hdr->checksum = cpu_to_le16(checksum_hdr);

	cal_data = blob_data + sizeof(struct apple_z2_hbpp_blob_hdr);
	error = device_property_read_u8_array(&z2->spidev->dev, CAL_PROP_NAME,
					      cal_data, cal_size);
	if (error)
		return ERR_PTR(error);

	checksum = 0;
	for (i = 0; i < cal_size; i++)
		checksum += cal_data[i];
	put_unaligned_le32(checksum, cal_data + cal_size);

	*size = blob_size;
	return no_free_ptr(blob_data);
}

static int apple_z2_send_firmware_blob(struct apple_z2 *z2, const u8 *data,
				       u32 size, bool init)
{
	struct spi_message msg;
	struct spi_transfer blob_xfer, ack_xfer;
	int error;

	z2->tx_buf[0] = 0x1a;
	z2->tx_buf[1] = 0xa1;

	spi_message_init(&msg);
	memset(&blob_xfer, 0, sizeof(blob_xfer));
	memset(&ack_xfer, 0, sizeof(ack_xfer));

	blob_xfer.tx_buf = data;
	blob_xfer.len = size;
	blob_xfer.bits_per_word = init ? 8 : 16;
	spi_message_add_tail(&blob_xfer, &msg);

	ack_xfer.tx_buf = z2->tx_buf;
	ack_xfer.len = 2;
	spi_message_add_tail(&ack_xfer, &msg);

	reinit_completion(&z2->boot_irq);
	error = spi_sync(z2->spidev, &msg);
	if (error)
		return error;

	/* Irq only happens sometimes, but the thing boots reliably nonetheless */
	wait_for_completion_timeout(&z2->boot_irq, msecs_to_jiffies(20));

	return 0;
}

static int apple_z2_upload_firmware(struct apple_z2 *z2)
{
	const struct apple_z2_fw_hdr *fw_hdr;
	size_t fw_idx = sizeof(struct apple_z2_fw_hdr);
	int error;
	u32 load_cmd;
	u32 address;
	bool init;
	size_t size;

	const struct firmware *fw __free(firmware) = NULL;
	error = request_firmware(&fw, z2->fw_name, &z2->spidev->dev);
	if (error) {
		dev_err(&z2->spidev->dev, "unable to load firmware\n");
		return error;
	}

	fw_hdr = (const struct apple_z2_fw_hdr *)fw->data;
	if (le32_to_cpu(fw_hdr->magic) != APPLE_Z2_FW_MAGIC || le32_to_cpu(fw_hdr->version) != 1) {
		dev_err(&z2->spidev->dev, "invalid firmware header\n");
		return -EINVAL;
	}

	/*
	 * This will interrupt the upload half-way if the file is malformed
	 * As the device has no non-volatile storage to corrupt, and gets reset
	 * on boot anyway, this is fine.
	 */
	while (fw_idx < fw->size) {
		if (fw->size - fw_idx < 8) {
			dev_err(&z2->spidev->dev, "firmware malformed\n");
			return -EINVAL;
		}

		load_cmd = le32_to_cpup((__force __le32 *)(fw->data + fw_idx));
		fw_idx += sizeof(u32);
		if (load_cmd == LOAD_COMMAND_INIT_PAYLOAD || load_cmd == LOAD_COMMAND_SEND_BLOB) {
			size = le32_to_cpup((__force __le32 *)(fw->data + fw_idx));
			fw_idx += sizeof(u32);
			if (fw->size - fw_idx < size) {
				dev_err(&z2->spidev->dev, "firmware malformed\n");
				return -EINVAL;
			}
			init = load_cmd == LOAD_COMMAND_INIT_PAYLOAD;
			error = apple_z2_send_firmware_blob(z2, fw->data + fw_idx,
							    size, init);
			if (error)
				return error;
			fw_idx += size;
		} else if (load_cmd == LOAD_COMMAND_SEND_CALIBRATION) {
			address = le32_to_cpup((__force __le32 *)(fw->data + fw_idx));
			fw_idx += sizeof(u32);

			const u8 *data __free(kfree) =
				apple_z2_build_cal_blob(z2, address, &size);
			if (IS_ERR(data))
				return PTR_ERR(data);

			if (data) {
				error = apple_z2_send_firmware_blob(z2, data, size, false);
				if (error)
					return error;
			}
		} else {
			dev_err(&z2->spidev->dev, "firmware malformed\n");
			return -EINVAL;
		}
		fw_idx = round_up(fw_idx, 4);
	}


	z2->booted = true;
	apple_z2_read_packet(z2);
	return 0;
}

static int apple_z2_boot(struct apple_z2 *z2)
{
	int error;

	reinit_completion(&z2->boot_irq);
	enable_irq(z2->spidev->irq);
	gpiod_set_value(z2->reset_gpio, 0);
	if (!wait_for_completion_timeout(&z2->boot_irq, msecs_to_jiffies(20)))
		return -ETIMEDOUT;

	error = apple_z2_upload_firmware(z2);
	if (error) {
		gpiod_set_value(z2->reset_gpio, 1);
		disable_irq(z2->spidev->irq);
		return error;
	}

	return 0;
}

static int apple_z2_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct apple_z2 *z2;
	int error;

	z2 = devm_kzalloc(dev, sizeof(*z2), GFP_KERNEL);
	if (!z2)
		return -ENOMEM;

	z2->tx_buf = devm_kzalloc(dev, sizeof(struct apple_z2_read_interrupt_cmd), GFP_KERNEL);
	if (!z2->tx_buf)
		return -ENOMEM;
	/* 4096 will end up being rounded up to 8192 due to devres header */
	z2->rx_buf = devm_kzalloc(dev, 4000, GFP_KERNEL);
	if (!z2->rx_buf)
		return -ENOMEM;

	z2->spidev = spi;
	init_completion(&z2->boot_irq);
	spi_set_drvdata(spi, z2);

	/* Reset the device on boot */
	z2->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(z2->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(z2->reset_gpio), "unable to get reset\n");

	error = devm_request_threaded_irq(dev, z2->spidev->irq, NULL, apple_z2_irq,
					  IRQF_ONESHOT | IRQF_NO_AUTOEN,
					  "apple-z2-irq", z2);
	if (error)
		return dev_err_probe(dev, error, "unable to request irq\n");

	error = device_property_read_string(dev, "firmware-name", &z2->fw_name);
	if (error)
		return dev_err_probe(dev, error, "unable to get firmware name\n");

	z2->input_dev = devm_input_allocate_device(dev);
	if (!z2->input_dev)
		return -ENOMEM;

	z2->input_dev->name = (char *)spi_get_device_id(spi)->driver_data;
	z2->input_dev->phys = "apple_z2";
	z2->input_dev->id.bustype = BUS_SPI;

	/* Allocate the axes before setting from DT */
	input_set_abs_params(z2->input_dev, ABS_MT_POSITION_X, 0, 0, 0, 0);
	input_set_abs_params(z2->input_dev, ABS_MT_POSITION_Y, 0, 0, 0, 0);
	touchscreen_parse_properties(z2->input_dev, true, &z2->props);
	input_abs_set_res(z2->input_dev, ABS_MT_POSITION_X, 100);
	input_abs_set_res(z2->input_dev, ABS_MT_POSITION_Y, 100);
	input_set_abs_params(z2->input_dev, ABS_MT_WIDTH_MAJOR, 0, 65535, 0, 0);
	input_set_abs_params(z2->input_dev, ABS_MT_WIDTH_MINOR, 0, 65535, 0, 0);
	input_set_abs_params(z2->input_dev, ABS_MT_TOUCH_MAJOR, 0, 65535, 0, 0);
	input_set_abs_params(z2->input_dev, ABS_MT_TOUCH_MINOR, 0, 65535, 0, 0);
	input_set_abs_params(z2->input_dev, ABS_MT_ORIENTATION, -32768, 32767, 0, 0);

	error = input_mt_init_slots(z2->input_dev, 256, INPUT_MT_DIRECT);
	if (error)
		return dev_err_probe(dev, error, "unable to initialize multitouch slots\n");

	error = input_register_device(z2->input_dev);
	if (error)
		return dev_err_probe(dev, error, "unable to register input device\n");

	/* Wait for device reset to finish */
	usleep_range(5000, 10000);
	error = apple_z2_boot(z2);
	if (error)
		return error;

	return 0;
}

static void apple_z2_shutdown(struct spi_device *spi)
{
	struct apple_z2 *z2 = spi_get_drvdata(spi);

	disable_irq(z2->spidev->irq);
	gpiod_direction_output(z2->reset_gpio, 1);
	z2->booted = false;
}

static int apple_z2_suspend(struct device *dev)
{
	apple_z2_shutdown(to_spi_device(dev));

	return 0;
}

static int apple_z2_resume(struct device *dev)
{
	struct apple_z2 *z2 = spi_get_drvdata(to_spi_device(dev));

	return apple_z2_boot(z2);
}

static DEFINE_SIMPLE_DEV_PM_OPS(apple_z2_pm, apple_z2_suspend, apple_z2_resume);

static const struct of_device_id apple_z2_of_match[] = {
	{ .compatible = "apple,j293-touchbar" },
	{ .compatible = "apple,j493-touchbar" },
	{}
};
MODULE_DEVICE_TABLE(of, apple_z2_of_match);

static struct spi_device_id apple_z2_of_id[] = {
	{ .name = "j293-touchbar", .driver_data = (kernel_ulong_t)"MacBookPro17,1 Touch Bar" },
	{ .name = "j493-touchbar", .driver_data = (kernel_ulong_t)"Mac14,7 Touch Bar" },
	{}
};
MODULE_DEVICE_TABLE(spi, apple_z2_of_id);

static struct spi_driver apple_z2_driver = {
	.driver = {
		.name	= "apple-z2",
		.pm	= pm_sleep_ptr(&apple_z2_pm),
		.of_match_table = apple_z2_of_match,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.id_table = apple_z2_of_id,
	.probe    = apple_z2_probe,
	.remove   = apple_z2_shutdown,
};

module_spi_driver(apple_z2_driver);

MODULE_LICENSE("GPL");
MODULE_FIRMWARE("apple/dfrmtfw-*.bin");
MODULE_DESCRIPTION("Apple Z2 touchscreens driver");
