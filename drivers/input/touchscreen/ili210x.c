// SPDX-License-Identifier: GPL-2.0-only
#include <linux/crc-ccitt.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/ihex.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <asm/unaligned.h>

#define ILI2XXX_POLL_PERIOD	15

#define ILI210X_DATA_SIZE	64
#define ILI211X_DATA_SIZE	43
#define ILI251X_DATA_SIZE1	31
#define ILI251X_DATA_SIZE2	20

/* Touchscreen commands */
#define REG_TOUCHDATA		0x10
#define REG_PANEL_INFO		0x20
#define REG_FIRMWARE_VERSION	0x40
#define REG_PROTOCOL_VERSION	0x42
#define REG_KERNEL_VERSION	0x61
#define REG_IC_BUSY		0x80
#define REG_IC_BUSY_NOT_BUSY	0x50
#define REG_GET_MODE		0xc0
#define REG_GET_MODE_AP		0x5a
#define REG_GET_MODE_BL		0x55
#define REG_SET_MODE_AP		0xc1
#define REG_SET_MODE_BL		0xc2
#define REG_WRITE_DATA		0xc3
#define REG_WRITE_ENABLE	0xc4
#define REG_READ_DATA_CRC	0xc7
#define REG_CALIBRATE		0xcc

#define ILI251X_FW_FILENAME	"ilitek/ili251x.bin"

struct ili2xxx_chip {
	int (*read_reg)(struct i2c_client *client, u8 reg,
			void *buf, size_t len);
	int (*get_touch_data)(struct i2c_client *client, u8 *data);
	bool (*parse_touch_data)(const u8 *data, unsigned int finger,
				 unsigned int *x, unsigned int *y,
				 unsigned int *z);
	bool (*continue_polling)(const u8 *data, bool touch);
	unsigned int max_touches;
	unsigned int resolution;
	bool has_calibrate_reg;
	bool has_firmware_proto;
	bool has_pressure_reg;
};

struct ili210x {
	struct i2c_client *client;
	struct input_dev *input;
	struct gpio_desc *reset_gpio;
	struct touchscreen_properties prop;
	const struct ili2xxx_chip *chip;
	u8 version_firmware[8];
	u8 version_kernel[5];
	u8 version_proto[2];
	u8 ic_mode[2];
	bool stop;
};

static int ili210x_read_reg(struct i2c_client *client,
			    u8 reg, void *buf, size_t len)
{
	struct i2c_msg msg[] = {
		{
			.addr	= client->addr,
			.flags	= 0,
			.len	= 1,
			.buf	= &reg,
		},
		{
			.addr	= client->addr,
			.flags	= I2C_M_RD,
			.len	= len,
			.buf	= buf,
		}
	};
	int error, ret;

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg)) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&client->dev, "%s failed: %d\n", __func__, error);
		return error;
	}

	return 0;
}

static int ili210x_read_touch_data(struct i2c_client *client, u8 *data)
{
	return ili210x_read_reg(client, REG_TOUCHDATA,
				data, ILI210X_DATA_SIZE);
}

static bool ili210x_touchdata_to_coords(const u8 *touchdata,
					unsigned int finger,
					unsigned int *x, unsigned int *y,
					unsigned int *z)
{
	if (!(touchdata[0] & BIT(finger)))
		return false;

	*x = get_unaligned_be16(touchdata + 1 + (finger * 4) + 0);
	*y = get_unaligned_be16(touchdata + 1 + (finger * 4) + 2);

	return true;
}

static bool ili210x_check_continue_polling(const u8 *data, bool touch)
{
	return data[0] & 0xf3;
}

static const struct ili2xxx_chip ili210x_chip = {
	.read_reg		= ili210x_read_reg,
	.get_touch_data		= ili210x_read_touch_data,
	.parse_touch_data	= ili210x_touchdata_to_coords,
	.continue_polling	= ili210x_check_continue_polling,
	.max_touches		= 2,
	.has_calibrate_reg	= true,
};

static int ili211x_read_touch_data(struct i2c_client *client, u8 *data)
{
	s16 sum = 0;
	int error;
	int ret;
	int i;

	ret = i2c_master_recv(client, data, ILI211X_DATA_SIZE);
	if (ret != ILI211X_DATA_SIZE) {
		error = ret < 0 ? ret : -EIO;
		dev_err(&client->dev, "%s failed: %d\n", __func__, error);
		return error;
	}

	/* This chip uses custom checksum at the end of data */
	for (i = 0; i < ILI211X_DATA_SIZE - 1; i++)
		sum = (sum + data[i]) & 0xff;

	if ((-sum & 0xff) != data[ILI211X_DATA_SIZE - 1]) {
		dev_err(&client->dev,
			"CRC error (crc=0x%02x expected=0x%02x)\n",
			sum, data[ILI211X_DATA_SIZE - 1]);
		return -EIO;
	}

	return 0;
}

static bool ili211x_touchdata_to_coords(const u8 *touchdata,
					unsigned int finger,
					unsigned int *x, unsigned int *y,
					unsigned int *z)
{
	u32 data;

	data = get_unaligned_be32(touchdata + 1 + (finger * 4) + 0);
	if (data == 0xffffffff)	/* Finger up */
		return false;

	*x = ((touchdata[1 + (finger * 4) + 0] & 0xf0) << 4) |
	     touchdata[1 + (finger * 4) + 1];
	*y = ((touchdata[1 + (finger * 4) + 0] & 0x0f) << 8) |
	     touchdata[1 + (finger * 4) + 2];

	return true;
}

static bool ili211x_decline_polling(const u8 *data, bool touch)
{
	return false;
}

static const struct ili2xxx_chip ili211x_chip = {
	.read_reg		= ili210x_read_reg,
	.get_touch_data		= ili211x_read_touch_data,
	.parse_touch_data	= ili211x_touchdata_to_coords,
	.continue_polling	= ili211x_decline_polling,
	.max_touches		= 10,
	.resolution		= 2048,
};

static bool ili212x_touchdata_to_coords(const u8 *touchdata,
					unsigned int finger,
					unsigned int *x, unsigned int *y,
					unsigned int *z)
{
	u16 val;

	val = get_unaligned_be16(touchdata + 3 + (finger * 5) + 0);
	if (!(val & BIT(15)))	/* Touch indication */
		return false;

	*x = val & 0x3fff;
	*y = get_unaligned_be16(touchdata + 3 + (finger * 5) + 2);

	return true;
}

static bool ili212x_check_continue_polling(const u8 *data, bool touch)
{
	return touch;
}

static const struct ili2xxx_chip ili212x_chip = {
	.read_reg		= ili210x_read_reg,
	.get_touch_data		= ili210x_read_touch_data,
	.parse_touch_data	= ili212x_touchdata_to_coords,
	.continue_polling	= ili212x_check_continue_polling,
	.max_touches		= 10,
	.has_calibrate_reg	= true,
};

static int ili251x_read_reg_common(struct i2c_client *client,
				   u8 reg, void *buf, size_t len,
				   unsigned int delay)
{
	int error;
	int ret;

	ret = i2c_master_send(client, &reg, 1);
	if (ret == 1) {
		if (delay)
			usleep_range(delay, delay + 500);

		ret = i2c_master_recv(client, buf, len);
		if (ret == len)
			return 0;
	}

	error = ret < 0 ? ret : -EIO;
	dev_err(&client->dev, "%s failed: %d\n", __func__, error);
	return ret;
}

static int ili251x_read_reg(struct i2c_client *client,
			    u8 reg, void *buf, size_t len)
{
	return ili251x_read_reg_common(client, reg, buf, len, 5000);
}

static int ili251x_read_touch_data(struct i2c_client *client, u8 *data)
{
	int error;

	error = ili251x_read_reg_common(client, REG_TOUCHDATA,
					data, ILI251X_DATA_SIZE1, 0);
	if (!error && data[0] == 2) {
		error = i2c_master_recv(client, data + ILI251X_DATA_SIZE1,
					ILI251X_DATA_SIZE2);
		if (error >= 0 && error != ILI251X_DATA_SIZE2)
			error = -EIO;
	}

	return error;
}

static bool ili251x_touchdata_to_coords(const u8 *touchdata,
					unsigned int finger,
					unsigned int *x, unsigned int *y,
					unsigned int *z)
{
	u16 val;

	val = get_unaligned_be16(touchdata + 1 + (finger * 5) + 0);
	if (!(val & BIT(15)))	/* Touch indication */
		return false;

	*x = val & 0x3fff;
	*y = get_unaligned_be16(touchdata + 1 + (finger * 5) + 2);
	*z = touchdata[1 + (finger * 5) + 4];

	return true;
}

static bool ili251x_check_continue_polling(const u8 *data, bool touch)
{
	return touch;
}

static const struct ili2xxx_chip ili251x_chip = {
	.read_reg		= ili251x_read_reg,
	.get_touch_data		= ili251x_read_touch_data,
	.parse_touch_data	= ili251x_touchdata_to_coords,
	.continue_polling	= ili251x_check_continue_polling,
	.max_touches		= 10,
	.has_calibrate_reg	= true,
	.has_firmware_proto	= true,
	.has_pressure_reg	= true,
};

static bool ili210x_report_events(struct ili210x *priv, u8 *touchdata)
{
	struct input_dev *input = priv->input;
	int i;
	bool contact = false, touch;
	unsigned int x = 0, y = 0, z = 0;

	for (i = 0; i < priv->chip->max_touches; i++) {
		touch = priv->chip->parse_touch_data(touchdata, i, &x, &y, &z);

		input_mt_slot(input, i);
		if (input_mt_report_slot_state(input, MT_TOOL_FINGER, touch)) {
			touchscreen_report_pos(input, &priv->prop, x, y, true);
			if (priv->chip->has_pressure_reg)
				input_report_abs(input, ABS_MT_PRESSURE, z);
			contact = true;
		}
	}

	input_mt_report_pointer_emulation(input, false);
	input_sync(input);

	return contact;
}

static irqreturn_t ili210x_irq(int irq, void *irq_data)
{
	struct ili210x *priv = irq_data;
	struct i2c_client *client = priv->client;
	const struct ili2xxx_chip *chip = priv->chip;
	u8 touchdata[ILI210X_DATA_SIZE] = { 0 };
	bool keep_polling;
	ktime_t time_next;
	s64 time_delta;
	bool touch;
	int error;

	do {
		time_next = ktime_add_ms(ktime_get(), ILI2XXX_POLL_PERIOD);
		error = chip->get_touch_data(client, touchdata);
		if (error) {
			dev_err(&client->dev,
				"Unable to get touch data: %d\n", error);
			break;
		}

		touch = ili210x_report_events(priv, touchdata);
		keep_polling = chip->continue_polling(touchdata, touch);
		if (keep_polling) {
			time_delta = ktime_us_delta(time_next, ktime_get());
			if (time_delta > 0)
				usleep_range(time_delta, time_delta + 1000);
		}
	} while (!priv->stop && keep_polling);

	return IRQ_HANDLED;
}

static int ili251x_firmware_update_resolution(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	u16 resx, resy;
	u8 rs[10];
	int error;

	/* The firmware update blob might have changed the resolution. */
	error = priv->chip->read_reg(client, REG_PANEL_INFO, &rs, sizeof(rs));
	if (error)
		return error;

	resx = le16_to_cpup((__le16 *)rs);
	resy = le16_to_cpup((__le16 *)(rs + 2));

	/* The value reported by the firmware is invalid. */
	if (!resx || resx == 0xffff || !resy || resy == 0xffff)
		return -EINVAL;

	input_abs_set_max(priv->input, ABS_X, resx - 1);
	input_abs_set_max(priv->input, ABS_Y, resy - 1);
	input_abs_set_max(priv->input, ABS_MT_POSITION_X, resx - 1);
	input_abs_set_max(priv->input, ABS_MT_POSITION_Y, resy - 1);

	return 0;
}

static ssize_t ili251x_firmware_update_firmware_version(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	int error;
	u8 fw[8];

	/* Get firmware version */
	error = priv->chip->read_reg(client, REG_FIRMWARE_VERSION,
				     &fw, sizeof(fw));
	if (!error)
		memcpy(priv->version_firmware, fw, sizeof(fw));

	return error;
}

static ssize_t ili251x_firmware_update_kernel_version(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	int error;
	u8 kv[5];

	/* Get kernel version */
	error = priv->chip->read_reg(client, REG_KERNEL_VERSION,
				     &kv, sizeof(kv));
	if (!error)
		memcpy(priv->version_kernel, kv, sizeof(kv));

	return error;
}

static ssize_t ili251x_firmware_update_protocol_version(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	int error;
	u8 pv[2];

	/* Get protocol version */
	error = priv->chip->read_reg(client, REG_PROTOCOL_VERSION,
				     &pv, sizeof(pv));
	if (!error)
		memcpy(priv->version_proto, pv, sizeof(pv));

	return error;
}

static ssize_t ili251x_firmware_update_ic_mode(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	int error;
	u8 md[2];

	/* Get chip boot mode */
	error = priv->chip->read_reg(client, REG_GET_MODE, &md, sizeof(md));
	if (!error)
		memcpy(priv->ic_mode, md, sizeof(md));

	return error;
}

static int ili251x_firmware_update_cached_state(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	int error;

	if (!priv->chip->has_firmware_proto)
		return 0;

	/* Wait for firmware to boot and stabilize itself. */
	msleep(200);

	/* Firmware does report valid information. */
	error = ili251x_firmware_update_resolution(dev);
	if (error)
		return error;

	error = ili251x_firmware_update_firmware_version(dev);
	if (error)
		return error;

	error = ili251x_firmware_update_kernel_version(dev);
	if (error)
		return error;

	error = ili251x_firmware_update_protocol_version(dev);
	if (error)
		return error;

	error = ili251x_firmware_update_ic_mode(dev);
	if (error)
		return error;

	return 0;
}

static ssize_t ili251x_firmware_version_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	u8 *fw = priv->version_firmware;

	return sysfs_emit(buf, "%02x%02x.%02x%02x.%02x%02x.%02x%02x\n",
			  fw[0], fw[1], fw[2], fw[3],
			  fw[4], fw[5], fw[6], fw[7]);
}
static DEVICE_ATTR(firmware_version, 0444, ili251x_firmware_version_show, NULL);

static ssize_t ili251x_kernel_version_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	u8 *kv = priv->version_kernel;

	return sysfs_emit(buf, "%02x.%02x.%02x.%02x.%02x\n",
			  kv[0], kv[1], kv[2], kv[3], kv[4]);
}
static DEVICE_ATTR(kernel_version, 0444, ili251x_kernel_version_show, NULL);

static ssize_t ili251x_protocol_version_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	u8 *pv = priv->version_proto;

	return sysfs_emit(buf, "%02x.%02x\n", pv[0], pv[1]);
}
static DEVICE_ATTR(protocol_version, 0444, ili251x_protocol_version_show, NULL);

static ssize_t ili251x_mode_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	u8 *md = priv->ic_mode;
	char *mode = "AP";

	if (md[0] == REG_GET_MODE_AP)		/* Application Mode */
		mode = "AP";
	else if (md[0] == REG_GET_MODE_BL)	/* BootLoader Mode */
		mode = "BL";
	else					/* Unknown Mode */
		mode = "??";

	return sysfs_emit(buf, "%02x.%02x:%s\n", md[0], md[1], mode);
}
static DEVICE_ATTR(mode, 0444, ili251x_mode_show, NULL);

static ssize_t ili210x_calibrate(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	unsigned long calibrate;
	int rc;
	u8 cmd = REG_CALIBRATE;

	if (kstrtoul(buf, 10, &calibrate))
		return -EINVAL;

	if (calibrate > 1)
		return -EINVAL;

	if (calibrate) {
		rc = i2c_master_send(priv->client, &cmd, sizeof(cmd));
		if (rc != sizeof(cmd))
			return -EIO;
	}

	return count;
}
static DEVICE_ATTR(calibrate, S_IWUSR, NULL, ili210x_calibrate);

static int ili251x_firmware_to_buffer(const struct firmware *fw,
				      u8 **buf, u16 *ac_end, u16 *df_end)
{
	const struct ihex_binrec *rec;
	u32 fw_addr, fw_last_addr = 0;
	u16 fw_len;
	u8 *fw_buf;
	int error;

	/*
	 * The firmware ihex blob can never be bigger than 64 kiB, so make this
	 * simple -- allocate a 64 kiB buffer, iterate over the ihex blob records
	 * once, copy them all into this buffer at the right locations, and then
	 * do all operations on this linear buffer.
	 */
	fw_buf = kzalloc(SZ_64K, GFP_KERNEL);
	if (!fw_buf)
		return -ENOMEM;

	rec = (const struct ihex_binrec *)fw->data;
	while (rec) {
		fw_addr = be32_to_cpu(rec->addr);
		fw_len = be16_to_cpu(rec->len);

		/* The last 32 Byte firmware block can be 0xffe0 */
		if (fw_addr + fw_len > SZ_64K || fw_addr > SZ_64K - 32) {
			error = -EFBIG;
			goto err_big;
		}

		/* Find the last address before DF start address, that is AC end */
		if (fw_addr == 0xf000)
			*ac_end = fw_last_addr;
		fw_last_addr = fw_addr + fw_len;

		memcpy(fw_buf + fw_addr, rec->data, fw_len);
		rec = ihex_next_binrec(rec);
	}

	/* DF end address is the last address in the firmware blob */
	*df_end = fw_addr + fw_len;
	*buf = fw_buf;
	return 0;

err_big:
	kfree(fw_buf);
	return error;
}

/* Switch mode between Application and BootLoader */
static int ili251x_switch_ic_mode(struct i2c_client *client, u8 cmd_mode)
{
	struct ili210x *priv = i2c_get_clientdata(client);
	u8 cmd_wren[3] = { REG_WRITE_ENABLE, 0x5a, 0xa5 };
	u8 md[2];
	int error;

	error = priv->chip->read_reg(client, REG_GET_MODE, md, sizeof(md));
	if (error)
		return error;
	/* Mode already set */
	if ((cmd_mode == REG_SET_MODE_AP && md[0] == REG_GET_MODE_AP) ||
	    (cmd_mode == REG_SET_MODE_BL && md[0] == REG_GET_MODE_BL))
		return 0;

	/* Unlock writes */
	error = i2c_master_send(client, cmd_wren, sizeof(cmd_wren));
	if (error != sizeof(cmd_wren))
		return -EINVAL;

	mdelay(20);

	/* Select mode (BootLoader or Application) */
	error = i2c_master_send(client, &cmd_mode, 1);
	if (error != 1)
		return -EINVAL;

	mdelay(200);	/* Reboot into bootloader takes a lot of time ... */

	/* Read back mode */
	error = priv->chip->read_reg(client, REG_GET_MODE, md, sizeof(md));
	if (error)
		return error;
	/* Check if mode is correct now. */
	if ((cmd_mode == REG_SET_MODE_AP && md[0] == REG_GET_MODE_AP) ||
	    (cmd_mode == REG_SET_MODE_BL && md[0] == REG_GET_MODE_BL))
		return 0;

	return -EINVAL;
}

static int ili251x_firmware_busy(struct i2c_client *client)
{
	struct ili210x *priv = i2c_get_clientdata(client);
	int error, i = 0;
	u8 data;

	do {
		/* The read_reg already contains suitable delay */
		error = priv->chip->read_reg(client, REG_IC_BUSY, &data, 1);
		if (error)
			return error;
		if (i++ == 100000)
			return -ETIMEDOUT;
	} while (data != REG_IC_BUSY_NOT_BUSY);

	return 0;
}

static int ili251x_firmware_write_to_ic(struct device *dev, u8 *fwbuf,
					u16 start, u16 end, u8 dataflash)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	u8 cmd_crc = REG_READ_DATA_CRC;
	u8 crcrb[4] = { 0 };
	u8 fw_data[33];
	u16 fw_addr;
	int error;

	/*
	 * The DF (dataflash) needs 2 bytes offset for unknown reasons,
	 * the AC (application) has 2 bytes CRC16-CCITT at the end.
	 */
	u16 crc = crc_ccitt(0, fwbuf + start + (dataflash ? 2 : 0),
			    end - start - 2);

	/* Unlock write to either AC (application) or DF (dataflash) area */
	u8 cmd_wr[10] = {
		REG_WRITE_ENABLE, 0x5a, 0xa5, dataflash,
		(end >> 16) & 0xff, (end >> 8) & 0xff, end & 0xff,
		(crc >> 16) & 0xff, (crc >> 8) & 0xff, crc & 0xff
	};

	error = i2c_master_send(client, cmd_wr, sizeof(cmd_wr));
	if (error != sizeof(cmd_wr))
		return -EINVAL;

	error = ili251x_firmware_busy(client);
	if (error)
		return error;

	for (fw_addr = start; fw_addr < end; fw_addr += 32) {
		fw_data[0] = REG_WRITE_DATA;
		memcpy(&(fw_data[1]), fwbuf + fw_addr, 32);
		error = i2c_master_send(client, fw_data, 33);
		if (error != sizeof(fw_data))
			return error;
		error = ili251x_firmware_busy(client);
		if (error)
			return error;
	}

	error = i2c_master_send(client, &cmd_crc, 1);
	if (error != 1)
		return -EINVAL;

	error = ili251x_firmware_busy(client);
	if (error)
		return error;

	error = priv->chip->read_reg(client, REG_READ_DATA_CRC,
				   &crcrb, sizeof(crcrb));
	if (error)
		return error;

	/* Check CRC readback */
	if ((crcrb[0] != (crc & 0xff)) || crcrb[1] != ((crc >> 8) & 0xff))
		return -EINVAL;

	return 0;
}

static int ili251x_firmware_reset(struct i2c_client *client)
{
	u8 cmd_reset[2] = { 0xf2, 0x01 };
	int error;

	error = i2c_master_send(client, cmd_reset, sizeof(cmd_reset));
	if (error != sizeof(cmd_reset))
		return -EINVAL;

	return ili251x_firmware_busy(client);
}

static void ili210x_hardware_reset(struct gpio_desc *reset_gpio)
{
	/* Reset the controller */
	gpiod_set_value_cansleep(reset_gpio, 1);
	usleep_range(12000, 15000);
	gpiod_set_value_cansleep(reset_gpio, 0);
	msleep(300);
}

static ssize_t ili210x_firmware_update_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);
	const char *fwname = ILI251X_FW_FILENAME;
	const struct firmware *fw;
	u16 ac_end, df_end;
	u8 *fwbuf;
	int error;
	int i;

	error = request_ihex_firmware(&fw, fwname, dev);
	if (error) {
		dev_err(dev, "Failed to request firmware %s, error=%d\n",
			fwname, error);
		return error;
	}

	error = ili251x_firmware_to_buffer(fw, &fwbuf, &ac_end, &df_end);
	release_firmware(fw);
	if (error)
		return error;

	/*
	 * Disable touchscreen IRQ, so that we would not get spurious touch
	 * interrupt during firmware update, and so that the IRQ handler won't
	 * trigger and interfere with the firmware update. There is no bit in
	 * the touch controller to disable the IRQs during update, so we have
	 * to do it this way here.
	 */
	disable_irq(client->irq);

	dev_dbg(dev, "Firmware update started, firmware=%s\n", fwname);

	ili210x_hardware_reset(priv->reset_gpio);

	error = ili251x_firmware_reset(client);
	if (error)
		goto exit;

	/* This may not succeed on first try, so re-try a few times. */
	for (i = 0; i < 5; i++) {
		error = ili251x_switch_ic_mode(client, REG_SET_MODE_BL);
		if (!error)
			break;
	}

	if (error)
		goto exit;

	dev_dbg(dev, "IC is now in BootLoader mode\n");

	msleep(200);	/* The bootloader seems to need some time too. */

	error = ili251x_firmware_write_to_ic(dev, fwbuf, 0xf000, df_end, 1);
	if (error) {
		dev_err(dev, "DF firmware update failed, error=%d\n", error);
		goto exit;
	}

	dev_dbg(dev, "DataFlash firmware written\n");

	error = ili251x_firmware_write_to_ic(dev, fwbuf, 0x2000, ac_end, 0);
	if (error) {
		dev_err(dev, "AC firmware update failed, error=%d\n", error);
		goto exit;
	}

	dev_dbg(dev, "Application firmware written\n");

	/* This may not succeed on first try, so re-try a few times. */
	for (i = 0; i < 5; i++) {
		error = ili251x_switch_ic_mode(client, REG_SET_MODE_AP);
		if (!error)
			break;
	}

	if (error)
		goto exit;

	dev_dbg(dev, "IC is now in Application mode\n");

	error = ili251x_firmware_update_cached_state(dev);
	if (error)
		goto exit;

	error = count;

exit:
	ili210x_hardware_reset(priv->reset_gpio);
	dev_dbg(dev, "Firmware update ended, error=%i\n", error);
	enable_irq(client->irq);
	kfree(fwbuf);
	return error;
}

static DEVICE_ATTR(firmware_update, 0200, NULL, ili210x_firmware_update_store);

static struct attribute *ili210x_attributes[] = {
	&dev_attr_calibrate.attr,
	&dev_attr_firmware_update.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_kernel_version.attr,
	&dev_attr_protocol_version.attr,
	&dev_attr_mode.attr,
	NULL,
};

static umode_t ili210x_attributes_visible(struct kobject *kobj,
					  struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct i2c_client *client = to_i2c_client(dev);
	struct ili210x *priv = i2c_get_clientdata(client);

	/* Calibrate is present on all ILI2xxx which have calibrate register */
	if (attr == &dev_attr_calibrate.attr)
		return priv->chip->has_calibrate_reg ? attr->mode : 0;

	/* Firmware/Kernel/Protocol/BootMode is implememted only for ILI251x */
	if (!priv->chip->has_firmware_proto)
		return 0;

	return attr->mode;
}

static const struct attribute_group ili210x_attr_group = {
	.attrs = ili210x_attributes,
	.is_visible = ili210x_attributes_visible,
};

static void ili210x_power_down(void *data)
{
	struct gpio_desc *reset_gpio = data;

	gpiod_set_value_cansleep(reset_gpio, 1);
}

static void ili210x_stop(void *data)
{
	struct ili210x *priv = data;

	/* Tell ISR to quit even if there is a contact. */
	priv->stop = true;
}

static int ili210x_i2c_probe(struct i2c_client *client)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(client);
	struct device *dev = &client->dev;
	const struct ili2xxx_chip *chip;
	struct ili210x *priv;
	struct gpio_desc *reset_gpio;
	struct input_dev *input;
	int error;
	unsigned int max_xy;

	dev_dbg(dev, "Probing for ILI210X I2C Touschreen driver");

	chip = device_get_match_data(dev);
	if (!chip && id)
		chip = (const struct ili2xxx_chip *)id->driver_data;
	if (!chip) {
		dev_err(&client->dev, "unknown device model\n");
		return -ENODEV;
	}

	if (client->irq <= 0) {
		dev_err(dev, "No IRQ!\n");
		return -EINVAL;
	}

	reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return PTR_ERR(reset_gpio);

	if (reset_gpio) {
		error = devm_add_action_or_reset(dev, ili210x_power_down,
						 reset_gpio);
		if (error)
			return error;

		ili210x_hardware_reset(reset_gpio);
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	input = devm_input_allocate_device(dev);
	if (!input)
		return -ENOMEM;

	priv->client = client;
	priv->input = input;
	priv->reset_gpio = reset_gpio;
	priv->chip = chip;
	i2c_set_clientdata(client, priv);

	/* Setup input device */
	input->name = "ILI210x Touchscreen";
	input->id.bustype = BUS_I2C;

	/* Multi touch */
	max_xy = (chip->resolution ?: SZ_64K) - 1;
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, max_xy, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, max_xy, 0, 0);
	if (priv->chip->has_pressure_reg)
		input_set_abs_params(input, ABS_MT_PRESSURE, 0, 0xa, 0, 0);
	error = ili251x_firmware_update_cached_state(dev);
	if (error) {
		dev_err(dev, "Unable to cache firmware information, err: %d\n",
			error);
		return error;
	}
	touchscreen_parse_properties(input, true, &priv->prop);

	error = input_mt_init_slots(input, priv->chip->max_touches,
				    INPUT_MT_DIRECT);
	if (error) {
		dev_err(dev, "Unable to set up slots, err: %d\n", error);
		return error;
	}

	error = devm_request_threaded_irq(dev, client->irq, NULL, ili210x_irq,
					  IRQF_ONESHOT, client->name, priv);
	if (error) {
		dev_err(dev, "Unable to request touchscreen IRQ, err: %d\n",
			error);
		return error;
	}

	error = devm_add_action_or_reset(dev, ili210x_stop, priv);
	if (error)
		return error;

	error = devm_device_add_group(dev, &ili210x_attr_group);
	if (error) {
		dev_err(dev, "Unable to create sysfs attributes, err: %d\n",
			error);
		return error;
	}

	error = input_register_device(priv->input);
	if (error) {
		dev_err(dev, "Cannot register input device, err: %d\n", error);
		return error;
	}

	return 0;
}

static const struct i2c_device_id ili210x_i2c_id[] = {
	{ "ili210x", (long)&ili210x_chip },
	{ "ili2117", (long)&ili211x_chip },
	{ "ili2120", (long)&ili212x_chip },
	{ "ili251x", (long)&ili251x_chip },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ili210x_i2c_id);

static const struct of_device_id ili210x_dt_ids[] = {
	{ .compatible = "ilitek,ili210x", .data = &ili210x_chip },
	{ .compatible = "ilitek,ili2117", .data = &ili211x_chip },
	{ .compatible = "ilitek,ili2120", .data = &ili212x_chip },
	{ .compatible = "ilitek,ili251x", .data = &ili251x_chip },
	{ }
};
MODULE_DEVICE_TABLE(of, ili210x_dt_ids);

static struct i2c_driver ili210x_ts_driver = {
	.driver = {
		.name = "ili210x_i2c",
		.of_match_table = ili210x_dt_ids,
	},
	.id_table = ili210x_i2c_id,
	.probe_new = ili210x_i2c_probe,
};

module_i2c_driver(ili210x_ts_driver);

MODULE_AUTHOR("Olivier Sobrie <olivier@sobrie.be>");
MODULE_DESCRIPTION("ILI210X I2C Touchscreen Driver");
MODULE_LICENSE("GPL");
