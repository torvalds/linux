/*drivers/spi/spi-rockchip-test.c -spi test driver
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* dts config
&spi0 {
	status = "okay";
	max-freq = <48000000>;   //spi internal clk, don't modify
	//dma-names = "tx", "rx";   //enable dma
	pinctrl-names = "default";  //pinctrl according to you board
	pinctrl-0 = <&spi0_clk &spi0_tx &spi0_rx &spi0_cs0 &spi0_cs1>;
	spi_test@00 {
		compatible = "rockchip,spi_test_bus0_cs0";
		reg = <0>;   //chip select  0:cs0  1:cs1
		spi-max-frequency = <24000000>;   //spi output clock
		//spi-cpha;      not support
		//spi-cpol; 	//if the property is here it is 1:clk is high, else 0:clk is low  when idle
	};

	spi_test@01 {
		compatible = "rockchip,spi_test_bus0_cs1";
		reg = <1>;
		spi-max-frequency = <24000000>;
		spi-cpha;
		spi-cpol;
	};
};
*/

/* how to test spi
* echo write 0 10 255 > /dev/spi_misc_test
* echo write 0 10 255 init.rc > /dev/spi_misc_test
* echo read 0 10 255 > /dev/spi_misc_test
* echo setspeed 0 1000000 > /dev/spi_misc_test
*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/miscdevice.h>
#include <linux/hrtimer.h>
#include <linux/platform_data/spi-rockchip.h>
#include <asm/uaccess.h>
#include <linux/syscalls.h>
#include "spi-rockchip-core.h"

#define MAX_SPI_DEV_NUM 6
#define SPI_MAX_SPEED_HZ	12000000

struct spi_test_data {
	struct device	*dev;
	struct spi_device *spi;
	char *rx_buf;
	int rx_len;
	char *tx_buf;
	int tx_len;
};

static struct spi_test_data *g_spi_test_data[MAX_SPI_DEV_NUM];

int spi_write_slt(int id, const void *txbuf, size_t n)
{
	int ret = -1;
	struct spi_device *spi = NULL;

	if (id >= MAX_SPI_DEV_NUM)
		return -1;
	if (!g_spi_test_data[id]) {
		pr_err("g_spi.%d is NULL\n", id);
		return -1;
	} else {
		spi = g_spi_test_data[id]->spi;
	}

	ret = spi_write(spi, txbuf, n);
	return ret;
}

int spi_read_slt(int id, void *rxbuf, size_t n)
{
	int ret = -1;
	struct spi_device *spi = NULL;

	if (id >= MAX_SPI_DEV_NUM)
		return ret;
	if (!g_spi_test_data[id]) {
		pr_err("g_spi.%d is NULL\n", id);
		return ret;
	} else {
		spi = g_spi_test_data[id]->spi;
	}

	ret = spi_read(spi, rxbuf, n);
	return ret;
}

int spi_write_then_read_slt(int id, const void *txbuf, unsigned n_tx,
		void *rxbuf, unsigned n_rx)
{
	int ret = -1;
	struct spi_device *spi = NULL;

	if (id >= MAX_SPI_DEV_NUM)
		return ret;
	if (!g_spi_test_data[id]) {
		pr_err("g_spi.%d is NULL\n", id);
		return ret;
	} else {
		spi = g_spi_test_data[id]->spi;
	}

	ret = spi_write_then_read(spi, txbuf, n_tx, rxbuf, n_rx);
	return ret;
}

int spi_write_and_read_slt(int id, const void *tx_buf,
			void *rx_buf, size_t len)
{
	int ret = -1;
	struct spi_device *spi = NULL;
	struct spi_transfer     t = {
			.tx_buf         = tx_buf,
			.rx_buf         = rx_buf,
			.len            = len,
		};
	struct spi_message      m;

	if (id >= MAX_SPI_DEV_NUM)
		return ret;
	if (!g_spi_test_data[id]) {
		pr_err("g_spi.%d is NULL\n", id);
		return ret;
	} else {
		spi = g_spi_test_data[id]->spi;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(spi, &m);
}

static ssize_t spi_test_write(struct file *file,
			const char __user *buf, size_t n, loff_t *offset)
{
	int argc = 0, i;
	char tmp[64];
	char *argv[16];
	char *cmd, *data;
	unsigned int id = 0, times = 0, size = 0;
	unsigned long us = 0, bytes = 0;
	char *txbuf = NULL, *rxbuf = NULL;
	ktime_t start_time;
	ktime_t end_time;
	ktime_t cost_time;

	memset(tmp, 0, sizeof(tmp));
	if (copy_from_user(tmp, buf, n))
		return -EFAULT;
	cmd = tmp;
	data = tmp;

	while (data < (tmp + n)) {
		data = strstr(data, " ");
		if (!data)
			break;
		*data = 0;
		argv[argc] = ++data;
		argc++;
		if (argc >= 16)
			break;
	}

	tmp[n - 1] = 0;

	if (!strcmp(cmd, "setspeed")) {
		int id = 0, val;
		struct spi_device *spi = NULL;

		sscanf(argv[0], "%d", &id);
		sscanf(argv[1], "%d", &val);

		if (id >= MAX_SPI_DEV_NUM)
			return n;
		if (!g_spi_test_data[id]) {
			pr_err("g_spi.%d is NULL\n", id);
			return n;
		} else {
			spi = g_spi_test_data[id]->spi;
		}
		spi->max_speed_hz = val;
	} else if (!strcmp(cmd, "write")) {
		char name[64];
		int fd;
		mm_segment_t old_fs = get_fs();

		sscanf(argv[0], "%d", &id);
		sscanf(argv[1], "%d", &times);
		sscanf(argv[2], "%d", &size);
		if (argc > 3) {
			sscanf(argv[3], "%s", name);
			set_fs(KERNEL_DS);
		}

		txbuf = kzalloc(size, GFP_KERNEL);
		if (!txbuf) {
			printk("spi write alloc buf size %d fail\n", size);
			return n;
		}

		if (argc > 3) {
			fd = sys_open(name, O_RDONLY, 0);
			if (fd < 0) {
				printk("open %s fail\n", name);
			} else {
				sys_read(fd, (char __user *)txbuf, size);
				sys_close(fd);
			}
			set_fs(old_fs);
		} else {
			for (i = 0; i < size; i++)
				txbuf[i] = i % 256;
		}

		start_time = ktime_get();
		for (i = 0; i < times; i++)
			spi_write_slt(id, txbuf, size);
		end_time = ktime_get();
		cost_time = ktime_sub(end_time, start_time);
		us = ktime_to_us(cost_time);

		bytes = size * times * 1;
		bytes = bytes * 1000 / us;
		printk("spi write %d*%d cost %ldus speed:%ldKB/S\n", size, times, us, bytes);

		kfree(txbuf);
	} else if (!strcmp(cmd, "read")) {
		sscanf(argv[0], "%d", &id);
		sscanf(argv[1], "%d", &times);
		sscanf(argv[2], "%d", &size);

		rxbuf = kzalloc(size, GFP_KERNEL);
		if (!rxbuf) {
			printk("spi read alloc buf size %d fail\n", size);
			return n;
		}

		start_time = ktime_get();
		for (i = 0; i < times; i++)
			spi_read_slt(id, rxbuf, size);
		end_time = ktime_get();
		cost_time = ktime_sub(end_time, start_time);
		us = ktime_to_us(cost_time);

		bytes = size * times * 1;
		bytes = bytes * 1000 / us;
		printk("spi read %d*%d cost %ldus speed:%ldKB/S\n", size, times, us, bytes);

		kfree(rxbuf);
	} else if (!strcmp(cmd, "loop")) {
		sscanf(argv[0], "%d", &id);
		sscanf(argv[1], "%d", &times);
		sscanf(argv[2], "%d", &size);

		txbuf = kzalloc(size, GFP_KERNEL);
		if (!txbuf) {
			printk("spi tx alloc buf size %d fail\n", size);
			return n;
		}

		rxbuf = kzalloc(size, GFP_KERNEL);
		if (!rxbuf) {
			kfree(txbuf);
			printk("spi rx alloc buf size %d fail\n", size);
			return n;
		}

		for (i = 0; i < size; i++)
			txbuf[i] = i % 256;

		start_time = ktime_get();
		for (i = 0; i < times; i++)
			spi_write_and_read_slt(id, txbuf, rxbuf, size);

		end_time = ktime_get();
		cost_time = ktime_sub(end_time, start_time);
		us = ktime_to_us(cost_time);

		if (memcmp(txbuf, rxbuf, size))
			printk("spi loop test fail\n");

		bytes = size * times;
		bytes = bytes * 1000 / us;
		printk("spi loop %d*%d cost %ldus speed:%ldKB/S\n", size, times, us, bytes);

		kfree(txbuf);
		kfree(rxbuf);
	} else {
		printk("echo write 0 10 255 > /dev/spi_misc_test\n");
		printk("echo write 0 10 255 init.rc > /dev/spi_misc_test\n");
		printk("echo read 0 10 255 > /dev/spi_misc_test\n");
		printk("echo setspeed 0 1000000 > /dev/spi_misc_test\n");
	}

	return n;
}

static const struct file_operations spi_test_fops = {
	.write = spi_test_write,
};

static struct miscdevice spi_test_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "spi_misc_test",
	.fops = &spi_test_fops,
};

#ifdef CONFIG_OF
static struct dw_spi_chip *rockchip_spi_parse_dt(struct device *dev)
{
	u32 temp;
	struct dw_spi_chip *spi_chip_data;

	spi_chip_data = devm_kzalloc(dev, sizeof(*spi_chip_data), GFP_KERNEL);
	if (!spi_chip_data) {
		dev_err(dev, "memory allocation for spi_chip_data failed\n");
		return ERR_PTR(-ENOMEM);
	}

	if (of_property_read_u32(dev->of_node, "poll_mode", &temp)) {
		dev_warn(dev, "fail to get poll_mode, default set 0\n");
		spi_chip_data->poll_mode = 0;
	} else {
		spi_chip_data->poll_mode = temp;
	}

	if (of_property_read_u32(dev->of_node, "type", &temp)) {
		dev_warn(dev, "fail to get type, default set 0\n");
		spi_chip_data->type = 0;
	} else {
		spi_chip_data->type = temp;
	}

	if (of_property_read_u32(dev->of_node, "enable_dma", &temp)) {
		dev_warn(dev, "fail to get enable_dma, default set 0\n");
		spi_chip_data->enable_dma = 0;
	} else {
		spi_chip_data->enable_dma = temp;
	}
#if 0
	if (of_property_read_u32(dev->of_node, "slave-enable", &temp)) {
		dev_warn(dev, "fail to get slave-enable, default set 0\n");
		spi_chip_data->slave_enable = 0;
	} else {
		spi_chip_data->slave_enable = temp;
	}
#endif
	return spi_chip_data;
}
#else
static struct spi_board_info *rockchip_spi_parse_dt(struct device *dev)
{
	return dev->platform_data;
}
#endif

static int rockchip_spi_test_probe(struct spi_device *spi)
{
	int ret;
	int id = 0;
	struct dw_spi_chip *spi_chip_data = NULL;
	struct spi_test_data *spi_test_data = NULL;

	if (!spi)
		return -ENOMEM;

	if (!spi->dev.of_node)
		return -ENOMEM;

	spi_chip_data = rockchip_spi_parse_dt(&spi->dev);
	if (IS_ERR(spi_chip_data))
		return -ENOMEM;

	spi_test_data = (struct spi_test_data *)kzalloc(sizeof(struct spi_test_data), GFP_KERNEL);
	if (!spi_test_data) {
		dev_err(&spi->dev, "ERR: no memory for spi_test_data\n");
		return -ENOMEM;
	}
	spi->bits_per_word = 8;
	spi->controller_data = spi_chip_data;

	spi_test_data->spi = spi;
	spi_test_data->dev = &spi->dev;

	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(spi_test_data->dev, "ERR: fail to setup spi\n");
		return -1;
	}

	if (of_property_read_u32(spi->dev.of_node, "id", &id)) {
		dev_warn(&spi->dev, "fail to get id, default set 0\n");
		id = 0;
	}

	g_spi_test_data[id] = spi_test_data;

	printk("%s:name=%s,bus_num=%d,cs=%d,mode=%d,speed=%d\n", __func__, spi->modalias, spi->master->bus_num, spi->chip_select, spi->mode, spi->max_speed_hz);

	printk("%s:poll_mode=%d, type=%d, enable_dma=%d\n", __func__, spi_chip_data->poll_mode, spi_chip_data->type, spi_chip_data->enable_dma);
	return ret;
}

static int rockchip_spi_test_remove(struct spi_device *spi)
{
	printk("%s\n", __func__);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id rockchip_spi_test_dt_match[] = {
	{ .compatible = "rockchip,spi_test_bus0_cs0", },
	{ .compatible = "rockchip,spi_test_bus0_cs1", },
	{ .compatible = "rockchip,spi_test_bus1_cs0", },
	{ .compatible = "rockchip,spi_test_bus1_cs1", },
	{ .compatible = "rockchip,spi_test_bus2_cs0", },
	{ .compatible = "rockchip,spi_test_bus2_cs1", },
	{ .compatible = "rockchip,spi_test_bus3_cs0", },
	{ .compatible = "rockchip,spi_test_bus3_cs1", },
	{ .compatible = "rockchip,spi_test_bus4_cs0", },
	{ .compatible = "rockchip,spi_test_bus4_cs1", },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_spi_test_dt_match);

#endif /* CONFIG_OF */

static struct spi_driver spi_rockchip_test_driver = {
	.driver = {
		.name	= "spi_test",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(rockchip_spi_test_dt_match),
	},
	.probe = rockchip_spi_test_probe,
	.remove = rockchip_spi_test_remove,
};

static int __init spi_rockchip_test_init(void)
{
	int ret = 0;

	misc_register(&spi_test_misc);
	ret = spi_register_driver(&spi_rockchip_test_driver);
	return ret;
}
module_init(spi_rockchip_test_init);

static void __exit spi_rockchip_test_exit(void)
{
	misc_deregister(&spi_test_misc);
	return spi_unregister_driver(&spi_rockchip_test_driver);
}
module_exit(spi_rockchip_test_exit);

MODULE_AUTHOR("Luo Wei <lw@rock-chips.com>");
MODULE_AUTHOR("Huibin Hong <hhb@rock-chips.com>");
MODULE_DESCRIPTION("ROCKCHIP SPI TEST Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spi_test");
