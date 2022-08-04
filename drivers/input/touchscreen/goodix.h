/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __GOODIX_H__
#define __GOODIX_H__

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/regulator/consumer.h>

/* Register defines */
#define GOODIX_REG_COMMAND			0x8040
#define GOODIX_CMD_SCREEN_OFF			0x05

#define GOODIX_GT1X_REG_CONFIG_DATA		0x8050
#define GOODIX_GT9X_REG_CONFIG_DATA		0x8047
#define GOODIX_REG_ID				0x8140
#define GOODIX_READ_COOR_ADDR			0x814E

#define GOODIX_ID_MAX_LEN			4
#define GOODIX_CONFIG_MAX_LENGTH		240
#define GOODIX_MAX_KEYS				7

enum goodix_irq_pin_access_method {
	IRQ_PIN_ACCESS_NONE,
	IRQ_PIN_ACCESS_GPIO,
	IRQ_PIN_ACCESS_ACPI_GPIO,
	IRQ_PIN_ACCESS_ACPI_METHOD,
};

struct goodix_ts_data;

struct goodix_chip_data {
	u16 config_addr;
	int config_len;
	int (*check_config)(struct goodix_ts_data *ts, const u8 *cfg, int len);
	void (*calc_config_checksum)(struct goodix_ts_data *ts);
};

struct goodix_ts_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	const struct goodix_chip_data *chip;
	struct touchscreen_properties prop;
	unsigned int max_touch_num;
	unsigned int int_trigger_type;
	struct regulator *avdd28;
	struct regulator *vddio;
	struct gpio_desc *gpiod_int;
	struct gpio_desc *gpiod_rst;
	int gpio_count;
	int gpio_int_idx;
	enum gpiod_flags gpiod_rst_flags;
	char id[GOODIX_ID_MAX_LEN + 1];
	u16 version;
	const char *cfg_name;
	bool reset_controller_at_probe;
	bool load_cfg_from_disk;
	struct completion firmware_loading_complete;
	unsigned long irq_flags;
	enum goodix_irq_pin_access_method irq_pin_access_method;
	unsigned int contact_size;
	u8 config[GOODIX_CONFIG_MAX_LENGTH];
	unsigned short keymap[GOODIX_MAX_KEYS];
};

int goodix_i2c_read(struct i2c_client *client, u16 reg, u8 *buf, int len);
int goodix_i2c_write(struct i2c_client *client, u16 reg, const u8 *buf, int len);
int goodix_i2c_write_u8(struct i2c_client *client, u16 reg, u8 value);
int goodix_send_cfg(struct goodix_ts_data *ts, const u8 *cfg, int len);
int goodix_int_sync(struct goodix_ts_data *ts);
int goodix_reset_no_int_sync(struct goodix_ts_data *ts);

#endif
