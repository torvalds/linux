/*
 *  linux/drivers/mtd/rknand/rknand_base.c
 *
 *  Copyright (C) 2005-2009 Fuzhou Rockchip Electronics
 *  ZYF <zyf@rock-chips.com>
 *
 *   
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/reboot.h>
#include <asm/io.h>
#include <asm/mach/flash.h>
#include "api_flash.h"
#include <linux/clk.h>
#include <linux/cpufreq.h>

extern int rknand_queue_read(int Index, int nSec, void *buf);
extern int rknand_queue_write(int Index, int nSec, void *buf,int mode);
extern int rknand_buffer_init(void);
extern void rknand_buffer_shutdown(void);
extern void rknand_buffer_sync(void);

#define DRIVER_NAME	"rk29xxnand"
const char rknand_base_version[] = "rknand_base.c version: 4.23 20110516";
#define NAND_DEBUG_LEVEL0 0
#define NAND_DEBUG_LEVEL1 1
#define NAND_DEBUG_LEVEL2 2
#define NAND_DEBUG_LEVEL3 3
//#define PAGE_REMAP

#ifndef CONFIG_RKFTL_PAGECACHE_SIZE
#define CONFIG_RKFTL_PAGECACHE_SIZE  64 //定义page映射区大小，单位为MB,mount 在/data/data下。
#endif

unsigned long SysImageWriteEndAdd = 0;
int g_num_partitions = 0;

#ifdef CONFIG_MTD_NAND_RK29XX_DEBUG
static int s_debug = CONFIG_MTD_NAND_RK29XX_DEBUG_VERBOSE;
#undef NAND_DEBUG
#define NAND_DEBUG(n, format, arg...) \
	if (n <= s_debug) {	 \
		printk(format,##arg); \
	}
#else
#undef NAND_DEBUG
#define NAND_DEBUG(n, arg...)
static const int s_debug = 0;
#endif

/*
* RK28 LBA PARTITIONS,the size and offset value below is default value in this program,
* when RK28 LBA FLASH init,the value will be modify to the value in the nand flash.
*/
static struct mtd_partition rk28_partition_info[] = {
	{ 
	  name: "misc",
	  offset:  0x2000*0x200,
	  size:    0x2000*0x200,//100MB
	},

	{ 
	  name: "kernel",
	  offset:  0x4000*0x200,
	  size:   0x4000*0x200,//200MB
	},

	{ 
	  name: "boot",
	  offset:  0x8000*0x200,
	  size:   0x2000*0x200,//200MB
	},

	{ 
	  name: "system",
	  offset:  0xE000*0x200,
	  size:   0x38000*0x200,//200MB
	},
     
};

#ifdef PAGE_REMAP
static struct mtd_partition rk28_page_part_info[] = {
	{ 
	  name: "pagecache",
	  offset:  0,
	  size:    CONFIG_RKFTL_PAGECACHE_SIZE * 0x800*0x200,//32MB
	},

	{ 
	  name: "swap",
	  offset:  (CONFIG_RKFTL_PAGECACHE_SIZE) * 0x800*0x200,
	  size:    64 * 0x800*0x200,//64MB
	},
};
#endif
/*
 * onenand_state_t - chip states
 * Enumeration for OneNAND flash chip state
 */
typedef enum {
	FL_READY,
	FL_READING,
	FL_WRITING,
	FL_ERASING,
	FL_SYNCING,
	FL_UNVALID,
} rknand_state_t;

struct rknand_chip {
	wait_queue_head_t	wq;
	rknand_state_t		state;
	int rknand_schedule_enable;//1 enable ,0 disable
    void (*pFlashCallBack)(void);//call back funtion
};

struct rknand_info {
	struct mtd_info		mtd;
    struct mtd_info		page_mtd;    
	struct mtd_partition *parts;
	struct rknand_chip	rknand;
    struct task_struct *thread;
	struct clk			*clk;
	unsigned long		 clk_rate;
#ifdef CONFIG_CPU_FREQ
    struct notifier_block   freq_transition;
#endif
};

struct rknand_info * gpNandInfo;

#include <linux/proc_fs.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 26))
#define NANDPROC_ROOT  (&proc_root)
#else
#define NANDPROC_ROOT  NULL
#endif

static struct proc_dir_entry *my_proc_entry;
extern int rkNand_proc_ftlread(char *page);
extern int rkNand_proc_bufread(char *page);
static int rkNand_proc_read(char *page,
			   char **start,
			   off_t offset, int count, int *eof, void *data)
{
	char *buf = page;
	int step = offset;
	*(int *)start = 1;
	if(step == 0)
	{
        buf += sprintf(buf, "%s\n", rknand_base_version);
        buf += rkNand_proc_ftlread(buf);
#ifdef  CONFIG_MTD_RKNAND_BUFFER
        buf += rkNand_proc_bufread(buf);
#endif        
    }
	return buf - page < count ? buf - page : count;
}

static void rk28nand_create_procfs(void)
{
    /* Install the proc_fs entry */
    my_proc_entry = create_proc_entry("rk29xxnand",
                           S_IRUGO | S_IFREG,
                           NANDPROC_ROOT);

    if (my_proc_entry) {
        my_proc_entry->write_proc = NULL;
        my_proc_entry->read_proc = rkNand_proc_read;
        my_proc_entry->data = NULL;
    } 
}

int rknand_schedule_enable(int en)
{
    int en_bak = gpNandInfo->rknand.rknand_schedule_enable;
    gpNandInfo->rknand.rknand_schedule_enable = en;
    return en_bak;
}

void rkNand_cond_resched(void)
{
    if(gpNandInfo->rknand.rknand_schedule_enable == 1)
	{
        //msleep(1);
        //mdelay(1);
        cond_resched();
    }
}

void printk_write_log(long lba,int len, const u_char *pbuf)
{
    char debug_buf[100];
    int i;
    for(i=0;i<len;i++)
    {
        sprintf(debug_buf,"%lx :",lba+i);
        print_hex_dump(KERN_WARNING, debug_buf, DUMP_PREFIX_NONE, 16,4, &pbuf[512*i], 8, 0);
    }
}

#ifdef  CONFIG_MTD_RKNAND_BUFFER
static int rk28xxnand_read(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, u_char *buf)
{
	int ret = 0;
	int sector = len>>9;
	int LBA = (int)(from>>9);
    //printk("rk28xxnand_read: from=%x,len=%x,\n",(int)from,len);
    if(sector)
    {
		ret = rknand_queue_read(LBA, sector, buf);
    }
	*retlen = len;
	return 0;//ret;
}

static int rk28xxnand_write(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, const u_char *buf)
{
	int ret = 0;
	int sector = len>>9;
	int LBA = (int)(from>>9);
	//printk("*");
    //printk(KERN_NOTICE "write: from=%lx,len=%x\n",(int)LBA,sector);
    //printk_write_log(LBA,sector,buf);
	if(sector)// cmy
	{
		if(LBA < SysImageWriteEndAdd)//0x4E000)
		{
			NAND_DEBUG(NAND_DEBUG_LEVEL0,">>> FtlWriteImage: LBA=0x%08X  sector=%d\n",LBA, sector);
            ret = rknand_queue_write(LBA, sector, (void *)buf,1);
        }
		else
        {
            ret = rknand_queue_write(LBA, sector, (void *)buf,0);
        }
	}
	*retlen = len;
	return 0;//ret;
}
#else

void rknand_queue_cond_resched(void)
{
    ;
}

static int rknand_get_device(int new_state)
{
	struct rknand_chip *nand_info = &gpNandInfo->rknand;
	DECLARE_WAITQUEUE(wait, current);
	while (1) {
		if (nand_info->state == FL_READY) {
			nand_info->state = new_state;
			break;
		}
		NAND_DEBUG(NAND_DEBUG_LEVEL1,"FLASH not ready\n");
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&nand_info->wq, &wait);
		schedule();
		remove_wait_queue(&nand_info->wq, &wait);
	}
	return 0;
}

static void rknand_release_device(void)
{
	struct rknand_chip *nand_info = &gpNandInfo->rknand;
	if(nand_info->pFlashCallBack)
	    nand_info->pFlashCallBack();//call back
	nand_info->pFlashCallBack = NULL;
	nand_info->state = FL_READY;
	wake_up(&nand_info->wq);
}

static int rk28xxnand_read(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, u_char *buf)
{
	int ret = 0;
	int sector = len>>9;
	int LBA = (int)(from>>9);
    rknand_get_device(FL_READING);
    if(sector)
    {
        {
		    ret = NandRead(LBA, sector, buf);
        }
    }
    rknand_release_device();
	*retlen = len;
	return 0;//ret;
}

static int rk28xxnand_write(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, const u_char *buf)
{
	int ret = 0;
	int sector = len>>9;
	int LBA = (int)(from>>9);
    //NAND_DEBUG(NAND_DEBUG_LEVEL0,"+");
	//printk(KERN_NOTICE "write: from=%lx,len=%x\n",(int)from,len);
    rknand_get_device(FL_WRITING);
	if(sector)// cmy
	{
		if(LBA < SysImageWriteEndAdd)//0x4E000)
		{
			printk(">>> FtlWriteImage: LBA=0x%08X  sector=%d\n",LBA, sector);
			ret = NandWriteImage(LBA&0xFFFFFFE0, sector, (void *)buf);// LBA align to 32
        }
		else
        {
            ret = NandWrite(LBA, sector, (void *)buf);
        }
	}
    rknand_release_device();
	*retlen = len;
	return 0;//ret;
}
#endif

#ifdef PAGE_REMAP
static int rk28xxnand_page_write(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen, const u_char *buf)
{
	int ret = 0;
	int sector = len;
	int LBA = (int)(from);
    NAND_DEBUG(NAND_DEBUG_LEVEL1,"*");
    //printk("+");
	//printk(KERN_NOTICE "pagewrite: from=%lx,len=%x\n",(int)from,len);
    rknand_get_device(FL_WRITING);
	if(sector)// cmy
	{
        ret = FtlPageWrite(LBA, sector, (void *)buf);
	}
    rknand_release_device();
	*retlen = len;
	return 0;//ret;
}

static int rk28xxnand_page_read(struct mtd_info *mtd, loff_t from, size_t len,
	size_t *retlen,  u_char *buf)
{
	int ret = 0;
	int sector = len;
	int LBA = (int)(from);
	NAND_DEBUG(NAND_DEBUG_LEVEL2,"-");
    //printk("-");
    //if(len&511)
    //    printk("rk28xxnand_read: from=%x,len=%x,\n",(int)from,len);
    rknand_get_device(FL_READING);
    if(sector)
    {
        ret = FtlPageRead(LBA, sector,(void *) buf);
    }
    rknand_release_device();
	*retlen = len;
	return 0;//ret;
}
#endif

static int rk28xxnand_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	int ret = 0;
    if (instr->callback)
		instr->callback(instr);
    NAND_DEBUG(NAND_DEBUG_LEVEL0,"rk28xxnand_erase,add:0x%012llx,len:0x%012llx\n",instr->addr,instr->len);
	return ret;
}

static void rk28xxnand_sync(struct mtd_info *mtd)
{
	NAND_DEBUG(NAND_DEBUG_LEVEL0,"rk28xxnand_sync: \n");
#ifdef CONFIG_MTD_RKNAND_BUFFER
    rknand_buffer_sync();
#endif
}

extern void FtlWriteCacheEn(int);
static int rk28xxnand_panic_write(struct mtd_info *mtd, loff_t to, size_t len, size_t *retlen, const u_char *buf)
{
	int sector = len >> 9;
	int LBA = (int)(to >> 9);

	if (sector) {
		FtlWriteCacheEn(0);
		NandWrite(LBA, sector, (void *)buf);
		FtlWriteCacheEn(1);
	}
	*retlen = len;
	return 0;
}

extern int FtlGetIdBlockSysData(char * buf, int Sector);
int GetIdBlockSysData(char * buf, int Sector)
{
    return (FtlGetIdBlockSysData(buf,Sector)); 
}

char GetSNSectorInfo(char * pbuf)
{
    return (GetIdBlockSysData(pbuf,3));    
}

char GetChipSectorInfo(char * pbuf)
{
    return (GetIdBlockSysData(pbuf,2));  
}

/* cpufreq driver support */
static int rk29xx_nand_timing_cfg(struct rknand_info *nand_info)
{
	unsigned long clkrate = clk_get_rate(nand_info->clk);
    nand_info->clk_rate = clkrate;
	clkrate /= 1000;	/* turn clock into KHz for ease of use */
    FlashTimingCfg(clkrate);
	return 0;
}

#ifdef CONFIG_CPU_FREQ
static int rk29_nand_cpufreq_transition(struct notifier_block *nb, unsigned long val, void *data)
{
	struct rknand_info *info = gpNandInfo;
	unsigned long newclk;

	newclk = clk_get_rate(info->clk);
	if (val == CPUFREQ_POSTCHANGE && newclk != info->clk_rate) 
	{
		rk29xx_nand_timing_cfg(info);
	}
	return 0;
}

static inline int rk29_nand_cpufreq_register(struct rknand_info *info)
{
	info->freq_transition.notifier_call = rk29_nand_cpufreq_transition;
	return cpufreq_register_notifier(&info->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void rk29_nand_cpufreq_deregister(struct rknand_info *info)
{
	cpufreq_unregister_notifier(&info->freq_transition, CPUFREQ_TRANSITION_NOTIFIER);
}
#else
static inline int rk29_nand_cpufreq_register(struct rknand_info *info)
{
	return 0;
}

static inline void rk29_nand_cpufreq_deregister(struct rknand_info *info)
{
}
#endif

static int rk28xxnand_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	return 0;
}

static int rk28xxnand_block_markbad(struct mtd_info *mtd, loff_t ofs)
{
	return 0;
}

static int rk28xxnand_init(struct rknand_info *nand_info)
{
	struct mtd_info	   *mtd = &nand_info->mtd;
	struct rknand_chip *rknand = &nand_info->rknand;
#ifdef PAGE_REMAP
	struct mtd_info	   *page_mtd = &nand_info->page_mtd;
#endif    

	nand_info->clk = clk_get(NULL, "nandc");
	clk_enable(nand_info->clk);

	rknand->state = FL_READY;
	rknand->rknand_schedule_enable = 1;
	rknand->pFlashCallBack = NULL;
	init_waitqueue_head(&rknand->wq);
    NAND_DEBUG(NAND_DEBUG_LEVEL0,"FTLInit ...: \n");
    if(NandInit())
	{
		NAND_DEBUG(NAND_DEBUG_LEVEL0,"FTLInit Error: \n");
		return -ENXIO;
    }
    rk29_nand_cpufreq_register(nand_info);
    rk29xx_nand_timing_cfg(nand_info);
    
    NAND_DEBUG(NAND_DEBUG_LEVEL0,"FTLInit OK: \n");
    mtd->size = (uint64_t)NandGetCapacity()*0x200;
    //readflash modify rk28_partition_info
    
    NAND_DEBUG(NAND_DEBUG_LEVEL0,"mtd->size: 0x%012llx\n",mtd->size);
    mtd->oobsize = 0;
    mtd->oobavail = 0;
    mtd->ecclayout = 0;
    mtd->erasesize = 32*0x200; //sectorFlashGetPageSize()
    mtd->writesize = 8*0x200; //FlashGetPageSize()

	// Fill in remaining MTD driver data 
	mtd->type = MTD_NANDFLASH;//MTD_RAM;//
	mtd->flags = (MTD_WRITEABLE|MTD_NO_ERASE);//
	mtd->erase = rk28xxnand_erase;
	mtd->point = NULL;
	mtd->unpoint = NULL;
	mtd->read = rk28xxnand_read;
	mtd->write = rk28xxnand_write;
	mtd->read_oob = NULL;
	mtd->write_oob = NULL;
	mtd->panic_write = rk28xxnand_panic_write;

	mtd->sync = rk28xxnand_sync;
	mtd->lock = NULL;
	mtd->unlock = NULL;
	mtd->suspend = NULL;
	mtd->resume = NULL;
	mtd->block_isbad = rk28xxnand_block_isbad;
	mtd->block_markbad = rk28xxnand_block_markbad;
	mtd->owner = THIS_MODULE;

#ifdef PAGE_REMAP
    page_mtd->size = FtlGetPageZoneCapacity()*0x200;
    //readflash modify rk28_partition_info
    NAND_DEBUG(NAND_DEBUG_LEVEL0,"page_mtd->size: 0x%012llx\n",page_mtd->size);
    page_mtd->oobsize = 0;
    page_mtd->oobavail = 0;
    page_mtd->ecclayout = 0;
    page_mtd->erasesize = FlashGetPageSize()*0x200; //sector
    page_mtd->writesize = FlashGetPageSize()*0x200;

	// Fill in remaining MTD driver data 
	page_mtd->type = MTD_NANDFLASH;//MTD_RAM;//
	page_mtd->flags = (MTD_WRITEABLE|MTD_NO_ERASE);//
	page_mtd->erase = rk28xxnand_erase;
	page_mtd->point = NULL;
	page_mtd->unpoint = NULL;
	page_mtd->read = rk28xxnand_page_read;
	page_mtd->write = rk28xxnand_page_write;
	page_mtd->read_oob = NULL;
	page_mtd->write_oob = NULL;
	page_mtd->panic_write = NULL;

	page_mtd->sync = rk28xxnand_sync;
	page_mtd->lock = NULL;
	page_mtd->unlock = NULL;
	page_mtd->suspend = NULL;
	page_mtd->resume = NULL;
	page_mtd->block_isbad = NULL;
	page_mtd->block_markbad = NULL;
	page_mtd->owner = THIS_MODULE;
#endif

    return 0;
}


/*
 * CMY: 增加了对命令行分区信息的支持
 *		若cmdline有提供分区信息，则使用cmdline的分区信息进行分区
 *		若cmdline没有提供分区信息，则使用默认的分区信息(rk28_partition_info)进行分区
 */

#ifdef CONFIG_MTD_CMDLINE_PARTS
const char *part_probes[] = { "cmdlinepart", NULL }; 
#endif 

static int rk28xxnand_add_partitions(struct rknand_info *nand_info)
{
#ifdef CONFIG_MTD_CMDLINE_PARTS
    int num_partitions = 0; 

	// 从命令行解析分区的信息
    num_partitions = parse_mtd_partitions(&(nand_info->mtd), part_probes, &nand_info->parts, 0); 
    printk("num_partitions = %d\n",num_partitions);
    if(num_partitions > 0) { 
    	int i;
    	for (i = 0; i < num_partitions; i++) 
        {
            nand_info->parts[i].offset *= 0x200;
            nand_info->parts[i].size   *=0x200;
		    //printk(KERN_ERR"offset 0x%012llx  size :0x%012llx\n",nand_info->parts[i].offset, nand_info->parts[i].size);
    	}
        nand_info->parts[num_partitions - 1].size = nand_info->mtd.size - nand_info->parts[num_partitions - 1].offset;
        
		g_num_partitions = num_partitions;
		return add_mtd_partitions(&nand_info->mtd, nand_info->parts, num_partitions);
    } 
#endif 
	// 如果命令行没有提供分区信息，则使用默认的分区信息
	printk("parse_mtd_partitions\n");

//	rk28_partition_info[1].size = nand_info->mtd.size - ((ROOTFS_PART_SIZE + PARA_PART_SIZE + KERNEL_PART_SIZE)*0x100000);
//	rk28_partition_info[2].offset = rk28_partition_info[1].size + rk28_partition_info[1].offset;
	g_num_partitions = sizeof(rk28_partition_info)/sizeof(struct mtd_partition);
	return add_mtd_partitions(&nand_info->mtd, rk28_partition_info, sizeof(rk28_partition_info)/sizeof(struct mtd_partition));//MAX_FLASH_PARTITION);
}

static int rknand_probe(struct platform_device *pdev)
{
	struct rknand_info *nand_info;
    struct mtd_partition *parts;
    int i;
	//struct resource *res = pdev->resource;
	int err = 0;
	NAND_DEBUG(NAND_DEBUG_LEVEL0,"rk28xxnand_probe: \n");
	gpNandInfo = kzalloc(sizeof(struct rknand_info), GFP_KERNEL);
	if (!gpNandInfo)
		return -ENOMEM;
    
    nand_info = gpNandInfo;
    
	nand_info->mtd.name = dev_name(&pdev->dev);//pdev->dev.bus_id
	nand_info->mtd.priv = &nand_info->rknand;
	nand_info->mtd.owner = THIS_MODULE;
    
	nand_info->page_mtd.name = dev_name(&pdev->dev);//pdev->dev.bus_id
	nand_info->page_mtd.priv = &nand_info->rknand;
	nand_info->page_mtd.owner = THIS_MODULE;

	if(rk28xxnand_init(nand_info))
	{
		err = -ENXIO;
		goto  exit_free;
	}
#ifdef  CONFIG_MTD_RKNAND_BUFFER
	if(rknand_buffer_init())
	{
		err = -ENXIO;
		goto  exit_free;
	}
#endif	
	rk28nand_create_procfs();
	/*{
	    char pbuf[512];
	    GetSNSectorInfo(pbuf);
        printk("SN: %x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x",pbuf[2],pbuf[3],pbuf[4],pbuf[5],pbuf[6],pbuf[7],pbuf[8],pbuf[9],
            pbuf[10],pbuf[11],pbuf[12],pbuf[13],pbuf[14],pbuf[15],pbuf[16],pbuf[17]);
    } */  
	
	rk28xxnand_add_partitions(nand_info);

#ifdef PAGE_REMAP
    rk28_page_part_info[1].size = nand_info->page_mtd.size - rk28_page_part_info[0].size;
    add_mtd_partitions(&nand_info->page_mtd, rk28_page_part_info, 2);
#endif
 
    parts = nand_info->parts;
    for(i=0;i<g_num_partitions;i++)
    {
        //printk(">>> part[%d]: name=%s offset=0x%012llx\n", i, parts[i].name, parts[i].offset);
        if(strcmp(parts[i].name,"backup") == 0)
        {
            SysImageWriteEndAdd = (unsigned long)(parts[i].offset + parts[i].size)>>9;//sector
	        printk(">>> SysImageWriteEndAdd=0x%lx\n", SysImageWriteEndAdd);
            break;
        }
    }

    NandSetSysProtAddr(SysImageWriteEndAdd);
	dev_set_drvdata(&pdev->dev, nand_info);

	return 0;

exit_free:
	if(nand_info)
      	kfree(nand_info);

	return err;
}
#if 0
static int rknand_remove(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rknand_info *nand_info = dev_get_drvdata(&pdev->dev);
	struct resource *res = pdev->resource;
	unsigned long size = res->end - res->start + 1;
	NAND_DEBUG(NAND_DEBUG_LEVEL0,"rk28xx_rknand_remove: \n");
	dev_set_drvdata(&pdev->dev, NULL);

	if (nand_info) {
		if (nand_info->parts)
			del_mtd_partitions(&nand_info->mtd);
		else
			del_mtd_device(&nand_info->mtd);

		//rknand_release(&nand_info->mtd);
		release_mem_region(res->start, size);		
		kfree(nand_info);
	}
	return 0;
}
#endif
static int rknand_suspend(struct platform_device *pdev, pm_message_t state)
{
    gpNandInfo->rknand.rknand_schedule_enable = 0;
	NAND_DEBUG(NAND_DEBUG_LEVEL0,"rknand_suspend: \n");
	return 0;
}

static int rknand_resume(struct platform_device *pdev)
{
    gpNandInfo->rknand.rknand_schedule_enable = 1;
	NAND_DEBUG(NAND_DEBUG_LEVEL0,"rknand_resume: \n");
	return 0;
}

void rknand_shutdown(struct platform_device *pdev)
{
#ifdef CONFIG_MTD_RKNAND_BUFFER
    //NAND_DEBUG(NAND_DEBUG_LEVEL0,"rknand_shutdown...\n");
    printk("rknand_shutdown...\n");
    gpNandInfo->rknand.rknand_schedule_enable = 0;
    rknand_buffer_shutdown();    
#else
	struct rknand_chip *nand_info = &gpNandInfo->rknand;
    nand_info->rknand_schedule_enable = 0;
	NAND_DEBUG(NAND_DEBUG_LEVEL0,"rknand_shutdown...\n");
    if (nand_info->state == FL_READY)
    {
        nand_info->state = FL_UNVALID;
        NandDeInit();
        rknand_release_device();
    }
    else
    {
        nand_info->pFlashCallBack = NandDeInit;
    }
#endif
}

static struct platform_driver rknand_driver = {
	.probe		= rknand_probe,
	//.remove		= rknand_remove,
	.suspend	= rknand_suspend,
	.resume		= rknand_resume,
	.shutdown   = rknand_shutdown,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		//.bus		= &platform_bus_type,
	},
};


MODULE_ALIAS(DRIVER_NAME);

static int __init rknand_init(void)
{
	int ret;
	NAND_DEBUG(NAND_DEBUG_LEVEL0,"rknand_init: \n");
	ret = platform_driver_register(&rknand_driver);
	NAND_DEBUG(NAND_DEBUG_LEVEL0,"platform_driver_register:ret = %x \n",ret);
	return ret;
}

static void __exit rknand_exit(void)
{
	//NAND_DEBUG(NAND_DEBUG_LEVEL0,"rknand_exit: \n");
    //rknand_get_device(FL_UNVALID);
    //FtlClose();
    //rknand_release_device();
    platform_driver_unregister(&rknand_driver);
}

module_init(rknand_init);
module_exit(rknand_exit);

#if 0//def CONFIG_rknand
/*
注册一个sys dev ，在关机和复位时回调，把flash关键信息写到nand flash中，下次开机时可以快速开机。
*/

#include <linux/sysdev.h>
#include <linux/timer.h>

static int rknand_sys_suspend(struct sys_device *dev, pm_message_t state)
{
	struct rknand_chip *nand_info = &gpNandInfo->rknand;
    extern void FtlCacheWriteBack(void);
    NAND_DEBUG(NAND_DEBUG_LEVEL0,"...rknand_sys_suspend...\n");
    nand_info->rknand_schedule_enable = 0;
    if (nand_info->state == FL_READY)
    {
        nand_info->state = FL_WRITING;
        FtlCacheWriteBack();
        rknand_release_device();
    }
    else
    {
        nand_info->pFlashCallBack = FtlCacheWriteBack;
    }  
    NAND_DEBUG(NAND_DEBUG_LEVEL0,"...rknand_sys_suspend done...\n");
	return 0;

}

static int rknand_sys_resume(struct sys_device *dev)
{
	struct rknand_chip *nand_info = &gpNandInfo->rknand;
    nand_info->rknand_schedule_enable = 1;
    NAND_DEBUG(NAND_DEBUG_LEVEL0,"...rknand_sys_resume...\n");
	return 0;
}

static int rknand_sys_shutdown(struct sys_device *dev)
{
	struct rknand_chip *nand_info = &gpNandInfo->rknand;
    NAND_DEBUG(NAND_DEBUG_LEVEL0,"...rknand_sys_shutdown...\n");
    nand_info->rknand_schedule_enable = 0;
    
    if (nand_info->state == FL_READY)
    {
        //rknand_get_device(FL_UNVALID);
        nand_info->state = FL_UNVALID;
        NandDeInit();
        rknand_release_device();
    }
    else
    {//flash not ready,use call back
        nand_info->pFlashCallBack = NandDeInit;
    } 
	return 0;
}

static struct sysdev_class rknand_sysclass = {
	.name		= "rknand_sysdev",
	.shutdown	= rknand_sys_shutdown,
	.suspend	= rknand_sys_suspend,
	.resume		= rknand_sys_resume,
};

static struct sys_device rknand_sysdevice = {
	.id		= 0,
	.cls		= &rknand_sysclass,
};

static int __init rknand_sys_init(void)
{
	int ret;
    NAND_DEBUG(NAND_DEBUG_LEVEL0,"rknand_sys_init!!!!!!!!!!!!!!\n");
	ret = sysdev_class_register(&rknand_sysclass);
	if (ret == 0)
		ret = sysdev_register(&rknand_sysdevice);
	return ret;
}

device_initcall(rknand_sys_init);
#endif


MODULE_LICENSE("");
MODULE_AUTHOR("ZYF <zyf@rock-chips.com>");
MODULE_DESCRIPTION("FTL layer for SLC and MlC nand flash on RK28xx SDK boards");


