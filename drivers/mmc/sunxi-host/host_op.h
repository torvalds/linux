/*
 * drivers/mmc/sunxi-host/host_op.h
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Aaron.Maoye <leafy.myeh@allwinnertech.com>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef _SW_HOST_OP_H_
#define _SW_HOST_OP_H_ "host_op.h"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>

#include <asm/cacheflush.h>
#include <plat/dma.h>
#include <plat/sys_config.h>

#include "host_plat.h"
#include "smc_syscall.h"

#define CARD_DETECT_BY_GPIO     (1)
#define CARD_DETECT_BY_DATA3    (2)        /* mmc detected by status of data3 */
#define CARD_ALWAYS_PRESENT     (3)        /* mmc always present, without detect pin */
#define CARD_DETECT_BY_FS		(4)		   /* mmc insert/remove by manual mode, from /proc/awsmc.x/insert node */

struct sunxi_mmc_host;
struct mmc_request;
struct sunxi_mmc_idma_des;

struct sunximmc_ctrl_regs {
	u32		gctrl;
	u32		clkc;
	u32		timeout;
	u32		buswid;
	u32		waterlvl;
	u32		funcsel;
	u32		debugc;
	u32		idmacc;
};

struct sunxi_mmc_host {
    
    struct platform_device      *pdev;
    struct mmc_host             *mmc;
    
    void __iomem	            *smc_base;          /* sdc I/O base address  */       
     
    struct resource	            *smc_base_res;      /* resources found       */
    
    /* clock management */
    struct clk                  *hclk;              //
    struct clk                  *mclk;              //
    u32                         clk_source;         // clock, source, 0-video pll, 1-ac320 pll
    
    u32                         power_on;         // power save, 0-normal, 1-power save
    u32                         power_save;         // power save, 0-normal, 1-power save
    u32                         mod_clk;            // source clock of controller
    u32                         cclk;               // requested card clock frequence
    u32                         real_cclk;          // real card clock to output
    u32                         bus_width;
    
    /* irq */
    int                         irq;                // irq number
    volatile u32				irq_flag;
    volatile u32                sdio_int;
    volatile u32                int_sum;
    
    int                         dma_no;             //dma number
    volatile u32                dodma;              //transfer with dma mode
    volatile u32                todma;
    volatile u32                dma_done;           //dma complete
    volatile u32                ahb_done;           //dma complete
    volatile u32                dataover;           //dma complete
    struct sunxi_mmc_idma_des*  pdes;
    
	u32                         pio_sgptr;
	u32                         pio_bytes;
	u32                         pio_count;
	u32                         *pio_ptr;
	u32                         pio_active;
#define XFER_NONE 0
#define XFER_READ 1
#define XFER_WRITE 2
	
    struct mmc_request	        *mrq;
    
    volatile u32                with_autostop;
    volatile u32                wait;
#define SDC_WAIT_NONE           (1<<0)
#define SDC_WAIT_CMD_DONE       (1<<1)
#define SDC_WAIT_DATA_OVER      (1<<2)
#define SDC_WAIT_AUTOCMD_DONE   (1<<3)
#define SDC_WAIT_READ_DONE      (1<<4)
#define SDC_WAIT_DMA_ERR        (1<<5)
#define SDC_WAIT_ERROR          (1<<6)
#define SDC_WAIT_FINALIZE       (1<<7)

    volatile u32                error;
    volatile u32                ferror;
    spinlock_t		            lock;
	struct tasklet_struct       tasklet;
	
    volatile u32                present;
    volatile u32                change;
    
    struct timer_list           cd_timer;
    s32                         cd_gpio;
    s32                         cd_mode;
    u32                         pio_hdle;
    u32                         read_only;
    
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry		*proc_root;
	struct proc_dir_entry		*proc_drvver;
	struct proc_dir_entry		*proc_hostinfo;
	struct proc_dir_entry		*proc_dbglevel;
	struct proc_dir_entry		*proc_regs;
	struct proc_dir_entry		*proc_insert;
#endif

	/* backup register structrue */
	struct sunximmc_ctrl_regs   bak_regs;
	user_gpio_set_t             bak_gpios[6];
	u32                         gpio_suspend_ok;
};


static __inline void eLIBs_CleanFlushDCacheRegion(void *adr, __u32 bytes)
{
	__cpuc_flush_dcache_area(adr, bytes + (1 << 5) * 2 - 2);
}

#define RESSIZE(res)        (((res)->end - (res)->start)+1)

void hexdump(char* name, char* base, int len);
void sunximmc_procfs_attach(struct sunxi_mmc_host *smc_host);
void sunximmc_procfs_remove(struct sunxi_mmc_host *smc_host);

#endif
