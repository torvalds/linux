/*drivers/serial/spi_test.c -spi test driver
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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

#include "spi-rockchip-core.h"

#define MAX_SPI_DEV_NUM 6
#define SPI_MAX_SPEED_HZ	12000000


struct spi_test_data {
	struct device	*dev;
	struct spi_device	*spi;
	char *rx_buf;
	int rx_len; 
	char *tx_buf;
	int tx_len; 
};

static struct spi_test_data *g_spi_test_data[MAX_SPI_DEV_NUM];


static ssize_t spi_test_write(struct file *file, 
			const char __user *buf, size_t count, loff_t *offset)
{
	u8 nr_buf[8];
	int nr = 0, ret;
	int i = 0;
	struct spi_device *spi = NULL;
	char txbuf[256],rxbuf[256];
	ktime_t k1,k2;

	printk("%s:0:bus=0,cs=0; 1:bus=0,cs=1; 2:bus=1,cs=0; 3:bus=1,cs=1; 4:bus=2,cs=0; 5:bus=2,cs=1\n",__func__);

	if(count > 5)
	    return -EFAULT;
	
	ret = copy_from_user(nr_buf, buf, count);
	if(ret < 0)
	    return -EFAULT;

	sscanf(nr_buf, "%d", &nr);
	if(nr >= 6 || nr < 0)
	{
		printk("%s:cmd is error\n",__func__);
		return -EFAULT;
	}
	
	for(i=0; i<256; i++)
	txbuf[i] = i;

	if(!g_spi_test_data[nr] || !g_spi_test_data[nr]->spi)
	{
		printk("%s:error g_spi_test_data is null\n",__func__);		
		return -EFAULT;
	}

	spi = g_spi_test_data[nr]->spi;
	k1 = ktime_get();
	for(i=0; i<5000; i++)
	{
		ret = spi_write(spi, txbuf, 256);
		ret = spi_read(spi, rxbuf, 255);
		ret = spi_write_then_read(spi,txbuf,254,rxbuf,253);
		ret = spi_write_and_read(spi,txbuf,rxbuf,252);
		ret = spi_write_and_read(spi,txbuf,rxbuf,251);
		if(i%500==0)
		printk("%s:test %d times\n\n",__func__,i+1);
	}
	k2 = ktime_get();
	k2 = ktime_sub(k2, k1);
	if(!ret)
	printk("%s:bus_num=%d,chip_select=%d,ok cost:%lldus data rate:%d Kbits/s\n",__func__,spi->master->bus_num, spi->chip_select, ktime_to_us(k2), 1536*5000*8/(s32)ktime_to_ms(k2));
	else
	printk("%s:bus_num=%d,chip_select=%d,error\n",__func__,spi->master->bus_num, spi->chip_select);
	
	return count;
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
	
	if(!spi)	
	return -ENOMEM;

	if (!spi_chip_data && spi->dev.of_node) {
		spi_chip_data = rockchip_spi_parse_dt(&spi->dev);
		if (IS_ERR(spi_chip_data))
		return -ENOMEM;
	}
	
	spi_test_data = (struct spi_test_data *)kzalloc(sizeof(struct spi_test_data), GFP_KERNEL);
	if(!spi_test_data){
		dev_err(&spi->dev, "ERR: no memory for spi_test_data\n");
		return -ENOMEM;
	}

	spi->bits_per_word = 8;	
	spi->controller_data = spi_chip_data;
	
	spi_test_data->spi = spi;
	spi_test_data->dev = &spi->dev;
	
	ret = spi_setup(spi);
	if (ret < 0){
		dev_err(spi_test_data->dev, "ERR: fail to setup spi\n");
		return -1;
	}	

	if((spi->master->bus_num == 0) && (spi->chip_select == 0))
		id = 0;
	else if((spi->master->bus_num == 0) && (spi->chip_select == 1))
		id = 1;
	else if ((spi->master->bus_num == 1) && (spi->chip_select == 0))
		id = 2;
	else if ((spi->master->bus_num == 1) && (spi->chip_select == 1))
		id = 3;
	else if ((spi->master->bus_num == 2) && (spi->chip_select == 0))
		id = 4;
	else if ((spi->master->bus_num == 2) && (spi->chip_select == 1))
		id = 5;

	g_spi_test_data[id] = spi_test_data;
		
	printk("%s:name=%s,bus_num=%d,cs=%d,mode=%d,speed=%d\n",__func__,spi->modalias, spi->master->bus_num, spi->chip_select, spi->mode, spi->max_speed_hz);

	printk("%s:poll_mode=%d, type=%d, enable_dma=%d\n",__func__, spi_chip_data->poll_mode, spi_chip_data->type, spi_chip_data->enable_dma);
	return ret;

}

static int rockchip_spi_test_remove(struct spi_device *spi)
{
	printk("%s\n",__func__);
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
	int ret= 0;
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
MODULE_DESCRIPTION("ROCKCHIP SPI TEST Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spi_test");

