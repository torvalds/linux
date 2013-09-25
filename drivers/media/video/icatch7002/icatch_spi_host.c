/*icatch  host mode ,spi device 
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
#include <mach/board.h>
#include "icatch_spi_host.h"
#include <linux/miscdevice.h>
struct spi_device* g_icatch_spi_dev = NULL;

static int __devinit spi_icatch_probe(struct spi_device *spi)
{	
	struct spi_test_data *spi_test_data;
	int ret = 0;
	
	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	ret = spi_setup(spi);
	if (ret < 0){
		dev_err(spi, "ERR: fail to setup spi\n");
		return -1;
	}	

	g_icatch_spi_dev = spi;

	printk("%s:bus_num=%d,ok\n",__func__,spi->master->bus_num);

	return ret;

}


static struct spi_driver spi_icatch_driver = {
	.driver = {
		.name		= "spi_icatch",
		.bus		= &spi_bus_type,
		.owner		= THIS_MODULE,
	},

	.probe		= spi_icatch_probe,
};

static struct miscdevice spi_test_icatch = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "spi_misc_icatch",
};

static int __init spi_icatch_init(void)
{	
	spi_register_board_info(board_spi_icatch_devices, ARRAY_SIZE(board_spi_icatch_devices));
	
	misc_register(&spi_test_icatch);
	return spi_register_driver(&spi_icatch_driver);
}

static void __exit spi_icatch_exit(void)
{
	
	misc_deregister(&spi_test_icatch);
	return spi_unregister_driver(&spi_icatch_driver);
}
module_init(spi_icatch_init);
module_exit(spi_icatch_exit);


