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
static int debug = 8;
module_param(debug,int,S_IRUGO|S_IWUSR);
//set dma transfer interval time (unit ms)
static int interval = 1000;
module_param(interval,int,S_IRUGO|S_IWUSR);


#define DMA_THREAD  1
#define MEMCPY_DMA_DBG(fmt...)  {if(debug > 0) printk(fmt);}

static struct Dma_MemToMem  DmaMemInfo0;
static struct Dma_MemToMem  DmaMemInfo1;
static struct Dma_MemToMem  DmaMemInfo2;
static struct Dma_MemToMem  DmaMemInfo3;
static struct Dma_MemToMem  DmaMemInfo4;
static struct Dma_MemToMem  DmaMemInfo5;
static struct Dma_MemToMem  DmaMemInfo6;
static struct Dma_MemToMem  DmaMemInfo7;

static struct rk29_dma_client rk29_dma_memcpy_client = {
        .name = "rk29-dma-memcpy",
};

static void rk29_dma_memcpy_callback0(void *buf_id, int size, enum rk29_dma_buffresult result)
{
	if(result != RK29_RES_OK) {
		MEMCPY_DMA_DBG("%s error:%d\n", __func__, result);
		return;
	}
	MEMCPY_DMA_DBG("%s ok\n", __func__);
	if(wq_condition == 0) {
		wq_condition = 1;
	 	wake_up_interruptible(&wq);
	}
	
}

static void rk29_dma_memcpy_callback1(void *buf_id, int size, enum rk29_dma_buffresult result)
{
	if(result != RK29_RES_OK) {
		MEMCPY_DMA_DBG("%s error:%d\n", __func__, result);
	}
	else
		MEMCPY_DMA_DBG("%s ok\n", __func__);
}

static void rk29_dma_memcpy_callback2(void *buf_id, int size, enum rk29_dma_buffresult result)
{
	if(result != RK29_RES_OK) {
		MEMCPY_DMA_DBG("%s error:%d\n", __func__, result);
	}
	else
		MEMCPY_DMA_DBG("%s ok\n", __func__);
}

static void rk29_dma_memcpy_callback3(void *buf_id, int size, enum rk29_dma_buffresult result)
{
	if(result != RK29_RES_OK) {
		MEMCPY_DMA_DBG("%s error:%d\n", __func__, result);
	}
	else
		MEMCPY_DMA_DBG("%s ok\n", __func__);
}

static void rk29_dma_memcpy_callback4(void *buf_id, int size, enum rk29_dma_buffresult result)
{
	if(result != RK29_RES_OK) {
		MEMCPY_DMA_DBG("%s error:%d\n", __func__, result);
	}
	else
		MEMCPY_DMA_DBG("%s ok\n", __func__);
}

static void rk29_dma_memcpy_callback5(void *buf_id, int size, enum rk29_dma_buffresult result)
{
	if(result != RK29_RES_OK) {
		MEMCPY_DMA_DBG("%s error:%d\n", __func__, result);
	}
	else
		MEMCPY_DMA_DBG("%s ok\n", __func__);
}


static void rk29_dma_memcpy_callback6(void *buf_id, int size, enum rk29_dma_buffresult result)
{
	if(result != RK29_RES_OK) {
		MEMCPY_DMA_DBG("%s error:%d\n", __func__, result);
	}
	else
		MEMCPY_DMA_DBG("%s ok\n", __func__);
}


static void rk29_dma_memcpy_callback7(void *buf_id, int size, enum rk29_dma_buffresult result)
{
	if(result != RK29_RES_OK) {
		MEMCPY_DMA_DBG("%s error:%d\n", __func__, result);
	}
	else
		MEMCPY_DMA_DBG("%s ok\n", __func__);
}

//int slecount = 0;
static ssize_t memcpy_dma_read(struct device *device,struct device_attribute *attr, char *argv)
{

     return 0;
}

static ssize_t memcpy_dma_write(struct device *device, struct device_attribute *attr, const char *argv, size_t count)
{
    int i;
    MEMCPY_DMA_DBG("memcpy_dma_write\n");

	switch(DMA_THREAD) {
		case 8:
			memset(DmaMemInfo7.src, 0x77, DMA_TEST_BUFFER_SIZE);
			memset(DmaMemInfo7.dst, 0x0, DMA_TEST_BUFFER_SIZE);
			rk29_dma_devconfig(DMACH_DMAC7_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo7.SrcAddr);
			rk29_dma_enqueue(DMACH_DMAC7_MEMTOMEM, NULL, DmaMemInfo7.DstAddr, DmaMemInfo7.MenSize);
		case 7:
			memset(DmaMemInfo6.src, 0x66, DMA_TEST_BUFFER_SIZE);
			memset(DmaMemInfo6.dst, 0x0, DMA_TEST_BUFFER_SIZE);
			rk29_dma_devconfig(DMACH_DMAC6_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo6.SrcAddr);
			rk29_dma_enqueue(DMACH_DMAC6_MEMTOMEM, NULL, DmaMemInfo6.DstAddr, DmaMemInfo6.MenSize);
		case 6:
			memset(DmaMemInfo5.src, 0x55, DMA_TEST_BUFFER_SIZE);
			memset(DmaMemInfo5.dst, 0x0, DMA_TEST_BUFFER_SIZE);
			rk29_dma_devconfig(DMACH_DMAC5_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo5.SrcAddr);
			rk29_dma_enqueue(DMACH_DMAC5_MEMTOMEM, NULL, DmaMemInfo5.DstAddr, DmaMemInfo5.MenSize);
		case 5:
			memset(DmaMemInfo4.src, 0x44, DMA_TEST_BUFFER_SIZE);
			memset(DmaMemInfo4.dst, 0x0, DMA_TEST_BUFFER_SIZE);
			rk29_dma_devconfig(DMACH_DMAC4_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo4.SrcAddr);
			rk29_dma_enqueue(DMACH_DMAC4_MEMTOMEM, NULL, DmaMemInfo4.DstAddr, DmaMemInfo4.MenSize);
		case 4:
			memset(DmaMemInfo3.src, 0x33, DMA_TEST_BUFFER_SIZE);
			memset(DmaMemInfo3.dst, 0x0, DMA_TEST_BUFFER_SIZE);
			rk29_dma_devconfig(DMACH_DMAC3_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo3.SrcAddr);
			rk29_dma_enqueue(DMACH_DMAC3_MEMTOMEM, NULL, DmaMemInfo3.DstAddr, DmaMemInfo3.MenSize);
		case 3:
			memset(DmaMemInfo2.src, 0x22, DMA_TEST_BUFFER_SIZE);
			memset(DmaMemInfo2.dst, 0x0, DMA_TEST_BUFFER_SIZE);
			rk29_dma_devconfig(DMACH_DMAC2_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo2.SrcAddr);
			rk29_dma_enqueue(DMACH_DMAC2_MEMTOMEM, NULL, DmaMemInfo2.DstAddr, DmaMemInfo2.MenSize);
		case 2:
			memset(DmaMemInfo1.src, 0xaa, DMA_TEST_BUFFER_SIZE);
			memset(DmaMemInfo1.dst, 0x0, DMA_TEST_BUFFER_SIZE);
			rk29_dma_devconfig(DMACH_DMAC1_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo1.SrcAddr);
			rk29_dma_enqueue(DMACH_DMAC1_MEMTOMEM, NULL, DmaMemInfo1.DstAddr, DmaMemInfo1.MenSize);
		case 1:
			memset(DmaMemInfo0.src, 0xaa, DMA_TEST_BUFFER_SIZE);
			memset(DmaMemInfo0.dst, 0x0, DMA_TEST_BUFFER_SIZE);
			rk29_dma_devconfig(DMACH_DMAC0_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo0.SrcAddr);
			rk29_dma_enqueue(DMACH_DMAC0_MEMTOMEM, NULL, DmaMemInfo0.DstAddr, DmaMemInfo0.MenSize);
			break;
		default:
			printk("%s no channel\n", __func__);
			break;
	
	}

	switch(DMA_THREAD) {
		case 8:
			rk29_dma_ctrl(DMACH_DMAC7_MEMTOMEM, RK29_DMAOP_START);
		case 7:
			rk29_dma_ctrl(DMACH_DMAC6_MEMTOMEM, RK29_DMAOP_START);			
		case 6:
			rk29_dma_ctrl(DMACH_DMAC5_MEMTOMEM, RK29_DMAOP_START);			
		case 5:
			rk29_dma_ctrl(DMACH_DMAC4_MEMTOMEM, RK29_DMAOP_START);		
		case 4:
			rk29_dma_ctrl(DMACH_DMAC3_MEMTOMEM, RK29_DMAOP_START);					
		case 3:
			rk29_dma_ctrl(DMACH_DMAC2_MEMTOMEM, RK29_DMAOP_START);		
		case 2:
			rk29_dma_ctrl(DMACH_DMAC1_MEMTOMEM, RK29_DMAOP_START);
		case 1:
			rk29_dma_ctrl(DMACH_DMAC0_MEMTOMEM, RK29_DMAOP_START);
			break;
		default:
			printk("%s no channel\n", __func__);
			break;
	
	}

	wait_event_interruptible_timeout(wq, wq_condition, 500);
    
    switch(DMA_THREAD) {
		case 8:
			for(i = 0; i < 16; i++) {
				MEMCPY_DMA_DBG("src7:%x", *(DmaMemInfo7.src + i*(DMA_TEST_BUFFER_SIZE/16)));
				MEMCPY_DMA_DBG(" -> dst7:%x\n", *(DmaMemInfo7.dst + i*(DMA_TEST_BUFFER_SIZE/16)));
			}
		case 7:
			for(i = 0; i < 16; i++) {
				MEMCPY_DMA_DBG("src6:%x", *(DmaMemInfo6.src + i*(DMA_TEST_BUFFER_SIZE/16)));
				MEMCPY_DMA_DBG(" -> dst6:%x\n", *(DmaMemInfo6.dst + i*(DMA_TEST_BUFFER_SIZE/16)));
			}
		case 6:
			for(i = 0; i < 16; i++) {
				MEMCPY_DMA_DBG("src5:%x", *(DmaMemInfo5.src + i*(DMA_TEST_BUFFER_SIZE/16)));
				MEMCPY_DMA_DBG(" -> dst5:%x\n", *(DmaMemInfo5.dst + i*(DMA_TEST_BUFFER_SIZE/16)));
			}
		case 5:
			for(i = 0; i < 16; i++) {
				MEMCPY_DMA_DBG("src4:%x", *(DmaMemInfo4.src + i*(DMA_TEST_BUFFER_SIZE/16)));
				MEMCPY_DMA_DBG(" -> dst4:%x\n", *(DmaMemInfo4.dst + i*(DMA_TEST_BUFFER_SIZE/16)));
			}			
		case 4:
			for(i = 0; i < 16; i++) {
				MEMCPY_DMA_DBG("src3:%x", *(DmaMemInfo3.src + i*(DMA_TEST_BUFFER_SIZE/16)));
				MEMCPY_DMA_DBG(" -> dst3:%x\n", *(DmaMemInfo3.dst + i*(DMA_TEST_BUFFER_SIZE/16)));
			}			
		case 3:
			for(i = 0; i < 16; i++) {
				MEMCPY_DMA_DBG("src2:%x", *(DmaMemInfo2.src + i*(DMA_TEST_BUFFER_SIZE/16)));
				MEMCPY_DMA_DBG(" -> dst2:%x\n", *(DmaMemInfo2.dst + i*(DMA_TEST_BUFFER_SIZE/16)));
			}
		case 2:
			for(i = 0; i < 16; i++) {
				MEMCPY_DMA_DBG("src1:%x", *(DmaMemInfo1.src + i*(DMA_TEST_BUFFER_SIZE/16)));
				MEMCPY_DMA_DBG(" -> dst1:%x\n", *(DmaMemInfo1.dst + i*(DMA_TEST_BUFFER_SIZE/16)));
			}			
		case 1:
			for(i = 0; i < 16; i++) {
				MEMCPY_DMA_DBG("src0:%x", *(DmaMemInfo0.src + i*(DMA_TEST_BUFFER_SIZE/16)));
				MEMCPY_DMA_DBG(" -> dst0:%x\n", *(DmaMemInfo0.dst + i*(DMA_TEST_BUFFER_SIZE/16)));
			}		
			break;
		default:
			printk("%s no channel\n", __func__);
			break;	
	}
    
	wq_condition = 0;
    return 0;
}

static DEVICE_ATTR(dmamemcpy,  S_IRUGO|S_IALLUGO, memcpy_dma_read, memcpy_dma_write);


static int __devinit dma_memcpy_probe(struct platform_device *pdev)
{
    int ret;

    ret = device_create_file(&pdev->dev, &dev_attr_dmamemcpy);
	printk(">>>>>>>>>>>>>>>>>>>>> dam_test_probe <<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
	
	switch(DMA_THREAD) {
		case 8:
			if (rk29_dma_request(DMACH_DMAC7_MEMTOMEM, &rk29_dma_memcpy_client, NULL) == -EBUSY) {
				printk("DMACH_DMAC7_MEMTOMEM request fail\n");
			} else {
				rk29_dma_config(DMACH_DMAC7_MEMTOMEM, 8, 16);
				rk29_dma_set_buffdone_fn(DMACH_DMAC7_MEMTOMEM, rk29_dma_memcpy_callback7);
				DmaMemInfo7.src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo7.SrcAddr, GFP_KERNEL);
				DmaMemInfo7.dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo7.DstAddr, GFP_KERNEL);
				DmaMemInfo7.MenSize = DMA_TEST_BUFFER_SIZE;
				if(DmaMemInfo7.src == NULL || DmaMemInfo7.dst == NULL)
					printk("DMACH_DMAC7_MEMTOMEM alloc memory fail\n");
				else
					printk("DMACH_DMAC7_MEMTOMEM request sucess\n");
			}	
			
		case 7:
			if (rk29_dma_request(DMACH_DMAC6_MEMTOMEM, &rk29_dma_memcpy_client, NULL) == -EBUSY) {
				printk("DMACH_DMAC6_MEMTOMEM request fail\n");
			} else {
				rk29_dma_config(DMACH_DMAC6_MEMTOMEM, 8, 16);
				rk29_dma_set_buffdone_fn(DMACH_DMAC6_MEMTOMEM, rk29_dma_memcpy_callback6);
				DmaMemInfo6.src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo6.SrcAddr, GFP_KERNEL);
				DmaMemInfo6.dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo6.DstAddr, GFP_KERNEL);
				DmaMemInfo6.MenSize = DMA_TEST_BUFFER_SIZE;
				if(DmaMemInfo6.src == NULL || DmaMemInfo6.dst == NULL)
					printk("DMACH_DMAC6_MEMTOMEM alloc memory fail\n");
				else	
					printk("DMACH_DMAC6_MEMTOMEM request sucess\n");
			}	
			
		case 6:
			if (rk29_dma_request(DMACH_DMAC5_MEMTOMEM, &rk29_dma_memcpy_client, NULL) == -EBUSY) {
				printk("DMACH_DMAC5_MEMTOMEM request fail\n");
			} else {
				rk29_dma_config(DMACH_DMAC5_MEMTOMEM, 8, 16);
				rk29_dma_set_buffdone_fn(DMACH_DMAC5_MEMTOMEM, rk29_dma_memcpy_callback5);
				DmaMemInfo5.src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo5.SrcAddr, GFP_KERNEL);
				DmaMemInfo5.dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo5.DstAddr, GFP_KERNEL);
				DmaMemInfo5.MenSize = DMA_TEST_BUFFER_SIZE;
				if(DmaMemInfo5.src == NULL || DmaMemInfo5.dst == NULL)
					printk("DMACH_DMAC5_MEMTOMEM alloc memory fail\n");
				else	
					printk("DMACH_DMAC5_MEMTOMEM request sucess\n");
			}
				
		case 5:
			if (rk29_dma_request(DMACH_DMAC4_MEMTOMEM, &rk29_dma_memcpy_client, NULL) == -EBUSY) {
				printk("DMACH_DMAC4_MEMTOMEM request fail\n");
			} else {
				rk29_dma_config(DMACH_DMAC4_MEMTOMEM, 8, 16);
				rk29_dma_set_buffdone_fn(DMACH_DMAC4_MEMTOMEM, rk29_dma_memcpy_callback4);
				DmaMemInfo4.src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo4.SrcAddr, GFP_KERNEL);
				DmaMemInfo4.dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo4.DstAddr, GFP_KERNEL);
				DmaMemInfo4.MenSize = DMA_TEST_BUFFER_SIZE;
				if(DmaMemInfo4.src == NULL || DmaMemInfo4.dst == NULL)
					printk("DMACH_DMAC4_MEMTOMEM alloc memory fail\n");
				else	
					printk("DMACH_DMAC4_MEMTOMEM request sucess\n");
			}	
			
		case 4:
			if (rk29_dma_request(DMACH_DMAC3_MEMTOMEM, &rk29_dma_memcpy_client, NULL) == -EBUSY) {
				printk("DMACH_DMAC3_MEMTOMEM request fail\n");
			} else {
				rk29_dma_config(DMACH_DMAC3_MEMTOMEM, 8, 16);
				rk29_dma_set_buffdone_fn(DMACH_DMAC3_MEMTOMEM, rk29_dma_memcpy_callback3);
				DmaMemInfo3.src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo3.SrcAddr, GFP_KERNEL);
				DmaMemInfo3.dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo3.DstAddr, GFP_KERNEL);
				DmaMemInfo3.MenSize = DMA_TEST_BUFFER_SIZE;
				if(DmaMemInfo3.src == NULL || DmaMemInfo3.dst == NULL)
					printk("DMACH_DMAC3_MEMTOMEM alloc memory fail\n");
				else	
					printk("DMACH_DMAC3_MEMTOMEM request sucess\n");
			}	
			
		case 3:
			if (rk29_dma_request(DMACH_DMAC2_MEMTOMEM, &rk29_dma_memcpy_client, NULL) == -EBUSY) {
				printk("DMACH_DMAC2_MEMTOMEM request fail\n");
			} else {
				rk29_dma_config(DMACH_DMAC2_MEMTOMEM, 8, 16);
				rk29_dma_set_buffdone_fn(DMACH_DMAC2_MEMTOMEM, rk29_dma_memcpy_callback2);
				DmaMemInfo2.src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo2.SrcAddr, GFP_KERNEL);
				DmaMemInfo2.dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo2.DstAddr, GFP_KERNEL);
				DmaMemInfo2.MenSize = DMA_TEST_BUFFER_SIZE;
				if(DmaMemInfo2.src == NULL || DmaMemInfo2.dst == NULL)
					printk("DMACH_DMAC2_MEMTOMEM alloc memory fail\n");
				else	
					printk("DMACH_DMAC2_MEMTOMEM request sucess\n");
			}	 
			
		case 2:
			if (rk29_dma_request(DMACH_DMAC1_MEMTOMEM, &rk29_dma_memcpy_client, NULL) == -EBUSY) {
				printk("DMACH_DMAC1_MEMTOMEM request fail\n");
			} else {
				rk29_dma_config(DMACH_DMAC1_MEMTOMEM, 8, 16);
				rk29_dma_set_buffdone_fn(DMACH_DMAC1_MEMTOMEM, rk29_dma_memcpy_callback1);
				DmaMemInfo1.src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo1.SrcAddr, GFP_KERNEL);
				DmaMemInfo1.dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo1.DstAddr, GFP_KERNEL);
				DmaMemInfo1.MenSize = DMA_TEST_BUFFER_SIZE;
				if(DmaMemInfo1.src == NULL || DmaMemInfo1.dst == NULL)
					printk("DMACH_DMAC1_MEMTOMEM alloc memory fail\n");
				else
					printk("DMACH_DMAC1_MEMTOMEM request sucess\n");
			}	
			
		case 1:
			if (rk29_dma_request(DMACH_DMAC0_MEMTOMEM, &rk29_dma_memcpy_client, NULL) == -EBUSY) {
				printk("DMACH_DMAC0_MEMTOMEM request fail\n");
			} else {
				rk29_dma_config(DMACH_DMAC0_MEMTOMEM, 8, 16);
				rk29_dma_set_buffdone_fn(DMACH_DMAC0_MEMTOMEM, rk29_dma_memcpy_callback0);
				DmaMemInfo0.src = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo0.SrcAddr, GFP_KERNEL);
				DmaMemInfo0.dst = dma_alloc_coherent(NULL, DMA_TEST_BUFFER_SIZE, &DmaMemInfo0.DstAddr, GFP_KERNEL);
				DmaMemInfo0.MenSize = DMA_TEST_BUFFER_SIZE;
				if(DmaMemInfo0.src == NULL || DmaMemInfo0.dst == NULL)
					printk("DMACH_DMAC0_MEMTOMEM alloc memory fail\n");
				else
					printk("DMACH_DMAC0_MEMTOMEM request sucess\n");
			}	
			break;
		default:
			printk("%s no channel\n", __func__);
			break;
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

module_init(dma_test_init);
module_exit(dma_test_exit);

MODULE_DESCRIPTION("RK29 PL330 Dma Test Deiver");
MODULE_LICENSE("GPL V2");
MODULE_AUTHOR("ZhenFu Fang <fzf@rock-chips.com>");
MODULE_AUTHOR("Hong Huibin<hhb@rock-chips.com>");
