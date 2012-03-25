/* drivers/mmc/host/rk29_sdmmc.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * mount -t debugfs debugfs /data/debugfs;cat /data/debugfs/mmc0/status
 * echo 't' >/proc/sysrq-trigger
 * echo 19 >/sys/module/wakelock/parameters/debug_mask
 * vdc volume uevent on
 */
 
#include <linux/blkdev.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/card.h>

#include <mach/board.h>
#include <mach/io.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#include <asm/dma.h>
#include <mach/dma-pl330.h>
#include <asm/scatterlist.h>

#include "rk29_sdmmc.h"


#define RK29_SDMMC_xbw_Debug 0

#if RK29_SDMMC_xbw_Debug 
int debug_level = 5;
#define xbwprintk(n, format, arg...) \
	if (n <= debug_level) {	 \
		printk(format,##arg); \
	}
#else
#define xbwprintk(n, arg...)
#endif

#define RK29_SDMMC_ERROR_FLAGS		(SDMMC_INT_FRUN | SDMMC_INT_HLE )

#if defined(CONFIG_ARCH_RK29) 
#define RK29_SDMMC_INTMASK_USEDMA   (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_CD)
#define RK29_SDMMC_INTMASK_USEIO    (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_CD| SDMMC_INT_TXDR | SDMMC_INT_RXDR )

#elif defined(CONFIG_ARCH_RK30)
#define RK29_SDMMC_INTMASK_USEDMA   (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | SDMMC_INT_UNBUSY |RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_CD)
#define RK29_SDMMC_INTMASK_USEIO    (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | SDMMC_INT_UNBUSY |RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_CD| SDMMC_INT_TXDR | SDMMC_INT_RXDR )
#endif

#define RK29_SDMMC_SEND_START_TIMEOUT   3000  //The time interval from the time SEND_CMD to START_CMD_BIT cleared.
#define RK29_ERROR_PRINTK_INTERVAL      200   //The time interval between the two printk for the same error. 
#define RK29_SDMMC_WAIT_DTO_INTERNVAL   4500  //The time interval from the CMD_DONE_INT to DTO_INT
#define RK29_SDMMC_REMOVAL_DELAY        2000  //The time interval from the CD_INT to detect_timer react.

#define RK29_SDMMC_VERSION "Ver.3.03 The last modify date is 2012-03-23,modifyed by XBW." 

#if !defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)	
#define RK29_CTRL_SDMMC_ID   0  //mainly used by SDMMC
#define RK29_CTRL_SDIO1_ID   1  //mainly used by sdio-wifi
#define RK29_CTRL_SDIO2_ID   2  //mainly used by sdio-card
#else
#define RK29_CTRL_SDMMC_ID   5  
#define RK29_CTRL_SDIO1_ID   1  
#define RK29_CTRL_SDIO2_ID   2  
#endif

#define SDMMC_CLOCK_TEST     0
#define RK29_SDMMC_NOTIFY_REMOVE_INSERTION /* use sysfs to notify the removal or insertion of sd-card*/
//#define RK29_SDMMC_LIST_QUEUE            /* use list-queue for multi-card*/

#define RK29_SDMMC_DEFAULT_SDIO_FREQ   0 // 1--run in default frequency(50Mhz); 0---run in 25Mhz, 
#define RK29_MAX_SDIO_FREQ   25000000    //set max-sdio-frequency 25Mhz at the present time¡£

enum {
	EVENT_CMD_COMPLETE = 0,
	EVENT_DATA_COMPLETE,
	EVENT_DATA_ERROR,
	EVENT_XFER_ERROR
};

enum rk29_sdmmc_state {
	STATE_IDLE = 0,
	STATE_SENDING_CMD,
	STATE_DATA_BUSY,
	STATE_SENDING_STOP,
};

struct rk29_sdmmc_dma_info {
	enum dma_ch chn;
	char *name;
	struct rk29_dma_client client;
};

static struct rk29_sdmmc_dma_info rk29_sdmmc_dma_infos[]= {
	{
		.chn = DMACH_SDMMC,
		.client = {
			.name = "rk29-dma-sdmmc0",
		}
	},
	{
		.chn = DMACH_SDIO,
		.client = {
			.name = "rk29-dma-sdio1",
		}
	},

	{
		.chn = DMACH_EMMC,
		.client = {
			.name = "rk29-dma-sdio2",
		}
	},
};


/* Interrupt Information */
typedef struct TagSDC_INT_INFO
{
    u32     transLen;               //the length of data sent.
    u32     desLen;                 //the total length of the all data.
    u32    *pBuf;                   //the data buffer for interrupt read or write.
}SDC_INT_INFO_T;


struct rk29_sdmmc {
	spinlock_t		lock;
	void __iomem	*regs;
	struct clk 		*clk;

	struct mmc_request	*mrq;
	struct mmc_request	*new_mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;

	dma_addr_t		dma_addr;;
	unsigned int	use_dma:1;
	char			dma_name[8];
	u32			cmd_status;
	u32			data_status;
	u32			stop_cmdr;

    u32         old_div;
	u32			cmdr;   //the value setted into command-register
	u32			dodma;  //sign the DMA used for transfer.
	u32         errorstep;//record the error point.
	u32         *pbuf;
	SDC_INT_INFO_T    intInfo; 
    struct rk29_sdmmc_dma_info 	dma_info;
    
	int error_times;
	u32 old_cmd;
	
	struct tasklet_struct	tasklet;
	unsigned long		pending_events;
	unsigned long		completed_events;
	enum rk29_sdmmc_state	state;

#ifdef RK29_SDMMC_LIST_QUEUE
	struct list_head	queue;
	struct list_head	queue_node;
#endif

	u32			bus_hz;
	struct platform_device	*pdev;
	struct mmc_host		*mmc;
	u32			ctype;
	unsigned int		clock;
	unsigned long		flags;
	
#define RK29_SDMMC_CARD_PRESENT	0

	int			id;

	struct timer_list	detect_timer; 
	struct timer_list	request_timer; //the timer for INT_CMD_DONE
	struct timer_list	DTO_timer;     //the timer for INT_DTO
	struct mmc_command	stopcmd;

	/* flag for current bus settings */
    u32 bus_mode;

    unsigned int            oldstatus;
    unsigned int            complete_done;
    unsigned int            retryfunc;
    
#ifdef CONFIG_PM
    int gpio_irq;
	int gpio_det;
#endif

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT) || defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
    int write_protect;
#endif

    void (*set_iomux)(int device_id, unsigned int bus_width);

};


#ifdef RK29_SDMMC_NOTIFY_REMOVE_INSERTION
static struct rk29_sdmmc    *globalSDhost[3];
#endif

#define rk29_sdmmc_test_and_clear_pending(host, event)		\
	test_and_clear_bit(event, &host->pending_events)
#define rk29_sdmmc_test_pending(host, event)		\
	test_bit(event, &host->pending_events)
#define rk29_sdmmc_set_completed(host, event)			\
	set_bit(event, &host->completed_events)

#define rk29_sdmmc_set_pending(host, event)				\
	set_bit(event, &host->pending_events)

static void rk29_sdmmc_start_error(struct rk29_sdmmc *host);
static int rk29_sdmmc_clear_fifo(struct rk29_sdmmc *host);
int rk29_sdmmc_hw_init(void *data);

static void rk29_sdmmc_write(unsigned char  __iomem	*regbase, unsigned int regOff,unsigned int val)
{
	__raw_writel(val,regbase + regOff);
}

static unsigned int rk29_sdmmc_read(unsigned char  __iomem	*regbase, unsigned int regOff)
{
	return __raw_readl(regbase + regOff);
}

static int rk29_sdmmc_regs_printk(struct rk29_sdmmc *host)
{
	printk("SDMMC_CTRL:   \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CTRL));
	printk("SDMMC_PWREN:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_PWREN));
	printk("SDMMC_CLKDIV: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKDIV));
	printk("SDMMC_CLKSRC: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKSRC));
	printk("SDMMC_CLKENA: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKENA));
	printk("SDMMC_TMOUT:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TMOUT));
	printk("SDMMC_CTYPE:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CTYPE));
	printk("SDMMC_BLKSIZ: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_BLKSIZ));
	printk("SDMMC_BYTCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_BYTCNT));
	printk("SDMMC_INTMASK:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_INTMASK));
	printk("SDMMC_CMDARG: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CMDARG));
	printk("SDMMC_CMD:    \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CMD));
	printk("SDMMC_RESP0:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP0));
	printk("SDMMC_RESP1:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP1));
	printk("SDMMC_RESP2:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP2));
	printk("SDMMC_RESP3:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP3));
	printk("SDMMC_MINTSTS:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_MINTSTS));
	printk("SDMMC_RINTSTS:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RINTSTS));
	printk("SDMMC_STATUS: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_STATUS));
	printk("SDMMC_FIFOTH: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_FIFOTH));
	printk("SDMMC_CDETECT:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CDETECT));
	printk("SDMMC_WRTPRT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_WRTPRT));
	printk("SDMMC_TCBCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TCBCNT));
	printk("SDMMC_TBBCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TBBCNT));
	printk("SDMMC_DEBNCE: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_DEBNCE));
	printk("=======printk %s-register end =========\n", host->dma_name);
	return 0;
}


#ifdef RK29_SDMMC_NOTIFY_REMOVE_INSERTION
ssize_t rk29_sdmmc_progress_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
    struct rk29_sdmmc	*host = NULL;
    static u32 unmounting_times = 0;
    static char oldbuf[64];
    
    if( !strncmp(buf,"version" , strlen("version")))
    {
        printk("\n The driver SDMMC named 'rk29_sdmmc.c' is %s. ==xbw==\n", RK29_SDMMC_VERSION);
        return count;
    }
    
    //envalue the address of host base on input-parameter.
    if( !strncmp(buf,"sd-" , strlen("sd-")) )
    {
        host = (struct rk29_sdmmc	*)globalSDhost[0];
        if(!host)
        {
            printk("%s..%d.. fail to call progress_store because the host is null. ==xbw==\n",__FUNCTION__,__LINE__);
            return count;
        }
    }    
    else if(  !strncmp(buf,"sdio1-" , strlen("sdio1-")) )
    {
        host = (struct rk29_sdmmc	*)globalSDhost[RK29_CTRL_SDIO1_ID];
        if(!host)
        {
            printk("%s..%d.. fail to call progress_store because the host-sdio1 is null. ==xbw==\n",__FUNCTION__,__LINE__);
            return count;
        }
    }
    else if(  !strncmp(buf,"sdio2-" , strlen("sdio2-")) )
    {
        host = (struct rk29_sdmmc	*)globalSDhost[RK29_CTRL_SDIO2_ID];
        if(!host)
        {
            printk("%s..%d.. fail to call progress_store because the host-sdio2 is null. ==xbw==\n",__FUNCTION__,__LINE__);
            return count;
        }
    }
    else
    {
        printk("%s..%d.. You want to use sysfs for SDMMC but input-parameter is wrong.====xbw====\n",__FUNCTION__,__LINE__);
        return count;
    }

    spin_lock(&host->lock);

    if(strncmp(buf,oldbuf , strlen(buf)))
    {
	    printk(".%d.. MMC0 receive the message %s from VOLD.====xbw[%s]====\n", __LINE__, buf, host->dma_name);
	    strcpy(oldbuf, buf);
	}

	/*
     *  //deal with the message
     *  insert card state-change:  No-Media ==> Pending ==> Idle-Unmounted ==> Checking ==>Mounted
     *  remove card state-change:  Unmounting ==> Idle-Unmounted ==> No-Media
    */
    #if !defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
    if(RK29_CTRL_SDMMC_ID == host->pdev->id)
    {
#if 1 //to wirte log in log-file-system during the stage of umount. Modifyed by xbw at 2011-12-26
        if(!strncmp(buf, "sd-Unmounting", strlen("sd-Unmounting")))
        {
            if(unmounting_times++%10 == 0)
            {
                printk(".%d.. MMC0 receive the message Unmounting(waitTimes=%d) from VOLD.====xbw[%s]====\n", \
                    __LINE__, unmounting_times, host->dma_name);
            }

            if(0 == host->mmc->re_initialized_flags)
                mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(RK29_SDMMC_REMOVAL_DELAY*2));
        }
        else if(!strncmp(buf, "sd-Idle-Unmounted", strlen("sd-Idle-Unmounted")))
        {
            if(0 == host->mmc->re_initialized_flags)
                mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(RK29_SDMMC_REMOVAL_DELAY*2));
        }
#else
        if(!strncmp(buf, "sd-Unmounting", strlen("sd-Unmounting")))
        {
            if(unmounting_times++%10 == 0)
            {
                printk(".%d.. MMC0 receive the message Unmounting(waitTimes=%d) from VOLD.====xbw[%s]====\n", \
                    __LINE__, unmounting_times, host->dma_name);
            }
            host->mmc->re_initialized_flags = 0;
            mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(RK29_SDMMC_REMOVAL_DELAY*2));
        } 
#endif
        else if( !strncmp(buf, "sd-No-Media", strlen("sd-No-Media")))
        {
            printk(".%d.. MMC0 receive the message No-Media from VOLD. waitTimes=%d ====xbw[%s]====\n" ,\
                __LINE__,unmounting_times, host->dma_name);
                
            del_timer_sync(&host->detect_timer);
            host->mmc->re_initialized_flags = 1;
            unmounting_times = 0;
            
            if(test_bit(RK29_SDMMC_CARD_PRESENT, &host->flags))
            {
                mmc_detect_change(host->mmc, 0);
            }
        }
        else if( !strncmp(buf, "sd-Ready", strlen("sd-Ready")))
        {
            printk(".%d.. MMC0 receive the message Ready(ReInitFlag=%d) from VOLD. waitTimes=%d====xbw[%s]====\n" ,\
                __LINE__, host->mmc->re_initialized_flags, unmounting_times, host->dma_name);
								
            unmounting_times = 0;
			host->mmc->re_initialized_flags = 1;            
        }
        else if( !strncmp(buf,"sd-reset" , strlen("sd-reset")) ) 
        {
            printk(".%d.. Now manual reset for SDMMC0. ====xbw[%s]====\n",__LINE__, host->dma_name);
            rk29_sdmmc_hw_init(host);
            mmc_detect_change(host->mmc, 0);           
        }
        else if( !strncmp(buf, "sd-regs", strlen("sd-regs")))
        {
            printk(".%d.. Now printk the register of SDMMC0. ====xbw[%s]====\n",__LINE__, host->dma_name); 
            rk29_sdmmc_regs_printk(host);
        }

    }
    #else
    if(0 == host->pdev->id)
    {
        if( !strncmp(buf,"sd-reset" , strlen("sd-reset")) ) 
        {
            printk(".%d.. Now manual reset for SDMMC0. ====xbw[%s]====\n",__LINE__, host->dma_name);
            rk29_sdmmc_hw_init(host);
            mmc_detect_change(host->mmc, 0);           
        }
        else if( !strncmp(buf, "sd-regs", strlen("sd-regs")))
        {
            printk(".%d.. Now printk the register of SDMMC0. ====xbw[%s]====\n",__LINE__, host->dma_name); 
            rk29_sdmmc_regs_printk(host);
        }
    }
    #endif
    else if(RK29_CTRL_SDIO1_ID == host->pdev->id)
    {
        if( !strncmp(buf, "sdio1-regs", strlen("sdio1-regs")))
        {
            printk(".%d.. Now printk the register of SDMMC1. ====xbw[%s]====\n",__LINE__, host->dma_name); 
            rk29_sdmmc_regs_printk(host);
        }
        else if( !strncmp(buf,"sdio1-reset" , strlen("sdio1-reset")) ) 
        {
            printk(".%d.. Now manual reset for SDMMC1. ====xbw[%s]====\n",__LINE__, host->dma_name);
            rk29_sdmmc_hw_init(host);
            mmc_detect_change(host->mmc, 0);           
        }
    }
    else if(RK29_CTRL_SDIO2_ID == host->pdev->id)
    {
        if( !strncmp(buf, "sdio2-regs", strlen("sdio2-regs")))
        {
            printk(".%d.. Now printk the register of SDMMC2. ====xbw[%s]====\n",__LINE__, host->dma_name); 
            rk29_sdmmc_regs_printk(host);
        }
        else if( !strncmp(buf,"sdio2-reset" , strlen("sdio2-reset")) ) 
        {
            printk(".%d.. Now manual reset for SDMMC2. ====xbw[%s]====\n",__LINE__, host->dma_name);
            rk29_sdmmc_hw_init(host);
            mmc_detect_change(host->mmc, 0);           
        }
    }
    
    spin_unlock(&host->lock);
    
    return count;
}



struct kobj_attribute mmc_reset_attrs = 
{
        .attr = {
                .name = "rescan",
                .mode = 0764},
        .show = NULL,
        .store = rk29_sdmmc_progress_store,
};
struct attribute *mmc_attrs[] = 
{
        &mmc_reset_attrs.attr,
        NULL
};

static struct kobj_type mmc_kset_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.default_attrs = &mmc_attrs[0],
};

static int rk29_sdmmc_progress_add_attr( struct platform_device *pdev )
{
        int result;
		 struct kobject *parentkobject; 
        struct kobject * me = kmalloc(sizeof(struct kobject) , GFP_KERNEL );
        if(!me)
        {
            return -ENOMEM;
        }
        memset(me ,0,sizeof(struct kobject));
        kobject_init( me , &mmc_kset_ktype );
        
        parentkobject = &pdev->dev.kobj ;
		result = kobject_add( me , parentkobject->parent->parent->parent,"%s", "sd-sdio" );	

        return result;
}
#endif

#if defined (CONFIG_DEBUG_FS)
static int rk29_sdmmc_regs_show(struct seq_file *s, void *v)
{
	struct rk29_sdmmc	*host = s->private;

	seq_printf(s, "SDMMC_CTRL:   \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CTRL));
	seq_printf(s, "SDMMC_PWREN:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_PWREN));
	seq_printf(s, "SDMMC_CLKDIV: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKDIV));
	seq_printf(s, "SDMMC_CLKSRC: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKSRC));
	seq_printf(s, "SDMMC_CLKENA: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKENA));
	seq_printf(s, "SDMMC_TMOUT:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TMOUT));
	seq_printf(s, "SDMMC_CTYPE:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CTYPE));
	seq_printf(s, "SDMMC_BLKSIZ: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_BLKSIZ));
	seq_printf(s, "SDMMC_BYTCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_BYTCNT));
	seq_printf(s, "SDMMC_INTMASK:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_INTMASK));
	seq_printf(s, "SDMMC_CMDARG: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CMDARG));
	seq_printf(s, "SDMMC_CMD:    \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CMD));
	seq_printf(s, "SDMMC_RESP0:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP0));
	seq_printf(s, "SDMMC_RESP1:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP1));
	seq_printf(s, "SDMMC_RESP2:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP2));
	seq_printf(s, "SDMMC_RESP3:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP3));
	seq_printf(s, "SDMMC_MINTSTS:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_MINTSTS));
	seq_printf(s, "SDMMC_RINTSTS:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RINTSTS));
	seq_printf(s, "SDMMC_STATUS: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_STATUS));
	seq_printf(s, "SDMMC_FIFOTH: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_FIFOTH));
	seq_printf(s, "SDMMC_CDETECT:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CDETECT));
	seq_printf(s, "SDMMC_WRTPRT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_WRTPRT));
	seq_printf(s, "SDMMC_TCBCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TCBCNT));
	seq_printf(s, "SDMMC_TBBCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TBBCNT));
	seq_printf(s, "SDMMC_DEBNCE: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_DEBNCE));

	return 0;
}


/*
 * The debugfs stuff below is mostly optimized away when
 * CONFIG_DEBUG_FS is not set.
 */
static int rk29_sdmmc_req_show(struct seq_file *s, void *v)
{
	struct rk29_sdmmc	*host = s->private;
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_command	*stop;
	struct mmc_data		*data;

	/* Make sure we get a consistent snapshot */
	spin_lock(&host->lock);
	mrq = host->mrq;

	if (mrq) {
		cmd = mrq->cmd;
		data = mrq->data;
		stop = mrq->stop;

		if (cmd)
			seq_printf(s,
				"CMD%u(0x%x) flg %x rsp %x %x %x %x err %d\n",
				cmd->opcode, cmd->arg, cmd->flags,
				cmd->resp[0], cmd->resp[1], cmd->resp[2],
				cmd->resp[2], cmd->error);
		if (data)
			seq_printf(s, "DATA %u / %u * %u flg %x err %d\n",
				data->bytes_xfered, data->blocks,
				data->blksz, data->flags, data->error);
		if (stop)
			seq_printf(s,
				"CMD%u(0x%x) flg %x rsp %x %x %x %x err %d\n",
				stop->opcode, stop->arg, stop->flags,
				stop->resp[0], stop->resp[1], stop->resp[2],
				stop->resp[2], stop->error);
	}

	spin_unlock(&host->lock);

	return 0;
}

static int rk29_sdmmc_req_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk29_sdmmc_req_show, inode->i_private);
}

static const struct file_operations rk29_sdmmc_req_fops = {
	.owner		= THIS_MODULE,
	.open		= rk29_sdmmc_req_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static int rk29_sdmmc_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk29_sdmmc_regs_show, inode->i_private);
}

static const struct file_operations rk29_sdmmc_regs_fops = {
	.owner		= THIS_MODULE,
	.open		= rk29_sdmmc_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void rk29_sdmmc_init_debugfs(struct rk29_sdmmc *host)
{
	struct mmc_host		*mmc = host->mmc;
	struct dentry		*root;
	struct dentry		*node;

	root = mmc->debugfs_root;
	if (!root)
		return;

	node = debugfs_create_file("regs", S_IRUSR, root, host,
			&rk29_sdmmc_regs_fops);
	if (IS_ERR(node))
		return;
	if (!node)
		goto err;

	node = debugfs_create_file("req", S_IRUSR, root, host, &rk29_sdmmc_req_fops);
	if (!node)
		goto err;

	node = debugfs_create_u32("state", S_IRUSR, root, (u32 *)&host->state);
	if (!node)
		goto err;

	node = debugfs_create_x32("pending_events", S_IRUSR, root,
				     (u32 *)&host->pending_events);
	if (!node)
		goto err;

	node = debugfs_create_x32("completed_events", S_IRUSR, root,
				     (u32 *)&host->completed_events);
	if (!node)
		goto err;

	return;

err:
	dev_err(&mmc->class_dev, "failed to initialize debugfs for host\n");
}
#endif


static u32 rk29_sdmmc_prepare_command(struct mmc_command *cmd)
{
	u32		cmdr = cmd->opcode;

	switch(cmdr)
	{
	    case MMC_GO_IDLE_STATE: 
            cmdr |= (SDMMC_CMD_INIT | SDMMC_CMD_PRV_DAT_NO_WAIT);
            break;

        case MMC_STOP_TRANSMISSION:
            cmdr |= (SDMMC_CMD_STOP | SDMMC_CMD_PRV_DAT_NO_WAIT);
            break;
        case MMC_SEND_STATUS:
        case MMC_GO_INACTIVE_STATE:   
            cmdr |= SDMMC_CMD_PRV_DAT_NO_WAIT;
            break;

        default:
            cmdr |= SDMMC_CMD_PRV_DAT_WAIT;
            break;
	}

    /* response type */
	switch(mmc_resp_type(cmd))
	{
	    case MMC_RSP_R1:
        case MMC_RSP_R1B:
            // case MMC_RSP_R5:  //R5,R6,R7 is same with the R1
            //case MMC_RSP_R6:
            // case R6m_TYPE:
            // case MMC_RSP_R7:
            cmdr |= (SDMMC_CMD_RESP_CRC | SDMMC_CMD_RESP_SHORT | SDMMC_CMD_RESP_EXP);
            break;
        case MMC_RSP_R3:
            //case MMC_RSP_R4:
            /* these response not contain crc7, so don't care crc error and response error */
            cmdr |= (SDMMC_CMD_RESP_NO_CRC | SDMMC_CMD_RESP_SHORT | SDMMC_CMD_RESP_EXP); 
            break;
        case MMC_RSP_R2:
            cmdr |= (SDMMC_CMD_RESP_CRC | SDMMC_CMD_RESP_LONG | SDMMC_CMD_RESP_EXP);
            break;
        case MMC_RSP_NONE:
            cmdr |= (SDMMC_CMD_RESP_CRC_NOCARE | SDMMC_CMD_RESP_NOCARE | SDMMC_CMD_RESP_NO_EXP);  
            break;
        default:
            cmdr |= (SDMMC_CMD_RESP_CRC_NOCARE | SDMMC_CMD_RESP_NOCARE | SDMMC_CMD_RESP_NO_EXP); 
            break;
	}

	return cmdr;
}

void  rk29_sdmmc_set_frq(struct rk29_sdmmc *host)
{
    struct mmc_host *mmchost = platform_get_drvdata(host->pdev);
    struct mmc_card	*card;
    struct mmc_ios *ios;
	unsigned int max_dtr;
    
    extern void mmc_set_clock(struct mmc_host *host, unsigned int hz);

    if(!mmchost)
        return;

    card = (struct mmc_card	*)mmchost->card;
    ios  = ( struct mmc_ios *)&mmchost->ios;

    if(!card || !ios)
        return;

    if(MMC_POWER_ON == ios->power_mode)
        return;

    max_dtr = (unsigned int)-1;
    
    if (mmc_card_highspeed(card)) 
    {
        if (max_dtr > card->ext_csd.hs_max_dtr)
            max_dtr = card->ext_csd.hs_max_dtr;
            
    }
    else if (max_dtr > card->csd.max_dtr) 
    {
        if(MMC_TYPE_SD == card->type)
        {
	        max_dtr = (card->csd.max_dtr > SD_FPP_FREQ) ? SD_FPP_FREQ : (card->csd.max_dtr);
	    }
	    else
	    {	
            max_dtr = (card->csd.max_dtr > MMC_FPP_FREQ) ? MMC_FPP_FREQ : (card->csd.max_dtr);
	    }	    
    }

    xbwprintk(7, "%s..%d...  call mmc_set_clock() set clk=%d ===xbw[%s]===\n", \
			__FUNCTION__, __LINE__, max_dtr, host->dma_name);

  
    mmc_set_clock(mmchost, max_dtr);

}


static int rk29_sdmmc_start_command(struct rk29_sdmmc *host, struct mmc_command *cmd, u32 cmd_flags)
{
 	int tmo = RK29_SDMMC_SEND_START_TIMEOUT*10;//wait 60ms cycle.
	
 	host->cmd = cmd;
 	host->old_cmd = cmd->opcode;
 	host->errorstep = 0;
 	host->pending_events = 0;
	host->completed_events = 0;	
 	host->complete_done = 0;
    host->retryfunc = 0;
    host->cmd_status = 0;

    if(MMC_STOP_TRANSMISSION != cmd->opcode)
    {
        host->data_status = 0;
    }
    
    if(RK29_CTRL_SDMMC_ID == host->pdev->id)
    {
        //adjust the frequency division control of SDMMC0 every time.
        rk29_sdmmc_set_frq(host);
    }
			
	rk29_sdmmc_write(host->regs, SDMMC_CMDARG, cmd->arg); // write to SDMMC_CMDARG register
	rk29_sdmmc_write(host->regs, SDMMC_CMD, cmd_flags | SDMMC_CMD_START); // write to SDMMC_CMD register


    xbwprintk(5, "\n%s..%d..************.start cmd=%d, arg=0x%x ********=====xbw[%s]=======\n", \
			__FUNCTION__, __LINE__, cmd->opcode, cmd->arg, host->dma_name);

	host->mmc->doneflag = 1;	

	/* wait until CIU accepts the command */
	while (--tmo && (rk29_sdmmc_read(host->regs, SDMMC_CMD) & SDMMC_CMD_START))
	{
		udelay(2);//cpu_relax();
	}
	
	if(!tmo)
	{
	    if(0==cmd->retries)
	    {
    		printk("%s..%d..  CMD_START timeout! CMD%d(arg=0x%x, retries=%d) ======xbw[%s]======\n",\
    				__FUNCTION__,__LINE__, cmd->opcode, cmd->arg, cmd->retries,host->dma_name);
		}

		cmd->error = -ETIMEDOUT;
		host->mrq->cmd->error = -ETIMEDOUT;
		del_timer_sync(&host->request_timer);
		
		host->errorstep = 0x1;
		return SDM_WAIT_FOR_CMDSTART_TIMEOUT;
	}
    host->errorstep = 0xfe;
 
	return SDM_SUCCESS;
}

static int rk29_sdmmc_reset_fifo(struct rk29_sdmmc *host)
{
    u32     value;
	int     timeout;
	int     ret = SDM_SUCCESS;
	
    value = rk29_sdmmc_read(host->regs, SDMMC_STATUS);
    if (!(value & SDMMC_STAUTS_FIFO_EMPTY))
    {
        value = rk29_sdmmc_read(host->regs, SDMMC_CTRL);
        value |= SDMMC_CTRL_FIFO_RESET;
        rk29_sdmmc_write(host->regs, SDMMC_CTRL, value);

        timeout = 1000;
        while (((value = rk29_sdmmc_read(host->regs, SDMMC_CTRL)) & (SDMMC_CTRL_FIFO_RESET)) && (timeout > 0))
        {
            udelay(1);
            timeout--;
        }
        if (timeout == 0)
        {
            host->errorstep = 0x2;
            ret = SDM_WAIT_FOR_FIFORESET_TIMEOUT;
        }
    }
    
	return ret;
	
}

static int rk29_sdmmc_wait_unbusy(struct rk29_sdmmc *host)
{
	int time_out = 500000;//250000; //max is 250ms; //adapt the value to the sick card.  modify at 2011-10-08

	while (rk29_sdmmc_read(host->regs, SDMMC_STATUS) & (SDMMC_STAUTS_DATA_BUSY|SDMMC_STAUTS_MC_BUSY)) 
	{
		udelay(1);
		time_out--;

		if(time_out == 0)
		{
		    host->errorstep = 0x3;
		    return SDM_BUSY_TIMEOUT;
		}
	}

	return SDM_SUCCESS;
}

static void rk29_sdmmc_dma_cleanup(struct rk29_sdmmc *host)
{
	if (host->data) 
	{
		dma_unmap_sg(&host->pdev->dev, host->data->sg, host->data->sg_len,
		     ((host->data->flags & MMC_DATA_WRITE)
		      ? DMA_TO_DEVICE : DMA_FROM_DEVICE));		
    }
}

static void rk29_sdmmc_stop_dma(struct rk29_sdmmc *host)
{
    int ret = 0;
    
	if(host->use_dma == 0)
		return;
		
	if (host->dma_info.chn> 0) 
	{
		rk29_sdmmc_dma_cleanup(host); 
		
		ret = rk29_dma_ctrl(host->dma_info.chn,RK29_DMAOP_STOP);
		if(ret < 0)
		{
            printk("%s..%d...rk29_dma_ctrl STOP error!===xbw[%s]====\n", __FUNCTION__, __LINE__, host->dma_name);
            host->errorstep = 0x95;
            return;
		}

		ret = rk29_dma_ctrl(host->dma_info.chn,RK29_DMAOP_FLUSH);
		if(ret < 0)
		{
            printk("%s..%d...rk29_dma_ctrl FLUSH error!===xbw[%s]====\n", __FUNCTION__, __LINE__, host->dma_name);
            host->errorstep = 0x96;
            return;
		}
		
	} 
	else 
	{
		/* Data transfer was stopped by the interrupt handler */
		rk29_sdmmc_set_pending(host, EVENT_DATA_COMPLETE);
	}
}

static void rk29_sdmmc_control_host_dma(struct rk29_sdmmc *host, bool enable)
{
    u32 value = rk29_sdmmc_read(host->regs, SDMMC_CTRL);

    if (enable)
    {
        value |= SDMMC_CTRL_DMA_ENABLE;
    }
    else
    {
        value &= ~(SDMMC_CTRL_DMA_ENABLE);
    }

    rk29_sdmmc_write(host->regs, SDMMC_CTRL, value);
}

static void send_stop_cmd(struct rk29_sdmmc *host)
{
    int ret;

    if(host->mrq->cmd->error)
    {
        //stop DMA
        if(host->dodma)
        {
            rk29_sdmmc_stop_dma(host);
            rk29_sdmmc_control_host_dma(host, FALSE);

            host->dodma = 0;
        }
        
        ret= rk29_sdmmc_clear_fifo(host);
        if(SDM_SUCCESS != ret)
        {
            xbwprintk(3, "%s..%d..  clear fifo error before call CMD_STOP ====xbw[%s]====\n", \
							__FUNCTION__, __LINE__, host->dma_name);
        }
    }
    
    mod_timer(&host->request_timer, jiffies + msecs_to_jiffies(RK29_SDMMC_SEND_START_TIMEOUT+1500));
		
    host->stopcmd.opcode = MMC_STOP_TRANSMISSION;
    host->stopcmd.flags  = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;;
    host->stopcmd.arg = 0;
    host->stopcmd.data = NULL;
    host->stopcmd.mrq = NULL;
    host->stopcmd.retries = 0;
    host->stopcmd.error = 0;
    if(host->mrq && host->mrq->stop)
    {
        host->mrq->stop->error = 0;
    }
    
    host->cmdr = rk29_sdmmc_prepare_command(&host->stopcmd);
    
    ret = rk29_sdmmc_start_command(host, &host->stopcmd, host->cmdr); 
    if(SDM_SUCCESS != ret)
    {
        rk29_sdmmc_start_error(host);

        host->state = STATE_IDLE;
        host->complete_done = 4;
    }
}


/* This function is called by the DMA driver from tasklet context. */
static void rk29_sdmmc_dma_complete(void *arg, int size, enum rk29_dma_buffresult result) 
{
	struct rk29_sdmmc	*host = arg;

	if(host->use_dma == 0)
		return;

	host->intInfo.transLen = host->intInfo.desLen;	
}

static int rk29_sdmmc_submit_data_dma(struct rk29_sdmmc *host, struct mmc_data *data)
{
	struct scatterlist		*sg;
	unsigned int			i,direction, sgDirection;
	int ret, dma_len=0;
	
	if(host->use_dma == 0)
	{
	    printk("%s..%d...setup DMA fail!!!!!!. host->use_dma=0 ===xbw=[%s]====\n", __FUNCTION__, __LINE__, host->dma_name);
	    host->errorstep = 0x4;
		return -ENOSYS;
	}
	/* If we don't have a channel, we can't do DMA */
	if (host->dma_info.chn < 0)
	{
	    printk("%s..%d...setup DMA fail!!!!!!!. dma_info.chn < 0  ===xbw[%s]====\n", __FUNCTION__, __LINE__, host->dma_name);
	    host->errorstep = 0x5;
		return -ENODEV;
	}

	if (data->blksz & 3)
		return -EINVAL;
	for_each_sg(data->sg, sg, data->sg_len, i) 
	{
		if (sg->offset & 3 || sg->length & 3)
		{
		    printk("%s..%d...call for_each_sg() fail !!===xbw[%s]====\n", __FUNCTION__, __LINE__, host->dma_name);
		    host->errorstep = 0x7;
			return -EINVAL;
		}
	}
	if (data->flags & MMC_DATA_READ)
	{
		direction = RK29_DMASRC_HW;  
		sgDirection = DMA_FROM_DEVICE; 
	}
	else
	{
		direction = RK29_DMASRC_MEM;
		sgDirection = DMA_TO_DEVICE;
	}

	ret = rk29_dma_ctrl(host->dma_info.chn,RK29_DMAOP_STOP);
	if(ret < 0)
	{
	    printk("%s..%d...rk29_dma_ctrl stop error!===xbw[%s]====\n", __FUNCTION__, __LINE__, host->dma_name);
	    host->errorstep = 0x91;
		return -ENOSYS;
	}
	
	ret = rk29_dma_ctrl(host->dma_info.chn,RK29_DMAOP_FLUSH);	
	if(ret < 0)
	{
        printk("%s..%d...rk29_dma_ctrl flush error!===xbw[%s]====\n", __FUNCTION__, __LINE__, host->dma_name);
        host->errorstep = 0x91;
        return -ENOSYS;
	}

	
    ret = rk29_dma_devconfig(host->dma_info.chn, direction, (unsigned long )(host->dma_addr));
    if(0 != ret)
    {
        printk("%s..%d...call rk29_dma_devconfig() fail !!!!===xbw=[%s]====\n", __FUNCTION__, __LINE__, host->dma_name);
        host->errorstep = 0x8;
        return -ENOSYS;
    }
    
	dma_len = dma_map_sg(&host->pdev->dev, data->sg, data->sg_len, sgDirection);						                	   
	for (i = 0; i < dma_len; i++)
	{
    	ret = rk29_dma_enqueue(host->dma_info.chn, host, sg_dma_address(&data->sg[i]),sg_dma_len(&data->sg[i]));
    	if(ret < 0)
    	{
            printk("%s..%d...call rk29_dma_devconfig() fail !!!!===xbw=[%s]====\n", __FUNCTION__, __LINE__, host->dma_name);
            host->errorstep = 0x93;
            return -ENOSYS;
    	}
    }
    	
	rk29_sdmmc_control_host_dma(host, TRUE);// enable dma
	ret = rk29_dma_ctrl(host->dma_info.chn, RK29_DMAOP_START);
	if(ret < 0)
	{
        printk("%s..%d...rk29_dma_ctrl start error!===xbw[%s]====\n", __FUNCTION__, __LINE__, host->dma_name);
        host->errorstep = 0x94;
        return -ENOSYS;
	}
	
	return 0;
}


static int rk29_sdmmc_prepare_write_data(struct rk29_sdmmc *host, struct mmc_data *data)
{
    //uint32 value;
    int     output;
    u32    i = 0;
    u32     dataLen;
    u32     count, *pBuf = (u32 *)host->pbuf;

    output = SDM_SUCCESS;
    dataLen = data->blocks*data->blksz;
    
    host->dodma = 0; //DMA still no request;
 
    //SDMMC controller request the data is multiple of 4.
    count = (dataLen >> 2) + ((dataLen & 0x3) ? 1:0);

    if (count <= FIFO_DEPTH)
    {
        for (i=0; i<count; i++)
        {
            rk29_sdmmc_write(host->regs, SDMMC_DATA, pBuf[i]);
        }
    }
    else
    {
        host->intInfo.desLen = count;
        host->intInfo.transLen = 0;
        host->intInfo.pBuf = (u32 *)pBuf;
        
        if(0)//(host->intInfo.desLen <= 512 ) 
        {  
            //use pio-mode          
            return SDM_SUCCESS;
        } 
        else 
        {
            xbwprintk(7, "%s..%d...   trace data,   ======xbw=[%s]====\n", __FUNCTION__, __LINE__,  host->dma_name);
            output = rk29_sdmmc_submit_data_dma(host, data);
            if(output)
            {
        		host->dodma = 0;
        			
        	    printk("%s..%d... CMD%d setupDMA failure!!!!! pre_cmd=%d  ==xbw[%s]==\n", \
						__FUNCTION__, __LINE__, host->cmd->opcode,host->old_cmd, host->dma_name);
        	    
				host->errorstep = 0x81;

        		rk29_sdmmc_control_host_dma(host, FALSE); 
    		}
    		else
    		{
    		    host->dodma = 1;
    		}
        }
       
    }

    return output;
}




static int rk29_sdmmc_prepare_read_data(struct rk29_sdmmc *host, struct mmc_data *data)
{
    u32  count = 0;
    u32  dataLen;
    int   output;

    output = SDM_SUCCESS;
    dataLen = data->blocks*data->blksz;
    
    host->dodma = 0;//DMA still no request;

    //SDMMC controller request the data is multiple of 4.
    count = (dataLen >> 2) ;//+ ((dataLen & 0x3) ? 1:0);

    host->intInfo.desLen = (dataLen >> 2);
    host->intInfo.transLen = 0;
    host->intInfo.pBuf = (u32 *)host->pbuf;
       
    if (count > (RX_WMARK+1))  //datasheet error.actually, it can nont waken the interrupt when less and equal than RX_WMARK+1
    {
        if(0) //(host->intInfo.desLen <= 512 )
        {
            //use pio-mode
            return SDM_SUCCESS;
        }        
        else 
        {
            output = rk29_sdmmc_submit_data_dma(host, data);
            if(output)
            {
        		host->dodma = 0;
        			
        	    printk("%s..%d... CMD%d setupDMA  failure!!!  ==xbw[%s]==\n", \
						__FUNCTION__, __LINE__, host->cmd->opcode, host->dma_name);

        	    host->errorstep = 0x82;

        		rk29_sdmmc_control_host_dma(host, FALSE); 
    		}
    		else
    		{
    		    host->dodma = 1;
    		}
        }
    }

    return output;
}



static int rk29_sdmmc_read_remain_data(struct rk29_sdmmc *host, u32 originalLen, void *pDataBuf)
{
    u32  value = 0;

    u32     i = 0;
    u32     *pBuf = (u32 *)pDataBuf;
    u8      *pByteBuf = (u8 *)pDataBuf;
    u32     lastData = 0;

    //SDMMC controller must be multiple of 32. so if transfer 13, then actuall we should write or read 16 byte.
    u32  count = (originalLen >> 2) + ((originalLen & 0x3) ? 1:0);

    if(1 == host->dodma)
    {
        //when use DMA, there are remain data only when datalen/4 less than  RX_WMARK+1 same as equaltion. or not multiple of 4
        if (!((value = rk29_sdmmc_read(host->regs, SDMMC_STATUS)) & SDMMC_STAUTS_FIFO_EMPTY))
        {
            if (count <= (RX_WMARK+1))
            {
                i = 0;
                while ((i<(originalLen >> 2))&&(!(value & SDMMC_STAUTS_FIFO_EMPTY)))
                {
                    pBuf[i++] = rk29_sdmmc_read(host->regs, SDMMC_DATA);
                    value = rk29_sdmmc_read(host->regs, SDMMC_STATUS);
                }
            }

            if (count > (originalLen >> 2))
            {
                lastData = rk29_sdmmc_read(host->regs, SDMMC_DATA);

                //fill the 1 to 3 byte.
                for (i=0; i<(originalLen & 0x3); i++)
                {
                    pByteBuf[(originalLen & 0xFFFFFFFC) + i] = (u8)((lastData >> (i << 3)) & 0xFF); //default little-endian
                }
            }
        }
    }
    else
    {
        if (!((value = rk29_sdmmc_read(host->regs, SDMMC_STATUS)) & SDMMC_STAUTS_FIFO_EMPTY))
        {
             while ( (host->intInfo.transLen < host->intInfo.desLen)  && (!(value & SDMMC_STAUTS_FIFO_EMPTY)) )
            {
                pBuf[host->intInfo.transLen++] = rk29_sdmmc_read(host->regs, SDMMC_DATA);  
                value = rk29_sdmmc_read(host->regs, SDMMC_STATUS);  
            }

            if (count > (originalLen >> 2))
            {
                lastData = rk29_sdmmc_read(host->regs, SDMMC_DATA); 

                //fill the 1 to 3 byte.
                for (i=0; i<(originalLen & 0x3); i++)
                {
                    pByteBuf[(originalLen & 0xFFFFFFFC) + i] = (u8)((lastData >> (i << 3)) & 0xFF);  //default little-endian
                }
            }
        }
    }
    
    return SDM_SUCCESS;
}


static void rk29_sdmmc_do_pio_read(struct rk29_sdmmc *host)
{
    int i;
    for (i=0; i<(RX_WMARK+1); i++)
    {
        host->intInfo.pBuf[host->intInfo.transLen + i] = rk29_sdmmc_read(host->regs, SDMMC_DATA);
    }
    host->intInfo.transLen += (RX_WMARK+1);
}

static void rk29_sdmmc_do_pio_write(struct rk29_sdmmc *host)
{
    int i;
    if ( (host->intInfo.desLen - host->intInfo.transLen) > (FIFO_DEPTH - TX_WMARK) )
    {
        for (i=0; i<(FIFO_DEPTH - TX_WMARK); i++)
        {
            rk29_sdmmc_write(host->regs, SDMMC_DATA, host->intInfo.pBuf[host->intInfo.transLen + i]);
        }
        host->intInfo.transLen += (FIFO_DEPTH - TX_WMARK);
    }
    else
    {
        for (i=0; i<(host->intInfo.desLen - host->intInfo.transLen); i++)
        {
            rk29_sdmmc_write(host->regs, SDMMC_DATA, host->intInfo.pBuf[host->intInfo.transLen + i]);
        }
        host->intInfo.transLen =  host->intInfo.desLen;
    }
      
}


static void rk29_sdmmc_submit_data(struct rk29_sdmmc *host, struct mmc_data *data)
{
    int ret;
    
    if(data)
    {
        host->data = data;
        data->error = 0;
        host->cmd->data = data;

        data->bytes_xfered = 0;
        host->pbuf = (u32*)sg_virt(data->sg);

        if (data->flags & MMC_DATA_STREAM)
		{
			host->cmdr |= SDMMC_CMD_STRM_MODE;    //set stream mode
		}
		else
		{
		    host->cmdr |= SDMMC_CMD_BLOCK_MODE;   //set block mode
		}
		
        //set the blocks and blocksize
		rk29_sdmmc_write(host->regs, SDMMC_BYTCNT,data->blksz*data->blocks);
		rk29_sdmmc_write(host->regs, SDMMC_BLKSIZ,data->blksz);

        xbwprintk(6, "%s..%d..CMD%d(arg=0x%x), data->blksz=%d, data->blocks=%d   ==xbw=[%s]==\n", \
            __FUNCTION__, __LINE__, host->cmd->opcode,host->cmd->arg,data->blksz, data->blocks,  host->dma_name);
            
		if (data->flags & MMC_DATA_WRITE)
		{
		    host->cmdr |= (SDMMC_CMD_DAT_WRITE | SDMMC_CMD_DAT_EXP);
            xbwprintk(7, "%s..%d...   write data, len=%d     ======xbw=[%s]====\n", \
					__FUNCTION__, __LINE__, data->blksz*data->blocks, host->dma_name);
		    
			ret = rk29_sdmmc_prepare_write_data(host, data);
	    }
	    else
	    {
	        host->cmdr |= (SDMMC_CMD_DAT_READ | SDMMC_CMD_DAT_EXP);
            xbwprintk(7, "%s..%d...   read data  len=%d   ======xbw=[%s]====\n", \
					__FUNCTION__, __LINE__, data->blksz*data->blocks, host->dma_name);
	        
			ret = rk29_sdmmc_prepare_read_data(host, data);
	    }

    }
    else
    {
        rk29_sdmmc_write(host->regs, SDMMC_BLKSIZ, 0);
        rk29_sdmmc_write(host->regs, SDMMC_BYTCNT, 0);
    }
}


static int sdmmc_send_cmd_start(struct rk29_sdmmc *host, unsigned int cmd)
{
	int tmo = RK29_SDMMC_SEND_START_TIMEOUT*10;//wait 60ms cycle.
	
	rk29_sdmmc_write(host->regs, SDMMC_CMD, SDMMC_CMD_START | cmd);		
	while (--tmo && (rk29_sdmmc_read(host->regs, SDMMC_CMD) & SDMMC_CMD_START))
	{
	    udelay(2);
	}
	
	if(!tmo) 
	{
		printk("%s.. %d   set cmd(value=0x%x) register timeout error !   ====xbw[%s]====\n",\
				__FUNCTION__,__LINE__, cmd, host->dma_name);

		host->errorstep = 0x9;
		return SDM_START_CMD_FAIL;
	}

	return SDM_SUCCESS;
}

static int rk29_sdmmc_get_cd(struct mmc_host *mmc)
{
	struct rk29_sdmmc *host = mmc_priv(mmc);
	u32 cdetect=1;

    switch(host->pdev->id)
    {
        case 0:
        {
            #ifdef CONFIG_PM
            	if(host->gpio_det == INVALID_GPIO)
            		return 1;
            #endif

        	cdetect = rk29_sdmmc_read(host->regs, SDMMC_CDETECT);

            cdetect = (cdetect & SDMMC_CARD_DETECT_N)?0:1;

            break;
        }        

        case 1:
        {
            #if defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)
            cdetect = 1;
            #else
            cdetect = test_bit(RK29_SDMMC_CARD_PRESENT, &host->flags)?1:0;
            #endif
            break;
        }
        
        default:
            cdetect = 1;
            break;
    
	}

	 return cdetect;
}


/****************************************************************/
//reset the SDMMC controller of the current host
/****************************************************************/
int rk29_sdmmc_reset_controller(struct rk29_sdmmc *host)
{
    u32  value = 0;
    int  timeOut = 0;

    rk29_sdmmc_write(host->regs, SDMMC_PWREN, POWER_ENABLE);

    /* reset SDMMC IP */
    //SDPAM_SDCClkEnable(host, TRUE);

    //Clean the fifo.
    for(timeOut=0; timeOut<FIFO_DEPTH; timeOut++)
    {
        if(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & SDMMC_STAUTS_FIFO_EMPTY)
            break;
            
        value = rk29_sdmmc_read(host->regs, SDMMC_DATA);
    }
   
    /* reset */
#if defined(CONFIG_ARCH_RK29)     
    rk29_sdmmc_write(host->regs, SDMMC_CTRL,(SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET ));
#elif defined(CONFIG_ARCH_RK30)
    rk29_sdmmc_write(host->regs, SDMMC_CTRL,(SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET));
#endif
    timeOut = 1000;
    value = rk29_sdmmc_read(host->regs, SDMMC_CTRL);
    while (( value & (SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_RESET)) && (timeOut > 0))
    {
        udelay(1);
        timeOut--;
        value = rk29_sdmmc_read(host->regs, SDMMC_CTRL);
    }

    if (timeOut == 0)
    {
        printk("%s..%s..%d..  reset controller fail!!! =====xbw[%s]=====\n",\
				__FILE__, __FUNCTION__,__LINE__, host->dma_name);

        host->errorstep = 0x0A;
        return SDM_WAIT_FOR_FIFORESET_TIMEOUT;
    }

     /* FIFO threshold settings  */
  	rk29_sdmmc_write(host->regs, SDMMC_FIFOTH, (SD_MSIZE_16 | (RX_WMARK << RX_WMARK_SHIFT) | (TX_WMARK << TX_WMARK_SHIFT)));
  	
    rk29_sdmmc_write(host->regs, SDMMC_CTYPE, SDMMC_CTYPE_1BIT);
    rk29_sdmmc_write(host->regs, SDMMC_CLKSRC, CLK_DIV_SRC_0);
    /* config debounce */
    host->bus_hz = clk_get_rate(host->clk);
    if((host->bus_hz > 52000000) || (host->bus_hz <= 0))
    {
        printk("%s..%s..%d..****Error!!!!!!  Bus clock %d hz is beyond the prescribed limits ====xbw[%s]===\n",\
            __FILE__, __FUNCTION__,__LINE__,host->bus_hz, host->dma_name);
        
		host->errorstep = 0x0B;            
        return SDM_PARAM_ERROR;            
    }

    rk29_sdmmc_write(host->regs, SDMMC_DEBNCE, (DEBOUNCE_TIME*host->bus_hz)&0xFFFFFF);

    /* config interrupt */
    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, 0xFFFFFFFF);

    if(host->use_dma)
    {
        if(RK29_CTRL_SDMMC_ID == host->pdev->id)
        {
		    rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEDMA);
		}
		else
		{
		    if(0== host->pdev->id)
		    {
    		    #if !defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
    		    rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEDMA | SDMMC_INT_SDIO);
    		    #else
    		    rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEDMA);
    		    #endif
		    }
		    else if(1== host->pdev->id)
		    {
		       #if !defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)
    		    rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEDMA | SDMMC_INT_SDIO);
    		   #else
    		    rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEDMA);
    		   #endif 
		    }
		    else
		    {
		        rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEDMA | SDMMC_INT_SDIO);
		    }
		}
	}
	else
	{
		if(RK29_CTRL_SDMMC_ID == host->pdev->id)
        {
		    rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEIO);
		}
		else
		{
		    if(0== host->pdev->id)
		    {
    		    #if !defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
    		    rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEIO | SDMMC_INT_SDIO);
    		    #else
    		    rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEIO);
    		    #endif
		    }
		    else if(1== host->pdev->id)
		    {
		        #if !defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)
    		    rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEIO | SDMMC_INT_SDIO);
    		    #else
    		    rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEIO);
    		    #endif
		    }
		    else
		    {
		        rk29_sdmmc_write(host->regs, SDMMC_INTMASK,RK29_SDMMC_INTMASK_USEDMA | SDMMC_INT_SDIO);
		    }
		}		
    }

	rk29_sdmmc_write(host->regs, SDMMC_PWREN, POWER_ENABLE);
	
   	rk29_sdmmc_write(host->regs, SDMMC_CTRL,SDMMC_CTRL_INT_ENABLE); // enable mci interrupt

    return SDM_SUCCESS;
}




//enable/disnable the clk.
static int rk29_sdmmc_control_clock(struct rk29_sdmmc *host, bool enable)
{
    u32           value = 0;
    int           tmo = 0;
    int           ret = SDM_SUCCESS;

    //wait previous start to clear
    tmo = 1000;
	while (--tmo && (rk29_sdmmc_read(host->regs, SDMMC_CMD) & SDMMC_CMD_START))
	{
		udelay(1);//cpu_relax();
	}
	if(!tmo)
	{
	    host->errorstep = 0x0C;
	    ret = SDM_START_CMD_FAIL;
		goto Error_exit;	
    }

    if(RK29_CTRL_SDMMC_ID == host->pdev->id)
    { 
        //SDMMC use low-power mode
        #if SDMMC_CLOCK_TEST
        if (enable)
        {
            value = (SDMMC_CLKEN_ENABLE);
        }
        else
        {
            value = (SDMMC_CLKEN_DISABLE);
        }
        
        #else
        {
            if (enable)
            {
                value = (SDMMC_CLKEN_LOW_PWR | SDMMC_CLKEN_ENABLE);
            }
            else
            {
                value = (SDMMC_CLKEN_LOW_PWR | SDMMC_CLKEN_DISABLE);
            }
        }
        #endif
    }
    else
    {
        //SDIO-card use non-low-power mode
        if (enable)
        {
            value = (SDMMC_CLKEN_ENABLE);
        }
        else
        {
            value = (SDMMC_CLKEN_DISABLE);
        }
    }
  
    rk29_sdmmc_write(host->regs, SDMMC_CLKENA, value);

	/* inform CIU */
	ret = sdmmc_send_cmd_start(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT);
    if(ret != SDM_SUCCESS)
    {
        goto Error_exit;
    }

    return SDM_SUCCESS;

Error_exit:
    printk("\n%s....%d..  control clock fail!!! Enable=%d, ret=0x%x ===xbw[%s]====\n",\
			__FILE__,__LINE__,enable,ret, host->dma_name);

    return ret;
    
}


//adjust the frequency.ie, to set the frequency division control
int rk29_sdmmc_change_clk_div(struct rk29_sdmmc *host, u32 freqHz)
{
    u32 div;
    u32 tmo;
    int ret = SDM_SUCCESS;

    if(0 == freqHz)
    {
        ret =  SDM_PARAM_ERROR;
        goto  SetFreq_error;
    }

    ret = rk29_sdmmc_control_clock(host, FALSE);
    if (ret != SDM_SUCCESS)
    {
        goto SetFreq_error;
    }

     
    host->bus_hz = clk_get_rate(host->clk);
    if((host->bus_hz > 52000000) || (host->bus_hz <= 0))
    {
        printk("%s..%s..%d..****Error!!!!!!  Bus clock %d hz is beyond the prescribed limits ====xbw[%s]===\n",\
            __FILE__, __FUNCTION__,__LINE__,host->bus_hz, host->dma_name);
            
        host->errorstep = 0x0D;    
        ret = SDM_PARAM_ERROR;   
        goto SetFreq_error;
    }

    //calculate the divider
    div = host->bus_hz/freqHz + ((( host->bus_hz%freqHz ) > 0) ? 1:0 );
    if( (div & 0x01) && (1 != div) )
    {
        //It is sure that the value of div is even. 
        ++div;
    }

    if(div > 1)
    {
        host->clock = host->bus_hz/div;
    }
    else
    {
        host->clock = host->bus_hz;
    }
    div = (div >> 1);

    //wait previous start to clear
    tmo = 1000;
	while (--tmo && (rk29_sdmmc_read(host->regs, SDMMC_CMD) & SDMMC_CMD_START))
	{
		udelay(1);//cpu_relax();
	}
	if(!tmo)
	{
	    host->errorstep = 0x0E; 
	    ret = SDM_START_CMD_FAIL;
		goto SetFreq_error;
    }
           
    /* set clock to desired speed */
    rk29_sdmmc_write(host->regs, SDMMC_CLKDIV, div);

    /* inform CIU */
    ret = sdmmc_send_cmd_start(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT);
    if(ret != SDM_SUCCESS)
    {
        host->errorstep = 0x0E1; 
        goto SetFreq_error;
    }
    
    if(host->old_div != div)
    {
        printk("%s..%d..  newDiv=%u, newCLK=%uKhz====xbw[%s]=====\n", \
            __FUNCTION__, __LINE__,div, host->clock/1000, host->dma_name);
    }

    ret = rk29_sdmmc_control_clock(host, TRUE);
    if(ret != SDM_SUCCESS)
    {
        goto SetFreq_error;
    }
    host->old_div = div;

    return SDM_SUCCESS;
    
SetFreq_error:

    printk("%s..%d..  change division fail, errorStep=0x%x,ret=%d  !!! ====xbw[%s]====\n",\
        __FILE__, __LINE__,host->errorstep,ret, host->dma_name);
        
    return ret;
    
}

int rk29_sdmmc_hw_init(void *data)
{
    struct rk29_sdmmc *host = (struct rk29_sdmmc *)data;

    //set the iomux
    host->ctype = SDMMC_CTYPE_1BIT;
    host->set_iomux(host->pdev->id, host->ctype);
    
    /* reset controller */
    rk29_sdmmc_reset_controller(host);

    rk29_sdmmc_change_clk_div(host, FOD_FREQ);
    
    return SDM_SUCCESS;    
}



int rk29_sdmmc_set_buswidth(struct rk29_sdmmc *host)
{
    //int ret;
    switch (host->ctype)
    {
        case SDMMC_CTYPE_1BIT:
        case SDMMC_CTYPE_4BIT:
            break;
        case SDMMC_CTYPE_8BIT:
            return SDM_PARAM_ERROR; //Now, not support 8 bit width
        default:
            return SDM_PARAM_ERROR;
    }

    host->set_iomux(host->pdev->id, host->ctype);

    /* Set the current  bus width */
	rk29_sdmmc_write(host->regs, SDMMC_CTYPE, host->ctype);

    return SDM_SUCCESS;
}


static void rk29_sdmmc_dealwith_timeout(struct rk29_sdmmc *host)
{ 
    if(0 == host->mmc->doneflag)
        return; //not to generate error flag if the command has been over.
        
    switch(host->state)
    {
        case STATE_IDLE:
        {
            #if 1
            break;
            #else
            if(!host->cmd)
                break;
                
            host->cmd->error = -EIO;
            
            if(host->cmd->data)
            {
                host->cmd->data->error = -EILSEQ;
            }
            host->state = STATE_SENDING_CMD;
            /* fall through */
            #endif    			    
    	}
    	    
    	case STATE_SENDING_CMD:
    	    host->cmd_status |= SDMMC_INT_RTO;
    	    host->cmd->error = -ETIME;
    	    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,(SDMMC_INT_CMD_DONE | SDMMC_INT_RTO));  //  clear interrupt
    	    rk29_sdmmc_set_pending(host, EVENT_CMD_COMPLETE);
    	    tasklet_schedule(&host->tasklet);
    	    break;
    	 case STATE_DATA_BUSY:
    	    host->data_status |= (SDMMC_INT_DCRC|SDMMC_INT_EBE);
    	    host->cmd->data->error = -EILSEQ;
    	    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_DTO);  // clear interrupt
    	    rk29_sdmmc_set_pending(host, EVENT_DATA_COMPLETE);
    	    tasklet_schedule(&host->tasklet);
    	    break;
    	 case STATE_SENDING_STOP: 
    	    host->cmd_status |= SDMMC_INT_RTO;
    	    host->cmd->error = -ETIME;
    	    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,(SDMMC_INT_CMD_DONE | SDMMC_INT_RTO));  //  clear interrupt
    	    rk29_sdmmc_set_pending(host, EVENT_CMD_COMPLETE);
    	    tasklet_schedule(&host->tasklet);
    	    break;
    }
}


static void rk29_sdmmc_INT_CMD_DONE_timeout(unsigned long host_data)
{
	struct rk29_sdmmc *host = (struct rk29_sdmmc *) host_data;
	unsigned long iflags;

	spin_lock_irqsave(&host->lock, iflags);
	
	if(STATE_SENDING_CMD == host->state)
	{
	    if(0==host->cmd->retries)
	    {
    	    printk("%s..%d... cmd=%d, INT_CMD_DONE timeout, errorStep=0x%x, host->state=%x ===xbw[%s]===\n",\
                __FUNCTION__, __LINE__,host->cmd->opcode, host->errorstep,host->state,host->dma_name);
        }
            
        rk29_sdmmc_dealwith_timeout(host);        
	}
	spin_unlock_irqrestore(&host->lock, iflags);
	
}


static void rk29_sdmmc_INT_DTO_timeout(unsigned long host_data)
{
	struct rk29_sdmmc *host = (struct rk29_sdmmc *) host_data;
	unsigned long iflags;

	spin_lock_irqsave(&host->lock, iflags);


	if( (host->cmdr & SDMMC_CMD_DAT_EXP) && (STATE_DATA_BUSY == host->state))
	{
	    if(0==host->cmd->retries)
	    {
    	   printk("%s..%d...cmd=%d DTO_timeout,cmdr=0x%x, errorStep=0x%x, Hoststate=%x===xbw[%s]===\n", \
    	        __FUNCTION__, __LINE__,host->cmd->opcode,host->cmdr ,host->errorstep,host->state,host->dma_name);
	    }

	    rk29_sdmmc_dealwith_timeout(host);  
	}
	spin_unlock_irqrestore(&host->lock, iflags);
 
 
}


//to excute a  request 
static int rk29_sdmmc_start_request(struct mmc_host *mmc )
{
    struct rk29_sdmmc *host = mmc_priv(mmc);
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	
	u32		cmdr, ret;
	unsigned long iflags;

	spin_lock_irqsave(&host->lock, iflags);
	
	mrq = host->new_mrq;
	cmd = mrq->cmd;
	cmd->error = 0;
	
	cmdr = rk29_sdmmc_prepare_command(cmd);
	ret = SDM_SUCCESS;
	

	/*clean FIFO if it is a new request*/
    if((RK29_CTRL_SDMMC_ID == host->pdev->id) && ( !(cmdr & SDMMC_CMD_STOP)))
    {
        ret = rk29_sdmmc_reset_fifo(host);
        if(SDM_SUCCESS != ret)
        {
        		host->mrq = host->new_mrq;///
            cmd->error = -ENOMEDIUM;
            host->errorstep = 0x0F; 
            ret = SDM_FALSE;
            goto start_request_Err; 
        }
    }

    //check data-busy if the current command has the bit13 in command register.
    if( cmdr & SDMMC_CMD_PRV_DAT_WAIT )
    {
        if(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & SDMMC_STAUTS_DATA_BUSY)
        {
        	host->mrq = host->new_mrq;///
            cmd->error = -ETIMEDOUT;
            ret = SDM_BUSY_TIMEOUT;
            host->errorstep = 0x10;
            if(0 == cmd->retries)
            {
                printk("%s..Error happen in CMD_PRV_DAT_WAIT. STATUS-reg=0x%x ===xbw[%s]===\n", \
                    __FUNCTION__, rk29_sdmmc_read(host->regs, SDMMC_STATUS),host->dma_name);
            }
            rk29_sdmmc_clear_fifo(host);
            goto start_request_Err; 
        }
    }
    
    host->state = STATE_SENDING_CMD;
    host->mrq = host->new_mrq;
	mrq = host->mrq;
	cmd = mrq->cmd;
	cmd->error = 0;
	cmd->data = NULL;

    host->cmdr = cmdr;
    host->cmd = cmd;
	host->data_status = 0;
	host->data = NULL;
	
	host->errorstep = 0;
	host->dodma = 0;



    //setting for the data
	rk29_sdmmc_submit_data(host, mrq->data);
    host->errorstep = 0xff;

	xbwprintk(7, "%s..%d...    CMD%d  begin to call rk29_sdmmc_start_command() ===xbw[%s]===\n", \
			__FUNCTION__, __LINE__ , cmd->opcode,host->dma_name);

	if(RK29_CTRL_SDMMC_ID == host->pdev->id)
	{
	    mod_timer(&host->request_timer, jiffies + msecs_to_jiffies(RK29_SDMMC_SEND_START_TIMEOUT+700));
	}
	else
	{
	    mod_timer(&host->request_timer, jiffies + msecs_to_jiffies(RK29_SDMMC_SEND_START_TIMEOUT+500));
	}


	ret = rk29_sdmmc_start_command(host, cmd, host->cmdr);
	if(SDM_SUCCESS != ret)
	{
        cmd->error = -ETIMEDOUT;
        if(0==cmd->retries)
        {
            printk("%s..%d...   start_command(CMD%d, arg=%x, retries=%d)  fail! ret=%d  =========xbw=[%s]===\n",\
                __FUNCTION__, __LINE__ , cmd->opcode,cmd->arg, cmd->retries,ret, host->dma_name);
        }
        host->errorstep = 0x11; 
        del_timer_sync(&host->request_timer);
        
        goto start_request_Err; 
	}
	host->errorstep = 0xfd;

    xbwprintk(7, "%s..%d...  CMD=%d, wait for INT_CMD_DONE, ret=%d , \n  \
        host->state=0x%x, cmdINT=0x%x \n    host->pendingEvent=0x%lu, host->completeEvents=0x%lu =========xbw=[%s]=====\n\n",\
        __FUNCTION__, __LINE__, host->cmd->opcode,ret, \
        host->state,host->cmd_status, host->pending_events,host->completed_events,host->dma_name);

    spin_unlock_irqrestore(&host->lock, iflags);
	return SDM_SUCCESS;
	
start_request_Err:
    rk29_sdmmc_start_error(host);

    if(0 == cmd->retries) 
    {
        printk("%s: CMD%d(arg=%x)  fail to start request.  err=%d, Errorstep=0x%x ===xbw[%s]==\n\n",\
            __FUNCTION__,  cmd->opcode, cmd->arg,ret,host->errorstep,host->dma_name);
    }

    host->state = STATE_IDLE;  //modifyed by xbw  at 2011-08-15
    
    if(host->mrq && host->mmc->doneflag)
    {
        host->mmc->doneflag = 0;
        spin_unlock_irqrestore(&host->lock, iflags);
        
        mmc_request_done(host->mmc, host->mrq);
    }
    else
    {
        spin_unlock_irqrestore(&host->lock, iflags);
    }
    
    return ret; 
	
}

 
static void rk29_sdmmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
    unsigned long iflags;
	struct rk29_sdmmc *host = mmc_priv(mmc); 
	
    spin_lock_irqsave(&host->lock, iflags);
    
	#if 0
	//set 1 to close the controller for Debug.
	if(RK29_CTRL_SDIO1_ID==host->pdev->id)//if(RK29_CTRL_SDMMC_ID==host->pdev->id)//
	{
	    mrq->cmd->error = -ENOMEDIUM;
	    printk("%s..%d..  ==== The %s had been closed by myself for the experiment. ====xbw[%s]===\n",\
				__FUNCTION__, __LINE__, host->dma_name, host->dma_name);

        host->state = STATE_IDLE;
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, 0xFFFFFFFF);
        spin_unlock_irqrestore(&host->lock, iflags);
	    mmc_request_done(mmc, mrq);
		return;
	}
	#endif

    xbwprintk(6, "\n%s..%d..New cmd=%2d(arg=0x%8x)=== cardPresent=0x%lu, state=0x%x ==xbw[%s]==\n", \
        __FUNCTION__, __LINE__,mrq->cmd->opcode, mrq->cmd->arg,host->flags,host->state, host->dma_name);

    if(RK29_CTRL_SDMMC_ID == host->pdev->id)
    {
        if(!rk29_sdmmc_get_cd(mmc) || ((0==mmc->re_initialized_flags)&&(MMC_GO_IDLE_STATE != mrq->cmd->opcode)))
        {
    		mrq->cmd->error = -ENOMEDIUM;

    		if((RK29_CTRL_SDMMC_ID == host->pdev->id)&&(0==mrq->cmd->retries))
    		{
    	    	if(host->old_cmd != mrq->cmd->opcode)
    	    	{
    	    	    if( ((17==host->old_cmd)&&(18==mrq->cmd->opcode)) || ((18==host->old_cmd)&&(17==mrq->cmd->opcode)) ||\
    	    	         ((24==host->old_cmd)&&(25==mrq->cmd->opcode)) || ((25==host->old_cmd)&&(24==mrq->cmd->opcode)))
    	    	    {
    	    	        host->old_cmd = mrq->cmd->opcode;
    	    	        if(host->error_times++ %RK29_ERROR_PRINTK_INTERVAL ==0)
            	        {
                    		printk("%s: Refuse to run CMD%2d(arg=0x%8x) due to the removal of card.  1==xbw[%s]==\n", \
                    		    __FUNCTION__, mrq->cmd->opcode, mrq->cmd->arg, host->dma_name);
                		}
    	    	    }
    	    	    else
    	    	    {
            	        host->old_cmd = mrq->cmd->opcode;
            	        host->error_times = 0;
            	        printk("%s: Refuse to run CMD%2d(arg=0x%8x) due to the removal of card.  2==xbw[%s]==\n", \
                    		    __FUNCTION__, mrq->cmd->opcode, mrq->cmd->arg, host->dma_name); 
                	}
        	    }
        	    else
        	    {
        	        if(host->error_times++ % (RK29_ERROR_PRINTK_INTERVAL*3) ==0)
        	        {
                		printk("%s: Refuse to run CMD%2d(arg=0x%8x) due to the removal of card.  3==xbw[%s]==\n", \
                    		    __FUNCTION__, mrq->cmd->opcode, mrq->cmd->arg, host->dma_name);
            		}
            		host->old_cmd = mrq->cmd->opcode;
        	    }	    
    		}
            host->state = STATE_IDLE;
            spin_unlock_irqrestore(&host->lock, iflags);
    		mmc_request_done(mmc, mrq);
    		return;
    	}
    	else
    	{
    		if(host->old_cmd != mrq->cmd->opcode)
    		{	
    			host->old_cmd = mrq->cmd->opcode;
				host->error_times = 0;
			}			
    	}
	}
	else
	{
        host->old_cmd = mrq->cmd->opcode;
        host->error_times = 0;

        if(!test_bit(RK29_SDMMC_CARD_PRESENT, &host->flags))
		{
		    host->state = STATE_IDLE;
		    mrq->cmd->error = -ENOMEDIUM;
            spin_unlock_irqrestore(&host->lock, iflags);
    		mmc_request_done(mmc, mrq);
    		return;
		}

	}
	
	#if 1  
    host->new_mrq = mrq;        

	spin_unlock_irqrestore(&host->lock, iflags);
        
    rk29_sdmmc_start_request(mmc);
	
	#else
	if (host->state == STATE_IDLE) 
	{
        spin_unlock_irqrestore(&host->lock, iflags);
        
        host->new_mrq = mrq;        
        rk29_sdmmc_start_request(mmc);
	} 
	else 
	{
        #ifdef RK29_SDMMC_LIST_QUEUE	
        
        printk("%s..%d...Danger! Danger! New request was added to queue. ===xbw[%s]===\n", \
				__FUNCTION__, __LINE__,host->dma_name);
        list_add_tail(&host->queue_node, &host->queue);
        
        #else

        printk("%s..%d..state Error! ,old_state=%d, OldCMD=%d ,NewCMD%2d,arg=0x%x ===xbw[%s]===\n", \
				__FUNCTION__, __LINE__, host->state, host->cmd->opcode,mrq->cmd->opcode,mrq->cmd->arg, host->dma_name);
				
		mrq->cmd->error = -ENOMEDIUM;

		spin_unlock_irqrestore(&host->lock, iflags);		
		mmc_request_done(mmc, mrq);
		
		return;
				
        #endif	
	}	
	#endif

}



static void rk29_sdmmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
    int timeout = 250;
    unsigned int value;
    unsigned long iflags;
	struct rk29_sdmmc *host = mmc_priv(mmc);

    spin_lock_irqsave(&host->lock, iflags);

    if(test_bit(RK29_SDMMC_CARD_PRESENT, &host->flags) || (RK29_CTRL_SDMMC_ID == host->pdev->id))
    {
        /*
         * Waiting SDIO controller to be IDLE.
        */
        while (timeout-- > 0)
    	{
    		value = rk29_sdmmc_read(host->regs, SDMMC_STATUS);
    		if ((value & SDMMC_STAUTS_DATA_BUSY) == 0 &&(value & SDMMC_CMD_FSM_MASK) == SDMMC_CMD_FSM_IDLE)
    		{
    			break;
    		}
    		
    		mdelay(1);
    	}
    	if (timeout <= 0)
    	{
    		printk("%s..%d...Waiting for SDMMC%d controller to be IDLE timeout.==xbw[%s]===\n", \
    				__FUNCTION__, __LINE__, host->pdev->id, host->dma_name);

    		goto out;
    	}
	}

    //if(host->bus_mode != ios->power_mode)
    {
        switch (ios->power_mode) 
        {
            case MMC_POWER_UP:
            	rk29_sdmmc_write(host->regs, SDMMC_PWREN, POWER_ENABLE);
            	
            	//reset the controller if it is SDMMC0
            	if(RK29_CTRL_SDMMC_ID == host->pdev->id)
            	{
            	    xbwprintk(7, "%s..%d..POWER_UP, call reset_controller, initialized_flags=%d ====xbw[%s]=====\n",\
            	        __FUNCTION__, __LINE__, host->mmc->re_initialized_flags,host->dma_name);
            	        
            	    mdelay(5);
            	        
            	    rk29_sdmmc_hw_init(host);
            	}
               	
            	break;
            case MMC_POWER_OFF:
              
                if(RK29_CTRL_SDMMC_ID == host->pdev->id)
                {
                	rk29_sdmmc_control_clock(host, FALSE);
                	rk29_sdmmc_write(host->regs, SDMMC_PWREN, POWER_DISABLE);
                
                	if(5 == host->bus_mode)
                	{
                        mdelay(5);
                        xbwprintk(7, "%s..%d..Fisrt powerOFF, call reset_controller ======xbw[%s]====\n", \
                            __FUNCTION__, __LINE__,host->dma_name);
                            
                        rk29_sdmmc_reset_controller(host);
                	}
              
            	}

            	break;        	
            default:
            	break;
    	}
    	
    	host->bus_mode = ios->power_mode;
    	
	}

    if(!(test_bit(RK29_SDMMC_CARD_PRESENT, &host->flags) || (RK29_CTRL_SDIO1_ID != host->pdev->id)))
        goto out; //exit the set_ios directly if the SDIO is not present. 
        
	if(host->ctype != ios->bus_width)
	{
    	switch (ios->bus_width) 
    	{
            case MMC_BUS_WIDTH_1:
                host->ctype = SDMMC_CTYPE_1BIT;
                break;
            case MMC_BUS_WIDTH_4:
                host->ctype = SDMMC_CTYPE_4BIT;
                break;
            case MMC_BUS_WIDTH_8:
                host->ctype = SDMMC_CTYPE_8BIT;
                break;
            default:
                host->ctype = 0;
                break;
    	}

	    rk29_sdmmc_set_buswidth(host);
	    
	}
	
	if (ios->clock && (ios->clock != host->clock)) 
	{	
		/*
		 * Use mirror of ios->clock to prevent race with mmc
		 * core ios update when finding the minimum.
		 */
		//host->clock = ios->clock;	
		rk29_sdmmc_change_clk_div(host, ios->clock);
	}
out:	

    spin_unlock_irqrestore(&host->lock, iflags);
}

static int rk29_sdmmc_get_ro(struct mmc_host *mmc)
{
    struct rk29_sdmmc *host = mmc_priv(mmc);
    int ret=0;

    switch(host->pdev->id)
    {
        case 0:
        {
            #if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT) || defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)            
        	if(INVALID_GPIO == host->write_protect)
        	    ret = 0;//no write-protect
        	else
                ret = gpio_get_value(host->write_protect)?1:0;
           
            xbwprintk(7,"%s..%d.. write_prt_pin=%d, get_ro=%d ===xbw[%s]===\n",\
                __FUNCTION__, __LINE__,host->write_protect, ret, host->dma_name);
                            
            #else
        	u32 wrtprt = rk29_sdmmc_read(host->regs, SDMMC_WRTPRT);
        	
        	ret = (wrtprt & SDMMC_WRITE_PROTECT)?1:0;
            #endif

            break;
        }
        
        case 1:
            ret = 0;//no write-protect
            break;
        
        default:
            ret = 0;
        break;   
    }

    return ret;

}


static void rk29_sdmmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	u32 intmask;
	unsigned long flags;
	struct rk29_sdmmc *host = mmc_priv(mmc);
	
	spin_lock_irqsave(&host->lock, flags);
	intmask = rk29_sdmmc_read(host->regs, SDMMC_INTMASK);
	
	if(enable)
		rk29_sdmmc_write(host->regs, SDMMC_INTMASK, intmask | SDMMC_INT_SDIO);
	else
		rk29_sdmmc_write(host->regs, SDMMC_INTMASK, intmask & ~SDMMC_INT_SDIO);
	spin_unlock_irqrestore(&host->lock, flags);
}

static void  rk29_sdmmc_init_card(struct mmc_host *mmc, struct mmc_card *card)
{
        card->quirks = MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;

}

static int rk29_sdmmc_clear_fifo(struct rk29_sdmmc *host)
{
    unsigned int timeout, value;
    int ret = SDM_SUCCESS;

    if(RK29_CTRL_SDMMC_ID == host->pdev->id)
    {
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, 0xFFFFFFFF);
    }

    rk29_sdmmc_stop_dma(host);
    rk29_sdmmc_control_host_dma(host, FALSE);
    host->dodma = 0;
   
    //Clean the fifo.
    for(timeout=0; timeout<FIFO_DEPTH; timeout++)
    {
        if(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & SDMMC_STAUTS_FIFO_EMPTY)
            break;
            
        value = rk29_sdmmc_read(host->regs, SDMMC_DATA);
    }

     /* reset */
    timeout = 1000;
    value = rk29_sdmmc_read(host->regs, SDMMC_CTRL);
    value |= (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET);
    rk29_sdmmc_write(host->regs, SDMMC_CTRL, value);

    value = rk29_sdmmc_read(host->regs, SDMMC_CTRL);
    
    while( (value & (SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_RESET | SDMMC_CTRL_DMA_RESET)) && (timeout > 0))
    {
        udelay(1);
        timeout--;
        value = rk29_sdmmc_read(host->regs, SDMMC_CTRL);
    }

    if (timeout == 0)
    {
        host->errorstep = 0x0A;
        ret = SDM_WAIT_FOR_FIFORESET_TIMEOUT;
    }

    return ret;
}



static const struct mmc_host_ops rk29_sdmmc_ops[] = {
	{
		.request	= rk29_sdmmc_request,
		.set_ios	= rk29_sdmmc_set_ios,
		.get_ro		= rk29_sdmmc_get_ro,
		.get_cd		= rk29_sdmmc_get_cd,
	},
	{
		.request	= rk29_sdmmc_request,
		.set_ios	= rk29_sdmmc_set_ios,
		.get_ro		= rk29_sdmmc_get_ro,
		.get_cd		= rk29_sdmmc_get_cd,
		.enable_sdio_irq = rk29_sdmmc_enable_sdio_irq,
		.init_card       = rk29_sdmmc_init_card,
	},
};

static void rk29_sdmmc_request_end(struct rk29_sdmmc *host, struct mmc_command *cmd)
{
	u32 status = host->data_status;
	int output=SDM_SUCCESS;

	xbwprintk(7, "%s..%d...  cmd=%d, host->state=0x%x,\n   pendingEvent=0x%lu, completeEvents=0x%lu ====xbw=[%s]====\n\n",\
        __FUNCTION__, __LINE__,cmd->opcode,host->state, host->pending_events,host->completed_events,host->dma_name);

    del_timer_sync(&host->DTO_timer);

    if(RK29_CTRL_SDMMC_ID == host->pdev->id)
    {
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, 0xFFFFFFFF); //added by xbw at 2011-08-15
    }

    //stop DMA
    if(host->dodma)
    {
        rk29_sdmmc_stop_dma(host);
        rk29_sdmmc_control_host_dma(host, FALSE);
        host->dodma = 0;
    }

    if(cmd->error)
    {
        goto exit;//It need not to wait-for-busy if the CMD-ERROR happen.
    }
    host->errorstep = 0xf7;
    if(cmd->data)
    {        
        if(host->cmdr & SDMMC_CMD_DAT_WRITE)
        {
            if(status & (SDMMC_INT_DCRC | SDMMC_INT_EBE))
            {
                cmd->data->error = -EILSEQ;               
                output = SDM_DATA_CRC_ERROR;
                host->errorstep = 0x16; 
            }
            else
            {
                output = rk29_sdmmc_wait_unbusy(host);
                if(SDM_SUCCESS != output)
                {
                    host->errorstep = 0x17;
                    cmd->data->error = -ETIMEDOUT;
                }

                host->data->bytes_xfered = host->data->blocks * host->data->blksz;
            }
        }
        else
        {
            if( status  & SDMMC_INT_SBE)
            {
                cmd->data->error = -EIO;
                host->errorstep = 0x18;
                output = SDM_START_BIT_ERROR;
            }
            else if((status  & SDMMC_INT_EBE) && (cmd->opcode != 14)) //MMC4.0, BUSTEST_R, A host read the reserved bus testing data parttern from a card.
            {
                cmd->data->error = -EILSEQ;
                host->errorstep = 0x19;
                output = SDM_END_BIT_ERROR;
            }
            else if(status  & SDMMC_INT_DRTO)
            {
                cmd->data->error = -ETIMEDOUT;
                host->errorstep = 0x1A;
                output = SDM_DATA_READ_TIMEOUT;
            }
            else if(status  & SDMMC_INT_DCRC)
            {
                host->errorstep = 0x1B;
                cmd->data->error = -EILSEQ;
                output = SDM_DATA_CRC_ERROR;
            }
            else
            {
                output = rk29_sdmmc_read_remain_data(host, (host->data->blocks * host->data->blksz), host->pbuf);
                if(SDM_SUCCESS == output)
                {
                    host->data->bytes_xfered = host->data->blocks * host->data->blksz;
                }
            }       
        }
    }

    if(SDM_SUCCESS == output)
    {
        if ((mmc_resp_type(cmd) == MMC_RSP_R1B) || (MMC_STOP_TRANSMISSION == cmd->opcode))
        {
            output = rk29_sdmmc_wait_unbusy(host);
            if((SDM_SUCCESS != output) && (!host->mrq->cmd->error))
            {
                printk("%s..%d...   CMD12 wait busy timeout!!!!! errorStep=0x%x     ====xbw=[%s]====\n", \
						__FUNCTION__, __LINE__, host->errorstep, host->dma_name);
                rk29_sdmmc_clear_fifo(host);
                cmd->error = -ETIMEDOUT;
                host->mrq->cmd->error = -ETIMEDOUT;
                host->errorstep = 0x1C;
            }
        }
    }
    host->errorstep = 0xf6;
    
    //trace error
    if(cmd->data && cmd->data->error)
    { 
        if( (!cmd->error) && (0==cmd->retries))
        {         
            printk("%s..%d......CMD=%d error!!!(arg=0x%x,cmdretry=%d,blksize=%d, blocks=%d), \n \
                statusReg=0x%x, ctrlReg=0x%x, nerrorTimes=%d, errorStep=0x%x ====xbw[%s]====\n",\
                __FUNCTION__, __LINE__, cmd->opcode, cmd->arg, cmd->retries,cmd->data->blksz, cmd->data->blocks,
                rk29_sdmmc_read(host->regs, SDMMC_STATUS),
                rk29_sdmmc_read(host->regs, SDMMC_CTRL),
                host->error_times,host->errorstep, host->dma_name);
        }
        cmd->error = -ENODATA;
    }
    host->errorstep = 0xf5;

exit:

#ifdef RK29_SDMMC_LIST_QUEUE
	if (!list_empty(&host->queue)) 
	{
		printk("%s..%d..  Danger!Danger!. continue the next request in the queue.  ====xbw[%s]====\n",\
		        __FUNCTION__, __LINE__, host->dma_name);

		host = list_entry(host->queue.next,
				struct rk29_sdmmc, queue_node);
		list_del(&host->queue_node);
		host->state = STATE_SENDING_CMD;
		rk29_sdmmc_start_request(host->mmc);
	} 
	else 
	{	
		dev_vdbg(&host->pdev->dev, "list empty\n");
		host->state = STATE_IDLE;
	}
#else
    dev_vdbg(&host->pdev->dev, "list empty\n");
	host->state = STATE_IDLE;
#endif
	
}

static int rk29_sdmmc_command_complete(struct rk29_sdmmc *host,
			struct mmc_command *cmd)
{
	u32	 value, status = host->cmd_status;
	int  timeout, output= SDM_SUCCESS;

    xbwprintk(7, "%s..%d.  cmd=%d, host->state=0x%x, cmdINT=0x%x\n,pendingEvent=0x%lu,completeEvents=0x%lu ===xbw[%s]===\n\n",\
        __FUNCTION__, __LINE__,cmd->opcode,host->state,status, host->pending_events,host->completed_events,host->dma_name);


    del_timer_sync(&host->request_timer);
    
    host->cmd_status = 0;

	if((RK29_CTRL_SDMMC_ID == host->pdev->id) && (host->cmdr & SDMMC_CMD_STOP))
    {
        output = rk29_sdmmc_reset_fifo(host);
        if (SDM_SUCCESS != output)
        {
            printk("%s..%d......reset fifo fail! CMD%d(arg=0x%x, Retries=%d) =======xbw[%s]=====\n",__FUNCTION__, __LINE__, \
                cmd->opcode, cmd->arg, cmd->retries,host->dma_name);
                
            cmd->error = -ETIMEDOUT;
            host->mrq->cmd->error = cmd->error;
            output = SDM_ERROR;
            host->errorstep = 0x1C;
            goto CMD_Errror;
        }
    }

    if(status & SDMMC_INT_RTO)
	{
	    cmd->error = -ENOMEM;
	    host->mrq->cmd->error = cmd->error;
        output = SDM_BUSY_TIMEOUT;

        //rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_RTO);
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,0xFFFFFFFF);  //modifyed by xbw at 2011-08-15
        
        if(host->use_dma)//if(host->dodma)
        {
           if(host->dodma) 
           {
                rk29_sdmmc_stop_dma(host);
                rk29_sdmmc_control_host_dma(host, FALSE);
                host->dodma = 0;
           }
            
            value = rk29_sdmmc_read(host->regs, SDMMC_CTRL);
            value |= SDMMC_CTRL_FIFO_RESET;
            rk29_sdmmc_write(host->regs, SDMMC_CTRL, value);

            timeout = 1000;
            while (((value = rk29_sdmmc_read(host->regs, SDMMC_CTRL)) & (SDMMC_CTRL_FIFO_RESET)) && (timeout > 0))
            {
                udelay(1);
                timeout--;
            }
            if (timeout == 0)
            {   
                output = SDM_FALSE;
                host->errorstep = 0x1D;
                printk("%s..%d......reset CTRL fail! CMD%d(arg=0x%x, Retries=%d) ===xbw[%s]===\n",\
                    __FUNCTION__, __LINE__, cmd->opcode, cmd->arg, cmd->retries,host->dma_name);
                
               goto CMD_Errror;
            }
        }

	}	

	if(cmd->flags & MMC_RSP_PRESENT) 
	{
	    if(cmd->flags & MMC_RSP_136) 
	    {
            cmd->resp[3] = rk29_sdmmc_read(host->regs, SDMMC_RESP0);
            cmd->resp[2] = rk29_sdmmc_read(host->regs, SDMMC_RESP1);
            cmd->resp[1] = rk29_sdmmc_read(host->regs, SDMMC_RESP2);
            cmd->resp[0] = rk29_sdmmc_read(host->regs, SDMMC_RESP3);
	    } 
	    else 
	    {
	        cmd->resp[0] = rk29_sdmmc_read(host->regs, SDMMC_RESP0);
	    }
	}
	
	if(cmd->error)
	{
	    del_timer_sync(&host->DTO_timer);

        //trace error
	    if((0==cmd->retries) && (host->error_times++%RK29_ERROR_PRINTK_INTERVAL == 0) && (12 != cmd->opcode))
	    {
	        if( ((RK29_CTRL_SDMMC_ID==host->pdev->id)&&(MMC_SLEEP_AWAKE!=cmd->opcode)) || 
	             ((RK29_CTRL_SDMMC_ID!=host->pdev->id)&&(MMC_SEND_EXT_CSD!=cmd->opcode))  )
	        {
	            printk("%s..%d...CMD%d(arg=0x%x), hoststate=%d, errorTimes=%d, errorStep=0x%x ! ===xbw[%s]===\n",\
                    __FUNCTION__, __LINE__, cmd->opcode, cmd->arg, host->state,host->error_times,host->errorstep, host->dma_name);
	        }
	    }

	}
    del_timer_sync(&host->request_timer);


	return SDM_SUCCESS;
   
CMD_Errror:
    del_timer_sync(&host->request_timer);
	del_timer_sync(&host->DTO_timer);

	if((0==cmd->retries) && (host->error_times++%RK29_ERROR_PRINTK_INTERVAL == 0))
    {
        printk("%s..%d....command_complete(CMD=%d, arg=%x) error=%d =======xbw[%s]=====\n",\
            __FUNCTION__, __LINE__, host->cmd->opcode,host->cmd->arg, output, host->dma_name);
    }
        
    return output;
    
}


static void rk29_sdmmc_start_error(struct rk29_sdmmc *host)
{
    host->cmd->error = -EIO;
    host->mrq->cmd->error = -EIO;
    host->cmd_status |= SDMMC_INT_RTO;

    del_timer_sync(&host->request_timer);

    rk29_sdmmc_command_complete(host, host->mrq->cmd);    
    rk29_sdmmc_request_end(host, host->mrq->cmd);

}

static void rk29_sdmmc_tasklet_func(unsigned long priv)
{
	struct rk29_sdmmc	*host = (struct rk29_sdmmc *)priv;
	struct mmc_data		*data = host->cmd->data;
	enum rk29_sdmmc_state	state = host->state;
	int pending_flag, stopflag;
	unsigned long iflags;
    
	spin_lock_irqsave(&host->lock, iflags); 
	
	state = host->state;
	pending_flag = 0;
	stopflag = 0;
	
	do 
	{
        switch (state) 
        {
            case STATE_IDLE:
            {
                xbwprintk(7, "%s..%d..   prev_state=  STATE_IDLE  ====xbw[%s]====\n", \
						__FUNCTION__, __LINE__, host->dma_name);
            	break;
            }

            case STATE_SENDING_CMD:
            {
                xbwprintk(7, "%s..%d..   prev_state=  STATE_SENDING_CMD, pendingEvernt=0x%lu  ====xbw[%s]====\n",\
                    __FUNCTION__, __LINE__,host->completed_events, host->dma_name);

                if (!rk29_sdmmc_test_and_clear_pending(host, EVENT_CMD_COMPLETE))
                	break;
                 host->errorstep = 0xfb;

                del_timer_sync(&host->request_timer); //delete the timer for INT_COME_DONE

                rk29_sdmmc_set_completed(host, EVENT_CMD_COMPLETE);
                rk29_sdmmc_command_complete(host, host->cmd);

                
                if (!data) 
                {
                    rk29_sdmmc_request_end(host, host->cmd);

                    xbwprintk(7, "%s..%d..  CMD%d call mmc_request_done()====xbw[%s]====\n", \
							__FUNCTION__, __LINE__,host->cmd->opcode,host->dma_name);
                    
                    host->complete_done = 1;
                    break;
                }
                host->errorstep = 0xfa;
                if(host->cmd->error)
                {
                    del_timer_sync(&host->DTO_timer); //delete the timer for INT_DTO
                    
                    if(data->stop)
                    {
                        xbwprintk(7, "%s..%d..  cmderr, so call send_stop_cmd() ====xbw[%s]====\n", \
								__FUNCTION__, __LINE__, host->dma_name);

                        #if 0
                        state = STATE_SENDING_CMD;//STATE_SENDING_STOP; 
                        send_stop_cmd(host);
                        #else
                        stopflag = 1;  //Moidfyed by xbw at 2011-09-08
                        #endif
                        break;
                    }

                    rk29_sdmmc_set_pending(host, EVENT_DATA_COMPLETE);
                }

                host->errorstep = 0xf9;
                state = STATE_DATA_BUSY;
                /* fall through */
            }

            case STATE_DATA_BUSY:
            {
                xbwprintk(7, "%s..%d..   prev_state= STATE_DATA_BUSY, pendingEvernt=0x%lu ====xbw[%s]====\n", \
						__FUNCTION__, __LINE__,host->pending_events, host->dma_name);

                if (!rk29_sdmmc_test_and_clear_pending(host, EVENT_DATA_COMPLETE))
                	break;	
                host->errorstep = 0xf8;
                rk29_sdmmc_set_completed(host, EVENT_DATA_COMPLETE);
                del_timer_sync(&host->DTO_timer); //delete the timer for INT_DTO

                rk29_sdmmc_request_end(host, host->cmd);

                if (data && !data->stop) 
                {
                    xbwprintk(7, "%s..%d..  CMD%d call mmc_request_done()====xbw[%s]====\n", \
							__FUNCTION__, __LINE__,host->cmd->opcode,host->dma_name);

                    if(!( (MMC_READ_SINGLE_BLOCK == host->cmd->opcode)&&( -EIO == data->error))) //deal with START_BIT_ERROR
                    {
                    	host->complete_done = 2;
                    	break;
                    }

                }
                host->errorstep = 0xf4;
                xbwprintk(7, "%s..%d..  after DATA_COMPLETE, so call send_stop_cmd() ====xbw[%s]====\n", \
						__FUNCTION__, __LINE__, host->dma_name);

                #if 0
                state = STATE_SENDING_CMD;
                send_stop_cmd(host);
                #else
                stopflag = 2; //Moidfyed by xbw at 2011-09-08
                #endif
                
                break;
            }

            case STATE_SENDING_STOP:
            {
                xbwprintk(7, "%s..%d..   prev_state=  STATE_SENDING_STOP, pendingEvernt=0x%lu  ====xbw[%s]====\n", \
						__FUNCTION__, __LINE__, host->pending_events, host->dma_name);

                if (!rk29_sdmmc_test_and_clear_pending(host, EVENT_CMD_COMPLETE))
                	break;

                rk29_sdmmc_command_complete(host, host->cmd);
                del_timer_sync(&host->request_timer); //delete the timer for INT_CMD_DONE int CMD12
                rk29_sdmmc_request_end(host, host->cmd);
                
                host->complete_done = 3;
                break;
            }
        	
        }

        pending_flag = (host->complete_done > 0) && (host->retryfunc<50) \
                       && (rk29_sdmmc_test_pending(host, EVENT_CMD_COMPLETE)|| rk29_sdmmc_test_pending(host, EVENT_DATA_COMPLETE) );
        if(pending_flag)
        {
            xbwprintk(7, "%s..%d...  cmd=%d(arg=0x%x),completedone=%d, retrycount=%d, doneflag=%d, \n \
                host->state=0x%x, switchstate=%x, \n \
                pendingEvent=0x%lu, completeEvents=0x%lu, \n \
                mrqCMD=%d, arg=0x%x \n ====xbw[%s]====\n",\
                
                __FUNCTION__, __LINE__,host->cmd->opcode, host->cmd->arg, host->complete_done,\
                host->retryfunc, host->mmc->doneflag,host->state, state, \
                host->pending_events,host->completed_events,\
                host->mrq->cmd->opcode, host->mrq->cmd->arg, host->dma_name);
                
            cpu_relax();
        }
                        
	} while(pending_flag && ++host->retryfunc); //while(0);

	 if(0!=stopflag)
    {
        if(host->cmd->error)
        xbwprintk(3,"%d:  call send_stop_cmd== %d,  completedone=%d, doneflag=%d, hoststate=%x, statusReg=0x%x \n", \
            __LINE__,stopflag, host->complete_done, host->mmc->doneflag, state, rk29_sdmmc_read(host->regs, SDMMC_STATUS));
            
        state = STATE_SENDING_CMD;
        send_stop_cmd(host);   //Moidfyed by xbw at 2011-09-08
    }

	host->state = state;
		 
    if(0==host->complete_done)
    {
        host->errorstep = 0xf2;
        spin_unlock_irqrestore(&host->lock, iflags);
        return;
    }
    host->errorstep = 0xf3; 
	 host->state = STATE_IDLE;
	 
	 if(host->mrq && host->mmc->doneflag)
	 {
	    host->mmc->doneflag = 0;
	    spin_unlock_irqrestore(&host->lock, iflags);
	    
	    mmc_request_done(host->mmc, host->mrq);
	 }
	 else
	 {
	    spin_unlock_irqrestore(&host->lock, iflags);
	 }
}


static inline void rk29_sdmmc_cmd_interrupt(struct rk29_sdmmc *host, u32 status)
{
    u32 multi, unit;
    
	host->cmd_status |= status;
    host->errorstep = 0xfc;
    if((MMC_STOP_TRANSMISSION != host->cmd->opcode) && (host->cmdr & SDMMC_CMD_DAT_EXP))
    {
        unit = 3*1024*1024;
        multi = rk29_sdmmc_read(host->regs, SDMMC_BYTCNT)/unit;
        multi += ((rk29_sdmmc_read(host->regs, SDMMC_BYTCNT)%unit) ? 1 :0 );
        multi = (multi>0) ? multi : 1;
        multi += (host->cmd->retries>2)?2:host->cmd->retries;
	    mod_timer(&host->DTO_timer, jiffies + msecs_to_jiffies(RK29_SDMMC_WAIT_DTO_INTERNVAL*multi));//max wait 8s larger  
	}
	
	smp_wmb();
	rk29_sdmmc_set_pending(host, EVENT_CMD_COMPLETE);
	tasklet_schedule(&host->tasklet);
}

static irqreturn_t rk29_sdmmc_interrupt(int irq, void *dev_id)
{
	struct rk29_sdmmc	*host = dev_id;
	u32			status,  pending;
	bool present;
	bool present_old;
	unsigned long iflags;

	spin_lock_irqsave(&host->lock, iflags);

    status = rk29_sdmmc_read(host->regs, SDMMC_RINTSTS);
    pending = rk29_sdmmc_read(host->regs, SDMMC_MINTSTS);// read only mask reg
    if (!pending)
    {
    	goto Exit_INT;
    }


    if(pending & SDMMC_INT_CD) 
    {
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, SDMMC_INT_CD); // clear sd detect int
    	present = rk29_sdmmc_get_cd(host->mmc);
    	present_old = test_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
  	
    	if(present != present_old)
    	{    	    
        	printk("\n******************\n%s:INT_CD=0x%x,INT-En=%d,hostState=%d,  present Old=%d ==> New=%d ==xbw[%s]==\n",\
                    __FUNCTION__, pending, host->mmc->re_initialized_flags, host->state, present_old, present,  host->dma_name);

    	    rk28_send_wakeup_key(); //wake up backlight
    	    host->error_times = 0;

    	    #if 1
    	    del_timer(&host->request_timer);
	        del_timer(&host->DTO_timer);
    	    rk29_sdmmc_dealwith_timeout(host);       	    
            #endif
        	            
    	    if(present)
    	    {
    	        set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);

    	        if(host->mmc->re_initialized_flags)
        	    {
        	        mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(RK29_SDMMC_REMOVAL_DELAY));
        	    }
        	    else
        	    {
        	        mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(RK29_SDMMC_REMOVAL_DELAY*2));
        	    }
    	    }
    	    else
    	    {
    	        clear_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
    	        host->mmc->re_initialized_flags = 0;

    	        mmc_detect_change(host->mmc, 200);
    	    }

    	}

        goto Exit_INT;

    }	


    if (pending & SDMMC_INT_CMD_DONE) {

        xbwprintk(6, "%s..%d..  CMD%d INT_CMD_DONE  INT=0x%x   ====xbw[%s]====\n", \
				__FUNCTION__, __LINE__, host->cmd->opcode,pending, host->dma_name);
        
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_CMD_DONE);  //  clear interrupt

        rk29_sdmmc_cmd_interrupt(host, status);

        goto Exit_INT;
    }

    if(pending & SDMMC_INT_SDIO) 
    {	
        xbwprintk(7, "%s..%d..  INT_SDIO  INT=0x%x   ====xbw[%s]====\n", \
				__FUNCTION__, __LINE__, pending, host->dma_name);

        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_SDIO);
        mmc_signal_sdio_irq(host->mmc);

        goto Exit_INT;
    }


    if(pending & SDMMC_INT_RTO) 
    {
    	xbwprintk(7, "%s..%d..  CMD%d CMD_ERROR_FLAGS  INT=0x%x   ====xbw[%s]====\n", \
				__FUNCTION__, __LINE__, host->cmd->opcode,pending, host->dma_name);

        //rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_RTO);
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,0xFFFFFFFF); //Modifyed by xbw at 2011-08-15
        host->cmd_status = status;
        smp_wmb();
        rk29_sdmmc_set_pending(host, EVENT_CMD_COMPLETE);

        if(!(pending & SDMMC_INT_CMD_DONE))
            tasklet_schedule(&host->tasklet);

        goto Exit_INT;
    }


    if(pending & SDMMC_INT_HLE)
    {
        printk("%s: Error due to hardware locked. Please check your hardware. INT=0x%x, CMD%d(arg=0x%x, retries=%d)==xbw[%s]==\n",\
				__FUNCTION__, pending,host->cmd->opcode, host->cmd->arg, host->cmd->retries, host->dma_name);  	      
    
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_HLE); 
        goto Exit_INT;
    }


    if(pending & SDMMC_INT_DTO) 
    {	
        xbwprintk(7, "%s..%d..  CMD%d  INT_DTO  INT=0x%x   ==xbw[%s]==\n", \
				__FUNCTION__, __LINE__,host->cmd->opcode,pending, host->dma_name);
        
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_DTO); 
        del_timer(&host->DTO_timer); //delete the timer for INT_DTO

    	host->data_status |= status;

        smp_wmb();

        rk29_sdmmc_set_pending(host, EVENT_DATA_COMPLETE);
        tasklet_schedule(&host->tasklet);
        goto Exit_INT;
    }


    if (pending & SDMMC_INT_FRUN) 
    { 
    	printk("%s: INT=0x%x Oh!My God,let me see!What happened?Why?Where? CMD%d(arg=0x%x, retries=%d) ==xbw[%s]==\n", \
				__FUNCTION__, pending, host->cmd->opcode, host->cmd->arg, host->cmd->retries,host->dma_name);
    	
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_FRUN); 
        goto Exit_INT;
    }

#if defined(CONFIG_ARCH_RK30)
    if(pending & SDMMC_INT_UNBUSY) 
    {
      //  printk("%d..%s:  ==test=== xbw======\n", __LINE__, __FUNCTION__);
       // rk29_sdmmc_regs_printk(host);
        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_UNBUSY); 
        goto Exit_INT;
    }
#endif

    if (pending & SDMMC_INT_RXDR) 
    {	
        xbwprintk(6, "%s..%d..  SDMMC_INT_RXDR  INT=0x%x   ====xbw[%s]====\n", \
				__FUNCTION__, __LINE__, pending, host->dma_name);

        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_RXDR);  //  clear interrupt
        rk29_sdmmc_do_pio_read(host);
    }

    if (pending & SDMMC_INT_TXDR) 
    {
        xbwprintk(6, "%s..%d..  SDMMC_INT_TXDR  INT=0x%x   ====xbw[%s]====\n", \
				__FUNCTION__, __LINE__, pending, host->dma_name);

        rk29_sdmmc_do_pio_write(host);       

        rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_TXDR);  //  clear interrupt
    }

Exit_INT:

	spin_unlock_irqrestore(&host->lock, iflags);
	return IRQ_HANDLED;
}

/*
 *
 * MMC card detect thread, kicked off from detect interrupt, 1 timer 
 *
 */
static void rk29_sdmmc_detect_change(unsigned long data)
{
	struct rk29_sdmmc *host = (struct rk29_sdmmc *)data;

	if(!host->mmc)
	    return;
   
	smp_rmb();

    if((RK29_CTRL_SDMMC_ID == host->pdev->id) && rk29_sdmmc_get_cd(host->mmc))
    {
        host->mmc->re_initialized_flags =1;
    }
    
	mmc_detect_change(host->mmc, 0);	

}

static void rk29_sdmmc1_check_status(unsigned long data)
{
        struct rk29_sdmmc *host = (struct rk29_sdmmc *)data;
        struct rk29_sdmmc_platform_data *pdata = host->pdev->dev.platform_data;
        unsigned int status;

    status = pdata->status(mmc_dev(host->mmc));
    
    pr_info("%s: slot status change detected(%d-%d)\n",mmc_hostname(host->mmc), host->oldstatus, status);
    
    if (status ^ host->oldstatus)
    {        
        if (status) 
        {
            rk29_sdmmc_hw_init(host);
            set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
            mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(200));
        }
        else 
        {
            clear_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
            rk29_sdmmc_detect_change((unsigned long)host);
        }
    }

    host->oldstatus = status;
}

static void rk29_sdmmc1_status_notify_cb(int card_present, void *dev_id)
{
        struct rk29_sdmmc *host = dev_id;
        //printk(KERN_INFO "%s, card_present %d\n", mmc_hostname(host->mmc), card_present);
        rk29_sdmmc1_check_status((unsigned long)host);
}


static int rk29_sdmmc_probe(struct platform_device *pdev)
{
	struct mmc_host 		*mmc;
	struct rk29_sdmmc		*host;
	struct resource			*regs;
	struct rk29_sdmmc_platform_data *pdata;
	int				irq;
	int				ret = 0;

    /* must have platform data */
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "Platform data missing\n");
		ret = -ENODEV;
		host->errorstep = 0x87;
		goto out;
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
	{
	    host->errorstep = 0x88;
		return -ENXIO;
	}

	mmc = mmc_alloc_host(sizeof(struct rk29_sdmmc), &pdev->dev);
	if (!mmc)
	{
	    host->errorstep = 0x89;
		ret = -ENOMEM;
		goto rel_regions;
	}	

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->pdev = pdev;	

	host->ctype = 0; // set default 1 bit mode
	host->errorstep = 0;
	host->bus_mode = 5;
	host->old_cmd = 100;
	host->clock =0;
	host->old_div = 0xFF;
	host->error_times = 0;
	host->state = STATE_IDLE;
	host->complete_done = 0;
	host->retryfunc = 0;
	host->mrq = NULL;
	host->new_mrq = NULL;
	
#ifdef CONFIG_PM
    host->gpio_det = pdata->detect_irq;
#endif
    host->set_iomux = pdata->set_iomux;

	if(pdata->io_init)
		pdata->io_init();
		
	spin_lock_init(&host->lock);
    
#ifdef RK29_SDMMC_LIST_QUEUE	
	INIT_LIST_HEAD(&host->queue);
#endif	

	host->clk = clk_get(&pdev->dev, "mmc");

#if RK29_SDMMC_DEFAULT_SDIO_FREQ
    clk_set_rate(host->clk,SDHC_FPP_FREQ);
#else    
    if(RK29_CTRL_SDMMC_ID== host->pdev->id)
	    clk_set_rate(host->clk,SDHC_FPP_FREQ);
	else
	    clk_set_rate(host->clk,RK29_MAX_SDIO_FREQ); 

#endif

	clk_enable(host->clk);
	clk_enable(clk_get(&pdev->dev, "hclk_mmc"));

	ret = -ENOMEM;
	host->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!host->regs)
	{
	    host->errorstep = 0x8A;
	    goto err_freemap; 
	}

    mmc->ops = &rk29_sdmmc_ops[pdev->id];
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))    
	mmc->pm_flags |= MMC_PM_IGNORE_PM_NOTIFY;
#endif	
	mmc->f_min = FOD_FREQ;
	
#if RK29_SDMMC_DEFAULT_SDIO_FREQ
    mmc->f_max = SDHC_FPP_FREQ;
#else
    if(RK29_CTRL_SDMMC_ID== host->pdev->id)
    {
        mmc->f_max = SDHC_FPP_FREQ;
    }
    else
    {
        mmc->f_max = RK29_MAX_SDIO_FREQ;
    }

#endif 
	//mmc->ocr_avail = pdata->host_ocr_avail;
	mmc->ocr_avail = MMC_VDD_27_28|MMC_VDD_28_29|MMC_VDD_29_30|MMC_VDD_30_31
                     | MMC_VDD_31_32|MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_34_35| MMC_VDD_35_36;    ///set valid volage 2.7---3.6v
	mmc->caps = pdata->host_caps;
	mmc->re_initialized_flags = 1;
	mmc->doneflag = 1;
	mmc->sdmmc_host_hw_init = rk29_sdmmc_hw_init;

    /*
	 * We can do SGIO
	*/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	mmc->max_segs = 64;
#else
	mmc->max_phys_segs = 64;
	mmc->max_hw_segs = 64; 
#endif

	/*
	 * Block size can be up to 2048 bytes, but must be a power of two.
	*/
	mmc->max_blk_size = 4095;

	/*
	 * No limit on the number of blocks transferred.
	*/
	mmc->max_blk_count = 4096; 

	/*
	 * Since we only have a 16-bit data length register, we must
	 * ensure that we don't exceed 2^16-1 bytes in a single request.
	*/
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count; //8M bytes(2K*4K)

    /*
	 * Set the maximum segment size.  Since we aren't doing DMA
	 * (yet) we are only limited by the data length register.
	*/
	mmc->max_seg_size = mmc->max_req_size;

	tasklet_init(&host->tasklet, rk29_sdmmc_tasklet_func, (unsigned long)host);

    /* Create card detect handler thread  */
	setup_timer(&host->detect_timer, rk29_sdmmc_detect_change,(unsigned long)host);
	setup_timer(&host->request_timer,rk29_sdmmc_INT_CMD_DONE_timeout,(unsigned long)host);
	setup_timer(&host->DTO_timer,rk29_sdmmc_INT_DTO_timeout,(unsigned long)host);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
	{
	    host->errorstep = 0x8B;
		ret = -EINVAL;
		goto err_freemap;
	}

    memcpy(host->dma_name, pdata->dma_name, 8);    
	host->use_dma = pdata->use_dma;

    xbwprintk(7,"%s..%s..%d..***********  Bus clock= %d Khz  ====xbw[%s]===\n",\
        __FILE__, __FUNCTION__,__LINE__,clk_get_rate(host->clk)/1000, host->dma_name);

	/*DMA init*/
	if(host->use_dma)
	{
        host->dma_info = rk29_sdmmc_dma_infos[host->pdev->id];
        ret = rk29_dma_request(host->dma_info.chn, &(host->dma_info.client), NULL); 
        if (ret < 0)
        {
        	printk("%s..%d...rk29_dma_request error=%d.===xbw[%s]====\n", \
					__FUNCTION__, __LINE__,ret, host->dma_name);
        	host->errorstep = 0x97;
            goto err_freemap; 
        }
        
#if 0  //deal with the old API of DMA-module 
		ret = rk29_dma_config(host->dma_info.chn, 4);
#else  //deal with the new API of DMA-module 
        if(RK29_CTRL_SDMMC_ID== host->pdev->id)
        {
            ret = rk29_dma_config(host->dma_info.chn, 4, 16);
        }
        else
        {
            ret = rk29_dma_config(host->dma_info.chn, 4, 1);
        }
#endif
        if(ret < 0)
		{
            printk("%s..%d..  rk29_dma_config error=%d ====xbw[%s]====\n", \
					__FUNCTION__, __LINE__, ret, host->dma_name);
            host->errorstep = 0x98;
            goto err_dmaunmap;
		}

        ret = rk29_dma_set_buffdone_fn(host->dma_info.chn, rk29_sdmmc_dma_complete);	
		if(ret < 0)
		{
            printk("%s..%d..  dma_set_buffdone_fn error=%d ====xbw[%s]====\n", \
					__FUNCTION__, __LINE__, ret, host->dma_name);
            host->errorstep = 0x99;
            goto err_dmaunmap;
		}
		
		host->dma_addr = regs->start + SDMMC_DATA;
	}

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT) || defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
	host->write_protect = pdata->write_prt;	
#endif	

    rk29_sdmmc_hw_init(host);

    ret = request_irq(irq, rk29_sdmmc_interrupt, 0, dev_name(&pdev->dev), host);
	if (ret)
	{	

	    printk("%s..%d..  request_irq error=%d ====xbw[%s]====\n", \
				__FUNCTION__, __LINE__, ret, host->dma_name);
	    host->errorstep = 0x8C;
	    goto err_dmaunmap;
	}

    /* setup sdmmc1 wifi card detect change */
    if (pdata->register_status_notify) {
        pdata->register_status_notify(rk29_sdmmc1_status_notify_cb, host);
    }

    if(RK29_CTRL_SDMMC_ID== host->pdev->id)
    {
        if(rk29_sdmmc_get_cd(host->mmc))
        {
            set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
        }
        else
        {
            clear_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
        }
    }
    else
    {
        #if defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
        if(0== host->pdev->id)
        {
            set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
        }
        #endif

        #if defined(CONFIG_USE_SDMMC1_FOR_WIFI_DEVELOP_BOARD)
        if(1== host->pdev->id)
        {
            set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
        }
        #endif
    }


    /* sdmmc1 wifi card slot status initially */
    if (pdata->status) {
        host->oldstatus = pdata->status(mmc_dev(host->mmc));
        if (host->oldstatus)  {
            set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
        }else {
            clear_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
        }
    }


	platform_set_drvdata(pdev, mmc); 	

	mmc_add_host(mmc);

#ifdef RK29_SDMMC_NOTIFY_REMOVE_INSERTION
    
    globalSDhost[pdev->id] = (struct rk29_sdmmc	*)host;
    if(0== host->pdev->id)
    {
        rk29_sdmmc_progress_add_attr(pdev);
    }
#endif	
	
#if defined (CONFIG_DEBUG_FS)
	rk29_sdmmc_init_debugfs(host);
#endif

    printk(".Line%d..The End of SDMMC-probe %s ===xbw[%s]===\n\n", __LINE__, RK29_SDMMC_VERSION,host->dma_name);
	return 0;


err_dmaunmap:
	if(host->use_dma)
	{
	    rk29_dma_free(host->dma_info.chn, &host->dma_info.client);
	}

err_freemap:
	iounmap(host->regs);

rel_regions:
    mmc_free_host(mmc);

out:
	
	return ret;
}



static int __exit rk29_sdmmc_remove(struct platform_device *pdev)
{

    struct mmc_host *mmc = platform_get_drvdata(pdev);
    struct rk29_sdmmc *host;
    struct resource		*regs;

    if (!mmc)
        return -1;

    host = mmc_priv(mmc); 
    
	smp_wmb();
    rk29_sdmmc_control_clock(host, 0);

    /* Shutdown detect IRQ and kill detect thread */
	del_timer_sync(&host->detect_timer);
	del_timer_sync(&host->request_timer);
	del_timer_sync(&host->DTO_timer);

	tasklet_disable(&host->tasklet);
	free_irq(platform_get_irq(pdev, 0), host);
	if(host->use_dma)
	{
		rk29_dma_free(host->dma_info.chn, &host->dma_info.client);
	}

	mmc_remove_host(mmc);

	iounmap(host->regs);
	
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(regs->start,resource_size(regs));  

    mmc_free_host(mmc);
    platform_set_drvdata(pdev, NULL);

	return 0;
}


#ifdef CONFIG_PM

#if defined(CONFIG_ARCH_RK29)
static irqreturn_t det_keys_isr(int irq, void *dev_id)
{
	struct rk29_sdmmc *host = dev_id;
	dev_info(&host->pdev->dev, "sd det_gpio changed(%s), send wakeup key!\n",
		gpio_get_value(RK29_PIN2_PA2)?"removed":"insert");
	rk29_sdmmc_detect_change((unsigned long)dev_id);

	return IRQ_HANDLED;
}

static int rk29_sdmmc_sdcard_suspend(struct rk29_sdmmc *host)
{
	int ret = 0;
	rk29_mux_api_set(GPIO2A2_SDMMC0DETECTN_NAME, GPIO2L_GPIO2A2);
	gpio_request(RK29_PIN2_PA2, "sd_detect");
	gpio_direction_input(RK29_PIN2_PA2);

	host->gpio_irq = gpio_to_irq(RK29_PIN2_PA2);
	ret = request_irq(host->gpio_irq, det_keys_isr,
					    (gpio_get_value(RK29_PIN2_PA2))?IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING,
					    "sd_detect",
					    host);
	
	enable_irq_wake(host->gpio_irq);

	return ret;
}
static void rk29_sdmmc_sdcard_resume(struct rk29_sdmmc *host)
{
	disable_irq_wake(host->gpio_irq);
	free_irq(host->gpio_irq,host);
	gpio_free(RK29_PIN2_PA2);
	rk29_mux_api_set(GPIO2A2_SDMMC0DETECTN_NAME, GPIO2L_SDMMC0_DETECT_N);
}

#elif defined(CONFIG_ARCH_RK30)
static irqreturn_t det_keys_isr(int irq, void *dev_id)
{
	struct rk29_sdmmc *host = dev_id;
	dev_info(&host->pdev->dev, "sd det_gpio changed(%s), send wakeup key!\n",
		gpio_get_value(RK30_PIN3_PB6)?"removed":"insert");
	rk29_sdmmc_detect_change((unsigned long)dev_id);

	return IRQ_HANDLED;
}

static int rk29_sdmmc_sdcard_suspend(struct rk29_sdmmc *host)
{
	int ret = 0;
	rk29_mux_api_set(GPIO3B6_SDMMC0DETECTN_NAME, GPIO3B_GPIO3B6);
	gpio_request(RK30_PIN3_PB6, "sd_detect");
	gpio_direction_input(RK30_PIN3_PB6);

	host->gpio_irq = gpio_to_irq(RK30_PIN3_PB6);
	ret = request_irq(host->gpio_irq, det_keys_isr,
					    (gpio_get_value(RK30_PIN3_PB6))?IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING,
					    "sd_detect",
					    host);
	
	enable_irq_wake(host->gpio_irq);

	return ret;
}
static void rk29_sdmmc_sdcard_resume(struct rk29_sdmmc *host)
{
	disable_irq_wake(host->gpio_irq);
	free_irq(host->gpio_irq,host);
	gpio_free(RK30_PIN3_PB6);
	rk29_mux_api_set(GPIO3B6_SDMMC0DETECTN_NAME, GPIO3B_SDMMC0_DETECT_N);
}

#endif


static int rk29_sdmmc_suspend(struct platform_device *pdev, pm_message_t state)
{
    struct mmc_host *mmc = platform_get_drvdata(pdev);
    struct rk29_sdmmc *host = mmc_priv(mmc);
    int ret = 0;

    if(host && host->pdev && (RK29_CTRL_SDMMC_ID == host->pdev->id)) //only the SDMMC0 have suspend-resume; noted by xbw
    {
        if (mmc)
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
            ret = mmc_suspend_host(mmc);
#else
            ret = mmc_suspend_host(mmc, state);
#endif

        if(rk29_sdmmc_sdcard_suspend(host) < 0)
			dev_info(&host->pdev->dev, "rk29_sdmmc_sdcard_suspend error\n");
    }

    return ret;
}

static int rk29_sdmmc_resume(struct platform_device *pdev)
{
    struct mmc_host *mmc = platform_get_drvdata(pdev);
    struct rk29_sdmmc *host = mmc_priv(mmc);
    int ret = 0;

    if(host && host->pdev && (RK29_CTRL_SDMMC_ID == host->pdev->id)) //only the SDMMC0 have suspend-resume; noted by xbw
    {
        if (mmc)
        {
            rk29_sdmmc_sdcard_resume(host);	
    		ret = mmc_resume_host(mmc);
    	}
	}

	return ret;
}
#else
#define rk29_sdmmc_suspend	NULL
#define rk29_sdmmc_resume	NULL
#endif

static struct platform_driver rk29_sdmmc_driver = {
	.suspend    = rk29_sdmmc_suspend,
	.resume     = rk29_sdmmc_resume,
	.remove		= __exit_p(rk29_sdmmc_remove),
	.driver		= {
		.name		= "rk29_sdmmc",
	},
};

static int __init rk29_sdmmc_init(void)
{
	return platform_driver_probe(&rk29_sdmmc_driver, rk29_sdmmc_probe);
}

static void __exit rk29_sdmmc_exit(void)
{
	platform_driver_unregister(&rk29_sdmmc_driver);
}

module_init(rk29_sdmmc_init);
module_exit(rk29_sdmmc_exit);

MODULE_DESCRIPTION("Rk29 Multimedia Card Interface driver");
MODULE_AUTHOR("xbw@rock-chips.com");
MODULE_LICENSE("GPL v2");
 
