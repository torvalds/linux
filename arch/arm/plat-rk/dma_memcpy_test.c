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
 * Date: 2012.03.26
 *
 * HOW TO USE IT?
 * enter the follow command at command line
 * echo 1 > sys/module/dma_memcpy_test/parameters/debug   enable log output,default is enable
 * echo 1 > sys/module/dma_memcpy_test/parameters/dmac1   set dmac1 memcpy
 * echo 1 > sys/module/dma_memcpy_test/parameters/dmac2   set dmac2 memcpy
 * echo 1000 > sys/module/dma_memcpy_test/parameters/interval   set dma transfer interval, default is 1000ms
 * echo 1 > /sys/devices/platform/dma_memcpy.0/dmamemcpy  to start the dma test
 *
 */



#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <mach/io.h>
#include <plat/dma-pl330.h>
#include <asm/uaccess.h>
#include <asm/current.h>

#define DMA_TEST_BUFFER_SIZE 4096
static DECLARE_WAIT_QUEUE_HEAD(wq);
static int wq_condition = 0;

struct Dma_MemToMem {
	dma_addr_t SrcAddr;			//phy address
	dma_addr_t DstAddr;
	unsigned char* src;			//virtual address
	unsigned char* dst;
	int MenSize;
};
//wait_queue_head_t	dma_memcpy_wait;

//enable log output
static int debug = 1;
module_param(debug,int,S_IRUGO|S_IWUSR);
//set dma transfer interval time (unit ms)
static int interval = 1000;
module_param(interval,int,S_IRUGO|S_IWUSR);

static int dmac1 = 1;
module_param(dmac1,int,S_IRUGO|S_IWUSR);

static int dmac2 = -1;
module_param(dmac2,int,S_IRUGO|S_IWUSR);


static struct Dma_MemToMem  DmaMemInfo1;
static struct Dma_MemToMem  DmaMemInfo2;


#define MEMCPY_DMA_DBG(fmt...)  {if(debug > 0) printk(fmt);}


static struct rk29_dma_client rk29_dma_memcpy_client = {
        .name = "rk29-dma-memcpy",
};



static void rk29_dma_memcpy_callback1(void *buf_id, int size, enum rk29_dma_buffresult result)
{
	if(result != RK29_RES_OK){
		return;
	}
	MEMCPY_DMA_DBG("rk29_dma_memcpy_callback1 ok\n");
	if(wq_condition == 0){
		wq_condition = 1;
	 	wake_up_interruptible(&wq);
	}
	//wake_up_interruptible(&dma_memcpy_wait);
}

static void rk29_dma_memcpy_callback2(void *buf_id, int size, enum rk29_dma_buffresult result)
{
	if(result != RK29_RES_OK){
		return;
	}
	MEMCPY_DMA_DBG("rk29_dma_memcpy_callback2 ok\n");
	if(wq_condition == 0){
		wq_condition = 1;
	 	wake_up_interruptible(&wq);
	}
	//wake_up_interruptible(&dma_memcpy_wait);
}

//int slecount = 0;
static ssize_t memcpy_dma_read(struct device *device,struct device_attribute *attr, char *argv)
{

     return 0;
}

static ssize_t memcpy_dma_write(struct device *device, struct device_attribute *attr, const char *argv, size_t count)
{
    int rt, i;
  //  struct Dma_MemToMem  *DmaMemInfo1 = (struct Dma_MemToMem *)argv;
    MEMCPY_DMA_DBG("memcpy_dma_write\n");

    //dmac1
    if(dmac1 > 0) {
		memset(DmaMemInfo1.src, 0x55, DMA_TEST_BUFFER_SIZE);
		memset(DmaMemInfo1.dst, 0x0, DMA_TEST_BUFFER_SIZE);
		rt = rk29_dma_devconfig(DMACH_DMAC1_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo1.SrcAddr);
		rt = rk29_dma_enqueue(DMACH_DMAC1_MEMTOMEM, NULL, DmaMemInfo1.DstAddr, DmaMemInfo1.MenSize);
		rt = rk29_dma_ctrl(DMACH_DMAC1_MEMTOMEM, RK29_DMAOP_START);
	}

    //dmac2
    if(dmac2 > 0) {
		memset(DmaMemInfo2.src, 0xaa, DMA_TEST_BUFFER_SIZE);
		memset(DmaMemInfo2.dst, 0x0, DMA_TEST_BUFFER_SIZE);
		rt = rk29_dma_devconfig(DMACH_DMAC2_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo2.SrcAddr);
		rt = rk29_dma_enqueue(DMACH_DMAC2_MEMTOMEM, NULL, DmaMemInfo2.DstAddr, DmaMemInfo2.MenSize);
		rt = rk29_dma_ctrl(DMACH_DMAC2_MEMTOMEM, RK29_DMAOP_START);
    }

    if(dmac2 > 0 || dmac1 > 0)
    	wait_event_interruptible_timeout(wq, wq_condition, 500);
    	
	if(dmac1 > 0) {
		for(i = 0; i < 16; i++) {
			MEMCPY_DMA_DBG("dmac1 src1:%x", *(DmaMemInfo1.src + i*(DMA_TEST_BUFFER_SIZE/16)));
			MEMCPY_DMA_DBG(" -> dst1:%x\n", *(DmaMemInfo1.dst + i*(DMA_TEST_BUFFER_SIZE/16)));
		}
	}	
	
	if(dmac2 > 0) {
		for(i = 0; i < 16; i++) {
			MEMCPY_DMA_DBG("dmac2 src2:%x", *(DmaMemInfo2.src + i*(DMA_TEST_BUFFER_SIZE/16)));
			MEMCPY_DMA_DBG(" -> dst2:%x\n", *(DmaMemInfo2.dst + i*(DMA_TEST_BUFFER_SIZE/16)));
		}
	}
	msleep(interval);
	wq_condition = 0;
	//init_waitqueue_head(&dma_memcpy_wait);
	//interruptible_sleep_on(&dma_memcpy_wait);
    return 0;
}

static DEVICE_ATTR(dmamemcpy,  S_IRUGO|S_IALLUGO, memcpy_dma_read, memcpy_dma_write);


static int __devinit dma_memcpy_probe(struct platform_device *pdev)
{
    int ret;

    ret = device_create_file(&pdev->dev, &dev_attr_dmamemcpy);
	printk(">>>>>>>>>>>>>>>>>>>>> dam_test_probe <<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
	
    //dmac1
	if (rk29_dma_request(DMACH_DMAC1_MEMTOMEM, &rk29_dma_memcpy_client, NULL) == -EBUSY) {
		printk("DMACH_DMAC1_MEMTOMEM request fail\n");
	} else {
		rk29_dma_config(DMACH_DMAC1_MEMTOMEM, 8, 16);
		rk29_dma_set_buffdone_fn(DMACH_DMAC1_MEMTOMEM, rk29_dma_memcpy_callback1);
		DmaMemInfo1.src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo1.SrcAddr, GFP_KERNEL);
		DmaMemInfo1.dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo1.DstAddr, GFP_KERNEL);
		DmaMemInfo1.MenSize = DMA_TEST_BUFFER_SIZE;
		printk("DMACH_DMAC1_MEMTOMEM request sucess\n");
	}	
	
    //dmac2
	if (rk29_dma_request(DMACH_DMAC2_MEMTOMEM, &rk29_dma_memcpy_client, NULL) == -EBUSY) {
		printk("DMACH_DMAC2_MEMTOMEM request fail\n");
	} else {
		rk29_dma_config(DMACH_DMAC2_MEMTOMEM, 8, 16);
		rk29_dma_set_buffdone_fn(DMACH_DMAC2_MEMTOMEM, rk29_dma_memcpy_callback2);
		DmaMemInfo2.src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo2.SrcAddr, GFP_KERNEL);
		DmaMemInfo2.dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo2.DstAddr, GFP_KERNEL);
		DmaMemInfo2.MenSize = DMA_TEST_BUFFER_SIZE;
		printk("DMACH_DMAC2_MEMTOMEM request sucess\n");
	}
    return 0;
}

static int __devexit dma_memcpy_remove(struct platform_device *pdev)
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
        .remove         = __devexit_p(dma_memcpy_remove),
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
        platform_driver_unregister(&dma_mempcy_driver);
}

late_initcall(dma_test_init);
module_exit(dma_test_exit);

MODULE_DESCRIPTION("RK29 PL330 Dma Test Deiver");
MODULE_LICENSE("GPL V2");
MODULE_AUTHOR("ZhenFu Fang <fzf@rock-chips.com>");
