// SPDX-License-Identifier: GPL-2.0
/*
 * ADM1266 - Cascadable Super Sequencer with Margin
 * Control and Fault Recording
 *
 * Copyright 2020 Analog Devices Inc.
 */

#include <linux/bitfield.h>
#include <linux/crc8.h>
#include <linux/debugfs.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/i2c-smbus.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include "pmbus.h"
#include <linux/slab.h>
#include <linux/timekeeping.h>

#define ADM1266_IC_DEVICE_REV	0xAE
#define ADM1266_BLACKBOX_CONFIG	0xD3
#define ADM1266_PDIO_CONFIG	0xD4
#define ADM1266_READ_STATE	0xD9
#define ADM1266_READ_BLACKBOX	0xDE
#define ADM1266_SET_RTC		0xDF
#define ADM1266_GPIO_CONFIG	0xE1
#define ADM1266_POWERUP_COUNTER	0xE4
#define ADM1266_BLACKBOX_INFO	0xE6
#define ADM1266_PDIO_STATUS	0xE9
#define ADM1266_GPIO_STATUS	0xEA

/* ADM1266 GPIO defines */
#define ADM1266_GPIO_NR			9
#define ADM1266_GPIO_FUNCTIONS(x)	FIELD_GET(BIT(0), x)
#define ADM1266_GPIO_INPUT_EN(x)	FIELD_GET(BIT(2), x)
#define ADM1266_GPIO_OUTPUT_EN(x)	FIELD_GET(BIT(3), x)
#define ADM1266_GPIO_OPEN_DRAIN(x)	FIELD_GET(BIT(4), x)

/* ADM1266 PDIO defines */
#define ADM1266_PDIO_NR			16
#define ADM1266_PDIO_PIN_CFG(x)		FIELD_GET(GENMASK(15, 13), x)
#define ADM1266_PDIO_GLITCH_FILT(x)	FIELD_GET(GENMASK(12, 9), x)
#define ADM1266_PDIO_OUT_CFG(x)		FIELD_GET(GENMASK(2, 0), x)

#define ADM1266_BLACKBOX_OFFSET		0
#define ADM1266_BLACKBOX_SIZE		64
#define ADM1266_BLACKBOX_MAX_RECORDS	32

#define ADM1266_PMBUS_BLOCK_MAX		255

struct adm1266_data {
	struct pmbus_driver_info info;
	struct gpio_chip gc;
	const char *gpio_names[ADM1266_GPIO_NR + ADM1266_PDIO_NR];
	struct i2c_client *client;
	struct dentry *debugfs_dir;
	struct nvmem_config nvmem_config;
	struct nvmem_device *nvmem;
	u8 *dev_mem;
	struct mutex buf_mutex;
	u8 write_buf[ADM1266_PMBUS_BLOCK_MAX + 1] ____cacheline_aligned;
	u8 read_buf[ADM1266_PMBUS_BLOCK_MAX + 2] ____cacheline_aligned;
};

static const struct nvmem_cell_info adm1266_nvmem_cells[] = {
	{
		.name           = "blackbox",
		.offset         = ADM1266_BLACKBOX_OFFSET,
		.bytes          = 2048,
	},
};

DECLARE_CRC8_TABLE(pmbus_crc_table);

/*
 * Different from Block Read as it sends data and waits for the slave to
 * return a value dependent on that data. The protocol is simply a Write Block
 * followed by a Read Block without the Read-Block command field and the
 * Write-Block STOP bit.
 */
static int adm1266_pmbus_block_xfer(struct adm1266_data *data, u8 cmd, u8 w_len, u8 *data_w,
				    u8 *data_r)
{
	struct i2c_client *client = data->client;
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = I2C_M_DMA_SAFE,
			.buf = data->write_buf,
			.len = w_len + 2,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD | I2C_M_DMA_SAFE,
			.buf = data->read_buf,
			.len = ADM1266_PMBUS_BLOCK_MAX + 2,
		}
	};
	u8 addr;
	u8 crc;
	int ret;

	mutex_lock(&data->buf_mutex);

	msgs[0].buf[0] = cmd;
	msgs[0].buf[1] = w_len;
	memcpy(&msgs[0].buf[2], data_w, w_len);

	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret != 2) {
		if (ret >= 0)
			ret = -EPROTO;

		mutex_unlock(&data->buf_mutex);

		return ret;
	}

	if (client->flags & I2C_CLIENT_PEC) {
		addr = i2c_8bit_addr_from_msg(&msgs[0]);
		crc = crc8(pmbus_crc_table, &addr, 1, 0);
		crc = crc8(pmbus_crc_table, msgs[0].buf,  msgs[0].len, crc);

		addr = i2c_8bit_addr_from_msg(&msgs[1]);
		crc = crc8(pmbus_crc_table, &addr, 1, crc);
		crc = crc8(pmbus_crc_table, msgs[1].buf,  msgs[1].buf[0] + 1, crc);

		if (crc != msgs[1].buf[msgs[1].buf[0] + 1]) {
			mutex_unlock(&data->buf_mutex);
			return -EBADMSG;
		}
	}

	memcpy(data_r, &msgs[1].buf[1], msgs[1].buf[0]);

	ret = msgs[1].buf[0];
	mutex_unlock(&data->buf_mutex);

	return ret;
}

static const unsigned int adm1266_gpio_mapping[ADM1266_GPIO_NR][2] = {
	{1, 0},
	{2, 1},
	{3, 2},
	{4, 8},
	{5, 9},
	{6, 10},
	{7, 11},
	{8, 6},
	{9, 7},
};

static const char *adm1266_names[ADM1266_GPIO_NR + ADM1266_PDIO_NR] = {
	"GPIO1", "GPIO2", "GPIO3", "GPIO4", "GPIO5", "GPIO6", "GPIO7", "GPIO8",
	"GPIO9", "PDIO1", "PDIO2", "PDIO3", "PDIO4", "PDIO5", "PDIO6",
	"PDIO7", "PDIO8", "PDIO9", "PDIO10", "PDIO11", "PDIO12", "PDIO13",
	"PDIO14", "PDIO15", "PDIO16",
};

static int adm1266_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct adm1266_data *data = gpiochip_get_data(chip);
	u8 read_buf[I2C_SMBUS_BLOCK_MAX + 1];
	unsigned long pins_status;
	unsigned int pmbus_cmd;
	int ret;

	if (offset < ADM1266_GPIO_NR)
		pmbus_cmd = ADM1266_GPIO_STATUS;
	else
		pmbus_cmd = ADM1266_PDIO_STATUS;

	guard(pmbus_lock)(data->client);

	ret = i2c_smbus_read_block_data(data->client, pmbus_cmd, read_buf);
	if (ret < 0)
		return ret;
	if (ret < 2)
		return -EIO;

	pins_status = read_buf[0] + (read_buf[1] << 8);
	if (offset < ADM1266_GPIO_NR)
		return test_bit(adm1266_gpio_mapping[offset][1], &pins_status);

	return test_bit(offset - ADM1266_GPIO_NR, &pins_status);
}

static int adm1266_gpio_get_multiple(struct gpio_chip *chip, unsigned long *mask,
				     unsigned long *bits)
{
	struct adm1266_data *data = gpiochip_get_data(chip);
	u8 read_buf[ADM1266_PMBUS_BLOCK_MAX + 1];
	unsigned long status;
	unsigned int gpio_nr;
	int ret;

	guard(pmbus_lock)(data->client);

	ret = i2c_smbus_read_block_data(data->client, ADM1266_GPIO_STATUS, read_buf);
	if (ret < 0)
		return ret;
	if (ret < 2)
		return -EIO;

	status = read_buf[0] + (read_buf[1] << 8);

	*bits = 0;
	for_each_set_bit(gpio_nr, mask, ADM1266_GPIO_NR) {
		if (test_bit(adm1266_gpio_mapping[gpio_nr][1], &status))
			set_bit(gpio_nr, bits);
	}

	ret = i2c_smbus_read_block_data(data->client, ADM1266_PDIO_STATUS, read_buf);
	if (ret < 0)
		return ret;
	if (ret < 2)
		return -EIO;

	status = read_buf[0] + (read_buf[1] << 8);

	for_each_set_bit_from(gpio_nr, mask, ADM1266_GPIO_NR + ADM1266_PDIO_NR) {
		if (test_bit(gpio_nr - ADM1266_GPIO_NR, &status))
			set_bit(gpio_nr, bits);
	}

	return 0;
}

static void adm1266_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	struct adm1266_data *data = gpiochip_get_data(chip);
	u8 read_buf[ADM1266_PMBUS_BLOCK_MAX + 1];
	unsigned long gpio_config;
	unsigned long pdio_config;
	unsigned long pin_cfg;
	u8 write_cmd;
	int ret;
	int i;

	guard(pmbus_lock)(data->client);

	for (i = 0; i < ADM1266_GPIO_NR; i++) {
		write_cmd = adm1266_gpio_mapping[i][1];
		ret = adm1266_pmbus_block_xfer(data, ADM1266_GPIO_CONFIG, 1, &write_cmd, read_buf);
		if (ret != 2)
			return;

		gpio_config = read_buf[0];
		seq_puts(s, adm1266_names[i]);

		seq_puts(s, " ( ");
		if (!ADM1266_GPIO_FUNCTIONS(gpio_config)) {
			seq_puts(s, "high-Z )\n");
			continue;
		}
		if (ADM1266_GPIO_INPUT_EN(gpio_config))
			seq_puts(s, "input ");
		if (ADM1266_GPIO_OUTPUT_EN(gpio_config))
			seq_puts(s, "output ");
		if (ADM1266_GPIO_OPEN_DRAIN(gpio_config))
			seq_puts(s, "open-drain )\n");
		else
			seq_puts(s, "push-pull )\n");
	}

	write_cmd = 0xFF;
	ret = adm1266_pmbus_block_xfer(data, ADM1266_PDIO_CONFIG, 1, &write_cmd, read_buf);
	if (ret != 32)
		return;

	for (i = 0; i < ADM1266_PDIO_NR; i++) {
		seq_puts(s, adm1266_names[ADM1266_GPIO_NR + i]);

		pdio_config = read_buf[2 * i];
		pdio_config += (read_buf[2 * i + 1] << 8);
		pin_cfg = ADM1266_PDIO_PIN_CFG(pdio_config);

		seq_puts(s, " ( ");
		if (!pin_cfg || pin_cfg > 5) {
			seq_puts(s, "high-Z )\n");
			continue;
		}

		if (pin_cfg & BIT(0))
			seq_puts(s, "output ");

		if (pin_cfg & BIT(1))
			seq_puts(s, "input ");

		seq_puts(s, ")\n");
	}
}

static int adm1266_config_gpio(struct adm1266_data *data)
{
	const char *name = dev_name(&data->client->dev);
	char *gpio_name;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(data->gpio_names); i++) {
		gpio_name = devm_kasprintf(&data->client->dev, GFP_KERNEL, "adm1266-%d-%x-%s",
					   data->client->adapter->nr, data->client->addr,
					   adm1266_names[i]);
		if (!gpio_name)
			return -ENOMEM;

		data->gpio_names[i] = gpio_name;
	}

	data->gc.label = name;
	data->gc.parent = &data->client->dev;
	data->gc.owner = THIS_MODULE;
	data->gc.can_sleep = true;
	data->gc.base = -1;
	data->gc.names = data->gpio_names;
	data->gc.ngpio = ARRAY_SIZE(data->gpio_names);
	data->gc.get = adm1266_gpio_get;
	data->gc.get_multiple = adm1266_gpio_get_multiple;
	data->gc.dbg_show = adm1266_gpio_dbg_show;

	ret = devm_gpiochip_add_data(&data->client->dev, &data->gc, data);
	if (ret)
		dev_err(&data->client->dev, "GPIO registering failed (%d)\n", ret);

	return ret;
}

static int adm1266_state_read(struct seq_file *s, void *pdata)
{
	struct device *dev = s->private;
	struct i2c_client *client = to_i2c_client(dev);
	int ret;

	guard(pmbus_lock)(client);
	ret = i2c_smbus_read_word_data(client, ADM1266_READ_STATE);
	if (ret < 0)
		return ret;

	seq_printf(s, "%d\n", ret);

	return 0;
}

/*
 * IC_DEVICE_REV (0xAE) returns an 8-byte block (datasheet Rev. D, Table 80):
 *   [2:0] firmware revision  major.minor.patch
 *   [5:3] bootloader revision major.minor.patch
 *   [7:6] silicon revision    two ASCII characters
 */
static int adm1266_firmware_revision_read(struct seq_file *s, void *pdata)
{
	struct device *dev = s->private;
	struct i2c_client *client = to_i2c_client(dev);
	u8 buf[I2C_SMBUS_BLOCK_MAX];
	int ret;

	guard(pmbus_lock)(client);
	ret = i2c_smbus_read_block_data(client, ADM1266_IC_DEVICE_REV, buf);
	if (ret < 0)
		return ret;
	if (ret < 3)
		return -EIO;

	seq_printf(s, "%u.%u.%u\n", buf[0], buf[1], buf[2]);

	return 0;
}

/*
 * POWERUP_COUNTER (0xE4) is a 2-byte little-endian non-volatile counter
 * that increments on every device power cycle (datasheet Rev. D, Table
 * 93). It saturates at 65535 and cannot be reset by the host. Each
 * blackbox record embeds the counter value at record time, so this live
 * read is mainly useful for matching a record back to its boot.
 */
static int adm1266_powerup_counter_read(struct seq_file *s, void *pdata)
{
	struct device *dev = s->private;
	struct i2c_client *client = to_i2c_client(dev);
	u8 buf[I2C_SMBUS_BLOCK_MAX];
	int ret;

	guard(pmbus_lock)(client);
	ret = i2c_smbus_read_block_data(client, ADM1266_POWERUP_COUNTER, buf);
	if (ret < 0)
		return ret;
	if (ret != 2)
		return -EIO;

	seq_printf(s, "%u\n", buf[0] | (buf[1] << 8));

	return 0;
}

/*
 * Clearing the blackbox is required when the device is configured in
 * single-recording mode (BLACKBOX_CONFIG[0] = 0): once the 32-record
 * buffer is full the device stops recording until cleared.
 *
 * The clear is issued as a 2-byte block-write to READ_BLACKBOX with
 * payload {0xFE, 0x00} per the datasheet. READ_BLACKBOX is also used
 * by adm1266_nvmem_read_blackbox() to walk records one at a time;
 * both paths run under pmbus_lock so the clear cannot interleave
 * mid-iteration and corrupt the read sequence.
 */
static ssize_t adm1266_clear_blackbox_write(struct file *file, const char __user *ubuf,
					    size_t count, loff_t *ppos)
{
	struct i2c_client *client = file->private_data;
	u8 payload[2] = { 0xFE, 0x00 };
	int ret;

	guard(pmbus_lock)(client);
	ret = i2c_smbus_write_block_data(client, ADM1266_READ_BLACKBOX,
					 sizeof(payload), payload);
	if (ret < 0)
		return ret;

	return count;
}

static const struct file_operations adm1266_clear_blackbox_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = adm1266_clear_blackbox_write,
	.llseek = noop_llseek,
};

/*
 * SET_RTC (0xDF) is a 6-byte block (datasheet Rev. D, Table 84):
 *   bytes [1:0] - fractional seconds (1/65536 s, written as zero)
 *   bytes [5:2] - seconds since 1970-01-01 UTC, little-endian
 *
 * The driver seeds it once at probe via adm1266_rtc_set().  Over a
 * long uptime the chip's counter drifts away from host wall-clock,
 * so expose it via debugfs:
 *
 *   read  -- returns the chip's current seconds counter, which lets
 *            userspace observe host-vs-chip drift.
 *   write -- the kernel re-reads ktime_get_real_seconds() and writes
 *            it to SET_RTC.  The write payload is ignored; userspace
 *            does not get to supply its own timestamp value, so
 *            there is no way to push a wrong time into the chip.
 *
 * A small userspace agent (chrony hook, systemd-timesyncd script,
 * or a periodic cron job) can write to this file to keep the
 * timestamp embedded in each blackbox record aligned with
 * wall-clock across long uptimes.
 */
static int adm1266_rtc_get(void *data, u64 *val)
{
	struct i2c_client *client = data;
	u8 buf[I2C_SMBUS_BLOCK_MAX];
	u32 seconds = 0;
	int ret, i;

	guard(pmbus_lock)(client);
	ret = i2c_smbus_read_block_data(client, ADM1266_SET_RTC, buf);
	if (ret < 0)
		return ret;
	if (ret < 6)
		return -EIO;

	for (i = 0; i < 4; i++)
		seconds |= (u32)buf[2 + i] << (i * 8);

	*val = seconds;

	return 0;
}

static int adm1266_rtc_set(void *data, u64 val)
{
	struct i2c_client *client = data;
	time64_t kt = ktime_get_real_seconds();
	u8 write_buf[6] = { 0 };
	int i;

	/* User-supplied @val is ignored on purpose; the kernel owns the
	 * time source so userspace cannot push a wrong value into the chip.
	 */
	for (i = 0; i < 4; i++)
		write_buf[2 + i] = (kt >> (i * 8)) & 0xFF;

	guard(pmbus_lock)(client);
	return i2c_smbus_write_block_data(client, ADM1266_SET_RTC,
					  sizeof(write_buf), write_buf);
}
DEFINE_DEBUGFS_ATTRIBUTE(adm1266_rtc_fops,
			 adm1266_rtc_get, adm1266_rtc_set, "%llu\n");

static void adm1266_init_debugfs(struct adm1266_data *data)
{
	struct dentry *root;

	root = pmbus_get_debugfs_dir(data->client);
	if (!root)
		return;

	data->debugfs_dir = debugfs_create_dir(data->client->name, root);

	debugfs_create_devm_seqfile(&data->client->dev, "sequencer_state", data->debugfs_dir,
				    adm1266_state_read);
	debugfs_create_devm_seqfile(&data->client->dev, "firmware_revision", data->debugfs_dir,
				    adm1266_firmware_revision_read);
	debugfs_create_devm_seqfile(&data->client->dev, "powerup_counter", data->debugfs_dir,
				    adm1266_powerup_counter_read);
	debugfs_create_file("clear_blackbox", 0200, data->debugfs_dir, data->client,
			    &adm1266_clear_blackbox_fops);
	debugfs_create_file("rtc", 0600, data->debugfs_dir, data->client,
			    &adm1266_rtc_fops);
}

static int adm1266_nvmem_read_blackbox(struct adm1266_data *data, u8 *read_buff)
{
	u8 record[ADM1266_PMBUS_BLOCK_MAX];
	int record_count;
	char index;
	u8 buf[I2C_SMBUS_BLOCK_MAX];
	int ret;

	ret = i2c_smbus_read_block_data(data->client, ADM1266_BLACKBOX_INFO, buf);
	if (ret < 0)
		return ret;

	if (ret != 4)
		return -EIO;

	record_count = buf[3];
	if (record_count > ADM1266_BLACKBOX_MAX_RECORDS)
		return -EIO;

	for (index = 0; index < record_count; index++) {
		ret = adm1266_pmbus_block_xfer(data, ADM1266_READ_BLACKBOX, 1, &index, record);
		if (ret < 0)
			return ret;

		if (ret != ADM1266_BLACKBOX_SIZE)
			return -EIO;

		memcpy(read_buff, record, ADM1266_BLACKBOX_SIZE);
		read_buff += ADM1266_BLACKBOX_SIZE;
	}

	return 0;
}

static int adm1266_nvmem_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct adm1266_data *data = priv;
	int ret;

	if (offset + bytes > data->nvmem_config.size)
		return -EINVAL;

	guard(pmbus_lock)(data->client);

	if (offset == 0) {
		memset(data->dev_mem, 0, data->nvmem_config.size);

		ret = adm1266_nvmem_read_blackbox(data, data->dev_mem);
		if (ret) {
			dev_err(&data->client->dev, "Could not read blackbox!");
			return ret;
		}
	}

	memcpy(val, data->dev_mem + offset, bytes);

	return 0;
}

static int adm1266_config_nvmem(struct adm1266_data *data)
{
	data->nvmem_config.name = dev_name(&data->client->dev);
	data->nvmem_config.dev = &data->client->dev;
	data->nvmem_config.root_only = true;
	data->nvmem_config.read_only = true;
	data->nvmem_config.owner = THIS_MODULE;
	data->nvmem_config.reg_read = adm1266_nvmem_read;
	data->nvmem_config.cells = adm1266_nvmem_cells;
	data->nvmem_config.ncells = ARRAY_SIZE(adm1266_nvmem_cells);
	data->nvmem_config.priv = data;
	data->nvmem_config.stride = 1;
	data->nvmem_config.word_size = 1;
	data->nvmem_config.size = adm1266_nvmem_cells[0].bytes;

	data->dev_mem = devm_kzalloc(&data->client->dev, data->nvmem_config.size, GFP_KERNEL);
	if (!data->dev_mem)
		return -ENOMEM;

	data->nvmem = devm_nvmem_register(&data->client->dev, &data->nvmem_config);
	if (IS_ERR(data->nvmem)) {
		dev_err(&data->client->dev, "Could not register nvmem!");
		return PTR_ERR(data->nvmem);
	}

	return 0;
}

static int adm1266_probe(struct i2c_client *client)
{
	struct adm1266_data *data;
	int ret;
	int i;

	data = devm_kzalloc(&client->dev, sizeof(struct adm1266_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	data->info.pages = 17;
	data->info.format[PSC_VOLTAGE_OUT] = linear;
	for (i = 0; i < data->info.pages; i++)
		data->info.func[i] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT;

	crc8_populate_msb(pmbus_crc_table, 0x7);
	mutex_init(&data->buf_mutex);

	ret = pmbus_do_probe(client, &data->info);
	if (ret)
		return ret;

	ret = adm1266_rtc_set(client, 0);
	if (ret < 0)
		return ret;

	ret = adm1266_config_nvmem(data);
	if (ret < 0)
		return ret;

	ret = adm1266_config_gpio(data);
	if (ret < 0)
		return ret;

	adm1266_init_debugfs(data);

	return 0;
}

static const struct of_device_id adm1266_of_match[] = {
	{ .compatible = "adi,adm1266" },
	{ }
};
MODULE_DEVICE_TABLE(of, adm1266_of_match);

static const struct i2c_device_id adm1266_id[] = {
	{ .name = "adm1266" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adm1266_id);

static struct i2c_driver adm1266_driver = {
	.driver = {
		   .name = "adm1266",
		   .of_match_table = adm1266_of_match,
		  },
	.probe = adm1266_probe,
	.id_table = adm1266_id,
};

module_i2c_driver(adm1266_driver);

MODULE_AUTHOR("Alexandru Tachici <alexandru.tachici@analog.com>");
MODULE_DESCRIPTION("PMBus driver for Analog Devices ADM1266");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("PMBUS");
