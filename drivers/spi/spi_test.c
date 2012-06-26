/*drivers/serial/spi_test.c -spi test driver
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <linux/miscdevice.h>
#include <asm/dma.h>
#include <linux/preempt.h>
#include "rk29_spim.h"
#include <linux/spi/spi.h>
#include <mach/board.h>

struct spi_test_data {
	struct device	*dev;
	struct spi_device	*spi;	
	char *rx_buf;
	int rx_len; 
	char *tx_buf;
	int tx_len; 
};
static struct spi_test_data *g_spi_test_data;


static struct rk29xx_spi_chip spi_test_chip = {
	//.poll_mode = 1,
	.enable_dma = 1,
};
	
static struct spi_board_info board_spi_test_devices[] = {	
	{
		.modalias  = "spi_test",
		.bus_num = 0,	//0 or 1
		.max_speed_hz  = 12*1000*1000,
		.chip_select   = 0,		
		.controller_data = &spi_test_chip,
	},	
	
};

static ssize_t spi_test_write(struct file *file, 
			const char __user *buf, size_t count, loff_t *offset)
{
        char nr_buf[8];
        int nr = 0, ret;
	int i = 0;
	struct spi_device *spi = g_spi_test_data->spi;
	char txbuf[256],rxbuf[256];

        if(count > 3)
                return -EFAULT;
        ret = copy_from_user(nr_buf, buf, count);
        if(ret < 0)
                return -EFAULT;

        sscanf(nr_buf, "%d", &nr);
        if(nr >= 2 || nr < 0)
        {
        	printk("%s:data is error\n",__func__);
                return -EFAULT;
        }
	
	for(i=0; i<256; i++)
	txbuf[i] = i;

	switch(nr)
	{
		case 0:
			spi->chip_select = 0;
			break;
		case 1:
			spi->chip_select = 1;
			break;

		default:
			break;

	}

	for(i=0; i<10; i++)
	{
		ret = spi_write(spi, txbuf, 256);
		ret = spi_read(spi, rxbuf, 256);
		ret = spi_write_then_read(spi,txbuf,256,rxbuf,256);
		printk("%s:test %d times\n\n",__func__,i+1);
	}
	
	if(!ret)
	printk("%s:ok\n",__func__);
	else
	printk("%s:error\n",__func__);
	
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

static int __devinit spi_test_probe(struct spi_device *spi)
{	
	struct spi_test_data *spi_test_data;
	int ret;
	
	spi_test_data = (struct spi_test_data *)kzalloc(sizeof(struct spi_test_data), GFP_KERNEL);
	if(!spi_test_data){
		dev_err(&spi->dev, "ERR: no memory for spi_test_data\n");
		return -ENOMEM;
	}

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	
	spi_test_data->spi = spi;
	spi_test_data->dev = &spi->dev;
	ret = spi_setup(spi);
	if (ret < 0){
		dev_err(spi_test_data->dev, "ERR: fail to setup spi\n");
		return -1;
	}	

	g_spi_test_data = spi_test_data;

	printk("%s:bus_num=%d,ok\n",__func__,spi->master->bus_num);

	return ret;

}


static struct spi_driver spi_test_driver = {
	.driver = {
		.name		= "spi_test",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},

	.probe		= spi_test_probe,
};

static int __init spi_test_init(void)
{	
	spi_register_board_info(board_spi_test_devices, ARRAY_SIZE(board_spi_test_devices));
	misc_register(&spi_test_misc);
	return spi_register_driver(&spi_test_driver);
}

static void __exit spi_test_exit(void)
{
        misc_deregister(&spi_test_misc);
	return spi_unregister_driver(&spi_test_driver);
}
module_init(spi_test_init);
module_exit(spi_test_exit);

