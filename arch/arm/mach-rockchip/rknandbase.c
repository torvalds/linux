
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/bootmem.h>
#include <asm/io.h>
#include <asm/cacheflush.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>
#include <linux/clk.h>

#define RKNAND_VERSION_AND_DATE  "rknandbase v1.0 2014-03-31"

#ifdef CONFIG_OF
#include <linux/of.h>
#endif

struct rknand_info {
    int tag;
    int enable;
    int clk_rate[2];
    int nand_suspend_state;
    int nand_shutdown_state;
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

struct rk_nandc_info 
{
    int             id;
    void __iomem    * reg_base ;
    int             irq;
    int             clk_rate;
	struct clk	    *clk;  // flash clk
	struct clk	    *hclk; // nandc clk
	struct clk	    *gclk; // flash clk gate
};

struct rknand_info * gpNandInfo = NULL;
static struct rk_nandc_info  g_nandc_info[2];

static char *cmdline=NULL;
int rknand_get_part_info(char **s)
{
	*s = cmdline;
    return 0;
}
EXPORT_SYMBOL(rknand_get_part_info); 

static char nand_idb_data[2048];
char GetSNSectorInfo(char * pbuf)
{
    memcpy(pbuf,&nand_idb_data[0x600],0x200);
    return 0;
}

char GetSNSectorInfoBeforeNandInit(char * pbuf)
{
    memcpy(pbuf,&nand_idb_data[0x600],0x200);
    return 0;
} 

char GetVendor0InfoBeforeNandInit(char * pbuf)
{
    memcpy(pbuf,&nand_idb_data[0x400+8],504);
    return 0;
}

char* rknand_get_idb_data(void)
{
    return nand_idb_data;
}
EXPORT_SYMBOL(rknand_get_idb_data);

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


int rk_nand_get_device(struct rknand_info ** prknand_Info)
{
    *prknand_Info = gpNandInfo;
    return 0;     
}
EXPORT_SYMBOL(rk_nand_get_device);

unsigned long rknand_dma_flush_dcache(unsigned long ptr,int size,int dir)
{
#ifdef CONFIG_ARM64
	__flush_dcache_area((void *)ptr, size + 63);
#else
     __cpuc_flush_dcache_area((void*)ptr, size + 63);
#endif
    return ((unsigned long )virt_to_phys((void *)ptr));
}
EXPORT_SYMBOL(rknand_dma_flush_dcache);

unsigned long rknand_dma_map_single(unsigned long ptr,int size,int dir)
{
#ifdef CONFIG_ARM64
    __dma_map_area((void*)ptr, size, dir);
    return ((unsigned long )virt_to_phys((void *)ptr));
#else
    return dma_map_single(NULL,(void*)ptr,size, dir?DMA_TO_DEVICE:DMA_FROM_DEVICE);
#endif
}
EXPORT_SYMBOL(rknand_dma_map_single);

void rknand_dma_unmap_single(unsigned long ptr,int size,int dir)
{
#ifdef CONFIG_ARM64
    __dma_unmap_area(phys_to_virt(ptr), size, dir);
#else
    dma_unmap_single(NULL, (dma_addr_t)ptr,size, dir?DMA_TO_DEVICE:DMA_FROM_DEVICE);
#endif
}
EXPORT_SYMBOL(rknand_dma_unmap_single);

int rknand_flash_cs_init(int id)
{
    return 0;
}
EXPORT_SYMBOL(rknand_flash_cs_init);

int rknand_get_reg_addr(unsigned long *pNandc0,unsigned long *pNandc1,unsigned long *pSDMMC0,unsigned long *pSDMMC1,unsigned long *pSDMMC2)
{
	*pNandc0 = (unsigned long)g_nandc_info[0].reg_base;
	*pNandc1 = (unsigned long)g_nandc_info[1].reg_base;
	return 0;
}
EXPORT_SYMBOL(rknand_get_reg_addr);

int rknand_nandc_irq_init(int id,int mode,void * pfun)
{
    int ret = 0;
    int irq= g_nandc_info[id].irq;

    if(mode)
    {
        ret = request_irq(irq, pfun, 0, "nandc", g_nandc_info[id].reg_base);
        //if(ret)
        //printk("request IRQ_NANDC %x irq %x, ret=%x.........\n",id,irq, ret);
    }
    else //deinit
    {
        free_irq(irq,  NULL);
    }
    return ret;
}
EXPORT_SYMBOL(rknand_nandc_irq_init);

/*1:flash 2:emmc 4:sdcard0 8:sdcard1*/
static int rknand_boot_media = 2;
int rknand_get_boot_media(void)
{
	return rknand_boot_media;
}
EXPORT_SYMBOL(rknand_get_boot_media);

static int rknand_probe(struct platform_device *pdev)
{
	unsigned int id = 0;
	int irq ;
	struct resource		*mem;
	void __iomem    *membase;

    if(gpNandInfo == NULL)
    {
        gpNandInfo = kzalloc(sizeof(struct rknand_info), GFP_KERNEL);
        if (!gpNandInfo)
            return -ENOMEM;
        gpNandInfo->nand_suspend_state = 0;
        gpNandInfo->nand_shutdown_state = 0;
	}
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	membase = devm_request_and_ioremap(&pdev->dev, mem);
	if (membase == 0) 
	{
		dev_err(&pdev->dev, "no reg resource?\n");
		return -1;
	}
	//printk("rknand_probe %d %x %x\n", pdev->id,(int)mem,(int)membase);
#ifdef CONFIG_OF
	if(0==of_property_read_u32(pdev->dev.of_node, "nandc_id", &id))
	{
	    ;
	}
    pdev->id = id;
#endif
    if(id == 0)
	{
        memcpy(nand_idb_data,membase+0x1000,0x800);
		if (*(int *)(&nand_idb_data[0]) == 0x44535953) {
			rknand_boot_media = *(int *)(&nand_idb_data[8]);
			if (rknand_boot_media == 2) /*boot from emmc*/
				return -1;
		}
	}
	else if(id >= 2)
	{
		dev_err(&pdev->dev, "nandc id = %d error!\n",id);
	}

    irq = platform_get_irq(pdev, 0);
	//printk("nand irq: %d\n",irq);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq;
	}
    g_nandc_info[id].id = id;
    g_nandc_info[id].irq = irq;
    g_nandc_info[id].reg_base = membase;

    g_nandc_info[id].hclk = devm_clk_get(&pdev->dev, "hclk_nandc");
    g_nandc_info[id].clk = devm_clk_get(&pdev->dev, "clk_nandc");
    g_nandc_info[id].gclk = devm_clk_get(&pdev->dev, "g_clk_nandc");

	if (unlikely(IS_ERR(g_nandc_info[id].clk)) || unlikely(IS_ERR(g_nandc_info[id].hclk))
	|| unlikely(IS_ERR(g_nandc_info[id].gclk))) {
        printk("rknand_probe get clk error\n");
        return -1;
	}

    clk_set_rate(g_nandc_info[id].clk,150*1000*1000);
	g_nandc_info[id].clk_rate = clk_get_rate(g_nandc_info[id].clk );
    printk("rknand_probe clk rate = %d\n",g_nandc_info[id].clk_rate);
    gpNandInfo->clk_rate[id] = g_nandc_info[id].clk_rate;
    
	clk_prepare_enable( g_nandc_info[id].clk );
	clk_prepare_enable( g_nandc_info[id].hclk);
	clk_prepare_enable( g_nandc_info[id].gclk);
	return 0;
}

static int rknand_suspend(struct platform_device *pdev, pm_message_t state)
{
    if(gpNandInfo->rknand_suspend  && gpNandInfo->nand_suspend_state == 0){
       gpNandInfo->nand_suspend_state = 1;
        gpNandInfo->rknand_suspend();
        //TODO:nandc clk disable
	}
	return 0;
}

static int rknand_resume(struct platform_device *pdev)
{
    if(gpNandInfo->rknand_resume && gpNandInfo->nand_suspend_state == 1){
       gpNandInfo->nand_suspend_state = 0;
       //TODO:nandc clk enable
       gpNandInfo->rknand_resume();  
	}
	return 0;
}

static void rknand_shutdown(struct platform_device *pdev)
{
    if(gpNandInfo->rknand_buffer_shutdown && gpNandInfo->nand_shutdown_state == 0){
        gpNandInfo->nand_shutdown_state = 1;
        gpNandInfo->rknand_buffer_shutdown();
    }
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
	printk("%s\n", RKNAND_VERSION_AND_DATE);

	cmdline = strstr(saved_command_line, "mtdparts=") + 9;

	gpNandInfo = NULL;
    memset(g_nandc_info,0,sizeof(g_nandc_info));

	ret = platform_driver_register(&rknand_driver);
	printk("rknand_driver:ret = %x \n",ret);
	return ret;
}

module_init(rknand_part_init);
module_exit(rknand_part_exit);
