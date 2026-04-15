// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/crc8.h>
#include <linux/firmware.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/sizes.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <drm/drm_bridge.h>
#include <drm/drm_of.h>

#define FW_FILE "lt8713sx_fw.bin"

#define REG_PAGE_CONTROL	0xff

#define LT8713SX_PAGE_SIZE	256

DECLARE_CRC8_TABLE(lt8713sx_crc_table);

struct lt8713sx {
	struct device *dev;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;

	struct regmap *regmap;
	/* Protects all accesses to registers by stopping the on-chip MCU */
	struct mutex ocm_lock;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;

	struct i2c_client *client;
	const struct firmware *fw;

	u8 *fw_buffer;

	u32 main_crc_value;
	u32 bank_crc_value[17];

	int bank_num;
};

static void lt8713sx_reset(struct lt8713sx *lt8713sx);

static const struct regmap_range lt8713sx_ranges[] = {
	{
		.range_min = 0x0000,
		.range_max = 0xffff
	},
};

static const struct regmap_access_table lt8713sx_table = {
	.yes_ranges = lt8713sx_ranges,
	.n_yes_ranges = ARRAY_SIZE(lt8713sx_ranges),
};

static const struct regmap_range_cfg lt8713sx_range_cfg = {
	.name = "lt8713sx",
	.range_min = 0x0000,
	.range_max = 0xffff,
	.selector_reg = REG_PAGE_CONTROL,
	.selector_mask = 0xff,
	.selector_shift = 0,
	.window_start = 0,
	.window_len = 0x100,
};

static const struct regmap_config lt8713sx_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.volatile_table = &lt8713sx_table,
	.ranges = &lt8713sx_range_cfg,
	.num_ranges = 1,
	.cache_type = REGCACHE_NONE,
	.max_register = 0xffff,
};

static void lt8713sx_i2c_enable(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xe0ee, 0x01);
}

static void lt8713sx_i2c_disable(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xe0ee, 0x00);
}

static int lt8713sx_prepare_firmware_data(struct lt8713sx *lt8713sx)
{
	int ret = 0;
	size_t sz_12k = 12 * SZ_1K;

	ret = request_firmware(&lt8713sx->fw, FW_FILE, lt8713sx->dev);
	if (ret < 0) {
		dev_err(lt8713sx->dev, "request firmware failed\n");
		return ret;
	}

	dev_dbg(lt8713sx->dev, "Firmware size: %zu bytes\n", lt8713sx->fw->size);

	if (lt8713sx->fw->size > SZ_256K - 1) {
		dev_err(lt8713sx->dev, "Firmware size exceeds 256KB limit\n");
		release_firmware(lt8713sx->fw);
		return -EINVAL;
	}

	lt8713sx->fw_buffer = kvmalloc(SZ_256K, GFP_KERNEL);
	if (!lt8713sx->fw_buffer) {
		release_firmware(lt8713sx->fw);
		return -ENOMEM;
	}

	memset(lt8713sx->fw_buffer, 0xff, SZ_256K);

	/* main firmware */
	memcpy(lt8713sx->fw_buffer, lt8713sx->fw->data, SZ_64K - 1);

	lt8713sx->fw_buffer[SZ_64K - 1] =
		crc8(lt8713sx_crc_table, lt8713sx->fw_buffer, SZ_64K - 1, 0);
	lt8713sx->main_crc_value = lt8713sx->fw_buffer[SZ_64K - 1];
	dev_dbg(lt8713sx->dev,
		"Main Firmware Data  Crc = 0x%02X\n", lt8713sx->main_crc_value);

	/* bank firmware */
	memcpy(lt8713sx->fw_buffer + SZ_64K,
	       lt8713sx->fw->data + SZ_64K,
	       lt8713sx->fw->size - SZ_64K);

	lt8713sx->bank_num = (lt8713sx->fw->size - SZ_64K + sz_12k - 1) / sz_12k;
	dev_dbg(lt8713sx->dev, "Bank Number Total is %d.\n", lt8713sx->bank_num);

	for (int i = 0; i < lt8713sx->bank_num; i++) {
		lt8713sx->bank_crc_value[i] =
			crc8(lt8713sx_crc_table, lt8713sx->fw_buffer + SZ_64K + i * sz_12k,
			     sz_12k, 0);
		dev_dbg(lt8713sx->dev, "Bank number:%d; Firmware Data  Crc:0x%02X\n",
			i, lt8713sx->bank_crc_value[i]);
	}
	return 0;
}

static void lt8713sx_config_parameters(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xe05e, 0xc1);
	regmap_write(lt8713sx->regmap, 0xe058, 0x00);
	regmap_write(lt8713sx->regmap, 0xe059, 0x50);
	regmap_write(lt8713sx->regmap, 0xe05a, 0x10);
	regmap_write(lt8713sx->regmap, 0xe05a, 0x00);
	regmap_write(lt8713sx->regmap, 0xe058, 0x21);
}

static void lt8713sx_wren(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xe103, 0xbf);
	regmap_write(lt8713sx->regmap, 0xe103, 0xff);
	regmap_write(lt8713sx->regmap, 0xe05a, 0x04);
	regmap_write(lt8713sx->regmap, 0xe05a, 0x00);
}

static void lt8713sx_wrdi(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xe05a, 0x08);
	regmap_write(lt8713sx->regmap, 0xe05a, 0x00);
}

static void lt8713sx_fifo_reset(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xe103, 0xbf);
	regmap_write(lt8713sx->regmap, 0xe103, 0xff);
}

static void lt8713sx_disable_sram_write(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xe055, 0x00);
}

static void lt8713sx_sram_to_flash(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xe05a, 0x30);
	regmap_write(lt8713sx->regmap, 0xe05a, 0x00);
}

static void lt8713sx_i2c_to_sram(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xe055, 0x80);
	regmap_write(lt8713sx->regmap, 0xe05e, 0xc0);
	regmap_write(lt8713sx->regmap, 0xe058, 0x21);
}

static u8 lt8713sx_read_flash_status(struct lt8713sx *lt8713sx)
{
	u32 flash_status = 0;

	regmap_write(lt8713sx->regmap,  0xe103, 0x3f);
	regmap_write(lt8713sx->regmap,  0xe103, 0xff);

	regmap_write(lt8713sx->regmap,  0xe05e, 0x40);
	regmap_write(lt8713sx->regmap,  0xe056, 0x05); /* opcode=read status register */
	regmap_write(lt8713sx->regmap,  0xe055, 0x25);
	regmap_write(lt8713sx->regmap,  0xe055, 0x01);
	regmap_write(lt8713sx->regmap,  0xe058, 0x21);

	regmap_read(lt8713sx->regmap, 0xe05f, &flash_status);
	dev_dbg(lt8713sx->dev, "flash_status:%x\n", flash_status);

	return flash_status;
}

static void lt8713sx_block_erase(struct lt8713sx *lt8713sx)
{
	u32 i = 0;
	u8 flash_status = 0;
	u8 blocknum = 0x00;
	u32 flashaddr = 0x00;

	for (blocknum = 0; blocknum < 8; blocknum++) {
		flashaddr = blocknum * SZ_32K;
		regmap_write(lt8713sx->regmap,  0xe05a, 0x04);
		regmap_write(lt8713sx->regmap,  0xe05a, 0x00);
		regmap_write(lt8713sx->regmap,  0xe05b, flashaddr >> 16);
		regmap_write(lt8713sx->regmap,  0xe05c, flashaddr >> 8);
		regmap_write(lt8713sx->regmap,  0xe05d, flashaddr);
		regmap_write(lt8713sx->regmap,  0xe05a, 0x01);
		regmap_write(lt8713sx->regmap,  0xe05a, 0x00);
		msleep(100);
		i = 0;
		while (1) {
			flash_status = lt8713sx_read_flash_status(lt8713sx);
			if ((flash_status & 0x01) == 0)
				break;

			if (i > 50)
				break;

			i++;
			msleep(50);
		}
	}
	dev_dbg(lt8713sx->dev, "erase flash done.\n");
}

static void lt8713sx_load_main_fw_to_sram(struct lt8713sx *lt8713sx)
{
	regmap_write(lt8713sx->regmap, 0xe068, 0x00);
	regmap_write(lt8713sx->regmap, 0xe069, 0x00);
	regmap_write(lt8713sx->regmap, 0xe06a, 0x00);
	regmap_write(lt8713sx->regmap, 0xe065, 0x00);
	regmap_write(lt8713sx->regmap, 0xe066, 0xff);
	regmap_write(lt8713sx->regmap, 0xe067, 0xff);
	regmap_write(lt8713sx->regmap, 0xe06b, 0x00);
	regmap_write(lt8713sx->regmap, 0xe06c, 0x00);
	regmap_write(lt8713sx->regmap, 0xe060, 0x01);
	msleep(200);
	regmap_write(lt8713sx->regmap, 0xe060, 0x00);
}

static void lt8713sx_load_bank_fw_to_sram(struct lt8713sx *lt8713sx, u64 addr)
{
	regmap_write(lt8713sx->regmap, 0xe068, ((addr & 0xff0000) >> 16));
	regmap_write(lt8713sx->regmap, 0xe069, ((addr & 0x00ff00) >> 8));
	regmap_write(lt8713sx->regmap, 0xe06a, (addr & 0x0000ff));
	regmap_write(lt8713sx->regmap, 0xe065, 0x00);
	regmap_write(lt8713sx->regmap, 0xe066, 0x30);
	regmap_write(lt8713sx->regmap, 0xe067, 0x00);
	regmap_write(lt8713sx->regmap, 0xe06b, 0x00);
	regmap_write(lt8713sx->regmap, 0xe06c, 0x00);
	regmap_write(lt8713sx->regmap, 0xe060, 0x01);
	msleep(50);
	regmap_write(lt8713sx->regmap, 0xe060, 0x00);
}

static int lt8713sx_write_data(struct lt8713sx *lt8713sx, const u8 *data, u64 filesize)
{
	int page = 0, num = 0, i = 0, val;

	page = (filesize % LT8713SX_PAGE_SIZE) ?
			((filesize / LT8713SX_PAGE_SIZE) + 1) : (filesize / LT8713SX_PAGE_SIZE);

	dev_dbg(lt8713sx->dev,
		"Writing to Sram=%u pages, total size = %llu bytes\n", page, filesize);

	for (num = 0; num < page; num++) {
		dev_dbg(lt8713sx->dev, "page[%d]\n", num);
		lt8713sx_i2c_to_sram(lt8713sx);

		for (i = 0; i < LT8713SX_PAGE_SIZE; i++) {
			if ((num * LT8713SX_PAGE_SIZE + i) < filesize)
				val = *(data + (num * LT8713SX_PAGE_SIZE + i));
			else
				val = 0xff;
			regmap_write(lt8713sx->regmap, 0xe059, val);
		}

		lt8713sx_wren(lt8713sx);
		lt8713sx_sram_to_flash(lt8713sx);
	}

	lt8713sx_wrdi(lt8713sx);
	lt8713sx_disable_sram_write(lt8713sx);

	return 0;
}

static void lt8713sx_main_upgrade_result(struct lt8713sx *lt8713sx)
{
	u32 main_crc_result;

	regmap_read(lt8713sx->regmap, 0xe023, &main_crc_result);

	dev_dbg(lt8713sx->dev, "Main CRC HW: 0x%02X\n", main_crc_result);
	dev_dbg(lt8713sx->dev, "Main CRC FW: 0x%02X\n", lt8713sx->main_crc_value);

	if (main_crc_result == lt8713sx->main_crc_value)
		dev_info(lt8713sx->dev, "Main Firmware Upgrade Success.\n");
	else
		dev_err(lt8713sx->dev, "Main Firmware Upgrade Failed.\n");
}

static void lt8713sx_bank_upgrade_result(struct lt8713sx *lt8713sx, u8 banknum)
{
	u32 bank_crc_result;

	regmap_read(lt8713sx->regmap, 0xe023, &bank_crc_result);

	dev_dbg(lt8713sx->dev, "Bank %d CRC Result: 0x%02X\n", banknum, bank_crc_result);

	if (bank_crc_result == lt8713sx->bank_crc_value[banknum])
		dev_info(lt8713sx->dev, "Bank %d Firmware Upgrade Success.\n", banknum);
	else
		dev_err(lt8713sx->dev, "Bank %d Firmware Upgrade Failed.\n", banknum);
}

static void lt8713sx_bank_result_check(struct lt8713sx *lt8713sx)
{
	int i;
	u64 addr = 0x010000;

	for (i = 0; i < lt8713sx->bank_num; i++) {
		lt8713sx_load_bank_fw_to_sram(lt8713sx, addr);
		lt8713sx_bank_upgrade_result(lt8713sx, i);
		addr += 0x3000;
	}
}

static int lt8713sx_firmware_upgrade(struct lt8713sx *lt8713sx)
{
	int ret;

	lt8713sx_config_parameters(lt8713sx);

	lt8713sx_block_erase(lt8713sx);

	if (lt8713sx->fw->size < SZ_64K) {
		ret = lt8713sx_write_data(lt8713sx, lt8713sx->fw_buffer, SZ_64K);
		if (ret < 0) {
			dev_err(lt8713sx->dev, "Failed to write firmware data: %d\n", ret);
			return ret;
		}
	} else {
		ret = lt8713sx_write_data(lt8713sx, lt8713sx->fw_buffer, lt8713sx->fw->size);
		if (ret < 0) {
			dev_err(lt8713sx->dev, "Failed to write firmware data: %d\n", ret);
			return ret;
		}
	}
	dev_dbg(lt8713sx->dev, "Write Data done.\n");

	return 0;
}

static int lt8713sx_firmware_update(struct lt8713sx *lt8713sx)
{
	int ret = 0;

	guard(mutex)(&lt8713sx->ocm_lock);
	lt8713sx_i2c_enable(lt8713sx);

	ret = lt8713sx_prepare_firmware_data(lt8713sx);
	if (ret < 0) {
		dev_err(lt8713sx->dev, "Failed to prepare firmware data: %d\n", ret);
		goto error;
	}

	ret = lt8713sx_firmware_upgrade(lt8713sx);
	if (ret < 0) {
		dev_err(lt8713sx->dev, "Upgrade failure.\n");
		goto error;
	}

	/* Validate CRC */
	lt8713sx_load_main_fw_to_sram(lt8713sx);
	lt8713sx_main_upgrade_result(lt8713sx);
	lt8713sx_wrdi(lt8713sx);
	lt8713sx_fifo_reset(lt8713sx);
	lt8713sx_bank_result_check(lt8713sx);
	lt8713sx_wrdi(lt8713sx);

error:
	lt8713sx_i2c_disable(lt8713sx);
	if (!ret)
		lt8713sx_reset(lt8713sx);

	kvfree(lt8713sx->fw_buffer);
	lt8713sx->fw_buffer = NULL;

	if (lt8713sx->fw) {
		release_firmware(lt8713sx->fw);
		lt8713sx->fw = NULL;
	}

	return ret;
}

static void lt8713sx_reset(struct lt8713sx *lt8713sx)
{
	dev_dbg(lt8713sx->dev, "reset bridge.\n");
	gpiod_set_value_cansleep(lt8713sx->reset_gpio, 1);
	msleep(20);

	gpiod_set_value_cansleep(lt8713sx->reset_gpio, 0);
	msleep(20);

	dev_dbg(lt8713sx->dev, "reset done.\n");
}

static int lt8713sx_regulator_enable(struct lt8713sx *lt8713sx)
{
	int ret;

	ret = devm_regulator_get_enable(lt8713sx->dev, "vdd");
	if (ret < 0)
		return dev_err_probe(lt8713sx->dev, ret, "failed to enable vdd regulator\n");

	usleep_range(1000, 10000);

	ret = devm_regulator_get_enable(lt8713sx->dev, "vcc");
	if (ret < 0)
		return dev_err_probe(lt8713sx->dev, ret, "failed to enable vcc regulator\n");
	return 0;
}

static int lt8713sx_bridge_attach(struct drm_bridge *bridge,
				  struct drm_encoder *encoder,
				  enum drm_bridge_attach_flags flags)
{
	struct lt8713sx *lt8713sx = container_of(bridge, struct lt8713sx, bridge);

	return drm_bridge_attach(encoder,
				lt8713sx->next_bridge,
				bridge, flags);
}

static int lt8713sx_gpio_init(struct lt8713sx *lt8713sx)
{
	struct device *dev = lt8713sx->dev;

	lt8713sx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(lt8713sx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(lt8713sx->reset_gpio),
				     "failed to acquire reset gpio\n");

	/* power enable gpio */
	lt8713sx->enable_gpio = devm_gpiod_get_optional(dev, "enable", GPIOD_OUT_HIGH);
	if (IS_ERR(lt8713sx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(lt8713sx->enable_gpio),
				     "failed to acquire enable gpio\n");
	return 0;
}

static ssize_t lt8713sx_firmware_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	struct lt8713sx *lt8713sx = dev_get_drvdata(dev);
	int ret;

	ret = lt8713sx_firmware_update(lt8713sx);
	if (ret < 0)
		return ret;
	return len;
}

static DEVICE_ATTR_WO(lt8713sx_firmware);

static struct attribute *lt8713sx_attrs[] = {
	&dev_attr_lt8713sx_firmware.attr,
	NULL,
};

static const struct attribute_group lt8713sx_attr_group = {
	.attrs = lt8713sx_attrs,
};

static const struct attribute_group *lt8713sx_attr_groups[] = {
	&lt8713sx_attr_group,
	NULL,
};

static const struct drm_bridge_funcs lt8713sx_bridge_funcs = {
	.attach = lt8713sx_bridge_attach,
};

static int lt8713sx_probe(struct i2c_client *client)
{
	struct lt8713sx *lt8713sx;
	struct device *dev = &client->dev;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return dev_err_probe(dev, -ENODEV, "device doesn't support I2C\n");

	lt8713sx = devm_drm_bridge_alloc(dev, struct lt8713sx, bridge, &lt8713sx_bridge_funcs);
	if (IS_ERR(lt8713sx))
		return PTR_ERR(lt8713sx);

	lt8713sx->dev = dev;
	lt8713sx->client = client;
	i2c_set_clientdata(client, lt8713sx);

	ret = devm_mutex_init(lt8713sx->dev, &lt8713sx->ocm_lock);
	if (ret)
		return ret;

	lt8713sx->regmap = devm_regmap_init_i2c(client, &lt8713sx_regmap_config);
	if (IS_ERR(lt8713sx->regmap))
		return dev_err_probe(dev, PTR_ERR(lt8713sx->regmap), "regmap i2c init failed\n");

	ret = drm_of_find_panel_or_bridge(lt8713sx->dev->of_node, 1, -1, NULL,
					  &lt8713sx->next_bridge);
	if (ret < 0)
		return ret;

	ret = lt8713sx_gpio_init(lt8713sx);
	if (ret < 0)
		return ret;

	ret = lt8713sx_regulator_enable(lt8713sx);
	if (ret)
		return ret;

	lt8713sx_reset(lt8713sx);

	lt8713sx->bridge.funcs = &lt8713sx_bridge_funcs;
	lt8713sx->bridge.of_node = dev->of_node;
	lt8713sx->bridge.type = DRM_MODE_CONNECTOR_DisplayPort;
	drm_bridge_add(&lt8713sx->bridge);

	crc8_populate_msb(lt8713sx_crc_table, 0x31);

	return 0;
}

static void lt8713sx_remove(struct i2c_client *client)
{
	struct lt8713sx *lt8713sx = i2c_get_clientdata(client);

	drm_bridge_remove(&lt8713sx->bridge);
}

static struct i2c_device_id lt8713sx_id[] = {
	{ "lontium,lt8713sx", 0 },
	{ /* sentinel */ }
};

static const struct of_device_id lt8713sx_match_table[] = {
	{ .compatible = "lontium,lt8713sx" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, lt8713sx_match_table);

static struct i2c_driver lt8713sx_driver = {
	.driver = {
		.name = "lt8713sx",
		.of_match_table = lt8713sx_match_table,
		.dev_groups = lt8713sx_attr_groups,
	},
	.probe = lt8713sx_probe,
	.remove = lt8713sx_remove,
	.id_table = lt8713sx_id,
};

module_i2c_driver(lt8713sx_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("lt8713sx drm bridge driver");
MODULE_AUTHOR("Vishnu Saini <vishnu.saini@oss.qualcomm.com>");
MODULE_FIRMWARE(FW_FILE);
