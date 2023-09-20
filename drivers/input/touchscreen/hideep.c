// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012-2017 Hideep, Inc.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/regulator/consumer.h>
#include <asm/unaligned.h>

#define HIDEEP_TS_NAME			"HiDeep Touchscreen"
#define HIDEEP_I2C_NAME			"hideep_ts"

#define HIDEEP_MT_MAX			10
#define HIDEEP_KEY_MAX			3

/* count(2) + touch data(100) + key data(6) */
#define HIDEEP_MAX_EVENT		108UL

#define HIDEEP_TOUCH_EVENT_INDEX	2
#define HIDEEP_KEY_EVENT_INDEX		102

/* Touch & key event */
#define HIDEEP_EVENT_ADDR		0x240

/* command list */
#define HIDEEP_WORK_MODE		0x081e
#define HIDEEP_RESET_CMD		0x9800

/* event bit */
#define HIDEEP_MT_RELEASED		BIT(4)
#define HIDEEP_KEY_PRESSED		BIT(7)
#define HIDEEP_KEY_FIRST_PRESSED	BIT(8)
#define HIDEEP_KEY_PRESSED_MASK		(HIDEEP_KEY_PRESSED | \
					 HIDEEP_KEY_FIRST_PRESSED)

#define HIDEEP_KEY_IDX_MASK		0x0f

/* For NVM */
#define HIDEEP_YRAM_BASE		0x40000000
#define HIDEEP_PERIPHERAL_BASE		0x50000000
#define HIDEEP_ESI_BASE			(HIDEEP_PERIPHERAL_BASE + 0x00000000)
#define HIDEEP_FLASH_BASE		(HIDEEP_PERIPHERAL_BASE + 0x01000000)
#define HIDEEP_SYSCON_BASE		(HIDEEP_PERIPHERAL_BASE + 0x02000000)

#define HIDEEP_SYSCON_MOD_CON		(HIDEEP_SYSCON_BASE + 0x0000)
#define HIDEEP_SYSCON_SPC_CON		(HIDEEP_SYSCON_BASE + 0x0004)
#define HIDEEP_SYSCON_CLK_CON		(HIDEEP_SYSCON_BASE + 0x0008)
#define HIDEEP_SYSCON_CLK_ENA		(HIDEEP_SYSCON_BASE + 0x000C)
#define HIDEEP_SYSCON_RST_CON		(HIDEEP_SYSCON_BASE + 0x0010)
#define HIDEEP_SYSCON_WDT_CON		(HIDEEP_SYSCON_BASE + 0x0014)
#define HIDEEP_SYSCON_WDT_CNT		(HIDEEP_SYSCON_BASE + 0x0018)
#define HIDEEP_SYSCON_PWR_CON		(HIDEEP_SYSCON_BASE + 0x0020)
#define HIDEEP_SYSCON_PGM_ID		(HIDEEP_SYSCON_BASE + 0x00F4)

#define HIDEEP_FLASH_CON		(HIDEEP_FLASH_BASE + 0x0000)
#define HIDEEP_FLASH_STA		(HIDEEP_FLASH_BASE + 0x0004)
#define HIDEEP_FLASH_CFG		(HIDEEP_FLASH_BASE + 0x0008)
#define HIDEEP_FLASH_TIM		(HIDEEP_FLASH_BASE + 0x000C)
#define HIDEEP_FLASH_CACHE_CFG		(HIDEEP_FLASH_BASE + 0x0010)
#define HIDEEP_FLASH_PIO_SIG		(HIDEEP_FLASH_BASE + 0x400000)

#define HIDEEP_ESI_TX_INVALID		(HIDEEP_ESI_BASE + 0x0008)

#define HIDEEP_PERASE			0x00040000
#define HIDEEP_WRONLY			0x00100000

#define HIDEEP_NVM_MASK_OFS		0x0000000C
#define HIDEEP_NVM_DEFAULT_PAGE		0
#define HIDEEP_NVM_SFR_WPAGE		1
#define HIDEEP_NVM_SFR_RPAGE		2

#define HIDEEP_PIO_SIG			0x00400000
#define HIDEEP_PROT_MODE		0x03400000

#define HIDEEP_NVM_PAGE_SIZE		128

#define HIDEEP_DWZ_INFO			0x000002C0

struct hideep_event {
	__le16 x;
	__le16 y;
	__le16 z;
	u8 w;
	u8 flag;
	u8 type;
	u8 index;
};

struct dwz_info {
	__be32 code_start;
	u8 code_crc[12];

	__be32 c_code_start;
	__be16 gen_ver;
	__be16 c_code_len;

	__be32 vr_start;
	__be16 rsv0;
	__be16 vr_len;

	__be32 ft_start;
	__be16 vr_version;
	__be16 ft_len;

	__be16 core_ver;
	__be16 boot_ver;

	__be16 release_ver;
	__be16 custom_ver;

	u8 factory_id;
	u8 panel_type;
	u8 model_name[6];

	__be16 extra_option;
	__be16 product_code;

	__be16 vendor_id;
	__be16 product_id;
};

struct pgm_packet {
	struct {
		u8 unused[3];
		u8 len;
		__be32 addr;
	} header;
	__be32 payload[HIDEEP_NVM_PAGE_SIZE / sizeof(__be32)];
};

#define HIDEEP_XFER_BUF_SIZE	sizeof(struct pgm_packet)

struct hideep_ts {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct regmap *reg;

	struct touchscreen_properties prop;

	struct gpio_desc *reset_gpio;

	struct regulator *vcc_vdd;
	struct regulator *vcc_vid;

	struct mutex dev_mutex;

	u32 tch_count;
	u32 lpm_count;

	/*
	 * Data buffer to read packet from the device (contacts and key
	 * states). We align it on double-word boundary to keep word-sized
	 * fields in contact data and double-word-sized fields in program
	 * packet aligned.
	 */
	u8 xfer_buf[HIDEEP_XFER_BUF_SIZE] __aligned(4);

	int key_num;
	u32 key_codes[HIDEEP_KEY_MAX];

	struct dwz_info dwz_info;

	unsigned int fw_size;
	u32 nvm_mask;
};

static int hideep_pgm_w_mem(struct hideep_ts *ts, u32 addr,
			    const __be32 *data, size_t count)
{
	struct pgm_packet *packet = (void *)ts->xfer_buf;
	size_t len = count * sizeof(*data);
	struct i2c_msg msg = {
		.addr	= ts->client->addr,
		.len	= len + sizeof(packet->header.len) +
				sizeof(packet->header.addr),
		.buf	= &packet->header.len,
	};
	int ret;

	if (len > HIDEEP_NVM_PAGE_SIZE)
		return -EINVAL;

	packet->header.len = 0x80 | (count - 1);
	packet->header.addr = cpu_to_be32(addr);
	memcpy(packet->payload, data, len);

	ret = i2c_transfer(ts->client->adapter, &msg, 1);
	if (ret != 1)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int hideep_pgm_r_mem(struct hideep_ts *ts, u32 addr,
			    __be32 *data, size_t count)
{
	struct pgm_packet *packet = (void *)ts->xfer_buf;
	size_t len = count * sizeof(*data);
	struct i2c_msg msg[] = {
		{
			.addr	= ts->client->addr,
			.len	= sizeof(packet->header.len) +
					sizeof(packet->header.addr),
			.buf	= &packet->header.len,
		},
		{
			.addr	= ts->client->addr,
			.flags	= I2C_M_RD,
			.len	= len,
			.buf	= (u8 *)data,
		},
	};
	int ret;

	if (len > HIDEEP_NVM_PAGE_SIZE)
		return -EINVAL;

	packet->header.len = count - 1;
	packet->header.addr = cpu_to_be32(addr);

	ret = i2c_transfer(ts->client->adapter, msg, ARRAY_SIZE(msg));
	if (ret != ARRAY_SIZE(msg))
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int hideep_pgm_r_reg(struct hideep_ts *ts, u32 addr, u32 *val)
{
	__be32 data;
	int error;

	error = hideep_pgm_r_mem(ts, addr, &data, 1);
	if (error) {
		dev_err(&ts->client->dev,
			"read of register %#08x failed: %d\n",
			addr, error);
		return error;
	}

	*val = be32_to_cpu(data);
	return 0;
}

static int hideep_pgm_w_reg(struct hideep_ts *ts, u32 addr, u32 val)
{
	__be32 data = cpu_to_be32(val);
	int error;

	error = hideep_pgm_w_mem(ts, addr, &data, 1);
	if (error) {
		dev_err(&ts->client->dev,
			"write to register %#08x (%#08x) failed: %d\n",
			addr, val, error);
		return error;
	}

	return 0;
}

#define SW_RESET_IN_PGM(clk)					\
{								\
	__be32 data = cpu_to_be32(0x01);			\
	hideep_pgm_w_reg(ts, HIDEEP_SYSCON_WDT_CNT, (clk));	\
	hideep_pgm_w_reg(ts, HIDEEP_SYSCON_WDT_CON, 0x03);	\
	/*							\
	 * The first write may already cause a reset, use a raw	\
	 * write for the second write to avoid error logging.	\
	 */							\
	hideep_pgm_w_mem(ts, HIDEEP_SYSCON_WDT_CON, &data, 1);	\
}

#define SET_FLASH_PIO(ce)					\
	hideep_pgm_w_reg(ts, HIDEEP_FLASH_CON,			\
			 0x01 | ((ce) << 1))

#define SET_PIO_SIG(x, y)					\
	hideep_pgm_w_reg(ts, HIDEEP_FLASH_PIO_SIG + (x), (y))

#define SET_FLASH_HWCONTROL()					\
	hideep_pgm_w_reg(ts, HIDEEP_FLASH_CON, 0x00)

#define NVM_W_SFR(x, y)						\
{								\
	SET_FLASH_PIO(1);					\
	SET_PIO_SIG(x, y);					\
	SET_FLASH_PIO(0);					\
}

static void hideep_pgm_set(struct hideep_ts *ts)
{
	hideep_pgm_w_reg(ts, HIDEEP_SYSCON_WDT_CON, 0x00);
	hideep_pgm_w_reg(ts, HIDEEP_SYSCON_SPC_CON, 0x00);
	hideep_pgm_w_reg(ts, HIDEEP_SYSCON_CLK_ENA, 0xFF);
	hideep_pgm_w_reg(ts, HIDEEP_SYSCON_CLK_CON, 0x01);
	hideep_pgm_w_reg(ts, HIDEEP_SYSCON_PWR_CON, 0x01);
	hideep_pgm_w_reg(ts, HIDEEP_FLASH_TIM, 0x03);
	hideep_pgm_w_reg(ts, HIDEEP_FLASH_CACHE_CFG, 0x00);
}

static int hideep_pgm_get_pattern(struct hideep_ts *ts, u32 *pattern)
{
	u16 p1 = 0xAF39;
	u16 p2 = 0xDF9D;
	int error;

	error = regmap_bulk_write(ts->reg, p1, &p2, 1);
	if (error) {
		dev_err(&ts->client->dev,
			"%s: regmap_bulk_write() failed with %d\n",
			__func__, error);
		return error;
	}

	usleep_range(1000, 1100);

	/* flush invalid Tx load register */
	error = hideep_pgm_w_reg(ts, HIDEEP_ESI_TX_INVALID, 0x01);
	if (error)
		return error;

	error = hideep_pgm_r_reg(ts, HIDEEP_SYSCON_PGM_ID, pattern);
	if (error)
		return error;

	return 0;
}

static int hideep_enter_pgm(struct hideep_ts *ts)
{
	int retry_count = 10;
	u32 pattern;
	int error;

	while (retry_count--) {
		error = hideep_pgm_get_pattern(ts, &pattern);
		if (error) {
			dev_err(&ts->client->dev,
				"hideep_pgm_get_pattern failed: %d\n", error);
		} else if (pattern != 0x39AF9DDF) {
			dev_err(&ts->client->dev, "%s: bad pattern: %#08x\n",
				__func__, pattern);
		} else {
			dev_dbg(&ts->client->dev, "found magic code");

			hideep_pgm_set(ts);
			usleep_range(1000, 1100);

			return 0;
		}
	}

	dev_err(&ts->client->dev, "failed to  enter pgm mode\n");
	SW_RESET_IN_PGM(1000);
	return -EIO;
}

static int hideep_nvm_unlock(struct hideep_ts *ts)
{
	u32 unmask_code;
	int error;

	hideep_pgm_w_reg(ts, HIDEEP_FLASH_CFG, HIDEEP_NVM_SFR_RPAGE);
	error = hideep_pgm_r_reg(ts, 0x0000000C, &unmask_code);
	hideep_pgm_w_reg(ts, HIDEEP_FLASH_CFG, HIDEEP_NVM_DEFAULT_PAGE);
	if (error)
		return error;

	/* make it unprotected code */
	unmask_code &= ~HIDEEP_PROT_MODE;

	/* compare unmask code */
	if (unmask_code != ts->nvm_mask)
		dev_warn(&ts->client->dev,
			 "read mask code different %#08x vs %#08x",
			 unmask_code, ts->nvm_mask);

	hideep_pgm_w_reg(ts, HIDEEP_FLASH_CFG, HIDEEP_NVM_SFR_WPAGE);
	SET_FLASH_PIO(0);

	NVM_W_SFR(HIDEEP_NVM_MASK_OFS, ts->nvm_mask);
	SET_FLASH_HWCONTROL();
	hideep_pgm_w_reg(ts, HIDEEP_FLASH_CFG, HIDEEP_NVM_DEFAULT_PAGE);

	return 0;
}

static int hideep_check_status(struct hideep_ts *ts)
{
	int time_out = 100;
	int status;
	int error;

	while (time_out--) {
		error = hideep_pgm_r_reg(ts, HIDEEP_FLASH_STA, &status);
		if (!error && status)
			return 0;

		usleep_range(1000, 1100);
	}

	return -ETIMEDOUT;
}

static int hideep_program_page(struct hideep_ts *ts, u32 addr,
			       const __be32 *ucode, size_t xfer_count)
{
	u32 val;
	int error;

	error = hideep_check_status(ts);
	if (error)
		return -EBUSY;

	addr &= ~(HIDEEP_NVM_PAGE_SIZE - 1);

	SET_FLASH_PIO(0);
	SET_FLASH_PIO(1);

	/* erase page */
	SET_PIO_SIG(HIDEEP_PERASE | addr, 0xFFFFFFFF);

	SET_FLASH_PIO(0);

	error = hideep_check_status(ts);
	if (error)
		return -EBUSY;

	/* write page */
	SET_FLASH_PIO(1);

	val = be32_to_cpu(ucode[0]);
	SET_PIO_SIG(HIDEEP_WRONLY | addr, val);

	hideep_pgm_w_mem(ts, HIDEEP_FLASH_PIO_SIG | HIDEEP_WRONLY,
			 ucode, xfer_count);

	val = be32_to_cpu(ucode[xfer_count - 1]);
	SET_PIO_SIG(124, val);

	SET_FLASH_PIO(0);

	usleep_range(1000, 1100);

	error = hideep_check_status(ts);
	if (error)
		return -EBUSY;

	SET_FLASH_HWCONTROL();

	return 0;
}

static int hideep_program_nvm(struct hideep_ts *ts,
			      const __be32 *ucode, size_t ucode_len)
{
	struct pgm_packet *packet_r = (void *)ts->xfer_buf;
	__be32 *current_ucode = packet_r->payload;
	size_t xfer_len;
	size_t xfer_count;
	u32 addr = 0;
	int error;

	error = hideep_nvm_unlock(ts);
	if (error)
		return error;

	while (ucode_len > 0) {
		xfer_len = min_t(size_t, ucode_len, HIDEEP_NVM_PAGE_SIZE);
		xfer_count = xfer_len / sizeof(*ucode);

		error = hideep_pgm_r_mem(ts, 0x00000000 + addr,
					 current_ucode, xfer_count);
		if (error) {
			dev_err(&ts->client->dev,
				"%s: failed to read page at offset %#08x: %d\n",
				__func__, addr, error);
			return error;
		}

		/* See if the page needs updating */
		if (memcmp(ucode, current_ucode, xfer_len)) {
			error = hideep_program_page(ts, addr,
						    ucode, xfer_count);
			if (error) {
				dev_err(&ts->client->dev,
					"%s: iwrite failure @%#08x: %d\n",
					__func__, addr, error);
				return error;
			}

			usleep_range(1000, 1100);
		}

		ucode += xfer_count;
		addr += xfer_len;
		ucode_len -= xfer_len;
	}

	return 0;
}

static int hideep_verify_nvm(struct hideep_ts *ts,
			     const __be32 *ucode, size_t ucode_len)
{
	struct pgm_packet *packet_r = (void *)ts->xfer_buf;
	__be32 *current_ucode = packet_r->payload;
	size_t xfer_len;
	size_t xfer_count;
	u32 addr = 0;
	int i;
	int error;

	while (ucode_len > 0) {
		xfer_len = min_t(size_t, ucode_len, HIDEEP_NVM_PAGE_SIZE);
		xfer_count = xfer_len / sizeof(*ucode);

		error = hideep_pgm_r_mem(ts, 0x00000000 + addr,
					 current_ucode, xfer_count);
		if (error) {
			dev_err(&ts->client->dev,
				"%s: failed to read page at offset %#08x: %d\n",
				__func__, addr, error);
			return error;
		}

		if (memcmp(ucode, current_ucode, xfer_len)) {
			const u8 *ucode_bytes = (const u8 *)ucode;
			const u8 *current_bytes = (const u8 *)current_ucode;

			for (i = 0; i < xfer_len; i++)
				if (ucode_bytes[i] != current_bytes[i])
					dev_err(&ts->client->dev,
						"%s: mismatch @%#08x: (%#02x vs %#02x)\n",
						__func__, addr + i,
						ucode_bytes[i],
						current_bytes[i]);

			return -EIO;
		}

		ucode += xfer_count;
		addr += xfer_len;
		ucode_len -= xfer_len;
	}

	return 0;
}

static int hideep_load_dwz(struct hideep_ts *ts)
{
	u16 product_code;
	int error;

	error = hideep_enter_pgm(ts);
	if (error)
		return error;

	msleep(50);

	error = hideep_pgm_r_mem(ts, HIDEEP_DWZ_INFO,
				 (void *)&ts->dwz_info,
				 sizeof(ts->dwz_info) / sizeof(__be32));

	SW_RESET_IN_PGM(10);
	msleep(50);

	if (error) {
		dev_err(&ts->client->dev,
			"failed to fetch DWZ data: %d\n", error);
		return error;
	}

	product_code = be16_to_cpu(ts->dwz_info.product_code);

	switch (product_code & 0xF0) {
	case 0x40:
		dev_dbg(&ts->client->dev, "used crimson IC");
		ts->fw_size = 1024 * 48;
		ts->nvm_mask = 0x00310000;
		break;
	case 0x60:
		dev_dbg(&ts->client->dev, "used lime IC");
		ts->fw_size = 1024 * 64;
		ts->nvm_mask = 0x0030027B;
		break;
	default:
		dev_err(&ts->client->dev, "product code is wrong: %#04x",
			product_code);
		return -EINVAL;
	}

	dev_dbg(&ts->client->dev, "firmware release version: %#04x",
		be16_to_cpu(ts->dwz_info.release_ver));

	return 0;
}

static int hideep_flash_firmware(struct hideep_ts *ts,
				 const __be32 *ucode, size_t ucode_len)
{
	int retry_cnt = 3;
	int error;

	while (retry_cnt--) {
		error = hideep_program_nvm(ts, ucode, ucode_len);
		if (!error) {
			error = hideep_verify_nvm(ts, ucode, ucode_len);
			if (!error)
				return 0;
		}
	}

	return error;
}

static int hideep_update_firmware(struct hideep_ts *ts,
				  const __be32 *ucode, size_t ucode_len)
{
	int error, error2;

	dev_dbg(&ts->client->dev, "starting firmware update");

	/* enter program mode */
	error = hideep_enter_pgm(ts);
	if (error)
		return error;

	error = hideep_flash_firmware(ts, ucode, ucode_len);
	if (error)
		dev_err(&ts->client->dev,
			"firmware update failed: %d\n", error);
	else
		dev_dbg(&ts->client->dev, "firmware updated successfully\n");

	SW_RESET_IN_PGM(1000);

	error2 = hideep_load_dwz(ts);
	if (error2)
		dev_err(&ts->client->dev,
			"failed to load dwz after firmware update: %d\n",
			error2);

	return error ?: error2;
}

static int hideep_power_on(struct hideep_ts *ts)
{
	int error = 0;

	error = regulator_enable(ts->vcc_vdd);
	if (error)
		dev_err(&ts->client->dev,
			"failed to enable 'vdd' regulator: %d", error);

	usleep_range(999, 1000);

	error = regulator_enable(ts->vcc_vid);
	if (error)
		dev_err(&ts->client->dev,
			"failed to enable 'vcc_vid' regulator: %d",
			error);

	msleep(30);

	if (ts->reset_gpio) {
		gpiod_set_value_cansleep(ts->reset_gpio, 0);
	} else {
		error = regmap_write(ts->reg, HIDEEP_RESET_CMD, 0x01);
		if (error)
			dev_err(&ts->client->dev,
				"failed to send 'reset' command: %d\n", error);
	}

	msleep(50);

	return error;
}

static void hideep_power_off(void *data)
{
	struct hideep_ts *ts = data;

	if (ts->reset_gpio)
		gpiod_set_value(ts->reset_gpio, 1);

	regulator_disable(ts->vcc_vid);
	regulator_disable(ts->vcc_vdd);
}

#define __GET_MT_TOOL_TYPE(type) ((type) == 0x01 ? MT_TOOL_FINGER : MT_TOOL_PEN)

static void hideep_report_slot(struct input_dev *input,
			       const struct hideep_event *event)
{
	input_mt_slot(input, event->index & 0x0f);
	input_mt_report_slot_state(input,
				   __GET_MT_TOOL_TYPE(event->type),
				   !(event->flag & HIDEEP_MT_RELEASED));
	if (!(event->flag & HIDEEP_MT_RELEASED)) {
		input_report_abs(input, ABS_MT_POSITION_X,
				 le16_to_cpup(&event->x));
		input_report_abs(input, ABS_MT_POSITION_Y,
				 le16_to_cpup(&event->y));
		input_report_abs(input, ABS_MT_PRESSURE,
				 le16_to_cpup(&event->z));
		input_report_abs(input, ABS_MT_TOUCH_MAJOR, event->w);
	}
}

static void hideep_parse_and_report(struct hideep_ts *ts)
{
	const struct hideep_event *events =
			(void *)&ts->xfer_buf[HIDEEP_TOUCH_EVENT_INDEX];
	const u8 *keys = &ts->xfer_buf[HIDEEP_KEY_EVENT_INDEX];
	int touch_count = ts->xfer_buf[0];
	int key_count = ts->xfer_buf[1] & 0x0f;
	int lpm_count = ts->xfer_buf[1] & 0xf0;
	int i;

	/* get touch event count */
	dev_dbg(&ts->client->dev, "mt = %d, key = %d, lpm = %02x",
		touch_count, key_count, lpm_count);

	touch_count = min(touch_count, HIDEEP_MT_MAX);
	for (i = 0; i < touch_count; i++)
		hideep_report_slot(ts->input_dev, events + i);

	key_count = min(key_count, HIDEEP_KEY_MAX);
	for (i = 0; i < key_count; i++) {
		u8 key_data = keys[i * 2];

		input_report_key(ts->input_dev,
				 ts->key_codes[key_data & HIDEEP_KEY_IDX_MASK],
				 key_data & HIDEEP_KEY_PRESSED_MASK);
	}

	input_mt_sync_frame(ts->input_dev);
	input_sync(ts->input_dev);
}

static irqreturn_t hideep_irq(int irq, void *handle)
{
	struct hideep_ts *ts = handle;
	int error;

	BUILD_BUG_ON(HIDEEP_MAX_EVENT > HIDEEP_XFER_BUF_SIZE);

	error = regmap_bulk_read(ts->reg, HIDEEP_EVENT_ADDR,
				 ts->xfer_buf, HIDEEP_MAX_EVENT / 2);
	if (error) {
		dev_err(&ts->client->dev, "failed to read events: %d\n", error);
		goto out;
	}

	hideep_parse_and_report(ts);

out:
	return IRQ_HANDLED;
}

static int hideep_get_axis_info(struct hideep_ts *ts)
{
	__le16 val[2];
	int error;

	error = regmap_bulk_read(ts->reg, 0x28, val, ARRAY_SIZE(val));
	if (error)
		return error;

	ts->prop.max_x = le16_to_cpup(val);
	ts->prop.max_y = le16_to_cpup(val + 1);

	dev_dbg(&ts->client->dev, "X: %d, Y: %d",
		ts->prop.max_x, ts->prop.max_y);

	return 0;
}

static int hideep_init_input(struct hideep_ts *ts)
{
	struct device *dev = &ts->client->dev;
	int i;
	int error;

	ts->input_dev = devm_input_allocate_device(dev);
	if (!ts->input_dev) {
		dev_err(dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	ts->input_dev->name = HIDEEP_TS_NAME;
	ts->input_dev->id.bustype = BUS_I2C;
	input_set_drvdata(ts->input_dev, ts);

	input_set_capability(ts->input_dev, EV_ABS, ABS_MT_POSITION_X);
	input_set_capability(ts->input_dev, EV_ABS, ABS_MT_POSITION_Y);
	input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, 65535, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_TOOL_TYPE,
			     0, MT_TOOL_MAX, 0, 0);
	touchscreen_parse_properties(ts->input_dev, true, &ts->prop);

	if (ts->prop.max_x == 0 || ts->prop.max_y == 0) {
		error = hideep_get_axis_info(ts);
		if (error)
			return error;
	}

	error = input_mt_init_slots(ts->input_dev, HIDEEP_MT_MAX,
				    INPUT_MT_DIRECT);
	if (error)
		return error;

	ts->key_num = device_property_count_u32(dev, "linux,keycodes");
	if (ts->key_num > HIDEEP_KEY_MAX) {
		dev_err(dev, "too many keys defined: %d\n",
			ts->key_num);
		return -EINVAL;
	}

	if (ts->key_num <= 0) {
		dev_dbg(dev,
			"missing or malformed 'linux,keycodes' property\n");
	} else {
		error = device_property_read_u32_array(dev, "linux,keycodes",
						       ts->key_codes,
						       ts->key_num);
		if (error) {
			dev_dbg(dev, "failed to read keymap: %d", error);
			return error;
		}

		if (ts->key_num) {
			ts->input_dev->keycode = ts->key_codes;
			ts->input_dev->keycodesize = sizeof(ts->key_codes[0]);
			ts->input_dev->keycodemax = ts->key_num;

			for (i = 0; i < ts->key_num; i++)
				input_set_capability(ts->input_dev, EV_KEY,
					ts->key_codes[i]);
		}
	}

	error = input_register_device(ts->input_dev);
	if (error) {
		dev_err(dev, "failed to register input device: %d", error);
		return error;
	}

	return 0;
}

static ssize_t hideep_update_fw(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hideep_ts *ts = i2c_get_clientdata(client);
	const struct firmware *fw_entry;
	char *fw_name;
	int mode;
	int error;

	error = kstrtoint(buf, 0, &mode);
	if (error)
		return error;

	fw_name = kasprintf(GFP_KERNEL, "hideep_ts_%04x.bin",
			    be16_to_cpu(ts->dwz_info.product_id));
	if (!fw_name)
		return -ENOMEM;

	error = request_firmware(&fw_entry, fw_name, dev);
	if (error) {
		dev_err(dev, "failed to request firmware %s: %d",
			fw_name, error);
		goto out_free_fw_name;
	}

	if (fw_entry->size % sizeof(__be32)) {
		dev_err(dev, "invalid firmware size %zu\n", fw_entry->size);
		error = -EINVAL;
		goto out_release_fw;
	}

	if (fw_entry->size > ts->fw_size) {
		dev_err(dev, "fw size (%zu) is too big (memory size %d)\n",
			fw_entry->size, ts->fw_size);
		error = -EFBIG;
		goto out_release_fw;
	}

	mutex_lock(&ts->dev_mutex);
	disable_irq(client->irq);

	error = hideep_update_firmware(ts, (const __be32 *)fw_entry->data,
				       fw_entry->size);

	enable_irq(client->irq);
	mutex_unlock(&ts->dev_mutex);

out_release_fw:
	release_firmware(fw_entry);
out_free_fw_name:
	kfree(fw_name);

	return error ?: count;
}

static ssize_t hideep_fw_version_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hideep_ts *ts = i2c_get_clientdata(client);
	ssize_t len;

	mutex_lock(&ts->dev_mutex);
	len = scnprintf(buf, PAGE_SIZE, "%04x\n",
			be16_to_cpu(ts->dwz_info.release_ver));
	mutex_unlock(&ts->dev_mutex);

	return len;
}

static ssize_t hideep_product_id_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hideep_ts *ts = i2c_get_clientdata(client);
	ssize_t len;

	mutex_lock(&ts->dev_mutex);
	len = scnprintf(buf, PAGE_SIZE, "%04x\n",
			be16_to_cpu(ts->dwz_info.product_id));
	mutex_unlock(&ts->dev_mutex);

	return len;
}

static DEVICE_ATTR(version, 0664, hideep_fw_version_show, NULL);
static DEVICE_ATTR(product_id, 0664, hideep_product_id_show, NULL);
static DEVICE_ATTR(update_fw, 0664, NULL, hideep_update_fw);

static struct attribute *hideep_ts_sysfs_entries[] = {
	&dev_attr_version.attr,
	&dev_attr_product_id.attr,
	&dev_attr_update_fw.attr,
	NULL,
};

static const struct attribute_group hideep_ts_attr_group = {
	.attrs = hideep_ts_sysfs_entries,
};

static void hideep_set_work_mode(struct hideep_ts *ts)
{
	/*
	 * Reset touch report format to the native HiDeep 20 protocol if requested.
	 * This is necessary to make touchscreens which come up in I2C-HID mode
	 * work with this driver.
	 *
	 * Note this is a kernel internal device-property set by x86 platform code,
	 * this MUST not be used in devicetree files without first adding it to
	 * the DT bindings.
	 */
	if (device_property_read_bool(&ts->client->dev, "hideep,force-native-protocol"))
		regmap_write(ts->reg, HIDEEP_WORK_MODE, 0x00);
}

static int hideep_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hideep_ts *ts = i2c_get_clientdata(client);

	disable_irq(client->irq);
	hideep_power_off(ts);

	return 0;
}

static int hideep_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct hideep_ts *ts = i2c_get_clientdata(client);
	int error;

	error = hideep_power_on(ts);
	if (error) {
		dev_err(&client->dev, "power on failed");
		return error;
	}

	hideep_set_work_mode(ts);

	enable_irq(client->irq);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(hideep_pm_ops, hideep_suspend, hideep_resume);

static const struct regmap_config hideep_regmap_config = {
	.reg_bits = 16,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_bits = 16,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.max_register = 0xffff,
};

static int hideep_probe(struct i2c_client *client)
{
	struct hideep_ts *ts;
	int error;

	/* check i2c bus */
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "check i2c device error");
		return -ENODEV;
	}

	if (client->irq <= 0) {
		dev_err(&client->dev, "missing irq: %d\n", client->irq);
		return -EINVAL;
	}

	ts = devm_kzalloc(&client->dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	ts->client = client;
	i2c_set_clientdata(client, ts);
	mutex_init(&ts->dev_mutex);

	ts->reg = devm_regmap_init_i2c(client, &hideep_regmap_config);
	if (IS_ERR(ts->reg)) {
		error = PTR_ERR(ts->reg);
		dev_err(&client->dev,
			"failed to initialize regmap: %d\n", error);
		return error;
	}

	ts->vcc_vdd = devm_regulator_get(&client->dev, "vdd");
	if (IS_ERR(ts->vcc_vdd))
		return PTR_ERR(ts->vcc_vdd);

	ts->vcc_vid = devm_regulator_get(&client->dev, "vid");
	if (IS_ERR(ts->vcc_vid))
		return PTR_ERR(ts->vcc_vid);

	ts->reset_gpio = devm_gpiod_get_optional(&client->dev,
						 "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ts->reset_gpio))
		return PTR_ERR(ts->reset_gpio);

	error = hideep_power_on(ts);
	if (error) {
		dev_err(&client->dev, "power on failed: %d\n", error);
		return error;
	}

	error = devm_add_action_or_reset(&client->dev, hideep_power_off, ts);
	if (error)
		return error;

	error = hideep_load_dwz(ts);
	if (error) {
		dev_err(&client->dev, "failed to load dwz: %d", error);
		return error;
	}

	hideep_set_work_mode(ts);

	error = hideep_init_input(ts);
	if (error)
		return error;

	error = devm_request_threaded_irq(&client->dev, client->irq,
					  NULL, hideep_irq, IRQF_ONESHOT,
					  client->name, ts);
	if (error) {
		dev_err(&client->dev, "failed to request irq %d: %d\n",
			client->irq, error);
		return error;
	}

	error = devm_device_add_group(&client->dev, &hideep_ts_attr_group);
	if (error) {
		dev_err(&client->dev,
			"failed to add sysfs attributes: %d\n", error);
		return error;
	}

	return 0;
}

static const struct i2c_device_id hideep_i2c_id[] = {
	{ HIDEEP_I2C_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, hideep_i2c_id);

#ifdef CONFIG_ACPI
static const struct acpi_device_id hideep_acpi_id[] = {
	{ "HIDP0001", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, hideep_acpi_id);
#endif

#ifdef CONFIG_OF
static const struct of_device_id hideep_match_table[] = {
	{ .compatible = "hideep,hideep-ts" },
	{ }
};
MODULE_DEVICE_TABLE(of, hideep_match_table);
#endif

static struct i2c_driver hideep_driver = {
	.driver = {
		.name			= HIDEEP_I2C_NAME,
		.of_match_table		= of_match_ptr(hideep_match_table),
		.acpi_match_table	= ACPI_PTR(hideep_acpi_id),
		.pm			= pm_sleep_ptr(&hideep_pm_ops),
	},
	.id_table	= hideep_i2c_id,
	.probe		= hideep_probe,
};

module_i2c_driver(hideep_driver);

MODULE_DESCRIPTION("Driver for HiDeep Touchscreen Controller");
MODULE_AUTHOR("anthony.kim@hideep.com");
MODULE_LICENSE("GPL v2");
