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

static struct rk29_dma_client rk29_dma_Memcpy_client = {
        .name = "rk29-dma-memcpy",
};


struct Dma_MemToMem {
	int SrcAddr;
	int DstAddr;
	int MenSize;
};


static void rk29_dma_memcpy_callback(struct rk29_dma_chan *dma_ch, void *buf_id, int size, enum rk29_dma_buffresult result)
{
    wq_condition = 1;
 	wake_up_interruptible(&wq);
}

//int slecount = 0;
static ssize_t Memcpy_Dma_read(struct device *device,struct device_attribute *attr, void *argv)
{

     return 0;
}

static ssize_t Memcpy_Dma_write (struct device *device,struct device_attribute *attr, void *argv)//(struct device_driver *device, const char *argv,size_t count)
{
    int dma_flag;
    u32 mcode_sbus;
    u32 mcode_dbus;
    int i;
    int rt;
    long usec1 = 0;
    // long usec2 = 0;

    struct Dma_MemToMem  *DmaMemInfo = (struct Dma_MemToMem *)argv;


    dma_flag = rk29_dma_request(DMACH_DMAC0_MemToMem, &rk29_dma_Memcpy_client, NULL);           
    dma_flag = DMACH_DMAC0_MemToMem;

    rt = rk29_dma_devconfig(dma_flag, RK29_DMASRC_MEMTOMEM, DmaMemInfo->SrcAddr);
    rt = rk29_dma_config(dma_flag, 8);
    rt = rk29_dma_set_buffdone_fn(dma_flag, rk29_dma_memcpy_callback);
    rt = rk29_dma_enqueue(dma_flag, NULL, DmaMemInfo->DstAddr, DmaMemInfo->MenSize);
    rt = rk29_dma_ctrl(dma_flag, RK29_DMAOP_START);    
    wait_event_interruptible_timeout(wq, wq_condition, HZ/20);
    wq_condition = 0; 
    return 0;
}

static DRIVER_ATTR(DmaMemcpy,  S_IRUGO|S_IALLUGO, Memcpy_Dma_read, Memcpy_Dma_write);


static int __init dma_memcpy_probe(struct platform_device *pdev)
{
    int ret;
      
    ret = device_create_file(&pdev->dev, &driver_attr_DmaMemcpy);
    if(ret)
    {
        printk(">> fb1 dsp win0 info device_create_file err\n");
        ret = -EINVAL;
    }
   // printk(">>>>>>>>>>>>>>>>>>>>> dam_test_probe ok>>>>>>>>>>>>>>>>>>>>>>>>");        
    return 0;
}

static int  dma_memcpy_remove(struct platform_device *pdev)
{
    int ret;
    driver_remove_file(&pdev->dev, &driver_attr_DmaMemcpy);
  
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




