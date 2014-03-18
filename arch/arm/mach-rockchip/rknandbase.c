
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/bootmem.h>
#include <asm/io.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>


#ifdef CONFIG_OF
#include <linux/of.h>
#endif

struct rknand_info {
    int tag;
    int enable;
    int reserved0[6];
    
    void (*rknand_suspend)(void);
    void (*rknand_resume)(void);
    void (*rknand_buffer_shutdown)(void);
    int (*rknand_exit)(void);
    
    int (*ftl_read) (int lun,int Index, int nSec, void *buf);  
    int (*ftl_write) (int lun,int Index, int nSec, void *buf);
    void (*nand_timing_config)(unsigned long AHBnKHz);
    void (*rknand_dev_cache_flush)(void);
    
    int reserved1[16];
};

struct rknand_info * gpNandInfo = NULL;
static char *cmdline=NULL;
int rknand_get_part_info(char **s)
{
	*s = cmdline;
    return 0;
}
EXPORT_SYMBOL(rknand_get_part_info); 

static char sn_data[512];
static char vendor0[512];

char GetSNSectorInfo(char * pbuf)
{
    memcpy(pbuf,sn_data,0x200);
    return 0;
}

char GetSNSectorInfoBeforeNandInit(char * pbuf)
{
    memcpy(pbuf,sn_data,0x200);
    return 0;
} 

char GetVendor0InfoBeforeNandInit(char * pbuf)
{
    memcpy(pbuf,vendor0 + 8,504);
    return 0;
}

int  GetParamterInfo(char * pbuf , int len)
{
    int ret = -1;
	return ret;
}

void rknand_spin_lock_init(spinlock_t * p_lock)
{
    spin_lock_init(p_lock);
}
EXPORT_SYMBOL(rknand_spin_lock_init);

void rknand_spin_lock(spinlock_t * p_lock)
{
    spin_lock_irq(p_lock);
}
EXPORT_SYMBOL(rknand_spin_lock);

void rknand_spin_unlock(spinlock_t * p_lock)
{
    spin_unlock_irq(p_lock);
}
EXPORT_SYMBOL(rknand_spin_unlock);


struct semaphore  g_rk_nand_ops_mutex;
void rknand_device_lock_init(void)
{
	sema_init(&g_rk_nand_ops_mutex, 1);
}
EXPORT_SYMBOL(rknand_device_lock_init);
void rknand_device_lock (void)
{
     down(&g_rk_nand_ops_mutex);
}
EXPORT_SYMBOL(rknand_device_lock);

int rknand_device_trylock (void)
{
    return down_trylock(&g_rk_nand_ops_mutex);
}
EXPORT_SYMBOL(rknand_device_trylock);

void rknand_device_unlock (void)
{
    up(&g_rk_nand_ops_mutex);
}
EXPORT_SYMBOL(rknand_device_unlock);


int rknand_get_device(struct rknand_info ** prknand_Info)
{
    *prknand_Info = gpNandInfo;
    return 0;    
}
EXPORT_SYMBOL(rknand_get_device);

int rknand_dma_map_single(unsigned long ptr,int size,int dir)
{
    return dma_map_single(NULL, ptr,size, dir?DMA_TO_DEVICE:DMA_FROM_DEVICE);
}
EXPORT_SYMBOL(rknand_dma_map_single);

void rknand_dma_unmap_single(unsigned long ptr,int size,int dir)
{
    dma_unmap_single(NULL, ptr,size, dir?DMA_TO_DEVICE:DMA_FROM_DEVICE);
}
EXPORT_SYMBOL(rknand_dma_unmap_single);

int rknand_flash_cs_init(void)
{

}
EXPORT_SYMBOL(rknand_flash_cs_init);

int rknand_get_reg_addr(int *pNandc0,int *pNandc1,int *pSDMMC0,int *pSDMMC1,int *pSDMMC2)
{
    //*pNandc = ioremap(RK30_NANDC_PHYS,RK30_NANDC_SIZE);
    //*pSDMMC0 = ioremap(SDMMC0_BASE_ADDR, 0x4000);
    //*pSDMMC1 = ioremap(SDMMC1_BASE_ADDR, 0x4000);
    //*pSDMMC2 = ioremap(EMMC_BASE_ADDR,   0x4000);
	*pNandc0 = ioremap(0x10500000,0x4000);
	//*pNandc1 = NULL;
}

EXPORT_SYMBOL(rknand_get_reg_addr);

static int g_nandc_irq = 59;
int rknand_nandc_irq_init(int mode,void * pfun)
{
    int ret = 0;
    if(mode) //init
    {
        ret = request_irq(g_nandc_irq, pfun, 0, "nandc", NULL);
        if(ret)
            printk("request IRQ_NANDC irq , ret=%x.........\n", ret);
    }
    else //deinit
    {
        free_irq(g_nandc_irq,  NULL);
    }
    return ret;
}
EXPORT_SYMBOL(rknand_nandc_irq_init);
static int rknand_probe(struct platform_device *pdev)
{
	g_nandc_irq = platform_get_irq(pdev, 0);
	printk("g_nandc_irq: %d\n",g_nandc_irq);
	if (g_nandc_irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return g_nandc_irq;
	}
	return 0;
}

static int rknand_suspend(struct platform_device *pdev, pm_message_t state)
{
    if(gpNandInfo->rknand_suspend)
        gpNandInfo->rknand_suspend();  
	return 0;
}

static int rknand_resume(struct platform_device *pdev)
{
    if(gpNandInfo->rknand_resume)
       gpNandInfo->rknand_resume();  
	return 0;
}

static void rknand_shutdown(struct platform_device *pdev)
{
    if(gpNandInfo->rknand_buffer_shutdown)
        gpNandInfo->rknand_buffer_shutdown();    
}

void rknand_dev_cache_flush(void)
{
    if(gpNandInfo->rknand_dev_cache_flush)
        gpNandInfo->rknand_dev_cache_flush();
}

#ifdef CONFIG_OF
static const struct of_device_id of_rk_nandc_match[] = {
	{ .compatible = "rockchip,rk-nandc" },
	{ /* Sentinel */ }
};
#endif

static struct platform_driver rknand_driver = {
	.probe		= rknand_probe,
	.suspend	= rknand_suspend,
	.resume		= rknand_resume,
	.shutdown   = rknand_shutdown,
	.driver		= {
	    .name	= "rknand",
#ifdef CONFIG_OF
    	.of_match_table	= of_rk_nandc_match,
#endif
		.owner	= THIS_MODULE,
	},
};

static void __exit rknand_part_exit(void)
{
	printk("rknand_part_exit: \n");
    platform_driver_unregister(&rknand_driver);
    if(gpNandInfo->rknand_exit)
        gpNandInfo->rknand_exit();    
	if (gpNandInfo)
	    kfree(gpNandInfo);
}

MODULE_ALIAS(DRIVER_NAME);
static int __init rknand_part_init(void)
{
	int ret = 0;
    char * pbuf = ioremap(0x10501400,0x400);
    memcpy(vendor0,pbuf,0x200);
    memcpy(sn_data,pbuf+0x200,0x200);
    iounmap(pbuf);
	cmdline = strstr(saved_command_line, "mtdparts=") + 9;
	gpNandInfo = kzalloc(sizeof(struct rknand_info), GFP_KERNEL);
	if (!gpNandInfo)
		return -ENOMEM;
    memset(gpNandInfo,0,sizeof(struct rknand_info));
	ret = platform_driver_register(&rknand_driver);
	printk("rknand_driver:ret = %x \n",ret);
	return ret;
}

module_init(rknand_part_init);
module_exit(rknand_part_exit);
