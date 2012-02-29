#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/sched.h>

#include <mach/rk29_iomap.h>
#include <mach/rk29-dma-pl330.h>
#include <asm/uaccess.h>
#include <asm/current.h>


static DECLARE_WAIT_QUEUE_HEAD(wq);
static int wq_condition = 0;
//wait_queue_head_t	dma_memcpy_wait;

static struct rk29_dma_client rk29_dma_memcpy_client = {
        .name = "rk29-dma-memcpy",
};


struct Dma_MemToMem {
	int SrcAddr;
	int DstAddr;
	int MenSize;
};


static void rk29_dma_memcpy_callback(void *buf_id, int size, enum rk29_dma_buffresult result)
{
    wq_condition = 1;
 	wake_up_interruptible(&wq);
	//wake_up_interruptible(&dma_memcpy_wait);
}

//int slecount = 0;
static ssize_t memcpy_dma_read(struct device *device,struct device_attribute *attr, char *argv)
{

     return 0;
}

static ssize_t memcpy_dma_write(struct device *device, struct device_attribute *attr, const char *argv, size_t count)
{
    int rt;
    struct Dma_MemToMem  *DmaMemInfo = (struct Dma_MemToMem *)argv;
 
    rt = rk29_dma_devconfig(DMACH_DMAC0_MEMTOMEM, RK29_DMASRC_MEMTOMEM, DmaMemInfo->SrcAddr);
    rt = rk29_dma_enqueue(DMACH_DMAC0_MEMTOMEM, NULL, DmaMemInfo->DstAddr, DmaMemInfo->MenSize);
    rt = rk29_dma_ctrl(DMACH_DMAC0_MEMTOMEM, RK29_DMAOP_START);    
    wait_event_interruptible_timeout(wq, wq_condition, 200);
    wq_condition = 0;  
	//init_waitqueue_head(&dma_memcpy_wait);
	//interruptible_sleep_on(&dma_memcpy_wait);
    return 0;
}

static DEVICE_ATTR(dmamemcpy,  S_IRUGO|S_IXUGO, memcpy_dma_read, memcpy_dma_write);


static int __devinit dma_memcpy_probe(struct platform_device *pdev)
{
    int ret;
      
    ret = device_create_file(&pdev->dev, &dev_attr_dmamemcpy);
    rk29_dma_request(DMACH_DMAC0_MEMTOMEM, &rk29_dma_memcpy_client, NULL); 
    rk29_dma_config(DMACH_DMAC0_MEMTOMEM, 8, 16);
    rk29_dma_set_buffdone_fn(DMACH_DMAC0_MEMTOMEM, rk29_dma_memcpy_callback);
    if(ret)
    {
        printk(">> fb1 dsp win0 info device_create_file err\n");
        ret = -EINVAL;
    }
   // printk(">>>>>>>>>>>>>>>>>>>>> dam_test_probe ok>>>>>>>>>>>>>>>>>>>>>>>>");        
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


static int __init dma_test_init(void)
{
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




