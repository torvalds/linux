/* drivers/staging/rk2818/rk2818_dsp/rk2818_dsp.c
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
 */


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <asm/delay.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <mach/scu.h>
#include <mach/rk2818_iomap.h>
#include <mach/irqs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/firmware.h>
#include <linux/miscdevice.h>
#include <linux/poll.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <asm/tcm.h>
#include "rk2818_dsp.h"
#include "queue.h"


#if 0
	#define dspprintk(msg...)	printk(msg);
#else
	#define dspprintk(msg...)
#endif

#define CONFIG_CHIP_RK2818 1 //the 32 kernel for rk2818

#define USE_PLL_REG		0		//是否直接控制PLL寄存器
#define ENTER_SLOW		0		//是否不使用DSP的时候立即进入SLOW MODE(DSP PLL BYPASS)
#define FIXED_REQ		560		//使用固定频率值, 0为禁用
#define CLOSE_CLK_GATE	0		//释放DSP时是否连clock gate一起关掉

struct rk28dsp_inf {
    struct miscdevice miscdev;
    struct device dev;

	void *piu_base;
	void *pmu_base;
	void *l1_dbase;
	void *l2_idbase;
	void *scu_base;
	void *regfile_base;

	int irq0;
	int irq1;

	void *bootaddress;
    void *codeprogramaddress;
	void *codetableaddress;
	void *codedataaddress;

	struct rk28dsp_msg rcvmsg;
	Queue *rcvmsg_q;

	int cur_req;
	pid_t cur_pid;
	int cur_freq;
	struct file *cur_file;
	int req_waited;

	char req1fwname[20];

	struct timer_list dsp_timer;
	int dsp_status;

	struct clk *clk;
	int clk_enabled;

	int in_suspend;
};

#define SCU_BASE_ADDR_VA		RK2818_SCU_BASE
#define REG_FILE_BASE_ADDR_VA 	RK2818_REGFILE_BASE

#define SET_BOOT_VECTOR(v)  __raw_writel(v,REG_FILE_BASE_ADDR_VA + 0x18);
#define DSP_BOOT_CTRL() __raw_writel(__raw_readl(REG_FILE_BASE_ADDR_VA + 0x14) | (1<<4),REG_FILE_BASE_ADDR_VA + 0x14);
#define DSP_BOOT_CLR()  __raw_writel(__raw_readl(REG_FILE_BASE_ADDR_VA + 0x14) & (~(1<<4)),REG_FILE_BASE_ADDR_VA + 0x14);


#define CODEC_SECTION_NO 0x5
#define DSP_MAJOR		232


/* CEVA memory map base address for ARM */
#define DSP_BASE_ADDR		RK2818_DSP_PHYS
#define DSP_L2_IMEM_BASE   (RK2818_DSP_PHYS + 0x200000)
#define DSP_L2_DMEM_BASE   (RK2818_DSP_PHYS + 0x400000)
#define SDRAM_BASE_ADDR     RK2818_SDRAM_PHYS
#define APB_SCU_BASE		0x18018000
#define APB_REG_FILE_BASE   0x18019000

#ifndef PIU_BASE_ADDR
#define PIU_BASE_ADDR      (RK2818_DSP_PHYS + 0x132000)
#endif

#ifndef PMU_BASE_ADDR
#define PMU_BASE_ADDR      (RK2818_DSP_PHYS + 0x130000)
#endif



#define PIU_PUT_IMASK(port,v) __raw_writel(v,inf->piu_base+PIU_IMASK_OFFSET);

#define PIU_GET_STATUS_REPX(channel) \
        (__raw_readl(inf->piu_base + PIU_STATUS_OFFSET) & (1 << ((channel) + PIU_STATUS_R0WRS)))

#define PIU_READ_REPX_VAL(channel) \
        __raw_readl(inf->piu_base + PIU_REPLY0_OFFSET + ((channel) << 2))

#define PIU_CLR_STATUS_REPX(channel) \
        __raw_writel(__raw_readl(inf->piu_base + PIU_STATUS_OFFSET) | (1 << ((channel) + PIU_STATUS_R0WRS)), \
        inf->piu_base+PIU_STATUS_OFFSET)

#define PIU_SEND_CMD(channel, cmd) \
        __raw_writel(cmd,inf->piu_base+PIU_CMD0_OFFSET + (channel << 2))

#define DSP_CLOCK_ENABLE()		if(!inf->clk_enabled) { clk_enable(inf->clk);  inf->clk_enabled = 1; }
#define DSP_CLOCK_DISABLE()		if(inf->clk_enabled)  { clk_disable(inf->clk); inf->clk_enabled = 0; }


typedef enum _DSP_STATUS {
    DS_NORMAL = 0,
    DS_TOSLEEP,
    DS_SLEEP,
} DSP_STATUS;

typedef enum _DSP_PWR_CTL {
    DPC_NORMAL = 0,
    DPC_SLEEP,
} DSP_PWR_CTL;


static struct rk28dsp_inf *g_inf = NULL;

static DECLARE_WAIT_QUEUE_HEAD(wq);
static int wq_condition = 1;

static DECLARE_WAIT_QUEUE_HEAD(wq2);
static int wq2_condition = 0;

static DECLARE_WAIT_QUEUE_HEAD(wq3);
static int wq3_condition = 0;

static DECLARE_MUTEX(sem);

static int rcv_quit = 0;



void dsp_powerctl(int ctl, int arg);

static int video_type = 0;//h264:1 ,rv40:2 , other: 0.

//need reset reg value when finishing the video play
static void resetRegValueForVideo()
{
#ifdef CONFIG_CHIP_RK2818
	struct rk28dsp_inf *inf = g_inf;
	if(!inf)      return;
	//disable the AXI bus
	//__raw_writel((__raw_readl(r1+0x20) & (~0x08000000)) , r1+0x20);
	//
	//__raw_writel((__raw_readl(r3+0x14) & (~0x20002000)) , r3+0x14);
	if(video_type)
	{
		__raw_writel((__raw_readl(inf->scu_base+0x20) | (1<<25)) , inf->scu_base+0x20);

		if(video_type == 2)
		{
			//mdelay(10);
			__raw_writel((__raw_readl(inf->scu_base+0x1c) | (0x400)) , inf->scu_base+0x1c);
			dspprintk("close rv40 hardware advice\n");
		}
		else
		{
			__raw_writel((__raw_readl(inf->scu_base+0x20) | (1<<20)) , inf->scu_base+0x20);
			dspprintk("close h264 hardware advice\n");
		}
		video_type = 0;
	}
#endif
  	return;
}
//add by Charles Chen for test 281x play RMVB
static void setRegValueForVideo(unsigned long type)
{
#ifdef CONFIG_CHIP_RK2818
	/*
		0x18018020 或上 0x08000000
		0x18018028  先或上 0x00000110
		等待一些时间再与上     ～0x00000110
		0x18019018 或上 0x00200000
	*/
	struct rk28dsp_inf *inf = g_inf;
	if(!inf)      return;

	video_type = 1;
	//axi bus
	__raw_writel((__raw_readl(inf->scu_base+0x20) | (0x08000000)) , inf->scu_base+0x20);

	//mc dma
	__raw_writel((__raw_readl(inf->scu_base+0x20) & ~(1<<25)) , inf->scu_base+0x20);
	//printk("------->0x18018020 value 0x%08x\n",__raw_readl(r1+0x20));
	if(!type)
	{
		video_type ++;
		//rv deblocking clock
		__raw_writel((__raw_readl(inf->scu_base+0x1c) & (~0x400)) , inf->scu_base+0x1c);
		mdelay(1);

		__raw_writel((__raw_readl(inf->scu_base+0x28) | (0x00000100)) , inf->scu_base+0x28);

		mdelay(5);

		__raw_writel((__raw_readl(inf->scu_base+0x28) & (~0x00000100)) , inf->scu_base+0x28);

		//rv deblocking bridge select
		__raw_writel((__raw_readl(inf->regfile_base+0x14) | (0x20002000)) , inf->regfile_base+0x14);


		dspprintk("%s this is rm 9 video\n",__func__);
	}
	else
	{
		//h264 hardware
		__raw_writel((__raw_readl(inf->scu_base+0x20) & ~(1<<20)) , inf->scu_base+0x20);
		dspprintk("%s this is h264 video\n",__func__);
	}

#endif
    return;
}

static int CheckDSPLIBHead(char *buff)
{
    if ((buff[0] != 'r')
		|| (buff[1] != 'k')
		|| (buff[2] != 'd')
		|| (buff[3] != 's')
		|| (buff[4] != 'p')
		|| (buff[5] != ' ')
		|| (buff[6] != 'X')
		|| (buff[7] != 'X'))
	{
        return -1;
	}
    else
    {
        return 0;
    }
}

void dsp_set_clk(int clkrate)
{
#if USE_PLL_REG
	//old: 0x01830310 300mhz  0x01820310 400mhz  0x01810290 500mhz  0x01982300 560mhz  0x01982580 600mhz
	//0x00030310 300mhz  0x00020310 400mhz  0x00010290 500mhz  0x00182300 560mhz  0x00182580 600mhz
	unsigned int freqreg = 0;
	int cnt = 0;

	if(0==clkrate) {
		/* pll set 300M */
		freqreg = ( __raw_readl(SCU_BASE_ADDR_VA+0x04)&(~0x003FFFFE) ) | 0x00030310;
    	__raw_writel(freqreg, SCU_BASE_ADDR_VA+0x04);
		udelay(10);

        /* dsp pll disable */
        __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x04) | (0x01u<<22)) , SCU_BASE_ADDR_VA+0x04);
        udelay(10);
		return;
	}

	if(clkrate) {
		clkrate = FIXED_REQ ? FIXED_REQ : clkrate;
	}

	/* dsp pll enable */
	__raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x04) & (~(0x01u<<22))) , SCU_BASE_ADDR_VA+0x04);
	udelay(300);		//0.3ms

	/* dsp set clk */
	switch(clkrate) {
	    case 300:   freqreg = 0x00030310;   break;
	    case 400:   freqreg = 0x00020310;   break;
	    case 560:   freqreg = 0x00182300;   break;
	    case 600:   freqreg = 0x00182580;   break;
	    case 500:
	    default:    freqreg = 0x00010290;   break;
    }
	freqreg = ( __raw_readl(SCU_BASE_ADDR_VA+0x04)&(~0x003FFFFE) ) | freqreg;
    __raw_writel(freqreg, SCU_BASE_ADDR_VA+0x04);
    mdelay(15);

	/* wait dsp pll lock */
	while(!(__raw_readl(REG_FILE_BASE_ADDR_VA) & 0x00000100)) {
		if(cnt++>30) {
			printk("wait dsp pll lock ... \n");
			/* dsp pll disable */
    		__raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x04) | (0x01u<<22)) , SCU_BASE_ADDR_VA+0x04);
			udelay(300);
			/* dsp pll enable */
    		__raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x04) & (~(0x01u<<22))) , SCU_BASE_ADDR_VA+0x04);
			udelay(300);
			cnt = 0;
		}
		udelay(10);
	}
#else
    struct rk28dsp_inf *inf = g_inf;
	if(clkrate) {
		clkrate = FIXED_REQ ? FIXED_REQ : clkrate;
	}
    if(inf) {
		if(inf->clk)	clk_set_rate(inf->clk, clkrate*1000000);
	}
#endif
}

#ifdef CONFIG_CHIP_RK2818
static void __tcmfunc dsp_open_power( void )
{
        unsigned long flags;
        dspprintk("enter %s!!\n",__func__);
        local_irq_save(flags);
        __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0xc) &(~0xc)) , SCU_BASE_ADDR_VA+0xc);
        ddr_pll_delay(1);	//开之前也得加,避免总线还在访问
        __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x10) & (~0x21)) , SCU_BASE_ADDR_VA+0x10);
        ddr_pll_delay(24);	//关中断时间不能太长 (6000大概为1us)
        __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0xc) |(1<<2)) , SCU_BASE_ADDR_VA+0xc);
        local_irq_restore(flags);
        dspprintk("exit %s!!\n",__func__);
}
#endif

void dsp_powerctl(int ctl, int arg)
{
    struct rk28dsp_inf *inf = g_inf;
    if(!inf)      return;

    switch(ctl)
    {
    case DPC_NORMAL:
        {
#ifdef CONFIG_CHIP_RK2818	//core电压不稳时,dsp上电会导致AHB取指错误,所以dsp上电后到稳定期间不操作AHB
            /* dsp subsys power on 0x21*/
            dsp_open_power();
            mdelay(10);
#else
            /* dsp subsys power on 0x21*/
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x10) & (~0x21)) , SCU_BASE_ADDR_VA+0x10);
            mdelay(15);
#endif
            /* dsp clock enable 0x12*/
			DSP_CLOCK_ENABLE();

            /* dsp core & peripheral rst */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x28) | 0x02000030) , SCU_BASE_ADDR_VA+0x28);

			/* dsp set clk */
            dsp_set_clk(arg);
			mdelay(5);

            /* dsp peripheral urst */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x28) & (~0x02000020)) , SCU_BASE_ADDR_VA+0x28);
            mdelay(1);

            /* dsp ahb bus clock enable*/
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x24) & (~0x04)) , SCU_BASE_ADDR_VA+0x24);

#ifdef CONFIG_CHIP_RK2818
            /* dsp master interface bridge clock enable */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x20) & (~0x40000000)) , SCU_BASE_ADDR_VA+0x20);
            /* dsp slave interface bridge clock enable */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x20) & (~0x20000000)) , SCU_BASE_ADDR_VA+0x20);
			/* dsp timer clock enable */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x20) & (~0x10000000)) , SCU_BASE_ADDR_VA+0x20);
#endif

            /* sram arm clock enable */
            //__raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x1c) & (~0x08)) , SCU_BASE_ADDR_VA+0x1c);
            /* sram dsp clock enable */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x1c) & (~0x10)) , SCU_BASE_ADDR_VA+0x1c);

			/* set CXCLK, XHCLK, XPCLK */
			__raw_writel(0, inf->pmu_base+0x08);	//CXCLK_DIV (clki:ceva)
			__raw_writel(2, inf->pmu_base+0x0c);	//XHCLK_DIV (ceva:hclk)
			__raw_writel(1, inf->pmu_base+0x10);	//XPCLK_DIV (hclk:pclk)

        }
        break;

    case DPC_SLEEP:
        {
            /* dsp work mode :slow mode*/
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x0c) & (~0x03)) , SCU_BASE_ADDR_VA+0x0c);
            /* dsp core/peripheral rest*/
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x28) | 0x02000030) , SCU_BASE_ADDR_VA+0x28);

            /* dsp ahb bus clock disable */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x24) | (0x04)) , SCU_BASE_ADDR_VA+0x24);
#ifdef CONFIG_CHIP_RK2818
            /* dsp master interface bridge clock disable */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x20) | (0x40000000)) , SCU_BASE_ADDR_VA+0x20);
            /* dsp slave interface bridge clock disable */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x20) | (0x20000000)) , SCU_BASE_ADDR_VA+0x20);
			/* dsp timer clock disable */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x20) | (0x10000000)) , SCU_BASE_ADDR_VA+0x20);
#endif

            /* sram arm clock disable */
            //__raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x1c) | (0x08)) , SCU_BASE_ADDR_VA+0x1c);
            /* sram dsp clock disable */
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x1c) | (0x10)) , SCU_BASE_ADDR_VA+0x1c);
            udelay(10);
            /* dsp clock disable */
            DSP_CLOCK_DISABLE();

            /* dsp pll close */
            dsp_set_clk(0);

            /* dsp subsys power off 0x21*/
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x10) | (0x21)) , SCU_BASE_ADDR_VA+0x10);

			/* close rv/h264 hardware advice after dsp has closed */
			udelay(10);
			resetRegValueForVideo();
        }
        break;
    default:
        break;
    }
}


static int _down_firmware(char *fwname, struct rk28dsp_inf *inf)
{
	static char lst_fwname[20] = {0};
	int ret = 0;

	if(NULL==fwname)	        return -EINVAL;
	if(0==strcmp(fwname, ""))	return -EINVAL;
	if((0==strcmp(lst_fwname, fwname)) && (DS_SLEEP!=inf->dsp_status)) {
	    if(1==inf->cur_req) {
	        dspprintk("%s already down, not redown! \n", fwname);

#if CLOSE_CLK_GATE
			/* sram dsp clock enable */
			__raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x1c) & (~0x10)) , SCU_BASE_ADDR_VA+0x1c);
			/* dsp ahb bus clock enable*/
			__raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x24) & (~0x04)) , SCU_BASE_ADDR_VA+0x24);
			/* dsp clock enable 0x12*/
			DSP_CLOCK_ENABLE();
#endif
	        /* change dsp & arm to normal mode */
	        inf->dsp_status = DS_NORMAL;
	        __raw_writel(0x5, SCU_BASE_ADDR_VA+0x0c);
            return 0;
	    }
    }

    if(DS_SLEEP==inf->dsp_status)    dspprintk("dsp : sleep -> normal \n");
    inf->dsp_status = DS_NORMAL;

    dspprintk("down firmware (%s) ... \n", fwname);
	{
		if(0==strcmp(fwname,"rk28_rv40.rkl"))
		{
			setRegValueForVideo(0);
		}
		else if((0 == strcmp(fwname,"rk28_h264.rkl"))||(0 == strcmp(fwname,"rk28_h264_db.rkl")))
		{
			setRegValueForVideo(1);
		}
	}
	{
		const struct firmware *fw;
		char *buf,*code_buf;
		int *pfile,*pboot;
		int indexoffset,dataOffset;
		int *pcodeprograml1,*pcodedatal1,*pcodetable;
		int address,length,indexNo,loadNo,i,j;
		int *fpIndexFile,*fpDataFile;

        __raw_writel((__raw_readl(REG_FILE_BASE_ADDR_VA + 0x10) | (0x6d8)), REG_FILE_BASE_ADDR_VA + 0x10);  // 0x6d8
        dsp_powerctl(DPC_NORMAL, inf->cur_freq);

		/* down dsp boot */
		dspprintk("request_firmware ... \n");
		ret = request_firmware(&fw, "DspBoot.rkl", &inf->dev);
	    if (ret) {
	    	printk(KERN_ERR "Failed to load boot image \"DspBoot.rkl\" err %d\n",ret);
	    	return -ENOMEM;
	    }
		buf = (char*)fw->data;
		if(CheckDSPLIBHead(buf) != 0){
	    	printk("dsp boot head failed ! \n");
            return -ENOMEM;
        }
		pboot = (int*)inf->bootaddress;
	    indexoffset = *(buf+8+32);
	    dataOffset = *(buf+8+32+4);
	    pfile = (int *)(buf+dataOffset);
		memcpy((char *)(pboot+16),(char *)(pfile),2000); /*copy boot to *pboot point*/
	    SET_BOOT_VECTOR(__pa(pboot+16));  /*set dsp boot program address*/
	    dspprintk("%s [%d]--%x\n",__FUNCTION__,__LINE__,__raw_readl(REG_FILE_BASE_ADDR_VA + 0x18));
	    release_firmware(fw);

		/* down dsp codec */
		pcodeprograml1 = (int*)inf->codeprogramaddress;
		pcodedatal1 = (int*)inf->codedataaddress;
		pcodetable = (int*)inf->codetableaddress;
		*(pboot+13) = __pa(pcodetable); /*set decode table address */
		*(pboot+14) = __pa(pcodeprograml1); /*set decode program address */
		*(pboot+15) = __pa(pcodedatal1); /*set decode data address */
		ret = request_firmware(&fw, fwname, &inf->dev);
		if (ret) {
			printk(KERN_ERR "Failed to load image \"%s\" err %d\n",fwname, ret);
			return ret;
		}
		code_buf = (char*)fw->data;
		if(CheckDSPLIBHead(code_buf) != 0){
			printk("dsp code head failed ! \n");
        	return -ENOMEM;
		}

		loadNo = CODEC_SECTION_NO;
		i=8+16;
		while ((loadNo--) != 0x0)
    	{
    		indexoffset = *((int *)(code_buf + i));
    		dataOffset = *((int *)(code_buf + i + 4));
    		//dspprintk("%s [%d]-- indexoffset=0x%x  dataOffset=0x%x  loadNo=%d \n",__FUNCTION__,__LINE__,indexoffset,dataOffset,loadNo);
    		i = i + 8;
    		if ((indexoffset != 0x0) && (dataOffset != 0x0))
        	{
        		fpIndexFile = (int *)(code_buf + indexoffset);
        		fpDataFile = (int *)(code_buf + dataOffset);
        		indexNo = *(fpIndexFile + 1);
        		j = 0;
                while ((indexNo--) != 0x0)
                {
                    if(indexNo<0)   break;
                    address = *(fpIndexFile + 2 + j*2);
                    length = *(fpIndexFile + 2 + 1 + j*2);
                    j = j + 1;
                    //dspprintk("%s [%d]-- address=0x%x  length =0x%x  indexNo=0x%x\n",__FUNCTION__,__LINE__,address,length,indexNo);
                    if(loadNo == (CODEC_SECTION_NO - 0x1)){
                        /* 如果为L1 data MEM的代码段，拷贝到固件的位置 */
                        memcpy((char *)(pcodeprograml1),(char *)(fpDataFile),length);
                    } else {
                        if (loadNo == (CODEC_SECTION_NO - 0x2)) {
                             /* 如果为L1 data MEM的数据段，拷贝到固件的位置 */
                            memcpy((char *)(pcodedatal1),(char *)(fpDataFile),length);
                        } else {
                            /* 如果为CEVA的内存区域，需要进行地址转换 */
                            if (address < SDRAM_BASE_ADDR) {
#if 1
                                int     k;
                                int    *buffL2;
                                address = (unsigned int)inf->l2_idbase + (address - 0x200000);
                                buffL2 = (int *)address;
                                for (k=0; k<(length >> 2); k++)
                                {
                                    buffL2[k] = fpDataFile[k];
                                }
                                fpDataFile = (int *)((int)fpDataFile + length);
#else
                                address = (unsigned int)inf->l2_idbase + (address - 0x200000);
                                memcpy((char *)(address), (char *)fpDataFile, length);
#endif
                            } else {
                                /* 如果为sdram中，则只能码表，且地址无关，
                                       并且只能有一段(即地址连续), 否则引起系统崩溃
                                       在退出视频或者加载不成功后，记得把此Buff释放掉 */
                                memcpy((char *)pcodetable, (char *)fpDataFile, length);
                            }
                        }
                    }

                }
        	}
    	}
		release_firmware(fw);
	}

	DSP_BOOT_CTRL();
    mdelay(10);

#if ENTER_SLOW
    /* dsp work mode :slow mode*/
    __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x0c) & (~0x03)) , SCU_BASE_ADDR_VA+0x0c);
    mdelay(1);
#endif

	/* dsp core urst*/
    __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x28) & (~0x00000010)) , SCU_BASE_ADDR_VA+0x28);
	mdelay(1);

    /* change dsp & arm to normal mode */
    __raw_writel(0x5, SCU_BASE_ADDR_VA+0x0c);

	strcpy(lst_fwname, fwname);

	return 0;
}


void dsptimer_callback(unsigned long arg)
{
    struct rk28dsp_inf *inf = g_inf;
    if(!inf)      return;

    switch(inf->dsp_status)
    {
    case DS_NORMAL:
		break;
    case DS_TOSLEEP:
		dsp_powerctl(DPC_SLEEP, 0);
      	inf->dsp_status = DS_SLEEP;
     	dspprintk("dsp : normal -> sleep \n");
        break;
    case DS_SLEEP:
        break;
    default:
        inf->dsp_status = DS_NORMAL;
        break;
    }
}


static int dsp_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct rk28dsp_inf *inf;
    unsigned long vma_size =  vma->vm_end - vma->vm_start;
	unsigned long pageFrameNo = 0;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;

    if(!g_inf)   return -EAGAIN;
    inf = g_inf;

	switch(vma_size)
	{
	case 0x10000:	//l1_dbase
	    offset += DSP_BASE_ADDR;
		pageFrameNo = (offset >> PAGE_SHIFT);
		//dspprintk("dsp_mmap l1_dbase \n");
		break;
	case 0x400000:	//l2_idbase
	    offset += DSP_L2_IMEM_BASE;
		pageFrameNo = (offset >> PAGE_SHIFT);
		//dspprintk("dsp_mmap l2_idbase \n");
		break;
    case 0x600000:  //dsp_base
        offset += DSP_BASE_ADDR;
        pageFrameNo = (offset >> PAGE_SHIFT);
		//dspprintk("dsp_mmap dsp_base \n");
		break;
	default:        //pmu_base (0x3000)
	    offset += PMU_BASE_ADDR;
	    pageFrameNo = (offset >> PAGE_SHIFT);
		//dspprintk("dsp_mmap pmu_base \n");
		break;
	}
	vma->vm_pgoff = pageFrameNo;
	vma->vm_flags |= VM_IO|VM_RESERVED|VM_LOCKED;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff, vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		printk("remap_pfn_range fail(vma_size=0x%08x)\n", (unsigned int)vma_size);
		return -EAGAIN;
	}
    return 0;
}


static long dsp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct rk28dsp_req req;
	struct rk28dsp_inf *inf;

	if(!g_inf)   return -EAGAIN;
    inf = g_inf;

	if(DSP_IOCTL_RES_REQUEST!=cmd && DSP_IOCTL_GET_TABLE_PHY!=cmd ) {
		down(&sem);
		if(inf->cur_pid!=current->tgid || inf->cur_file!=file) {
		    dspprintk("res is obtain by pid %d(cur_file=0x%08x), refuse this req(pid=%d file=0x%08x cmd=0x%08x) \n",
		        inf->cur_pid, (u32)inf->cur_file, current->tgid, (u32)file, cmd);
			up(&sem);
		    return -EBUSY;
		}
		up(&sem);
	}

	switch(cmd)
	{
	case DSP_IOCTL_RES_REQUEST:
		if(copy_from_user(&req, (void *)arg, sizeof(struct rk28dsp_req)))
			return -EFAULT;
		dspprintk("DSP_IOCTL_RES_REQUEST reqno=%d(%d->%d)  cur_req=%d(%d) \n",
		        req.reqno, current->tgid, current->pid, inf->cur_req, inf->cur_pid);

		if(0==req.reqno)	return -EINVAL;
		if(0==strcmp(req.fwname, ""))   return -EINVAL;

		down(&sem);
		if(0==inf->cur_req && !inf->req_waited)
		{
			inf->cur_req = req.reqno;
			inf->cur_pid = current->tgid;
			inf->cur_freq = req.freq;
			inf->cur_file = file;
			if(inf->cur_freq<24 || inf->cur_freq>600)	inf->cur_freq = 500;
			if(1==req.reqno)	strcpy(inf->req1fwname, req.fwname);
		}
		else if(1==inf->cur_req && !inf->req_waited && inf->cur_req!=req.reqno)
		{
			inf->req_waited = 1;
			wq_condition = 0;
			up(&sem);
			dspprintk("wait_event \n");
			wait_event(wq, wq_condition);
			down(&sem);
			inf->req_waited = 0;
			inf->cur_req = req.reqno;
			inf->cur_pid = current->tgid;
			inf->cur_freq = req.freq;
			inf->cur_file = file;
			if(inf->cur_freq<24 || inf->cur_freq>600)	inf->cur_freq = 500;
		} else {
		    ret = -EBUSY;
		}

		if(0==ret) {
			del_timer(&inf->dsp_timer);
		    _down_firmware(req.fwname, inf);
		    queue_flush(inf->rcvmsg_q);
		    rcv_quit = 0;
			wq2_condition = 0;
		}
		up(&sem);
		break;

	case DSP_IOCTL_RES_RELEASE:
	    dspprintk("DSP_IOCTL_RES_RELEASE cur_req = %d \n",inf->cur_req);
	    down(&sem);
		if(inf->cur_req) {
			if(1==inf->cur_req) {
				if(1==arg)	strcpy(inf->req1fwname, "");
				wq_condition = 1;
				wake_up(&wq);
			} else {
				if(strcmp(inf->req1fwname, "")) {
					//_down_firmware(inf->req1fwname, inf);
				}
			}

			inf->cur_req = 0;
			inf->cur_pid = 0;
			inf->cur_file = NULL;

#if ENTER_SLOW
			/* dsp work mode :slow mode*/
            __raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x0c) & (~0x03)) , SCU_BASE_ADDR_VA+0x0c);
#endif

#if CLOSE_CLK_GATE
			/* dsp clock disable */
			DSP_CLOCK_DISABLE();
			/* dsp ahb bus clock disable */
			__raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x24) | (0x04)) , SCU_BASE_ADDR_VA+0x24);
			/* sram dsp clock disable */
			__raw_writel((__raw_readl(SCU_BASE_ADDR_VA+0x1c) | (0x10)) , SCU_BASE_ADDR_VA+0x1c);
#endif

			if(DS_SLEEP!=inf->dsp_status) {
      	        mod_timer(&inf->dsp_timer, jiffies + 5*HZ);
				inf->dsp_status = DS_TOSLEEP;
    	    }
		}

        /* force DSP_IOCTL_RECV_MSG return */
	    rcv_quit = 1;
        wq2_condition = 1;
        wake_up(&wq2);

        up(&sem);
		dspprintk("\n");
		break;

    case DSP_IOCTL_SEND_MSG:
        //dspprintk("DSP_IOCTL_SEND_MSG \n");
        {
            struct rk28dsp_msg msg;
            if(copy_from_user(&msg, (void *)arg, sizeof(struct rk28dsp_msg)))
                return -EFAULT;

            if(CODEC_MSG_ICU_CHANNEL==msg.channel) {
               __raw_writel((__raw_readl(inf->pmu_base+0x043C) | 0x100), inf->pmu_base+0x043C);
            } else {
                PIU_SEND_CMD(msg.channel, msg.cmd);
            }
        }
        break;

    case DSP_IOCTL_RECV_MSG:
        {
            struct rk28dsp_msg *pmsg = NULL;
            struct rk28dsp_msg msg;
            if(NULL==inf->rcvmsg_q)     return -EFAULT;

            while(NULL==pmsg)
            {
                pmsg = queue_get(inf->rcvmsg_q);
                if(pmsg)    break;

                if(copy_from_user(&msg, (void *)arg, sizeof(struct rk28dsp_msg)))   return -EFAULT;
                if(0==msg.rcv_timeout)      return -EAGAIN;

                if(-1==msg.rcv_timeout) {
                    if(rcv_quit)    return -EAGAIN;
                    wait_event(wq2, wq2_condition);
                    wq2_condition = 0;
                    if(rcv_quit)    return -EAGAIN;
                } else {
                    if(rcv_quit)    return -EAGAIN;
                    if(!wait_event_timeout(wq2, wq2_condition, msecs_to_jiffies(msg.rcv_timeout)))   { wq2_condition = 0; return -EAGAIN; }
                    wq2_condition = 0;
                    if(rcv_quit)    return -EAGAIN;
                }
            }

            if(pmsg) {
                if(copy_to_user((void __user *)arg, pmsg, sizeof(struct rk28dsp_msg)))  ret = -EFAULT;
                kfree(pmsg);
            } else {
                ret = -EFAULT;
            }
        }
        break;

	case DSP_IOCTL_SET_FREQ:
		dspprintk("DSP_IOCTL_SET_FREQ: %dM!\n", (int)arg);
		{
			dsp_set_clk((int)arg);
			inf->cur_freq = (int)arg;
			if(inf->cur_freq<24 || inf->cur_freq>600)	inf->cur_freq = 500;
        }
        break;

    case DSP_IOCTL_GET_TABLE_PHY:
        {
            unsigned int table_phy = __pa(inf->codetableaddress);
            if(copy_to_user((void __user *)arg, (void*)&table_phy, 4))  ret = -EFAULT;
        }
        break;

	default:
		break;
	}

    return ret;
}


static int dsp_open(struct inode *inode, struct file *file)
{
    return 0;
}


static int dsp_release(struct inode *inode, struct file *file)
{
	dsp_ioctl(file, DSP_IOCTL_RES_RELEASE, 1);
    return 0;
}

static irqreturn_t rk28_dsp_irq(int irq, void *dev_id)
{
	struct platform_device *pdev = (struct platform_device*)dev_id;
    struct rk28dsp_inf *inf = platform_get_drvdata(pdev);
    struct rk28dsp_msg *pmsg = NULL;
    unsigned int cmd = 0;

	if(NULL==inf || NULL==inf->rcvmsg_q)		return IRQ_HANDLED;

    if(IRQ_NR_PIUCMD == irq)
    {
        if (PIU_GET_STATUS_REPX(CODEC_OUTPUT_PIU_CHANNEL)) {
            cmd = PIU_READ_REPX_VAL(CODEC_OUTPUT_PIU_CHANNEL);
            if(inf->cur_pid && !queue_is_full(inf->rcvmsg_q)) {
                pmsg = kmalloc(sizeof(struct rk28dsp_msg), GFP_ATOMIC);
                if(pmsg) {
                    pmsg->channel = CODEC_OUTPUT_PIU_CHANNEL;
                    pmsg->cmd = cmd;
                    pmsg->rcv_timeout = 0;
                    if(!queue_put(inf->rcvmsg_q, pmsg, sizeof(struct rk28dsp_msg))) {
                        wq2_condition = 1;
                        wake_up(&wq2);
                    } else {
                        printk("queue_put fail! \n");
                    }
                } else {
                    printk("kmalloc fail! \n");
                }
            }
            PIU_CLR_STATUS_REPX(CODEC_OUTPUT_PIU_CHANNEL);  // clear status.
        }

        if (PIU_GET_STATUS_REPX(CODEC_MSG_PIU_CHANNEL)) {
            cmd = PIU_READ_REPX_VAL(CODEC_MSG_PIU_CHANNEL);
            if(inf->cur_pid && !queue_is_full(inf->rcvmsg_q)) {
                pmsg = kmalloc(sizeof(struct rk28dsp_msg), GFP_ATOMIC);
                if(pmsg) {
                    pmsg->channel = CODEC_MSG_PIU_CHANNEL;
                    pmsg->cmd = cmd;
                    pmsg->rcv_timeout = 0;
                    if(!queue_put(inf->rcvmsg_q, pmsg, sizeof(struct rk28dsp_msg))) {
                        wq2_condition = 1;
                        wake_up(&wq2);
                    } else {
                        printk("queue_put fail! \n");
                    }
                } else {
                    printk("kmalloc fail! \n");
                }
            }
            PIU_CLR_STATUS_REPX(CODEC_MSG_PIU_CHANNEL);  // clear status.
        }

        if (PIU_GET_STATUS_REPX(CODEC_MSG_PIU_NEXT_CHANNEL)) {
            cmd = PIU_READ_REPX_VAL(CODEC_MSG_PIU_NEXT_CHANNEL);
            if(inf->cur_pid && !queue_is_full(inf->rcvmsg_q)) {
                pmsg = kmalloc(sizeof(struct rk28dsp_msg), GFP_ATOMIC);
                if(pmsg) {
                    pmsg->channel = CODEC_MSG_PIU_NEXT_CHANNEL;
                    pmsg->cmd = cmd;
                    pmsg->rcv_timeout = 0;
                    if(!queue_put(inf->rcvmsg_q, pmsg, sizeof(struct rk28dsp_msg))) {
                        wq2_condition = 1;
                        wake_up(&wq2);
                    } else {
                        printk("queue_put fail! \n");
                    }
                } else {
                    printk("kmalloc fail! \n");
                }
            }
            PIU_CLR_STATUS_REPX(CODEC_MSG_PIU_NEXT_CHANNEL);    // clear status.
        }
    }


    if(IRQ_NR_DSPSWI == irq)
    {
        __raw_writel((__raw_readl(inf->pmu_base+0x2c10) & (~0x40)), inf->pmu_base+0x2c10);    // clear status.
        if(inf->cur_pid && !queue_is_full(inf->rcvmsg_q)) {
            pmsg = kmalloc(sizeof(struct rk28dsp_msg), GFP_ATOMIC);
            if(pmsg) {
                pmsg->channel = CODEC_MSG_ICU_CHANNEL;
                pmsg->cmd = 0;
                pmsg->rcv_timeout = 0;
                if(!queue_put(inf->rcvmsg_q, pmsg, sizeof(struct rk28dsp_msg))) {
                    wq2_condition = 1;
                    wake_up(&wq2);
                } else {
                    printk("queue_put fail! \n");
                }
            } else {
                printk("kmalloc fail! \n");
            }
        }
    }

    return IRQ_HANDLED;
}


struct file_operations dsp_fops = {
    .open           = dsp_open,
	.release        = dsp_release,
	.mmap           = dsp_mmap,
	.unlocked_ioctl = dsp_ioctl,
};

static struct miscdevice dsp_dev ={
    .minor = DSP_MAJOR,
    .name = "rk28-dsp",
    .fops = &dsp_fops,
};

void destruct(void *param, void *elem)
{
    if(elem)    kfree(elem);
}


static int __init dsp_drv_probe(struct platform_device *pdev)
{
	struct rk28dsp_inf *inf;
	int ret = 0;

	inf = kmalloc(sizeof(struct rk28dsp_inf), GFP_KERNEL);
	if(NULL==inf) { ret = -ENOMEM;  goto alloc_fail; }
	memset(inf, 0, sizeof(struct rk28dsp_inf));

	inf->clk = clk_get(NULL, "dsp_pll");
	if(inf->clk) 	DSP_CLOCK_ENABLE();

	inf->piu_base = (void*)ioremap(PIU_BASE_ADDR, 0x70);
	inf->pmu_base = (void*)ioremap(PMU_BASE_ADDR, 0x3000);
	inf->l1_dbase = (void*)ioremap(DSP_BASE_ADDR, 0x10000);
	inf->l2_idbase = (void*)ioremap(DSP_L2_IMEM_BASE, 0x400000);
	inf->scu_base = (void *)ioremap(APB_SCU_BASE, 0x60);
	inf->regfile_base = (void *)ioremap(APB_REG_FILE_BASE, 0x60);

	inf->irq0 = pdev->resource[1].start;
	inf->irq1 = pdev->resource[2].start;

	inf->bootaddress = (void*)__get_free_page(GFP_DMA);
	inf->codeprogramaddress = (void*)__get_free_pages(GFP_DMA,4);
	inf->codedataaddress = (void*)__get_free_pages(GFP_DMA,4);
	inf->codetableaddress = (void*)__get_free_pages(GFP_DMA,6);
	if(0==inf->bootaddress)			{ ret = -ENOMEM; printk("alloc bootaddress fail!\n"); goto alloc2_fail; }
	if(0==inf->codeprogramaddress)	{ ret = -ENOMEM; printk("alloc codeprogramaddress fail!\n");   goto alloc2_fail; }
	if(0==inf->codedataaddress)		{ ret = -ENOMEM; printk("alloc codedataaddress fail!\n");   goto alloc2_fail; }
	if(0==inf->codetableaddress)	{ ret = -ENOMEM; printk("alloc codetableaddress fail!\n");   goto alloc2_fail; }

	platform_set_drvdata(pdev, inf);
	inf->dev = pdev->dev;

	inf->rcvmsg_q = queue_init(256, destruct, NULL);

	ret = request_irq(inf->irq0, rk28_dsp_irq, IRQF_SHARED, "rk28-dsp", pdev);
    if (ret < 0) {
		goto irq0_fail;
	}

	ret = request_irq(inf->irq1, rk28_dsp_irq, IRQF_SHARED, "rk28-dsp", pdev);
    if (ret < 0) {
		goto irq1_fail;
	}

	init_MUTEX(&sem);

    init_timer(&inf->dsp_timer);
    inf->dsp_timer.function = dsptimer_callback;
    inf->dsp_timer.expires = jiffies + 5*HZ;
    add_timer(&inf->dsp_timer);

	inf->miscdev = dsp_dev;
	ret = misc_register(&(inf->miscdev));
	if(ret) {
		goto reg_fail;
	}

	g_inf = inf;

	dsp_powerctl(DPC_SLEEP, 0);
    inf->dsp_status = DS_SLEEP;

    return 0;

reg_fail:
	if(inf->irq1)	free_irq(inf->irq1, &inf->miscdev);
irq1_fail:
	if(inf->irq0)	free_irq(inf->irq0, &inf->miscdev);
irq0_fail:
alloc2_fail:
	if(inf->bootaddress)        free_page((unsigned int)inf->bootaddress);
	if(inf->codeprogramaddress) free_pages((unsigned int)inf->codeprogramaddress,4);
	if(inf->codedataaddress)    free_pages((unsigned int)inf->codedataaddress,4);
	if(inf->codetableaddress)   free_pages((unsigned int)inf->codetableaddress,6);
	if(inf)    kfree(inf);
alloc_fail:
	return ret;
}

static int dsp_drv_remove(struct platform_device *pdev)
{
	struct rk28dsp_inf *inf = platform_get_drvdata(pdev);
    dspprintk("%s [%d]\n",__FUNCTION__,__LINE__);

    misc_deregister(&(inf->miscdev));
	if(inf->irq0)       free_irq(inf->irq0, &inf->miscdev);
    if(inf->irq1)       free_irq(inf->irq1, &inf->miscdev);

    if(inf->bootaddress)        free_page((unsigned int)inf->bootaddress);
	if(inf->codeprogramaddress) free_pages((unsigned int)inf->codeprogramaddress,4);
	if(inf->codedataaddress)    free_pages((unsigned int)inf->codedataaddress,4);
	if(inf->codetableaddress)   free_pages((unsigned int)inf->codetableaddress,6);

    iounmap((void __iomem *)(inf->piu_base));
	iounmap((void __iomem *)(inf->pmu_base));
    iounmap((void __iomem *)(inf->l1_dbase));
    iounmap((void __iomem *)(inf->l2_idbase));
    iounmap((void __iomem *)(inf->scu_base));
    iounmap((void __iomem *)(inf->regfile_base));

	if(inf->clk) {
		DSP_CLOCK_DISABLE();
		clk_put(inf->clk);
	}

    kfree(inf);
    return 0;
}


#if defined(CONFIG_PM)
static int dsp_drv_suspend(struct platform_device *pdev, pm_message_t state)
{
    struct rk28dsp_inf *inf = platform_get_drvdata(pdev);

    dspprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    if(!inf) {
        printk("inf==0, dsp_drv_suspend fail! \n");
        return -EINVAL;
    }

 	if(DS_NORMAL==inf->dsp_status)  return -EPERM;	//DSP正在使用中

	if(DS_SLEEP != inf->dsp_status ) {
		inf->dsp_status = DS_SLEEP;
	    dsp_powerctl(DPC_SLEEP, 0);
	    dspprintk("dsp : normal -> sleep \n");
	}

    return 0;
}

static int dsp_drv_resume(struct platform_device *pdev)
{
    struct rk28dsp_inf *inf = platform_get_drvdata(pdev);

    dspprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);

    if(!inf) {
        printk("inf==0, dsp_drv_resume fail! \n");
        return -EINVAL;
    }

    return 0;
}
#else
#define dsp_drv_suspend		NULL
#define dsp_drv_resume		NULL
#endif /* CONFIG_PM */

typedef struct android_early_suspend android_early_suspend_t;
struct android_early_suspend
{
	struct list_head link;
	int level;
	void (*suspend)(android_early_suspend_t *h);
	void (*resume)(android_early_suspend_t *h);
};

static void dsp_early_suspend(android_early_suspend_t *h)
{
	dspprintk(">>>>>> %s : %s\n", __FILE__, __FUNCTION__);
	if(g_inf)
	{
		down(&sem);
		if(0==g_inf->cur_req) {
			g_inf->in_suspend = 1;
		    up(&sem);
		} else if(2==g_inf->cur_req){	//can not in early suspend
		        up(&sem);
			return;
		} else {
			g_inf->in_suspend = 1;
		    wq3_condition = 0;
		    up(&sem);
			wait_event(wq3, wq3_condition);
		}
                del_timer(&g_inf->dsp_timer);
		if(DS_SLEEP != g_inf->dsp_status ) {
		    g_inf->dsp_status = DS_SLEEP;
		    dsp_powerctl(DPC_SLEEP, 0);
		    dspprintk("dsp : normal -> sleep \n");
		}
	}
}

static void dsp_drv_shutdown(struct platform_device *pdev)
{
        printk("%s:: shutdown dsp\n" , __func__ );
        dsp_early_suspend( NULL );
}

static struct platform_driver dsp_driver = {
	.probe		= dsp_drv_probe,
	.remove		= dsp_drv_remove,
	.suspend	= dsp_drv_suspend,
	.resume		= dsp_drv_resume,
	.driver		= {
		.name	= "rk28-dsp",
	},
	.shutdown       = dsp_drv_shutdown,
};

static int __init rk2818_dsp_init(void)
{
	return platform_driver_register(&dsp_driver);
}

static void __exit rk2818_dsp_exit(void)
{
	platform_driver_unregister(&dsp_driver);
}

subsys_initcall(rk2818_dsp_init);
module_exit(rk2818_dsp_exit);


/* Module information */
MODULE_AUTHOR(" dukunming  dkm@rock-chips.com");
MODULE_DESCRIPTION("Driver for rk2818 dsp device");
MODULE_LICENSE("GPL");

