// SPDX-License-Identifier: GPL-2.0
/*
 * w1_ds250x.c - w1 family 09/0b/89/91 (DS250x) driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/crc16.h>

#include <linux/w1.h>
#include <linux/nvmem-provider.h>

#define W1_DS2501_UNW_FAMILY    0x91
#define W1_DS2501_SIZE          64

#define W1_DS2502_FAMILY        0x09
#define W1_DS2502_UNW_FAMILY    0x89
#define W1_DS2502_SIZE          128

#define W1_DS2505_FAMILY	0x0b
#define W1_DS2505_SIZE		2048

#define W1_PAGE_SIZE		32

#define W1_EXT_READ_MEMORY	0xA5
#define W1_READ_DATA_CRC        0xC3

#define OFF2PG(off)	((off) / W1_PAGE_SIZE)

#define CRC16_INIT		0
#define CRC16_VALID		0xb001

struct w1_eprom_data {
	size_t size;
	int (*read)(struct w1_slave *sl, int pageno);
	u8 eprom[W1_DS2505_SIZE];
	DECLARE_BITMAP(page_present, W1_DS2505_SIZE / W1_PAGE_SIZE);
	char nvmem_name[64];
};

static int w1_ds2502_read_page(struct w1_slave *sl, int pageno)
{
	struct w1_eprom_data *data = sl->family_data;
	int pgoff = pageno * W1_PAGE_SIZE;
	int ret = -EIO;
	u8 buf[3];
	u8 crc8;

	if (test_bit(pageno, data->page_present))
		return 0; /* page already present */

	mutex_lock(&sl->master->bus_mutex);

	if (w1_reset_select_slave(sl))
		goto err;

	buf[0] = W1_READ_DATA_CRC;
	buf[1] = pgoff & 0xff;
	buf[2] = pgoff >> 8;
	w1_write_block(sl->master, buf, 3);

	crc8 = w1_read_8(sl->master);
	if (w1_calc_crc8(buf, 3) != crc8)
		goto err;

	w1_read_block(sl->master, &data->eprom[pgoff], W1_PAGE_SIZE);

	crc8 = w1_read_8(sl->master);
	if (w1_calc_crc8(&data->eprom[pgoff], W1_PAGE_SIZE) != crc8)
		goto err;

	set_bit(pageno, data->page_present); /* mark page present */
	ret = 0;
err:
	mutex_unlock(&sl->master->bus_mutex);
	return ret;
}

static int w1_ds2505_read_page(struct w1_slave *sl, int pageno)
{
	struct w1_eprom_data *data = sl->family_data;
	int redir_retries = 16;
	int pgoff, epoff;
	int ret = -EIO;
	u8 buf[6];
	u8 redir;
	u16 crc;

	if (test_bit(pageno, data->page_present))
		return 0; /* page already present */

	epoff = pgoff = pageno * W1_PAGE_SIZE;
	mutex_lock(&sl->master->bus_mutex);

retry:
	if (w1_reset_select_slave(sl))
		goto err;

	buf[0] = W1_EXT_READ_MEMORY;
	buf[1] = pgoff & 0xff;
	buf[2] = pgoff >> 8;
	w1_write_block(sl->master, buf, 3);
	w1_read_block(sl->master, buf + 3, 3); /* redir, crc16 */
	redir = buf[3];
	crc = crc16(CRC16_INIT, buf, 6);

	if (crc != CRC16_VALID)
		goto err;


	if (redir != 0xff) {
		redir_retries--;
		if (redir_retries < 0)
			goto err;

		pgoff = (redir ^ 0xff) * W1_PAGE_SIZE;
		goto retry;
	}

	w1_read_block(sl->master, &data->eprom[epoff], W1_PAGE_SIZE);
	w1_read_block(sl->master, buf, 2); /* crc16 */
	crc = crc16(CRC16_INIT, &data->eprom[epoff], W1_PAGE_SIZE);
	crc = crc16(crc, buf, 2);

	if (crc != CRC16_VALID)
		goto err;

	set_bit(pageno, data->page_present);
	ret = 0;
err:
	mutex_unlock(&sl->master->bus_mutex);
	return ret;
}

static int w1_nvmem_read(void *priv, unsigned int off, void *buf, size_t count)
{
	struct w1_slave *sl = priv;
	struct w1_eprom_data *data = sl->family_data;
	size_t eprom_size = data->size;
	int ret;
	int i;

	if (off > eprom_size)
		return -EINVAL;

	if ((off + count) > eprom_size)
		count = eprom_size - off;

	i = OFF2PG(off);
	do {
		ret = data->read(sl, i++);
		if (ret < 0)
			return ret;
	} while (i < OFF2PG(off + count));

	memcpy(buf, &data->eprom[off], count);
	return 0;
}

static int w1_eprom_add_slave(struct w1_slave *sl)
{
	struct w1_eprom_data *data;
	struct nvmem_device *nvmem;
	struct nvmem_config nvmem_cfg = {
		.dev = &sl->dev,
		.reg_read = w1_nvmem_read,
		.type = NVMEM_TYPE_OTP,
		.read_only = true,
		.word_size = 1,
		.priv = sl,
		.id = -1
	};

	data = devm_kzalloc(&sl->dev, sizeof(struct w1_eprom_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	sl->family_data = data;
	switch (sl->family->fid) {
	case W1_DS2501_UNW_FAMILY:
		data->size = W1_DS2501_SIZE;
		data->read = w1_ds2502_read_page;
		break;
	case W1_DS2502_FAMILY:
	case W1_DS2502_UNW_FAMILY:
		data->size = W1_DS2502_SIZE;
		data->read = w1_ds2502_read_page;
		break;
	case W1_DS2505_FAMILY:
		data->size = W1_DS2505_SIZE;
		data->read = w1_ds2505_read_page;
		break;
	}

	if (sl->master->bus_master->dev_id)
		snprintf(data->nvmem_name, sizeof(data->nvmem_name),
			 "%s-%02x-%012llx",
			 sl->master->bus_master->dev_id, sl->reg_num.family,
			 (unsigned long long)sl->reg_num.id);
	else
		snprintf(data->nvmem_name, sizeof(data->nvmem_name),
			 "%02x-%012llx",
			 sl->reg_num.family,
			 (unsigned long long)sl->reg_num.id);

	nvmem_cfg.name = data->nvmem_name;
	nvmem_cfg.size = data->size;

	nvmem = devm_nvmem_register(&sl->dev, &nvmem_cfg);
	return PTR_ERR_OR_ZERO(nvmem);
}

static const struct w1_family_ops w1_eprom_fops = {
	.add_slave	= w1_eprom_add_slave,
};

static struct w1_family w1_family_09 = {
	.fid = W1_DS2502_FAMILY,
	.fops = &w1_eprom_fops,
};

static struct w1_family w1_family_0b = {
	.fid = W1_DS2505_FAMILY,
	.fops = &w1_eprom_fops,
};

static struct w1_family w1_family_89 = {
	.fid = W1_DS2502_UNW_FAMILY,
	.fops = &w1_eprom_fops,
};

static struct w1_family w1_family_91 = {
	.fid = W1_DS2501_UNW_FAMILY,
	.fops = &w1_eprom_fops,
};

static int __init w1_ds250x_init(void)
{
	int err;

	err = w1_register_family(&w1_family_09);
	if (err)
		return err;

	err = w1_register_family(&w1_family_0b);
	if (err)
		goto err_0b;

	err = w1_register_family(&w1_family_89);
	if (err)
		goto err_89;

	err = w1_register_family(&w1_family_91);
	if (err)
		goto err_91;

	return 0;

err_91:
	w1_unregister_family(&w1_family_89);
err_89:
	w1_unregister_family(&w1_family_0b);
err_0b:
	w1_unregister_family(&w1_family_09);
	return err;
}

static void __exit w1_ds250x_exit(void)
{
	w1_unregister_family(&w1_family_09);
	w1_unregister_family(&w1_family_0b);
	w1_unregister_family(&w1_family_89);
	w1_unregister_family(&w1_family_91);
}

module_init(w1_ds250x_init);
module_exit(w1_ds250x_exit);

MODULE_AUTHOR("Thomas Bogendoerfer <tbogendoerfe@suse.de>");
MODULE_DESCRIPTION("w1 family driver for DS250x Add Only Memory");
MODULE_LICENSE("GPL");
MODULE_ALIAS("w1-family-" __stringify(W1_DS2502_FAMILY));
MODULE_ALIAS("w1-family-" __stringify(W1_DS2505_FAMILY));
MODULE_ALIAS("w1-family-" __stringify(W1_DS2501_UNW_FAMILY));
MODULE_ALIAS("w1-family-" __stringify(W1_DS2502_UNW_FAMILY));
