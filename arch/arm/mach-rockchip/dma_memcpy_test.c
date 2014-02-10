/*
 *
 * arch/arm/plat-rk/dma_memcpy_test.c
 *
 * Copyright (C) 2012 Rochchip.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Author: hhb@rock-chips.com
 * Create Date: 2012.03.26
 * 
 * HOW TO USE IT?
 * enter the follow command at command line
 * echo 1 > sys/module/dma_memcpy_test/parameters/debug   enable log output,default is enable
 * echo 1000 > sys/module/dma_memcpy_test/parameters/interval   set dma transfer interval, default is 1000ms
 * echo 1 > /sys/devices/platform/dma_memcpy.0/dmamemcpy  to start the dma test
 *
 */

/*
*		Driver Version Note
*
*v1.0 : 1. add dam thread number from 2 to 8;
*		
*		
*/
#define VERSION_AND_TIME  "dma_memcpy_test.c v1.0 2012-08-13"

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <asm/current.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/amba/bus.h>
#include <linux/amba/pl330.h>
#include <linux/slab.h>


#define DMA_TEST_BUFFER_SIZE 512
#define DMA_THREAD  6

struct Dma_MemToMem {
	const char *name;
	unsigned char* src;			//virtual address
	unsigned char* dst;
	int MenSize;
    dma_cap_mask_t  cap_mask;
	struct dma_chan *dma_chan;
	struct dma_slave_config *config;
	struct dma_async_tx_descriptor *tx;
};

//enable log output
static int debug = 8;
module_param(debug,int,S_IRUGO|S_IWUSR);
#define MEMCPY_DMA_DBG(fmt...)  {if(debug > 0) printk(fmt);}

static struct Dma_MemToMem DmaMemInfo[DMA_THREAD];

static void dma_memtest_transfer(struct Dma_MemToMem *DmaMemInfo)
{
	dma_async_tx_descriptor_init(DmaMemInfo->tx, DmaMemInfo->dma_chan);
	
	dma_async_memcpy_buf_to_buf(DmaMemInfo->dma_chan, DmaMemInfo->dst,
			DmaMemInfo->src, DMA_TEST_BUFFER_SIZE);

	dma_wait_for_async_tx(DmaMemInfo->tx);

	dmaengine_terminate_all(DmaMemInfo->dma_chan);
}

//int slecount = 0;
static ssize_t memcpy_dma_read(struct device *device,struct device_attribute *attr, char *argv)
{

     return 0;
}

static ssize_t memcpy_dma_write(struct device *device, struct device_attribute *attr, const char *argv, size_t count)
{
 
    int i,j;
    printk("memcpy_dma_write\n");

	for(j = DMA_THREAD; j > 0; j--)
	{
        memset(DmaMemInfo[j-1].src, ((j-1)<<4|(j-1)), DMA_TEST_BUFFER_SIZE);			
		memset(DmaMemInfo[j-1].dst, 0x0, DMA_TEST_BUFFER_SIZE);
	}

	switch(DMA_THREAD) {		
		case 8:			
			dma_memtest_transfer(&DmaMemInfo[7]);		
		case 7:			
			dma_memtest_transfer(&DmaMemInfo[6]);	
		case 6:			
			dma_memtest_transfer(&DmaMemInfo[5]);
		case 5:			
			dma_memtest_transfer(&DmaMemInfo[4]);	
		case 4:			
			dma_memtest_transfer(&DmaMemInfo[3]);	
		case 3:			
			dma_memtest_transfer(&DmaMemInfo[2]);	
		case 2:			
			dma_memtest_transfer(&DmaMemInfo[1]);	
		case 1:			
			dma_memtest_transfer(&DmaMemInfo[0]);
			break;		
		default:			
			printk("%s no channel\n", __func__);			
		break;		
	}
	

	for(i = 0; i < 16; i++) {	
		for(j = DMA_THREAD; j > 0; j--)
		{
			printk("src%d:%2x",j,*(DmaMemInfo[j-1].src + i*(DMA_TEST_BUFFER_SIZE/16)));				
			printk(" -> dst%d:%2x\n",j,*(DmaMemInfo[j-1].dst + i*(DMA_TEST_BUFFER_SIZE/16)));	
		}
	}	

    return count;
}

static DEVICE_ATTR(dmamemcpy,  S_IRUGO|S_IALLUGO, memcpy_dma_read, memcpy_dma_write);


static int dma_memtest_channel_setup_and_init(struct Dma_MemToMem *DmaMemInfo, const char *name,
		u32 direction, u32 addr_width, u32 maxburst)
{
	DmaMemInfo->name = name;
	dma_cap_set(DMA_MEMCPY, DmaMemInfo->cap_mask);
	DmaMemInfo->dma_chan = dma_request_channel(DmaMemInfo->cap_mask,NULL,NULL);

	if(DmaMemInfo->dma_chan==NULL)
	{
		printk("request dma_chan %s fail\n",DmaMemInfo->name);
		return -1;
	}else
	{
		printk("request dma_chan %s success\n",DmaMemInfo->name);
	}

	DmaMemInfo->config = kmalloc(sizeof(struct dma_slave_config *),GFP_KERNEL);
	DmaMemInfo->tx = kmalloc(sizeof(struct dma_async_tx_descriptor *),GFP_KERNEL);
	if(DmaMemInfo->config == NULL)
	{
		printk("struct config kmalloc memory %s fail\n",DmaMemInfo->name);
		return -1;
	}
	else	
	{
		printk("struct config kmalloc memory %s sucess\n",DmaMemInfo->name);
	}
	

	DmaMemInfo->src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo->config->src_addr, GFP_KERNEL); 
	DmaMemInfo->dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo->config->dst_addr, GFP_KERNEL);
	if(DmaMemInfo->src == NULL || DmaMemInfo->dst == NULL)
	{
		printk("dma_alloc_coherent %s fail\n",DmaMemInfo->name);
		return -1;
	}
	else						
	{
		printk("dma_alloc_coherent %s success\n",DmaMemInfo->name);
	}
	
	DmaMemInfo->MenSize = DMA_TEST_BUFFER_SIZE;
	DmaMemInfo->config->direction = direction;
	DmaMemInfo->config->src_addr_width = addr_width;
	DmaMemInfo->config->dst_addr_width = addr_width;
	DmaMemInfo->config->src_maxburst = maxburst;
	DmaMemInfo->config->dst_maxburst = maxburst;
	
	dmaengine_slave_config(DmaMemInfo->dma_chan,DmaMemInfo->config);

	return 0;

}


static int dma_memcpy_probe(struct platform_device *pdev)
{
    int ret;

    ret = device_create_file(&pdev->dev, &dev_attr_dmamemcpy);
	printk(">>>>>>>>>>>>>>>>>>>>> dam_test_probe <<<<<<<<<<<<<<<<<<<<<<<<<<<\n");

    switch(DMA_THREAD) {		
		case 8:			
			dma_memtest_channel_setup_and_init(&DmaMemInfo[7], "DmaMemInfo[7]",
                DMA_MEM_TO_MEM, DMA_SLAVE_BUSWIDTH_8_BYTES, 16);			
		case 7:			
			dma_memtest_channel_setup_and_init(&DmaMemInfo[6], "DmaMemInfo[6]",
                DMA_MEM_TO_MEM, DMA_SLAVE_BUSWIDTH_8_BYTES, 16);	
		case 6:			
			dma_memtest_channel_setup_and_init(&DmaMemInfo[5], "DmaMemInfo[5]",
                DMA_MEM_TO_MEM, DMA_SLAVE_BUSWIDTH_8_BYTES, 16);	
		case 5:			
			dma_memtest_channel_setup_and_init(&DmaMemInfo[4], "DmaMemInfo[4]",
                DMA_MEM_TO_MEM, DMA_SLAVE_BUSWIDTH_8_BYTES, 16);		
		case 4:			
			dma_memtest_channel_setup_and_init(&DmaMemInfo[3], "DmaMemInfo[3]",
                DMA_MEM_TO_MEM, DMA_SLAVE_BUSWIDTH_8_BYTES, 16);		
		case 3:			
			dma_memtest_channel_setup_and_init(&DmaMemInfo[2], "DmaMemInfo[2]",
                DMA_MEM_TO_MEM, DMA_SLAVE_BUSWIDTH_8_BYTES, 16);	
		case 2:			
			dma_memtest_channel_setup_and_init(&DmaMemInfo[1], "DmaMemInfo[1]",
                DMA_MEM_TO_MEM, DMA_SLAVE_BUSWIDTH_8_BYTES, 16);	
		case 1:			
			dma_memtest_channel_setup_and_init(&DmaMemInfo[0], "DmaMemInfo[0]",
                DMA_MEM_TO_MEM, DMA_SLAVE_BUSWIDTH_8_BYTES, 16);
			break;		
		default:			
			printk("%s no channel\n", __func__);			
		break;		
	}
	
	printk("dma_memcpy_probe sucess\n");
    return 0;
}

static int dma_memcpy_remove(struct platform_device *pdev)
{
    device_remove_file(&pdev->dev, &dev_attr_dmamemcpy);

    return 0;
}

static struct platform_driver dma_mempcy_driver = {
        .driver = {
                .name   = "dma_memcpy",
                .owner  = THIS_MODULE,
        },
        .probe          = dma_memcpy_probe,
        .remove         = dma_memcpy_remove,
};

struct platform_device rk29_device_dma_cpy = {
	.name		  = "dma_memcpy",
	.id		  = 0,

};


static int __init dma_test_init(void)
{
		platform_device_register(&rk29_device_dma_cpy);
		return platform_driver_register(&dma_mempcy_driver);
}

static void __exit dma_test_exit(void)
{
		dma_release_channel(DmaMemInfo[0].dma_chan);
		platform_driver_unregister(&dma_mempcy_driver);
}

late_initcall(dma_test_init);
module_exit(dma_test_exit);

MODULE_DESCRIPTION("RK29 PL330 Dma Test Deiver");
MODULE_LICENSE("GPL V2");
MODULE_AUTHOR("ZhenFu Fang <fzf@rock-chips.com>");
MODULE_AUTHOR("Hong Huibin<hhb@rock-chips.com>");
