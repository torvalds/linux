//[*]--------------------------------------------------------------------------------------------------[*]
//
//
// 
//  ODROID IOBOARD Board : IOBOARD SST25WF020 SPI Flash driver (charles.park)
//  2013.08.28
// 
//
//[*]--------------------------------------------------------------------------------------------------[*]
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/slab.h>

#include <linux/spi/spi.h>

//[*]--------------------------------------------------------------------------------------------------[*]
#define SST25WF020_NAME 	    "ioboard-spi"

//[*]--------------------------------------------------------------------------------------------------[*]
#define SPI_MAX_RETRY_CNT       1000

#define CMD_READ            0x03
#define CMD_HIGH_SPD_READ   0x0B
#define CMD_RDSR            0x05
#define CMD_ERASE_4KB       0x20
#define CMD_ERASE_32KB      0x52
#define CMD_ERASE_64KB      0xD8
#define CMD_ERASE_ALL       0x60

static int wait_until_ready (struct spi_device *spi, unsigned int check_usec)
{
    unsigned char   cmd, rdata, retry_cnt;

    do  {
        cmd = CMD_RDSR; // ReaD Status Register(RDSR)
        
        spi_write_then_read(spi, &cmd, sizeof(cmd), &rdata, sizeof(rdata));
        udelay(check_usec);
        
        if(retry_cnt++ >= SPI_MAX_RETRY_CNT)    return  -1;
    }   while(rdata & 0x01);
    
    return  0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int ioboard_spi_erase        (struct spi_device *spi, unsigned int addr, unsigned char cmd)
{
    // cmd = 4KB/32KB/64KB/ALL
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int ioboard_spi_word_write   (struct spi_device *spi, unsigned int addr, unsigned short wdata)
{
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int ioboard_spi_byte_write   (struct spi_device *spi, unsigned int addr, unsigned char wdata)
{
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int ioboard_spi_probe    (struct spi_device *spi)
{
	int ret;
	
	unsigned char   tx[10], rx[10];
	
	printk("-------------------- %s -----------------------\n", __func__);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);

	if (ret < 0) {
		printk("spi_setup() fail ret : %d\n", ret);
		return ret;
	}

#if 0
    // write
    tx[0] = 0x50;
    spi_write_then_read(spi, &tx[0], 1, NULL, 0);
    // status-read
    do  {
        tx[0] = 0x05;
        spi_write_then_read(spi, &tx[0], 1, &rx[0], 1);
        mdelay(10);
        printk("-------> rx[0]= 0x%02X\n", rx[0]);
    }   while(rx[0] & 0x01);

    // write
    tx[0] = 0x06;
    spi_write_then_read(spi, &tx[0], 1, NULL, 0);
    // status-read
    do  {
        tx[0] = 0x05;
        spi_write_then_read(spi, &tx[0], 1, &rx[0], 1);
        mdelay(10);
        printk("-------> rx[0]= 0x%02X\n", rx[0]);
    }   while(rx[0] & 0x01);

    // write
    tx[0] = 0x01;
    tx[1] = 0x00;
    spi_write_then_read(spi, &tx[0], 2, NULL, 0);
    // status-read
    do  {
        tx[0] = 0x05;
        spi_write_then_read(spi, &tx[0], 1, &rx[0], 1);
        mdelay(10);
        printk("-------> rx[0]= 0x%02X\n", rx[0]);
    }   while(rx[0] & 0x01);

    // write
    tx[0] = 0x06;
    spi_write_then_read(spi, &tx[0], 1, &rx[0], 0);
    // status-read
    do  {
        tx[0] = 0x05;
        spi_write_then_read(spi, &tx[0], 1, &rx[0], 1);
        mdelay(10);
        printk("-------> rx[0]= 0x%02X\n", rx[0]);
    }   while(rx[0] & 0x01);

    // erase 4K
    tx[0] = 0x20;
    tx[1] = 0x00;
    tx[2] = 0x00;
    tx[3] = 0x00;
    spi_write_then_read(spi, &tx[0], 4, NULL, 0);
    // status-read
    do  {
        tx[0] = 0x05;
        spi_write_then_read(spi, &tx[0], 1, &rx[0], 1);
        mdelay(10);
        printk("-------> rx[0]= 0x%02X\n", rx[0]);
    }   while(rx[0] & 0x01);

    // write
    tx[0] = 0x06;
    spi_write_then_read(spi, &tx[0], 1, &rx[0], 0);
    do  {
        tx[0] = 0x05;
        spi_write_then_read(spi, &tx[0], 1, &rx[0], 1);
        mdelay(10);
        printk("-------> rx[0]= 0x%02X\n", rx[0]);
    }   while(rx[0] & 0x01);

    tx[0] = 0x02;
    tx[1] = 0x00;
    tx[2] = 0x00;
    tx[3] = 0x00;
    tx[4] = 0x5A;
    spi_write_then_read(spi, &tx[0], 5, NULL, 0);

    do  {
        tx[0] = 0x05;
        spi_write_then_read(spi, &tx[0], 1, &rx[0], 1);
        mdelay(10);
        printk("-------> rx[0]= 0x%02X\n", rx[0]);
    }   while(rx[0] & 0x01);

    // status-read
    tx[0] = 0x05;
    spi_write_then_read(spi, &tx[0], 1, &rx[0], 1);
    printk("------------> rx[0] = 0x%02X, rx[1] = 0x%02X, rx[2] = 0x%02X, rx[3] = 0x%02X\n", rx[0], rx[1], rx[2], rx[3]); 

    // read
    tx[0] = 0x03;
    tx[1] = 0x00;
    tx[2] = 0x00;
    tx[3] = 0x00;
    spi_write_then_read(spi, &tx[0], 4, &rx[0], 4);
    printk("------------> rx[0] = 0x%02X, rx[1] = 0x%02X, rx[2] = 0x%02X, rx[3] = 0x%02X\n", rx[0], rx[1], rx[2], rx[3]); 

{
    int i=0;
    
    for(i=0;i<1; i++) {
    // read
    tx[0] = 0x9f;
    spi_write_then_read(spi, &tx[0], 1, &rx[0], 3);
    printk("------------> rx[0] = 0x%02X, rx[1] = 0x%02X, rx[2] = 0x%02X, rx[3] = 0x%02X\n", rx[0], rx[1], rx[2], rx[3]); 
    }
}
#endif

	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static int ioboard_spi_remove   (struct spi_device *spi)
{
	return 0;
}

//[*]--------------------------------------------------------------------------------------------------[*]
static struct spi_driver ioboard_spi_driver = {
	.driver = {
		.name	= SST25WF020_NAME,
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= ioboard_spi_probe,
	.remove	= ioboard_spi_remove,
};

//[*]--------------------------------------------------------------------------------------------------[*]
/*
 * Module init and exit
 */
//[*]--------------------------------------------------------------------------------------------------[*]
static int __init ioboard_spi_init(void)
{
	return spi_register_driver(&ioboard_spi_driver);
}
module_init(ioboard_spi_init);

//[*]--------------------------------------------------------------------------------------------------[*]
static void __exit ioboard_spi_exit(void)
{
	spi_unregister_driver(&ioboard_spi_driver);
}
module_exit(ioboard_spi_exit);

//[*]--------------------------------------------------------------------------------------------------[*]
MODULE_DESCRIPTION("IOBOARD driver for ODROIDXU-Dev board");
MODULE_AUTHOR("Hard-Kernel");
MODULE_LICENSE("GPL");

//[*]--------------------------------------------------------------------------------------------------[*]
//[*]--------------------------------------------------------------------------------------------------[*]
