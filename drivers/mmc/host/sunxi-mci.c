/*
 * drivers/mmc/host/sunxi-mci.c
 * (C) Copyright 2010-2015
 * Reuuimlla Technology Co., Ltd. <www.reuuimllatech.com>
 * Aaron.Maoye <leafy.myeh@reuuimllatech.com>
 * James Deng <csjamesdeng@reuuimllatech.com>
 *
 * description for this code
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>

#include <asm/cacheflush.h>
#include <asm/uaccess.h>

#include <plat/system.h>
#include <plat/sys_config.h>
#include <mach/hardware.h>
#include <mach/platform.h>
#include <mach/gpio.h>
#include <mach/clock.h>

#include "sunxi-mci.h"

#if defined CONFIG_MMC_SUNXI || defined CONFIG_MMC_SUNXI_MODULE
#error Only one of the old and new SUNXI MMC drivers may be selected
#endif

#define sw_host_num (sunxi_is_sun5i() ? 3 : 4)

static DEFINE_MUTEX(sw_host_rescan_mutex);
static int sw_host_rescan_pending[4] = { 0, };
static struct sunxi_mmc_host* sw_host[4] = {NULL, NULL, NULL, NULL};

static const char * const mmc_para_io[10] = { "sdc_clk", "sdc_cmd", "sdc_d0",
	"sdc_d1", "sdc_d2", "sdc_d3", "sdc_d4", "sdc_d5", "sdc_d6", "sdc_d7" };

#if 0
static void dumphex32(char* name, char* base, int len)
{
	u32 i;

	printk("dump %s registers:", name);
	for (i=0; i<len; i+=4) {
		if (!(i&0xf))
			printk("\n0x%p : ", base + i);
		printk("0x%08x ", readl(base + i));
	}
	printk("\n");
}

static void hexdump(char* name, char* base, int len)
{
	u32 i;

	printk("%s :", name);
	for (i=0; i<len; i++) {
		if (!(i&0x1f))
			printk("\n0x%p : ", base + i);
		if (!(i&0xf))
			printk(" ");
		printk("%02x ", readb(base + i));
	}
	printk("\n");
}
#endif

static s32 sw_mci_init_host(struct sunxi_mmc_host* smc_host)
{
	u32 rval;

	SMC_DBG(smc_host, "MMC Driver init host %d\n", smc_host->pdev->id);

	/* reset controller */
	rval = mci_readl(smc_host, REG_GCTRL) | SDXC_HWReset;
	mci_writel(smc_host, REG_GCTRL, rval);

	mci_writel(smc_host, REG_FTRGL, 0x70008);
	mci_writel(smc_host, REG_TMOUT, 0xffffffff);
	mci_writel(smc_host, REG_IMASK, 0);
	mci_writel(smc_host, REG_RINTR, 0xffffffff);
	mci_writel(smc_host, REG_DBGC, 0xdeb);
	mci_writel(smc_host, REG_FUNS, 0xceaa0000);
	rval = mci_readl(smc_host, REG_GCTRL)|SDXC_INTEnb;
	mci_writel(smc_host, REG_GCTRL, rval);

	smc_host->voltage = SDC_WOLTAGE_OFF;
	return 0;
}

s32 sw_mci_exit_host(struct sunxi_mmc_host* smc_host)
{
	u32 rval;

	SMC_DBG(smc_host, "MMC Driver exit host %d\n", smc_host->pdev->id);
	smc_host->ferror = 0;
	smc_host->voltage = SDC_WOLTAGE_OFF;

	rval = mci_readl(smc_host, REG_GCTRL) | SDXC_HWReset;
	mci_writel(smc_host, REG_GCTRL, SDXC_HWReset);
	return 0;
}

s32 sw_mci_set_vddio(struct sunxi_mmc_host* smc_host, u32 vdd)
{
	char* vddstr[] = {"3.3V", "1.8V", "1.2V", "OFF"};
	static u32 on[4] = {0};
	u32 id = smc_host->pdev->id;

	if (smc_host->regulator == NULL)
		return 0;
	BUG_ON(vdd > SDC_WOLTAGE_OFF);
	switch (vdd) {
		case SDC_WOLTAGE_3V3:
			regulator_set_voltage(smc_host->regulator, 3300000, 3300000);
			if (!on[id]) {
				SMC_DBG(smc_host, "regulator on\n");
				regulator_enable(smc_host->regulator);
				on[id] = 1;
			}
			break;
		case SDC_WOLTAGE_1V8:
			regulator_set_voltage(smc_host->regulator, 1800000, 1800000);
			if (!on[id]) {
				SMC_DBG(smc_host, "regulator on\n");
				regulator_enable(smc_host->regulator);
				on[id] = 1;
			}
			break;
		case SDC_WOLTAGE_1V2:
			regulator_set_voltage(smc_host->regulator, 1200000, 1200000);
			if (!on[id]) {
				SMC_DBG(smc_host, "regulator on\n");
				regulator_enable(smc_host->regulator);
				on[id] = 1;
			}
			break;
		case SDC_WOLTAGE_OFF:
			if (on[id]) {
				SMC_DBG(smc_host, "regulator off\n");
				regulator_force_disable(smc_host->regulator);
				on[id] = 0;
			}
			break;
	}
	SMC_MSG(smc_host, "sdc%d switch io voltage to %s\n", smc_host->pdev->id, vddstr[vdd]);
	return 0;
}

s32 sw_mci_update_clk(struct sunxi_mmc_host* smc_host)
{
  	u32 rval;
  	s32 expire = jiffies + msecs_to_jiffies(1000);	//1000ms timeout
  	s32 ret = 0;

  	rval = SDXC_Start|SDXC_UPCLKOnly|SDXC_WaitPreOver;
	if (smc_host->voltage_switching)
		rval |= SDXC_VolSwitch;
	mci_writel(smc_host, REG_CMDR, rval);

	do {
		rval = mci_readl(smc_host, REG_CMDR);
	} while (jiffies < expire && (rval & SDXC_Start));

	if (rval & SDXC_Start) {
		smc_host->ferror = 1;
		SMC_ERR(smc_host, "update clock timeout, fatal error\n");
		ret = -1;
	}

	return ret;
}

/* UHS-I Operation Modes
 * DS		25MHz	12.5MB/s	3.3V
 * HS		50MHz	25MB/s		3.3V
 * SDR12	25MHz	12.5MB/s	1.8V
 * SDR25	50MHz	25MB/s		1.8V
 * SDR50	100MHz	50MB/s		1.8V
 * SDR104	208MHz	104MB/s		1.8V
 * DDR50	50MHz	50MB/s		1.8V
 * MMC Operation Modes
 * DS		26MHz	26MB/s		3/1.8/1.2V
 * HS		52MHz	52MB/s		3/1.8/1.2V
 * HSDDR	52MHz	104MB/s		3/1.8/1.2V
 * HS200	200MHz	200MB/s		1.8/1.2V
 *
 * Spec. Timing
 * SD3.0
 * Fcclk    Tcclk   Fsclk   Tsclk   Tis     Tih     odly  RTis     RTih
 * 400K     2.5us   24M     41ns    5ns     5ns     1     2209ns   41ns
 * 25M      40ns    600M    1.67ns  5ns     5ns     3     14.99ns  5.01ns
 * 50M      20ns    600M    1.67ns  6ns     2ns     3     14.99ns  5.01ns
 * 50MDDR   20ns    600M    1.67ns  6ns     0.8ns   2     6.67ns   3.33ns
 * 104M     9.6ns   600M    1.67ns  3ns     0.8ns   1     7.93ns   1.67ns
 * 208M     4.8ns   600M    1.67ns  1.4ns   0.8ns   1     3.33ns   1.67ns

 * 25M      40ns    300M    3.33ns  5ns     5ns     2     13.34ns   6.66ns
 * 50M      20ns    300M    3.33ns  6ns     2ns     2     13.34ns   6.66ns
 * 50MDDR   20ns    300M    3.33ns  6ns     0.8ns   1     6.67ns    3.33ns
 * 104M     9.6ns   300M    3.33ns  3ns     0.8ns   0     7.93ns    1.67ns
 * 208M     4.8ns   300M    3.33ns  1.4ns   0.8ns   0     3.13ns    1.67ns

 * eMMC4.5
 * 400K     2.5us   24M     41ns    3ns     3ns     1     2209ns    41ns
 * 25M      40ns    600M    1.67ns  3ns     3ns     3     14.99ns   5.01ns
 * 50M      20ns    600M    1.67ns  3ns     3ns     3     14.99ns   5.01ns
 * 50MDDR   20ns    600M    1.67ns  2.5ns   2.5ns   2     6.67ns    3.33ns
 * 200M     5ns     600M    1.67ns  1.4ns   0.8ns   1     3.33ns    1.67ns
 */
struct sw_mmc_clk_dly {
	u32 mode;
#define MMC_CLK_400K		0
#define MMC_CLK_25M		1
#define MMC_CLK_50M		2
#define MMC_CLK_50MDDR		3
#define MMC_CLK_50MDDR_8BIT	4
#define MMC_CLK_100M		5
#define MMC_CLK_200M		6
#define MMC_CLK_MOD_NUM		7
	u32 oclk_dly;
	u32 sclk_dly;
} mmc_clk_dly [MMC_CLK_MOD_NUM] = {
	{MMC_CLK_400K,        0, 7},
	{MMC_CLK_25M,         0, 5},
	{MMC_CLK_50M,         3, 5},
	{MMC_CLK_50MDDR,      2, 4},
	{MMC_CLK_50MDDR_8BIT, 2, 4},
	{MMC_CLK_100M,        1, 4},
	{MMC_CLK_200M,        1, 4},
};

s32 sw_mci_set_clk_dly(struct sunxi_mmc_host* smc_host, u32 oclk_dly, u32 sclk_dly)
{
	u32 smc_no = smc_host->pdev->id;
	void __iomem *mclk_base = __io_address(0x01c20088 + 0x4 * smc_no);
	u32 rval;
	unsigned long iflags;

	spin_lock_irqsave(&smc_host->lock, iflags);
	rval = readl(mclk_base);
	rval &= ~((0x7U << 8) | (0x7U << 20));
	rval |= (oclk_dly << 8) | (sclk_dly << 20);
	writel(rval, mclk_base);
	spin_unlock_irqrestore(&smc_host->lock, iflags);

	smc_host->oclk_dly = oclk_dly;
	smc_host->sclk_dly = sclk_dly;
	SMC_DBG(smc_host, "oclk_dly %d, sclk_dly %d\n", oclk_dly, sclk_dly);
	return 0;
}

s32 sw_mci_oclk_onoff(struct sunxi_mmc_host* smc_host, u32 oclk_en, u32 pwr_save)
{
	u32 rval = mci_readl(smc_host, REG_CLKCR);
	rval &= ~(SDXC_CardClkOn | SDXC_LowPowerOn);
	if (oclk_en)
		rval |= SDXC_CardClkOn;
	if (pwr_save || !smc_host->io_flag)
		rval |= SDXC_LowPowerOn;
	mci_writel(smc_host, REG_CLKCR, rval);
	sw_mci_update_clk(smc_host);
	return 0;
}

static void sw_mci_send_cmd(struct sunxi_mmc_host* smc_host, struct mmc_command* cmd)
{
	u32 imask = SDXC_IntErrBit;
	u32 cmd_val = SDXC_Start|(cmd->opcode&0x3f);
	unsigned long iflags;
	u32 wait = SDC_WAIT_NONE;

	wait = SDC_WAIT_CMD_DONE;
	if (cmd->opcode == MMC_GO_IDLE_STATE) {
		cmd_val |= SDXC_SendInitSeq;
		imask |= SDXC_CmdDone;
	}

	if (cmd->opcode == SD_SWITCH_VOLTAGE) {
		cmd_val |= SDXC_VolSwitch;
		imask |= SDXC_VolChgDone;
		smc_host->voltage_switching = 1;
		wait = SDC_WAIT_SWITCH1V8;
		/* switch controller to high power mode */
		sw_mci_oclk_onoff(smc_host, 1, 0);
	}

	if (cmd->flags & MMC_RSP_PRESENT) {
		cmd_val |= SDXC_RspExp;
		if (cmd->flags & MMC_RSP_136)
			cmd_val |= SDXC_LongRsp;
		if (cmd->flags & MMC_RSP_CRC)
			cmd_val |= SDXC_CheckRspCRC;

		if ((cmd->flags & MMC_CMD_MASK) == MMC_CMD_ADTC) {
			cmd_val |= SDXC_DataExp | SDXC_WaitPreOver;
			wait = SDC_WAIT_DATA_OVER;
			if (cmd->data->flags & MMC_DATA_STREAM) {
				imask |= SDXC_AutoCMDDone;
				cmd_val |= SDXC_Seqmod | SDXC_SendAutoStop;
				wait = SDC_WAIT_AUTOCMD_DONE;
			}
			if (cmd->data->stop) {
				imask |= SDXC_AutoCMDDone;
				cmd_val |= SDXC_SendAutoStop;
				wait = SDC_WAIT_AUTOCMD_DONE;
			} else
				imask |= SDXC_DataOver;

			if (cmd->data->flags & MMC_DATA_WRITE)
				cmd_val |= SDXC_Write;
			else
				wait |= SDC_WAIT_DMA_DONE;
		} else
			imask |= SDXC_CmdDone;

	} else
		imask |= SDXC_CmdDone;
	SMC_DBG(smc_host, "smc %d cmd %d(%08x) arg %x ie 0x%08x wt %x len %d\n",
		smc_host->pdev->id, cmd_val&0x3f, cmd->arg, cmd_val, imask, wait,
		smc_host->mrq->data ? smc_host->mrq->data->blksz * smc_host->mrq->data->blocks : 0);
	spin_lock_irqsave(&smc_host->lock, iflags);
	smc_host->wait = wait;
	smc_host->state = SDC_STATE_SENDCMD;
	mci_writew(smc_host, REG_IMASK, imask);
	mci_writel(smc_host, REG_CARG, cmd->arg);
	mci_writel(smc_host, REG_CMDR, cmd_val);
	smp_wmb();
	spin_unlock_irqrestore(&smc_host->lock, iflags);
}

static void sw_mci_init_idma_des(struct sunxi_mmc_host* smc_host, struct mmc_data* data)
{
	struct sunxi_mmc_idma_des* pdes = (struct sunxi_mmc_idma_des*)smc_host->sg_cpu;
	struct sunxi_mmc_idma_des* pdes_pa = (struct sunxi_mmc_idma_des*)smc_host->sg_dma;
	u32 des_idx = 0;
	u32 buff_frag_num = 0;
	u32 remain;
	u32 i, j;
	u32 config;

	for (i=0; i<data->sg_len; i++) {
		buff_frag_num = data->sg[i].length >> SDXC_DES_NUM_SHIFT;
		remain = data->sg[i].length & (SDXC_DES_BUFFER_MAX_LEN-1);
		if (remain)
			buff_frag_num ++;
		else
			remain = SDXC_DES_BUFFER_MAX_LEN;

		for (j=0; j < buff_frag_num; j++, des_idx++) {
			memset((void*)&pdes[des_idx], 0, sizeof(struct sunxi_mmc_idma_des));
			config = SDXC_IDMAC_DES0_CH|SDXC_IDMAC_DES0_OWN|SDXC_IDMAC_DES0_DIC;

		    	if (buff_frag_num > 1 && j != buff_frag_num-1)
				pdes[des_idx].data_buf1_sz = SDXC_DES_BUFFER_MAX_LEN;
		    	else
				pdes[des_idx].data_buf1_sz = remain;

			pdes[des_idx].buf_addr_ptr1 = sg_dma_address(&data->sg[i])
							+ j * SDXC_DES_BUFFER_MAX_LEN;
			if (i==0 && j==0)
				config |= SDXC_IDMAC_DES0_FD;

			if ((i == data->sg_len-1) && (j == buff_frag_num-1)) {
				config &= ~SDXC_IDMAC_DES0_DIC;
				config |= SDXC_IDMAC_DES0_LD|SDXC_IDMAC_DES0_ER;
				pdes[des_idx].buf_addr_ptr2 = 0;
			} else {
				pdes[des_idx].buf_addr_ptr2 = (u32)&pdes_pa[des_idx+1];
			}
			pdes[des_idx].config = config;
			SMC_INF(smc_host, "sg %d, frag %d, remain %d, des[%d](%08x): "
		    		"[0] = %08x, [1] = %08x, [2] = %08x, [3] = %08x\n", i, j, remain,
				des_idx, (u32)&pdes[des_idx],
				(u32)((u32*)&pdes[des_idx])[0], (u32)((u32*)&pdes[des_idx])[1],
				(u32)((u32*)&pdes[des_idx])[2], (u32)((u32*)&pdes[des_idx])[3]);
		}
	}
	smp_wmb();
	return;
}

static int sw_mci_prepare_dma(struct sunxi_mmc_host* smc_host, struct mmc_data* data)
{
	u32 dma_len;
	u32 i;
	u32 temp;
	struct scatterlist *sg;

	if (smc_host->sg_cpu == NULL)
		return -ENOMEM;

	dma_len = dma_map_sg(mmc_dev(smc_host->mmc), data->sg, data->sg_len,
			(data->flags & MMC_DATA_WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (dma_len == 0) {
		SMC_ERR(smc_host, "no dma map memory\n");
		return -ENOMEM;
	}

	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & 3 || sg->length & 3) {
			SMC_ERR(smc_host, "unaligned scatterlist: os %x length %d\n",
				sg->offset, sg->length);
			return -EINVAL;
		}
	}

	sw_mci_init_idma_des(smc_host, data);
	temp = mci_readl(smc_host, REG_GCTRL);
	temp |= SDXC_DMAEnb;
	mci_writel(smc_host, REG_GCTRL, temp);
	temp |= SDXC_DMAReset;
	mci_writel(smc_host, REG_GCTRL, temp);
	mci_writel(smc_host, REG_DMAC, SDXC_IDMACSoftRST);
	temp = SDXC_IDMACFixBurst|SDXC_IDMACIDMAOn;
	mci_writel(smc_host, REG_DMAC, temp);
	temp = mci_readl(smc_host, REG_IDIE);
	temp &= ~(SDXC_IDMACReceiveInt|SDXC_IDMACTransmitInt);
	if (data->flags & MMC_DATA_WRITE)
		temp |= SDXC_IDMACTransmitInt;
	else
		temp |= SDXC_IDMACReceiveInt;
	mci_writel(smc_host, REG_IDIE, temp);

	//write descriptor address to register
	mci_writel(smc_host, REG_DLBA, smc_host->sg_dma);
	mci_writel(smc_host, REG_FTRGL, smc_host->pdata->dma_tl);

	return 0;
}

int sw_mci_send_manual_stop(struct sunxi_mmc_host* smc_host, struct mmc_request* req)
{
	struct mmc_data* data = req->data;
	u32 cmd_val = SDXC_Start | SDXC_RspExp | SDXC_StopAbortCMD
			| SDXC_CheckRspCRC | MMC_STOP_TRANSMISSION;
	u32 iflags = 0;
	u32 imask = 0;
	int ret = 0;
	u32 expire = jiffies + msecs_to_jiffies(1000);

	if (!data) {
		SMC_ERR(smc_host, "no data request\n");
		return -1;
	}
	/* disable interrupt */
	imask = mci_readw(smc_host, REG_IMASK);
	mci_writew(smc_host, REG_IMASK, 0);

	mci_writel(smc_host, REG_CARG, 0);
	mci_writel(smc_host, REG_CMDR, cmd_val);
	do {
		iflags = mci_readw(smc_host, REG_RINTR);
	} while(!(iflags & (SDXC_CmdDone | SDXC_IntErrBit)) && jiffies < expire);

	if (iflags & SDXC_IntErrBit) {
		SMC_ERR(smc_host, "sdc %d send stop command failed\n", smc_host->pdev->id);
		ret = -1;
	}

	if (req->stop)
		req->stop->resp[0] = mci_readl(smc_host, REG_RESP0);

	mci_writew(smc_host, REG_RINTR, iflags);

	/* enable interrupt */
	mci_writew(smc_host, REG_IMASK, imask);

	return ret;
}

void sw_mci_dump_errinfo(struct sunxi_mmc_host* smc_host)
{
	SMC_ERR(smc_host, "smc %d err, cmd %d, %s%s%s%s%s%s%s%s%s%s\n",
		smc_host->pdev->id, smc_host->mrq->cmd ? smc_host->mrq->cmd->opcode : -1,
		smc_host->int_sum & SDXC_RespErr     ? " RE"     : "",
		smc_host->int_sum & SDXC_RespCRCErr  ? " RCE"    : "",
		smc_host->int_sum & SDXC_DataCRCErr  ? " DCE"    : "",
		smc_host->int_sum & SDXC_RespTimeout ? " RTO"    : "",
		smc_host->int_sum & SDXC_DataTimeout ? " DTO"    : "",
		smc_host->int_sum & SDXC_DataStarve  ? " DS"     : "",
		smc_host->int_sum & SDXC_FIFORunErr  ? " FE"     : "",
		smc_host->int_sum & SDXC_HardWLocked ? " HL"     : "",
		smc_host->int_sum & SDXC_StartBitErr ? " SBE"    : "",
		smc_host->int_sum & SDXC_EndBitErr   ? " EBE"    : ""
		);
}

s32 sw_mci_request_done(struct sunxi_mmc_host* smc_host)
{
	struct mmc_request* req = smc_host->mrq;
	u32 temp;
	s32 ret = 0;

	if (smc_host->int_sum & SDXC_IntErrBit) {
		/* if we got response timeout error information, we should check 
		   if the command done status has been set. if there is no command
		   done information, we should wait this bit to be set */
		if ((smc_host->int_sum & SDXC_RespTimeout) && !(smc_host->int_sum & SDXC_CmdDone)) {
			u32 rint;
			u32 expire = jiffies + 1;
			do {
				rint = mci_readl(smc_host, REG_RINTR);
			} while (jiffies < expire && !(rint & SDXC_CmdDone));
		}
			
		sw_mci_dump_errinfo(smc_host);
		if (req->data)
			SMC_ERR(smc_host, "In data %s operation\n",
				req->data->flags & MMC_DATA_WRITE ? "write" : "read");
		ret = -1;
		goto out;
	}

	if (req->cmd) {
		if (req->cmd->flags & MMC_RSP_136) {
			req->cmd->resp[0] = mci_readl(smc_host, REG_RESP3);
			req->cmd->resp[1] = mci_readl(smc_host, REG_RESP2);
			req->cmd->resp[2] = mci_readl(smc_host, REG_RESP1);
			req->cmd->resp[3] = mci_readl(smc_host, REG_RESP0);
		} else {
			req->cmd->resp[0] = mci_readl(smc_host, REG_RESP0);
		}
	}

out:
	if (req->data) {
		struct mmc_data* data = req->data;
		mci_writel(smc_host, REG_IDST, 0x337);
		mci_writel(smc_host, REG_IDIE, 0);
		mci_writel(smc_host, REG_DMAC, 0);
		temp = mci_readl(smc_host, REG_GCTRL);
		mci_writel(smc_host, REG_GCTRL, temp|SDXC_DMAReset);
		temp &= ~SDXC_DMAEnb;
		mci_writel(smc_host, REG_GCTRL, temp);
		temp |= SDXC_FIFOReset;
		mci_writel(smc_host, REG_GCTRL, temp);
		dma_unmap_sg(mmc_dev(smc_host->mmc), data->sg, data->sg_len,
				data->flags & MMC_DATA_WRITE ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	}

	mci_writew(smc_host, REG_IMASK, 0);
	if (smc_host->int_sum & (SDXC_RespErr | SDXC_HardWLocked | SDXC_RespTimeout)) {
		SMC_DBG(smc_host, "sdc %d abnormal status: %s\n", smc_host->pdev->id,
			smc_host->int_sum & SDXC_HardWLocked ? "HardWLocked" : "RespErr");
	}

	mci_writew(smc_host, REG_RINTR, 0xffff);

	SMC_DBG(smc_host, "smc %d done, resp %08x %08x %08x %08x\n", smc_host->pdev->id,
		req->cmd->resp[0], req->cmd->resp[1], req->cmd->resp[2], req->cmd->resp[3]);

	if (req->data  && (smc_host->int_sum & SDXC_IntErrBit)) {
		SMC_MSG(smc_host, "found data error, need to send stop command\n");
		sw_mci_send_manual_stop(smc_host, req);
	}

	return ret;
}

/* static s32 sw_mci_set_clk(struct sunxi_mmc_host* smc_host, u32 clk);
 * set clock and the phase of output/input clock incording on
 * the different timing condition
 */
static int sw_mci_set_clk(struct sunxi_mmc_host* smc_host, u32 clk)
{
	struct clk *sclk = NULL;
	u32 mod_clk = 0;
	u32 src_clk = 0;
	u32 temp;
	u32 oclk_dly = 2;
	u32 sclk_dly = 2;
	struct sw_mmc_clk_dly* dly = NULL;
	s32 err;
	u32 rate;

	if (clk <= 400000) {
		mod_clk = smc_host->mod_clk;
		sclk = clk_get(&smc_host->pdev->dev, MMC_SRCCLK_HOSC);
	} else {
		mod_clk = smc_host->mod_clk;
		sclk = clk_get(&smc_host->pdev->dev, MMC_SRCCLK_PLL6);
	}
	if (IS_ERR(sclk)) {
		SMC_ERR(smc_host, "Error to get source clock for clk %dHz\n", clk);
		return -1;
	}
	err = clk_set_parent(smc_host->mclk, sclk);
	if (err) {
		SMC_ERR(smc_host, "sdc%d set mclk parent error\n", smc_host->pdev->id);
		clk_put(sclk);
		return -1;
	}
	err = clk_set_rate(smc_host->mclk, mod_clk);
	if (err) {
		SMC_ERR(smc_host, "sdc%d set mclk rate error, rate %dHz\n",
						smc_host->pdev->id, mod_clk);
		clk_put(sclk);
		return -1;
	}
	rate = clk_get_rate(smc_host->mclk);
	if (0 == rate) {
		SMC_ERR(smc_host, "sdc%d get mclk rate error\n",
			smc_host->pdev->id);
		clk_put(sclk);
		return -1;
	}
	src_clk = clk_get_rate(sclk);
	clk_put(sclk);
	smc_host->mod_clk = smc_host->card_clk = rate;

	SMC_MSG(smc_host, "sdc%d set round clock %d, src %d\n", smc_host->pdev->id, rate, src_clk);
	sw_mci_oclk_onoff(smc_host, 0, 0);
	/* clear internal divider */
	temp = mci_readl(smc_host, REG_CLKCR);
	temp &= ~0xff;
	mci_writel(smc_host, REG_CLKCR, temp);
	sw_mci_oclk_onoff(smc_host, 0, 0);

	if (clk <= 400000) {
		dly = &mmc_clk_dly[MMC_CLK_400K];
	} else if (clk <= 25000000) {
		dly = &mmc_clk_dly[MMC_CLK_25M];
	} else if (clk <= 50000000) {
		if (smc_host->ddr) {
			if (smc_host->bus_width == 8)
				dly = &mmc_clk_dly[MMC_CLK_50MDDR_8BIT];
			else
				dly = &mmc_clk_dly[MMC_CLK_50MDDR];
		} else {
			dly = &mmc_clk_dly[MMC_CLK_50M];
		}
	} else if (clk <= 104000000) {
		dly = &mmc_clk_dly[MMC_CLK_100M];
	} else if (clk <= 208000000) {
		dly = &mmc_clk_dly[MMC_CLK_200M];
	} else
		dly = &mmc_clk_dly[MMC_CLK_50M];
	oclk_dly = dly->oclk_dly;
	sclk_dly = dly->sclk_dly;
	if (src_clk >= 300000000 && src_clk <= 400000000) {
		if (oclk_dly)
			oclk_dly--;
		if (sclk_dly)
			sclk_dly--;
	}
	sw_mci_set_clk_dly(smc_host, oclk_dly, sclk_dly);
	sw_mci_oclk_onoff(smc_host, 1, 0);
	return 0;
}

static void sw_mci_update_io_driving(struct sunxi_mmc_host *smc_host, u32 drv)
{
	u32 smc_no = smc_host->pdev->id;
	struct sunxi_mmc_platform_data *pdata = smc_host->pdata;
	int i, r;

	for (i = 0; i < pdata->width + 2; i++) {
		r = gpio_set_one_pin_driver_level(pdata->mmcio[i], drv,
						  mmc_para_io[i]);
		if (r != 0) {
			SMC_ERR(smc_host, "sdc%u set %s drvlvl failed\n",
				smc_no, mmc_para_io[i]);
		}
	}
	SMC_DBG(smc_host, "sdc%u set mmcio driving to %d\n", smc_no, drv);
}

static int sw_mci_resource_request(struct sunxi_mmc_host *smc_host)
{
	struct platform_device *pdev = smc_host->pdev;
	u32 smc_no = pdev->id;
	char hclk_name[16] = {0};
	char mclk_name[8] = {0};
	struct resource* res = NULL;
	s32 ret;

	/* io mapping */
	res = request_mem_region(SMC_BASE(smc_no), SMC_BASE_OS, pdev->name);
	if (!res) {
		SMC_ERR(smc_host, "Failed to request io memory region.\n");
		return -ENOENT;
	}
	smc_host->reg_base = ioremap(res->start, SMC_BASE_OS);
	if (!smc_host->reg_base) {
		SMC_ERR(smc_host, "Failed to ioremap() io memory region.\n");
		ret = -EINVAL;
		goto free_mem_region;
	}
	/* hclk */
	sprintf(hclk_name, MMC_AHBCLK_PREFIX"%d", smc_no);
	smc_host->hclk = clk_get(&pdev->dev, hclk_name);
	if (IS_ERR(smc_host->hclk)) {
		ret = PTR_ERR(smc_host->hclk);
		SMC_ERR(smc_host, "Error to get ahb clk for %s\n", hclk_name);
		goto iounmap;
	}
	/* mclk */
	sprintf(mclk_name, MMC_MODCLK_PREFIX"%d", smc_no);
	smc_host->mclk = clk_get(&pdev->dev, mclk_name);
	if (IS_ERR(smc_host->mclk)) {
		ret = PTR_ERR(smc_host->mclk);
		SMC_ERR(smc_host, "Error to get clk for %s\n", mclk_name);
		goto free_hclk;
	}

	/* alloc idma descriptor structure */
	smc_host->sg_cpu = dma_alloc_writecombine(NULL, PAGE_SIZE,
					&smc_host->sg_dma, GFP_KERNEL);
	if (smc_host->sg_cpu == NULL) {
		SMC_ERR(smc_host, "alloc dma des failed\n");
		goto free_mclk;
	}

	/* get power regulator */
	if (smc_host->pdata->regulator[0]) {
		smc_host->regulator = regulator_get(NULL, smc_host->pdata->regulator);
		if (!smc_host->regulator) {
			SMC_ERR(smc_host, "Get regulator %s failed\n", smc_host->pdata->regulator);
			goto free_sgbuff;
		}
	}

	return 0;
free_sgbuff:
	dma_free_coherent(NULL, PAGE_SIZE, smc_host->sg_cpu, smc_host->sg_dma);
	smc_host->sg_cpu = NULL;
	smc_host->sg_dma = 0;
free_mclk:
	clk_put(smc_host->mclk);
	smc_host->mclk = NULL;
free_hclk:
	clk_put(smc_host->hclk);
	smc_host->hclk = NULL;
iounmap:
	iounmap(smc_host->reg_base);
free_mem_region:
	release_mem_region(SMC_BASE(smc_no), SMC_BASE_OS);

	return -1;
}


static int sw_mci_resource_release(struct sunxi_mmc_host *smc_host)
{
	/* free power regulator */
	if (smc_host->regulator) {
		regulator_put(smc_host->regulator);
		smc_host->regulator = NULL;
	}
	/* free idma descriptor structrue */
	if (smc_host->sg_cpu) {
		dma_free_coherent(NULL, PAGE_SIZE,
				  smc_host->sg_cpu, smc_host->sg_dma);
		smc_host->sg_cpu = NULL;
		smc_host->sg_dma = 0;
	}

	clk_put(smc_host->hclk);
	smc_host->hclk = NULL;
	clk_put(smc_host->mclk);
	smc_host->mclk = NULL;

	iounmap(smc_host->reg_base);
	release_mem_region(SMC_BASE(smc_host->pdev->id), SMC_BASE_OS);

	return 0;
}

static void sw_mci_hold_io(struct sunxi_mmc_host* smc_host)
{
	int ret;
	u32 i;
	struct sunxi_mmc_platform_data *pdata = smc_host->pdata;

	for (i = 0; i < pdata->width + 2; i++) {
		user_gpio_set_t settings = pdata->mmcio_settings[i];
		settings.mul_sel = 0;
		settings.pull = 0;
		ret = gpio_set_one_pin_status(pdata->mmcio[i], &settings,
					      mmc_para_io[i], 1);
		if (ret != 0) {
			SMC_ERR(smc_host, "sdc%d hold mmcio%d failed\n",
						smc_host->pdev->id, i);
			return;
		}
	}
	SMC_DBG(smc_host, "mmc %d suspend pins\n", smc_host->pdev->id);

	return;
}

static void sw_mci_restore_io(struct sunxi_mmc_host* smc_host)
{
	int ret;
	u32 i;
	struct sunxi_mmc_platform_data *pdata = smc_host->pdata;

	for (i = 0; i < pdata->width + 2; i++) {
		ret = gpio_set_one_pin_status(pdata->mmcio[i], NULL,
					      mmc_para_io[i], 0);
		if (ret) {
			SMC_ERR(smc_host, "sdc%d restore mmcio%d failed\n",
						smc_host->pdev->id, i);
			return;
		}
	}

	SMC_DBG(smc_host, "mmc %d resume pins\n", smc_host->pdev->id);
}

static void sw_mci_finalize_request(struct sunxi_mmc_host *smc_host)
{
	struct mmc_request* mrq = smc_host->mrq;
	unsigned long iflags;

	spin_lock_irqsave(&smc_host->lock, iflags);
	if (smc_host->wait != SDC_WAIT_FINALIZE) {
		spin_unlock_irqrestore(&smc_host->lock, iflags);
		SMC_MSG(smc_host, "nothing finalize, wt %x, st %d\n",
				smc_host->wait, smc_host->state);
		return;
	}
	smc_host->wait = SDC_WAIT_NONE;
	smc_host->state = SDC_STATE_IDLE;
	smc_host->trans_done = 0;
	smc_host->dma_done = 0;
	spin_unlock_irqrestore(&smc_host->lock, iflags);

	sw_mci_request_done(smc_host);
	if (smc_host->error) {
		mrq->cmd->error = -ETIMEDOUT;
		if (mrq->data)
			mrq->data->error = -ETIMEDOUT;
		if (mrq->stop)
			mrq->stop->error = -ETIMEDOUT;
	} else {
		if (mrq->data)
			mrq->data->bytes_xfered = (mrq->data->blocks * mrq->data->blksz);
	}

	smc_host->mrq = NULL;
	smc_host->error = 0;
	smc_host->int_sum = 0;
	smp_wmb();
	mmc_request_done(smc_host->mmc, mrq);
	return;
}

static s32 sw_mci_get_ro(struct mmc_host *mmc)
{
	struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
	struct sunxi_mmc_platform_data *pdata = smc_host->pdata;
	u32 wp_val;

	if (pdata->wpmode) {
		wp_val = gpio_read_one_pin_value(pdata->wp, "sdc_wp");
		SMC_DBG(smc_host, "sdc fetch card wp pin status: %d \n", wp_val);
		if (!wp_val) {
			smc_host->read_only = 0;
			return 0;
		} else {
			SMC_MSG(smc_host, "Card is write-protected\n");
			smc_host->read_only = 1;
			return 1;
		}
	} else {
		smc_host->read_only = 0;
		return 0;
	}
	return 0;
}

static void sw_mci_cd_cb(unsigned long data)
{
	struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;
	struct sunxi_mmc_platform_data *pdata = smc_host->pdata;
	u32 gpio_val = 0;
	u32 present;
	u32 i = 0;

	for (i=0; i<5; i++) {
		gpio_val += gpio_read_one_pin_value(pdata->cd, "sdc_det");
		mdelay(1);
	}
	if (gpio_val==5) {
		present = 0;
	} else if (gpio_val==0)
		present = 1;
	else
		goto modtimer;
	SMC_DBG(smc_host, "cd %d, host present %d, cur present %d\n",
			gpio_val, smc_host->present, present);

	if (smc_host->present ^ present) {
		SMC_MSG(smc_host, "mmc %d detect change, present %d\n",
				smc_host->pdev->id, present);
		smc_host->present = present;
		smp_wmb();
		if (smc_host->present)
			mmc_detect_change(smc_host->mmc, msecs_to_jiffies(500));
		else
			mmc_detect_change(smc_host->mmc, msecs_to_jiffies(50));
	}

modtimer:
	if (smc_host->cd_mode == CARD_DETECT_BY_GPIO_POLL)
		mod_timer(&smc_host->cd_timer, jiffies + msecs_to_jiffies(300));

	return;
}

#if 0
static u32 sw_mci_cd_irq(void *data)
{
	sw_mci_cd_cb((unsigned long)data);
	return 0;
}
#endif

static int sw_mci_card_present(struct mmc_host *mmc)
{
	struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
	return smc_host->present;
}

static irqreturn_t sw_mci_irq(int irq, void *dev_id)
{
	struct sunxi_mmc_host *smc_host = dev_id;
	u32 sdio_int = 0;
	u32 raw_int;
	u32 msk_int;
	u32 idma_inte;
	u32 idma_int;

	spin_lock(&smc_host->lock);

	idma_int  = mci_readl(smc_host, REG_IDST);
	idma_inte = mci_readl(smc_host, REG_IDIE);
	raw_int   = mci_readl(smc_host, REG_RINTR);
	msk_int   = mci_readl(smc_host, REG_MISTA);
	if (!msk_int && !idma_int) {
		SMC_MSG(smc_host, "sdc%d nop irq: ri %08x mi %08x ie %08x idi %08x\n",
			smc_host->pdev->id, raw_int, msk_int, idma_inte, idma_int);
		spin_unlock(&smc_host->lock);
		return IRQ_HANDLED;
	}

	smc_host->int_sum |= raw_int;
	SMC_INF(smc_host, "smc %d irq, ri %08x(%08x) mi %08x ie %08x idi %08x\n",
		smc_host->pdev->id, raw_int, smc_host->int_sum,
		msk_int, idma_inte, idma_int);

	if (msk_int & SDXC_SDIOInt) {
		sdio_int = 1;
		mci_writel(smc_host, REG_RINTR, SDXC_SDIOInt);
		goto sdio_out;
	}

	if (smc_host->wait == SDC_WAIT_NONE && !sdio_int) {
		SMC_ERR(smc_host, "smc %x, nothing to complete, ri %08x, "
			"mi %08x\n", smc_host->pdev->id, raw_int, msk_int);
		goto irq_out;
	}

	if ((raw_int & SDXC_IntErrBit) || (idma_int & SDXC_IDMA_ERR)) {
		smc_host->error = raw_int & SDXC_IntErrBit;
		smc_host->wait = SDC_WAIT_FINALIZE;
		smc_host->state = SDC_STATE_CMDDONE;
		goto irq_out;
	}
	if (idma_int & (SDXC_IDMACTransmitInt|SDXC_IDMACReceiveInt))
		smc_host->dma_done = 1;
	if (msk_int & (SDXC_AutoCMDDone|SDXC_DataOver|SDXC_CmdDone|SDXC_VolChgDone))
		smc_host->trans_done = 1;
	if ((smc_host->trans_done && (smc_host->wait == SDC_WAIT_AUTOCMD_DONE
					|| smc_host->wait == SDC_WAIT_DATA_OVER
					|| smc_host->wait == SDC_WAIT_CMD_DONE
					|| smc_host->wait == SDC_WAIT_SWITCH1V8))
		|| (smc_host->trans_done && smc_host->dma_done && (smc_host->wait & SDC_WAIT_DMA_DONE))) {
		smc_host->wait = SDC_WAIT_FINALIZE;
		smc_host->state = SDC_STATE_CMDDONE;
	}

irq_out:
	mci_writel(smc_host, REG_RINTR, msk_int&(~SDXC_SDIOInt));
	mci_writel(smc_host, REG_IDST, idma_int);

	if (smc_host->wait == SDC_WAIT_FINALIZE) {
		smp_wmb();
		mci_writew(smc_host, REG_IMASK, 0);
		tasklet_schedule(&smc_host->tasklet);
	}

sdio_out:
	spin_unlock(&smc_host->lock);

	if (sdio_int)
		mmc_signal_sdio_irq(smc_host->mmc);

	return IRQ_HANDLED;
}

static void sw_mci_tasklet(unsigned long data)
{
	struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *) data;
	sw_mci_finalize_request(smc_host);
}

static void sw_mci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
	char* bus_mode[] = {"", "OD", "PP"};
	char* pwr_mode[] = {"OFF", "UP", "ON"};
	char* vdd[] = {"3.3V", "1.8V", "1.2V"};
	char* timing[] = {"LEGACY(SDR12)", "MMC-HS(SDR20)", "SD-HS(SDR25)",
			"UHS-SDR50", "UHS-SDR104", "UHS-DDR50", "MMC-HS200"};
	char* drv_type[] = {"B", "A", "C", "D"};
	static u32 last_clock[4] = {0};
	u32 id = smc_host->pdev->id;
	u32 temp;
	s32 err;

	BUG_ON(ios->bus_mode >= sizeof(bus_mode)/sizeof(bus_mode[0]));
	BUG_ON(ios->power_mode >= sizeof(pwr_mode)/sizeof(pwr_mode[0]));
	BUG_ON(ios->signal_voltage >= sizeof(vdd)/sizeof(vdd[0]));
	BUG_ON(ios->timing >= sizeof(timing)/sizeof(timing[0]));
	SMC_MSG(smc_host, "sdc%d set ios: "
		"clk %dHz bm %s pm %s vdd %s width %d timing %s dt %s\n",
		smc_host->pdev->id, ios->clock, bus_mode[ios->bus_mode],
		pwr_mode[ios->power_mode], vdd[ios->signal_voltage],
		1 << ios->bus_width, timing[ios->timing], drv_type[ios->drv_type]);

	/* Set the power state */
	switch (ios->power_mode) {
		case MMC_POWER_ON:
			break;
		case MMC_POWER_UP:
			if (!smc_host->power_on) {
				SMC_MSG(smc_host, "sdc%d power on\n", smc_host->pdev->id);
				sw_mci_restore_io(smc_host);
				err = clk_enable(smc_host->hclk);
				if (err) {
					SMC_ERR(smc_host, "Failed to enable sdc%d hclk\n",
								smc_host->pdev->id);
				}
				err = clk_enable(smc_host->mclk);
				if (err) {
					SMC_ERR(smc_host, "Failed to enable sdc%d mclk\n",
								smc_host->pdev->id);
				}
				err = clk_reset(smc_host->mclk, AW_CCU_CLK_NRESET);
				if (err) {
					SMC_ERR(smc_host, "Failed to release sdc%d reset\n",
								smc_host->pdev->id);
				}
				mdelay(1);
				sw_mci_init_host(smc_host);
				enable_irq(smc_host->irq);
				smc_host->power_on = 1;
			}
			break;
		case MMC_POWER_OFF:
			if (smc_host->power_on) {
				SMC_MSG(smc_host, "sdc%d power off\n", smc_host->pdev->id);
				disable_irq(smc_host->irq);
				sw_mci_exit_host(smc_host);
				err = clk_reset(smc_host->mclk, AW_CCU_CLK_RESET);
				if (err) {
					SMC_ERR(smc_host, "Failed to set sdc%d reset\n",
								smc_host->pdev->id);
				}
				clk_disable(smc_host->mclk);
				clk_disable(smc_host->hclk);
				sw_mci_hold_io(smc_host);
				smc_host->power_on = 0;
				smc_host->ferror = 0;
				last_clock[id] = 0;
			}
			break;
	}
	/* set bus width */
	switch (ios->bus_width) {
		case MMC_BUS_WIDTH_1:
			mci_writel(smc_host, REG_WIDTH, SDXC_WIDTH1);
			smc_host->bus_width = 1;
			break;
		case MMC_BUS_WIDTH_4:
			mci_writel(smc_host, REG_WIDTH, SDXC_WIDTH4);
			smc_host->bus_width = 4;
			break;
		case MMC_BUS_WIDTH_8:
			mci_writel(smc_host, REG_WIDTH, SDXC_WIDTH8);
			smc_host->bus_width = 8;
			break;
	}

	/* set ddr mode */
	temp = mci_readl(smc_host, REG_GCTRL);
	if (ios->timing == MMC_TIMING_UHS_DDR50) {
		temp |= SDXC_DDR_MODE;
		smc_host->ddr = 1;
		/* change io driving */
		sw_mci_update_io_driving(smc_host, 3);
	} else {
		temp &= ~SDXC_DDR_MODE;
		smc_host->ddr = 0;
	}
	mci_writel(smc_host, REG_GCTRL, temp);

	/* set up clock */
	if (ios->clock && ios->clock != last_clock[id]) {
		if (smc_host->ddr)
			ios->clock = smc_host->pdata->f_ddr_max;
		/* 8bit ddr, mod_clk = 2 * card_clk */
		if (smc_host->ddr && smc_host->bus_width == 8)
			smc_host->mod_clk = ios->clock << 1;
		else
			smc_host->mod_clk = ios->clock;
		smc_host->card_clk = ios->clock;
		if (smc_host->mod_clk > 45000000)
			smc_host->mod_clk = 45000000;
		sw_mci_set_clk(smc_host, smc_host->card_clk);
		last_clock[id] = ios->clock;
		usleep_range(50000, 55000);
	} else if (!ios->clock) {
		last_clock[id] = 0;
		sw_mci_update_clk(smc_host);
	}
}

static void sw_mci_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
	unsigned long flags;
	u32 imask;

	spin_lock_irqsave(&smc_host->lock, flags);
	imask = mci_readl(smc_host, REG_IMASK);
	if (enable)
		imask |= SDXC_SDIOInt;
	else
		imask &= ~SDXC_SDIOInt;
	mci_writel(smc_host, REG_IMASK, imask);
	spin_unlock_irqrestore(&smc_host->lock, flags);
}

void sw_mci_hw_reset(struct mmc_host *mmc)
{
	struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
	u32 id = smc_host->pdev->id;

	if (id == 2 || id == 3) {
		mci_writel(smc_host, REG_HWRST, 0);
		udelay(10);
		mci_writel(smc_host, REG_HWRST, 1);
		udelay(300);
	}
}

static void sw_mci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
	struct mmc_command* cmd = mrq->cmd;
	struct mmc_data* data = mrq->data;
	u32 byte_cnt = 0;
	int ret;

	if (sw_mci_card_present(mmc) == 0 || smc_host->ferror || 
			smc_host->suspend || !smc_host->power_on) {
		SMC_DBG(smc_host, "no medium present, ferr %d, suspend %d pwd %d\n",
			    smc_host->ferror, smc_host->suspend, smc_host->power_on);
		mrq->cmd->error = -ENOMEDIUM;
		mmc_request_done(mmc, mrq);
		return;
	}

	smc_host->mrq = mrq;
	if (data) {
		byte_cnt = data->blksz * data->blocks;
		mci_writel(smc_host, REG_BLKSZ, data->blksz);
		mci_writel(smc_host, REG_BCNTR, byte_cnt);
		ret = sw_mci_prepare_dma(smc_host, data);
		if (ret < 0) {
			SMC_ERR(smc_host, "smc %d prepare DMA failed\n", smc_host->pdev->id);
			cmd->error = ret;
			cmd->data->error = ret;
			smp_wmb();
			mmc_request_done(smc_host->mmc, mrq);
			return;
		}
	}
	sw_mci_send_cmd(smc_host, cmd);
}

static int sw_mci_do_voltage_switch(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sunxi_mmc_host *smc_host = mmc_priv(mmc);

	if (smc_host->voltage != SDC_WOLTAGE_3V3 &&
			ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330) {
		sw_mci_set_vddio(smc_host, SDC_WOLTAGE_3V3);
		/* wait for 5ms */
		usleep_range(1000, 1500);
		smc_host->voltage = SDC_WOLTAGE_3V3;
		return 0;
	} else if (smc_host->voltage != SDC_WOLTAGE_1V8 &&
			(ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180)) {
		u32 data_down;
		/* clock off */
		sw_mci_oclk_onoff(smc_host, 0, 0);
		/* check whether data[3:0] is 0000 */
		data_down = mci_readl(smc_host, REG_STAS);
		if (!(data_down & SDXC_CardPresent)) {
			/* switch voltage of card vdd to 1.8V */
			sw_mci_set_vddio(smc_host, SDC_WOLTAGE_1V8);
			/* the standard defines the time limit is 5ms, here we
			   wait for 8ms to make sure that the card completes the
			   voltage switching */
			usleep_range(8000, 8500);
			/* clock on again */
			sw_mci_oclk_onoff(smc_host, 1, 0);
			/* wait for 1ms */
			usleep_range(2000, 2500);

			/* check whether data[3:0] is 1111 */
			data_down = mci_readl(smc_host, REG_STAS);
			if (data_down & SDXC_CardPresent) {
				u32 rval = mci_readl(smc_host, REG_RINTR);
				if ((rval & SDXC_VolChgDone & SDXC_CmdDone)
						== (SDXC_VolChgDone & SDXC_CmdDone)) {
					smc_host->voltage = SDC_WOLTAGE_1V8;
					mci_writew(smc_host, REG_RINTR,
						SDXC_VolChgDone | SDXC_CmdDone);
					smc_host->voltage_switching = 0;
					return 0;
				}
			}
		}

		/*
		 * If we are here, that means the switch to 1.8V signaling
		 * failed. We power cycle the card, and retry initialization
		 * sequence by setting S18R to 0.
		 */
		usleep_range(5000, 5500);
		sw_mci_set_vddio(smc_host, SDC_WOLTAGE_OFF);
		usleep_range(1000, 1500);
		sw_mci_set_vddio(smc_host, SDC_WOLTAGE_3V3);
		SMC_ERR(smc_host, ": Switching to 1.8V signalling "
			"voltage failed, retrying with S18R set to 0\n");
		mci_writel(smc_host, REG_GCTRL, mci_readl(smc_host, REG_GCTRL)|SDXC_HWReset);
		mci_writew(smc_host, REG_RINTR, SDXC_VolChgDone | SDXC_CmdDone);
		sw_mci_oclk_onoff(smc_host, 1, 0);
		smc_host->voltage_switching = 0;
		return -EAGAIN;
	} else
		return 0;
}

/*
 * Here we execute a tuning operation to find the sample window of MMC host.
 * Then we select the best sampling point in the host for DDR50, SDR50, and
 * SDR104 modes.
 */
static int sw_mci_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	static const char tuning_blk_4b[] = {
		0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc,
		0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
		0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb,
		0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
		0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c,
		0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
		0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff,
		0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde
	};
	static const char tuning_blk_8b[] = {
		0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
		0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc,
		0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff,
		0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff,
		0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd,
		0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb,
		0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff,
		0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff,
		0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00,
		0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc,
		0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff,
		0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee,
		0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd,
		0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff,
		0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff,
		0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee
	};
	struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
	u32 sample_min = 1;
	u32 sample_max = 0;
	u32 sample_bak = smc_host->sclk_dly;
	u32 sample_dly = 0;
	u32 sample_win = 0;
	u32 loops = 64;
	u32 tuning_done = 0;
	char* rcv_pattern = (char*)kmalloc(128, GFP_KERNEL|GFP_DMA);
	char* std_pattern = NULL;
	int err = 0;

	if (!rcv_pattern) {
		SMC_ERR(smc_host, "sdc%d malloc tuning pattern buffer failed\n",
				smc_host->pdev->id);
		return -EIO;
	}
	SMC_MSG(smc_host, "sdc%d executes tuning operation\n", smc_host->pdev->id);
	/*
	 * The Host Controller needs tuning only in case of SDR104 mode
	 * and for SDR50 mode. Issue CMD19 repeatedly till get all of the
	 * sample points or the number of loops reaches 40 times or a
	 * timeout of 150ms occurs.
	 */
	do {
		struct mmc_command cmd = {0};
		struct mmc_data data = {0};
		struct mmc_request mrq = {0};
		struct scatterlist sg;

		sw_mci_set_clk_dly(smc_host, smc_host->oclk_dly, sample_dly);
		cmd.opcode = opcode;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
		if (opcode == MMC_SEND_TUNING_BLOCK_HS200) {
			if (mmc->ios.bus_width == MMC_BUS_WIDTH_8) {
				sg.length = 128;
				data.blksz = 128;
				std_pattern = (char*)tuning_blk_8b;
			} else if (mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
				sg.length = 64;
				data.blksz = 64;
				std_pattern = (char*)tuning_blk_4b;
			}
		} else {
			sg.length = 64;
			data.blksz = 64;
			std_pattern = (char*)tuning_blk_4b;
		}
		data.blocks = 1;
		data.flags = MMC_DATA_READ;
		data.sg = &sg;
		data.sg_len = 1;
		sg_init_one(&sg, rcv_pattern, sg.length);

		mrq.cmd = &cmd;
		mrq.data = &data;

		mmc_wait_for_req(mmc, &mrq);
		/*
		 * If no error happened in the transmission, compare data with
		 * the tuning pattern. If there is no error, record the minimal
		 * and the maximal value of the sampling clock delay to find
		 * the best sampling point in the sampling window.
		 */
		if (!cmd.error && !data.error) {
			if (!memcmp(rcv_pattern, std_pattern, data.blksz)) {
				SMC_MSG(smc_host, "sdc%d tuning ok, sclk_dly %d\n",
					smc_host->pdev->id, sample_dly);
				if (!sample_win)
					sample_min = sample_dly;
				sample_win++;
				if (sample_dly == 7) {
					SMC_MSG(smc_host, "sdc%d tuning reach to max sclk_dly 7\n",
						smc_host->pdev->id);
					tuning_done = 1;
					sample_max = sample_dly;
					break;
				}
			} else if (sample_win) {
				SMC_MSG(smc_host, "sdc%d tuning data failed, sclk_dly %d\n",
					smc_host->pdev->id, sample_dly);
				tuning_done = 1;
				sample_max = sample_dly-1;
				break;
			}
		} else if (sample_win) {
			SMC_MSG(smc_host, "sdc%d tuning trans fail, sclk_dly %d\n",
				smc_host->pdev->id, sample_dly);
			tuning_done = 1;
			sample_max = sample_dly-1;
			break;
		}
		sample_dly++;
		/* if sclk_dly reach to 7(maximum), down the clock and tuning again */
		if (sample_dly == 8 && loops)
			break;
	} while (!tuning_done && loops--);

	/* select the best sampling point from the sampling window */
	if (sample_win) {
		sample_dly = sample_min + sample_win/2;
		SMC_MSG(smc_host, "sdc%d sample_window:[%d, %d], sample_point %d\n",
				smc_host->pdev->id, sample_min, sample_max, sample_dly);
		sw_mci_set_clk_dly(smc_host, smc_host->oclk_dly, sample_dly);
		err = 0;
	} else {
		SMC_ERR(smc_host, "sdc%d cannot find a sample point\n", smc_host->pdev->id);
		sw_mci_set_clk_dly(smc_host, smc_host->oclk_dly, sample_bak);
		mmc->ios.bus_width = MMC_BUS_WIDTH_1;
		mmc->ios.timing = MMC_TIMING_LEGACY;
		err = -EIO;
	}

	kfree(rcv_pattern);
	return err;
}

/*
 * Here provide a function to scan card, for some SDIO cards that
 * may stay in busy status after writing operations. MMC host does
 * not wait for ready itself. So the driver of this kind of cards
 * should call this function to check the real status of the card.
 */
void sunximmc_rescan_card(unsigned id, unsigned insert)
{
	struct sunxi_mmc_host *smc_host = NULL;
	if (id > 3) {
		pr_err("%s: card id more than 3.\n", __func__);
		return;
	}

	mutex_lock(&sw_host_rescan_mutex);
	smc_host = sw_host[id];
	if (!smc_host)
		sw_host_rescan_pending[id] = insert;
	mutex_unlock(&sw_host_rescan_mutex);
	if (!smc_host)
		return;

	smc_host->present = insert ? 1 : 0;
	mmc_detect_change(smc_host->mmc, 0);
	return;
}
EXPORT_SYMBOL_GPL(sunximmc_rescan_card);

int sw_mci_check_r1_ready(struct mmc_host* mmc, unsigned ms)
{
	struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
	unsigned expire = jiffies + msecs_to_jiffies(ms);
	do {
		if (!(mci_readl(smc_host, REG_STAS) & SDXC_CardDataBusy))
			break;
	} while (jiffies < expire);

	if ((mci_readl(smc_host, REG_STAS) & SDXC_CardDataBusy)) {
		SMC_MSG(smc_host, "wait r1 rdy %d ms timeout\n", ms);
		return -1;
	} else
		return 0;
}
EXPORT_SYMBOL_GPL(sw_mci_check_r1_ready);

static struct mmc_host_ops sw_mci_ops = {
	.request	= sw_mci_request,
	.set_ios	= sw_mci_set_ios,
	.get_ro		= sw_mci_get_ro,
	.get_cd		= sw_mci_card_present,
	.enable_sdio_irq= sw_mci_enable_sdio_irq,
	.hw_reset	= sw_mci_hw_reset,
	.start_signal_voltage_switch = sw_mci_do_voltage_switch,
	.execute_tuning = sw_mci_execute_tuning,
};

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
static int sw_mci_proc_drvversion(char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
	char *p = page;

	p += sprintf(p, "%s\n", DRIVER_VERSION);
	return p - page;
}

static int sw_mci_proc_hostinfo(char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
	char *p = page;
	struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;
	struct device* dev = &smc_host->pdev->dev;
	char* cd_mode[] = {"None", "GPIO Check", "GPIO IRQ", "Always In", "Manual"};
	char* state[] = {"Idle", "Sending CMD", "CMD Done"};
	char* vol[] = {"3.3V", "1.8V", "1.2V", "off"};
	u32 Fmclk_MHz = (smc_host->mod_clk == 24000000 ? 24000000 : 600000000)/1000000;
	u32 Tmclk_ns = Fmclk_MHz ? 10000/Fmclk_MHz : 0;
	u32 odly = smc_host->oclk_dly ? Tmclk_ns*smc_host->oclk_dly : Tmclk_ns >> 1;
	u32 sdly = smc_host->sclk_dly ? Tmclk_ns*smc_host->sclk_dly : Tmclk_ns >> 1;

	p += sprintf(p, " %s Host Info:\n", dev_name(dev));
	p += sprintf(p, " REG Base  : %p\n", smc_host->reg_base);
	p += sprintf(p, " DMA Desp  : %p(%08x)\n", smc_host->sg_cpu, smc_host->sg_dma);
	p += sprintf(p, " Mod Clock : %d\n", smc_host->mod_clk);
	p += sprintf(p, " Card Clock: %d\n", smc_host->card_clk);
	p += sprintf(p, " Oclk Delay: %d(%d.%dns)\n", smc_host->oclk_dly, odly/10, odly%10);
	p += sprintf(p, " Sclk Delay: %d(%d.%dns)\n", smc_host->sclk_dly, sdly/10, odly%10);
	p += sprintf(p, " Bus Width : %d\n", smc_host->bus_width);
	p += sprintf(p, " DDR Mode  : %d\n", smc_host->ddr);
	p += sprintf(p, " Voltage   : %s\n", vol[smc_host->voltage]);
	p += sprintf(p, " Present   : %d\n", smc_host->present);
	p += sprintf(p, " CD Mode   : %s\n", cd_mode[smc_host->cd_mode]);
	p += sprintf(p, " Read Only : %d\n", smc_host->read_only);
	p += sprintf(p, " State     : %s\n", state[smc_host->state]);
	p += sprintf(p, " Regulator : %s\n", smc_host->pdata->regulator);

	return p - page;
}

static int sw_mci_proc_read_regs(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	char *p = page;
	struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;
	u32 i;

	p += sprintf(p, "Dump smc regs:\n");
	for (i=0; i<0x100; i+=4) {
		if (!(i&0xf))
			p += sprintf(p, "\n0x%08x : ", (u32)(smc_host->reg_base + i));
		p += sprintf(p, "%08x ", readl(smc_host->reg_base + i));
	}
	p += sprintf(p, "\n");

	p += sprintf(p, "Dump ccmu regs:\n");
	for (i=0; i<0x170; i+=4) {
		if (!(i&0xf))
			p += sprintf(p, "\n0x%08x : ", SW_VA_CCM_IO_BASE + i);
		p += sprintf(p, "%08x ", readl(SW_VA_CCM_IO_BASE + i));
	}
	p += sprintf(p, "\n");

	p += sprintf(p, "Dump gpio regs:\n");
	for (i=0; i<0x120; i+=4) {
		if (!(i&0xf))
			p += sprintf(p, "\n0x%08x : ", SW_VA_PORTC_IO_BASE + i);
		p += sprintf(p, "%08x ", readl(SW_VA_PORTC_IO_BASE + i));
	}
	p += sprintf(p, "\n");

	p += sprintf(p, "Dump gpio irqc:\n");
	for (i=0x200; i<0x300; i+=4) {
		if (!(i&0xf))
			p += sprintf(p, "\n0x%08x : ", SW_VA_PORTC_IO_BASE + i);
		p += sprintf(p, "%08x ", readl(SW_VA_PORTC_IO_BASE + i));
	}
	p += sprintf(p, "\n");

	return p - page;
}

static int sw_mci_proc_read_dbglevel(char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
	char *p = page;
	struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;

	p += sprintf(p, "Debug-Level : 0- msg&err, 1- +info, 2- +dbg, 3- all\n");
	p += sprintf(p, "current debug-level : %d\n", smc_host->debuglevel);
	return p - page;
}

static int sw_mci_proc_write_dbglevel(struct file *file, const char __user *buffer,
					unsigned long count, void *data)
{
	u32 smc_debug;
	struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;
	smc_debug = simple_strtoul(buffer, NULL, 10);

	smc_host->debuglevel = smc_debug;
	return sizeof(smc_debug);
}

static int sw_mci_proc_read_cdmode(char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
	char *p = page;
	struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;

	p += sprintf(p, "card detect mode: %d\n", smc_host->cd_mode);
	return p - page;
}

static int sw_mci_proc_write_cdmode(struct file *file, const char __user *buffer,
					unsigned long count, void *data)
{
	u32 cdmode;
	struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;
	cdmode = simple_strtoul(buffer, NULL, 10);

	smc_host->cd_mode = cdmode;
	return sizeof(cdmode);
}

static int sw_mci_proc_read_insert_status(char *page, char **start, off_t off,
					int coutn, int *eof, void *data)
{
	char *p = page;
	struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;

	p += sprintf(p, "Usage: \"echo 1 > insert\" to scan card and "
			"\"echo 0 > insert\" to remove card\n");
	if (smc_host->cd_mode != CARD_DETECT_BY_FS)
		p += sprintf(p, "Sorry, this node if only for manual "
				"attach mode(cd mode 4)\n");

	p += sprintf(p, "card attach status: %s\n",
		smc_host->present ? "inserted" : "removed");


	return p - page;
}

static int sw_mci_proc_card_insert_ctrl(struct file *file, const char __user *buffer,
					unsigned long count, void *data)
{
	u32 insert = simple_strtoul(buffer, NULL, 10);
	struct sunxi_mmc_host *smc_host = (struct sunxi_mmc_host *)data;
	u32 present = insert ? 1 : 0;

	if (smc_host->present ^ present) {
		smc_host->present = present;
		mmc_detect_change(smc_host->mmc, msecs_to_jiffies(300));
	}

	return sizeof(insert);
}

void sw_mci_procfs_attach(struct sunxi_mmc_host *smc_host)
{
	struct device *dev = &smc_host->pdev->dev;
	char sw_mci_proc_rootname[32] = {0};

	//make mmc dir in proc fs path
	snprintf(sw_mci_proc_rootname, sizeof(sw_mci_proc_rootname),
			"driver/%s", dev_name(dev));
	smc_host->proc_root = proc_mkdir(sw_mci_proc_rootname, NULL);
	if (IS_ERR(smc_host->proc_root))
		SMC_MSG(smc_host, "%s: failed to create procfs \"driver/mmc\".\n", dev_name(dev));

	smc_host->proc_drvver = create_proc_read_entry("drv-version", 0444,
				smc_host->proc_root, sw_mci_proc_drvversion, NULL);
	if (IS_ERR(smc_host->proc_root))
		SMC_MSG(smc_host, "%s: failed to create procfs \"drv-version\".\n", dev_name(dev));

	smc_host->proc_hostinfo = create_proc_read_entry("hostinfo", 0444,
				smc_host->proc_root, sw_mci_proc_hostinfo, smc_host);
	if (IS_ERR(smc_host->proc_hostinfo))
		SMC_MSG(smc_host, "%s: failed to create procfs \"hostinfo\".\n", dev_name(dev));

	smc_host->proc_regs = create_proc_read_entry("register", 0444,
				smc_host->proc_root, sw_mci_proc_read_regs, smc_host);
	if (IS_ERR(smc_host->proc_regs))
		SMC_MSG(smc_host, "%s: failed to create procfs \"hostinfo\".\n", dev_name(dev));

	smc_host->proc_dbglevel = create_proc_entry("debug-level", 0644, smc_host->proc_root);
	if (IS_ERR(smc_host->proc_dbglevel))
		SMC_MSG(smc_host, "%s: failed to create procfs \"debug-level\".\n", dev_name(dev));

	smc_host->proc_dbglevel->data = smc_host;
	smc_host->proc_dbglevel->read_proc = sw_mci_proc_read_dbglevel;
	smc_host->proc_dbglevel->write_proc = sw_mci_proc_write_dbglevel;

	smc_host->proc_cdmode = create_proc_entry("cdmode", 0644, smc_host->proc_root);
	if (IS_ERR(smc_host->proc_cdmode))
		SMC_MSG(smc_host, "%s: failed to create procfs \"cdmode\".\n", dev_name(dev));

	smc_host->proc_cdmode->data = smc_host;
	smc_host->proc_cdmode->read_proc = sw_mci_proc_read_cdmode;
	smc_host->proc_cdmode->write_proc = sw_mci_proc_write_cdmode;

	smc_host->proc_insert = create_proc_entry("insert", 0644, smc_host->proc_root);
	if (IS_ERR(smc_host->proc_insert))
		SMC_MSG(smc_host, "%s: failed to create procfs \"insert\".\n", dev_name(dev));

	smc_host->proc_insert->data = smc_host;
	smc_host->proc_insert->read_proc = sw_mci_proc_read_insert_status;
	smc_host->proc_insert->write_proc = sw_mci_proc_card_insert_ctrl;
}

void sw_mci_procfs_remove(struct sunxi_mmc_host *smc_host)
{
	struct device *dev = &smc_host->pdev->dev;
	char sw_mci_proc_rootname[32] = {0};

	snprintf(sw_mci_proc_rootname, sizeof(sw_mci_proc_rootname),
		"driver/%s", dev_name(dev));
	remove_proc_entry("io-drive", smc_host->proc_root);
	remove_proc_entry("insert", smc_host->proc_root);
	remove_proc_entry("cdmode", smc_host->proc_root);
	remove_proc_entry("debug-level", smc_host->proc_root);
	remove_proc_entry("register", smc_host->proc_root);
	remove_proc_entry("hostinfo", smc_host->proc_root);
	remove_proc_entry("drv-version", smc_host->proc_root);
	remove_proc_entry(sw_mci_proc_rootname, NULL);
}

#else

void sw_mci_procfs_attach(struct sunxi_mmc_host *smc_host) { }
void sw_mci_procfs_remove(struct sunxi_mmc_host *smc_host) { }

#endif	//PROC_FS

static int __devinit sw_mci_probe(struct platform_device *pdev)
{
	struct sunxi_mmc_host *smc_host = NULL;
	struct mmc_host	*mmc = NULL;
	int ret = 0;

	mmc = mmc_alloc_host(sizeof(struct sunxi_mmc_host), &pdev->dev);
	if (!mmc) {
		SMC_ERR(smc_host, "mmc alloc host failed\n");
		ret = -ENOMEM;
		goto probe_out;
	}

	smc_host = mmc_priv(mmc);
	memset((void*)smc_host, 0, sizeof(smc_host));
	smc_host->mmc	= mmc;
	smc_host->pdev	= pdev;
	smc_host->pdata	= pdev->dev.platform_data;
	smc_host->cd_mode = smc_host->pdata->cdmode;
	smc_host->io_flag = smc_host->pdata->isiodev ? 1 : 0;
	smc_host->debuglevel = CONFIG_MMC_PRE_DBGLVL_SUNXI;

	spin_lock_init(&smc_host->lock);
	tasklet_init(&smc_host->tasklet, sw_mci_tasklet, (unsigned long) smc_host);

	if (sw_mci_resource_request(smc_host)) {
		SMC_ERR(smc_host, "%s: Failed to get resouce.\n", dev_name(&pdev->dev));
		goto probe_free_host;
	}

	smc_host->mod_clk = 400000;
	if (sw_mci_set_clk(smc_host, 400000)) {
		SMC_ERR(smc_host, "Failed to set clock to 400KHz\n");
		ret = -ENOENT;
		goto probe_free_resource;
	}
	clk_enable(smc_host->mclk);
	clk_enable(smc_host->hclk);
	sw_mci_init_host(smc_host);

	sw_mci_procfs_attach(smc_host);

	smc_host->irq = SMC_IRQNO(pdev->id);
	if (request_irq(smc_host->irq, sw_mci_irq, 0, DRIVER_NAME, smc_host)) {
		SMC_ERR(smc_host, "Failed to request smc card interrupt.\n");
		ret = -ENOENT;
		goto probe_free_resource;
	}
	disable_irq(smc_host->irq);

	if (smc_host->cd_mode == CARD_ALWAYS_PRESENT) {
		smc_host->present = 1;
	} else if (smc_host->cd_mode == CARD_DETECT_BY_GPIO_IRQ) {
#if 0 // FIXME
		u32 cd_hdle;
		cd_hdle = sw_gpio_irq_request(smc_host->pdata->cd.gpio, TRIG_EDGE_DOUBLE,
					&sw_mci_cd_irq, smc_host);
		if (!cd_hdle) {
			SMC_ERR(smc_host, "Failed to get gpio irq for card detection\n");
		}
		smc_host->cd_hdle = cd_hdle;
		smc_host->present = !__gpio_get_value(smc_host->pdata->cd.gpio);
#else
		SMC_ERR(smc_host, "irq based card detect not supported\n");
		ret = -ENOENT;
		goto probe_free_resource;
#endif
	} else if (smc_host->cd_mode == CARD_DETECT_BY_GPIO_POLL) {
		init_timer(&smc_host->cd_timer);
		smc_host->cd_timer.expires = jiffies + 1*HZ;
		smc_host->cd_timer.function = &sw_mci_cd_cb;
		smc_host->cd_timer.data = (unsigned long)smc_host;
		add_timer(&smc_host->cd_timer);
		smc_host->present = 0;
	}

	mmc->ops        = &sw_mci_ops;
	mmc->ocr_avail	= smc_host->pdata->ocr_avail;
	mmc->caps	= smc_host->pdata->caps;
	mmc->caps2	= smc_host->pdata->caps2;
	mmc->pm_caps	= MMC_PM_KEEP_POWER|MMC_PM_WAKE_SDIO_IRQ;
	mmc->f_min	= smc_host->pdata->f_min;
	mmc->f_max      = smc_host->pdata->f_max;
	mmc->max_blk_count	= 8192;
	mmc->max_blk_size	= 4096;
	mmc->max_req_size	= mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size	= mmc->max_req_size;
	mmc->max_segs	    	= 128;
	if (smc_host->io_flag)
		mmc->pm_flags = MMC_PM_IGNORE_PM_NOTIFY;

	ret = mmc_add_host(mmc);
	if (ret) {
		SMC_ERR(smc_host, "Failed to add mmc host.\n");
		goto probe_free_irq;
	}
	platform_set_drvdata(pdev, mmc);

	mutex_lock(&sw_host_rescan_mutex);
	if (sw_host_rescan_pending[pdev->id]) {
		smc_host->present = 1;
		mmc_detect_change(smc_host->mmc, msecs_to_jiffies(300));
	}
	sw_host[pdev->id] = smc_host;
	mutex_unlock(&sw_host_rescan_mutex);

	SMC_MSG(smc_host, "sdc%d Probe: base:0x%p irq:%u sg_cpu:%p(%x) ret %d.\n",
		pdev->id, smc_host->reg_base, smc_host->irq,
		smc_host->sg_cpu, smc_host->sg_dma, ret);

	goto probe_out;

probe_free_irq:
	if (smc_host->irq)
		free_irq(smc_host->irq, smc_host);
probe_free_resource:
	sw_mci_resource_release(smc_host);
probe_free_host:
	mmc_free_host(mmc);
probe_out:
	return ret;
}

static int __devexit sw_mci_remove(struct platform_device *pdev)
{
	struct mmc_host    	*mmc  = platform_get_drvdata(pdev);
	struct sunxi_mmc_host	*smc_host = mmc_priv(mmc);

	SMC_MSG(smc_host, "%s: Remove.\n", dev_name(&pdev->dev));

	sw_mci_exit_host(smc_host);

	sw_mci_procfs_remove(smc_host);
	mmc_remove_host(mmc);

	tasklet_disable(&smc_host->tasklet);
	free_irq(smc_host->irq, smc_host);
	if (smc_host->cd_mode == CARD_DETECT_BY_GPIO_POLL)
		del_timer(&smc_host->cd_timer);
#if 0
	else if (smc_host->cd_mode == CARD_DETECT_BY_GPIO_IRQ)
		sw_gpio_irq_free(smc_host->cd_hdle);
#endif

	sw_mci_resource_release(smc_host);

	mmc_free_host(mmc);
	sw_host[pdev->id] = NULL;

	return 0;
}

#ifdef CONFIG_PM

void sw_mci_regs_save(struct sunxi_mmc_host* smc_host)
{
	struct sunxi_mmc_ctrl_regs* bak_regs = &smc_host->bak_regs;

	bak_regs->gctrl		= mci_readl(smc_host, REG_GCTRL);
	bak_regs->clkc		= mci_readl(smc_host, REG_CLKCR);
	bak_regs->timeout	= mci_readl(smc_host, REG_TMOUT);
	bak_regs->buswid	= mci_readl(smc_host, REG_WIDTH);
	bak_regs->waterlvl	= mci_readl(smc_host, REG_FTRGL);
	bak_regs->funcsel	= mci_readl(smc_host, REG_FUNS);
	bak_regs->debugc	= mci_readl(smc_host, REG_DBGC);
	bak_regs->idmacc	= mci_readl(smc_host, REG_DMAC);
}

void sw_mci_regs_restore(struct sunxi_mmc_host* smc_host)
{
	struct sunxi_mmc_ctrl_regs* bak_regs = &smc_host->bak_regs;

	mci_writel(smc_host, REG_GCTRL, bak_regs->gctrl   );
	mci_writel(smc_host, REG_CLKCR, bak_regs->clkc    );
	mci_writel(smc_host, REG_TMOUT, bak_regs->timeout );
	mci_writel(smc_host, REG_WIDTH, bak_regs->buswid  );
	mci_writel(smc_host, REG_FTRGL, bak_regs->waterlvl);
	mci_writel(smc_host, REG_FUNS , bak_regs->funcsel );
	mci_writel(smc_host, REG_DBGC , bak_regs->debugc  );
	mci_writel(smc_host, REG_DMAC , bak_regs->idmacc  );
}

static int sw_mci_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	int ret = 0;

	if (mmc) {
		struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
		ret = mmc_suspend_host(mmc);
		smc_host->suspend = ret ? 0 : 1;
		if (!ret && mmc_card_keep_power(mmc)) {
			sw_mci_regs_save(smc_host);
			/* gate clock for lower power */
			clk_disable(smc_host->hclk);
			clk_disable(smc_host->mclk);
		}
		SMC_MSG(NULL, "smc %d suspend\n", pdev->id);
	}

	return ret;
}

static int sw_mci_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	int ret = 0;

	if (mmc) {
		struct sunxi_mmc_host *smc_host = mmc_priv(mmc);
		smc_host->suspend = 0;
		if (mmc_card_keep_power(mmc)) {
			/* enable clock for resotre */
			clk_enable(smc_host->mclk);
			clk_enable(smc_host->hclk);
			sw_mci_regs_restore(smc_host);
			sw_mci_update_clk(smc_host);
		}
		if (smc_host->cd_mode == CARD_DETECT_BY_GPIO_IRQ)
			sw_mci_cd_cb((unsigned long)smc_host);
		ret = mmc_resume_host(mmc);
		smc_host->suspend = ret ? 1 : 0;
		SMC_MSG(NULL, "smc %d resume\n", pdev->id);
	}

	return ret;
}

static const struct dev_pm_ops sw_mci_pm = {
	.suspend	= sw_mci_suspend,
	.resume		= sw_mci_resume,
};
#define sw_mci_pm_ops &sw_mci_pm

#else /* CONFIG_PM */

#define sw_mci_pm_ops NULL

#endif /* CONFIG_PM */

static struct sunxi_mmc_platform_data sw_mci_pdata[4] = {
	[0] = {
		.ocr_avail = MMC_VDD_28_29 | MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32
				| MMC_VDD_32_33 | MMC_VDD_33_34,
		.caps = MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED
			| MMC_CAP_SDIO_IRQ
			| MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR50
			| MMC_CAP_UHS_DDR50
			| MMC_CAP_SET_XPC_330 | MMC_CAP_DRIVER_TYPE_A,
		.f_min = 400000,
		.f_max = 50000000,
		.f_ddr_max = 47000000,
		.dma_tl= 0x20070008,
	},
	[1] = {
		.ocr_avail = MMC_VDD_28_29 | MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32
				| MMC_VDD_32_33 | MMC_VDD_33_34,
		.caps = MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED
			| MMC_CAP_SDIO_IRQ,
		.f_min = 400000,
		.f_max = 50000000,
		.dma_tl= 0x20070008,
	},
	[2] = {
		.ocr_avail = MMC_VDD_28_29 | MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32
				| MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195,
		.caps = MMC_CAP_4_BIT_DATA | MMC_CAP_NONREMOVABLE
			| MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED
			| MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR50
			| MMC_CAP_UHS_DDR50
			| MMC_CAP_1_8V_DDR
			#ifndef CONFIG_AW_FPGA_PLATFORM
			| MMC_CAP_8_BIT_DATA
			#endif
			| MMC_CAP_SDIO_IRQ
			| MMC_CAP_SET_XPC_330 | MMC_CAP_DRIVER_TYPE_A,
		.caps2 = MMC_CAP2_HS200_1_8V_SDR,
		.f_min = 400000,
		.f_max = 120000000,
		.f_ddr_max = 50000000,
		.dma_tl= 0x20070008,
	},
	[3] = {
		.ocr_avail = MMC_VDD_28_29 | MMC_VDD_29_30 | MMC_VDD_30_31 | MMC_VDD_31_32
				| MMC_VDD_32_33 | MMC_VDD_33_34 | MMC_VDD_165_195,
		.caps = MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE
			| MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED
			| MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR50
			| MMC_CAP_UHS_DDR50
			| MMC_CAP_1_8V_DDR
			| MMC_CAP_8_BIT_DATA
			| MMC_CAP_SDIO_IRQ
			| MMC_CAP_SET_XPC_330 | MMC_CAP_DRIVER_TYPE_A,
		.caps2 = MMC_CAP2_HS200_1_8V_SDR,
		.f_min = 400000,
		.f_max = 120000000,
		.f_ddr_max = 50000000,
		.dma_tl= MMC3_DMA_TL,
	},
};
static struct platform_device sw_mci_device[4] = {
	[0] = {.name = DRIVER_NAME, .id = 0, .dev.platform_data = &sw_mci_pdata[0]},
	[1] = {.name = DRIVER_NAME, .id = 1, .dev.platform_data = &sw_mci_pdata[1]},
	[2] = {.name = DRIVER_NAME, .id = 2, .dev.platform_data = &sw_mci_pdata[2]},
	[3] = {.name = DRIVER_NAME, .id = 3, .dev.platform_data = &sw_mci_pdata[3]},
};

static struct platform_driver sw_mci_driver = {
	.driver.name    = DRIVER_NAME,
	.driver.owner   = THIS_MODULE,
	.driver.pm	= sw_mci_pm_ops,
	.probe          = sw_mci_probe,
	.remove         = __devexit_p(sw_mci_remove),
};

static int __init sw_mci_get_mmcinfo(int i)
{
	int j, r, val;
	char p[16];
	struct sunxi_mmc_platform_data* mmcinfo;
	script_parser_value_type_t type;

	mmcinfo = &sw_mci_pdata[i];
	sprintf(p, "mmc%d_para", i);

	/* get used information */
	r = script_parser_fetch(p, "sdc_used", &val, 1);
	if (r != 0) {
		SMC_MSG(NULL, "get mmc%d's used failed\n", i);
		goto fail;
	}
	mmcinfo->used = val;
	if (!mmcinfo->used)
		return 0;

	/* get cdmode information */
	r = script_parser_fetch(p, "sdc_detmode", &val, 1);
	if (r != 0) {
		SMC_MSG(NULL, "get mmc%d's detmode failed\n", i);
		goto fail;
	}
	mmcinfo->cdmode = val;
	if (mmcinfo->cdmode == CARD_DETECT_BY_GPIO_POLL ||
		mmcinfo->cdmode == CARD_DETECT_BY_GPIO_IRQ) {
		mmcinfo->cd = gpio_request_ex(p, "sdc_det");
		if (!mmcinfo->cd) {
			SMC_MSG(NULL, "get mmc%d's IO(det) failed\n", i);
			goto fail;
		}
	}
	/* get buswidth information */
	r = script_parser_fetch(p, "sdc_buswidth", &val, 1);
	if (r == 0) {
		mmcinfo->width = val;
	} else {
		/* No bus_width info, use old driver hardcoded defaults */
		mmcinfo->width = 4;
	}

	/* get mmc IOs information */
	for (j = 0; j < mmcinfo->width + 2; j++) {
		mmcinfo->mmcio[j] = gpio_request_ex(p, mmc_para_io[j]);
		if (!mmcinfo->mmcio[j]) {
			SMC_MSG(NULL, "get mmc%d's IO(%s) failed\n", i,
				mmc_para_io[j]);
			goto fail;
		}
		r = gpio_get_one_pin_status(mmcinfo->mmcio[j],
			&mmcinfo->mmcio_settings[j], mmc_para_io[j], 0);
		if (r != 0) {
			SMC_MSG(NULL, "get mmc%d's IO(%s) settings failed\n",
				i, mmc_para_io[j]);
			goto fail;
		}
	}

	/* get wpmode information */
	r = script_parser_fetch(p, "sdc_use_wp", &val, 1);
	if (r == 0) {
		mmcinfo->wpmode = val;
	} else {
		SMC_MSG(NULL, "get mmc%d's use_wp failed\n", i);
		mmcinfo->wpmode = 0;
	}
	if (mmcinfo->wpmode) {
		/* if wpmode==1 but cann't get the wp IO, we assume there is no
		   write protect detection */
		mmcinfo->wp = gpio_request_ex(p, "sdc_wp");
		if (!mmcinfo->wp) {
			SMC_MSG(NULL, "get mmc%d's IO(sdc_wp) failed\n", i);
			mmcinfo->wpmode = 0;
		}
	}

	/* get emmc-rst information */
	mmcinfo->hwrst = gpio_request_ex(p, "emmc_rst");
	mmcinfo->has_hwrst = mmcinfo->hwrst != 0;

	/* get sdio information */
	r = script_parser_fetch(p, "sdc_isio", &val, 1);
	if (r == 0) {
		mmcinfo->isiodev = val;
	} else {
		/* No sdio info, use old driver hardcoded defaults */
		int default_iodev = sunxi_is_sun5i() ? 1 : 3;
		mmcinfo->isiodev = i == default_iodev;
	}

	/* get regulator information */
	type = SCRIPT_PARSER_VALUE_TYPE_STRING;
	r = script_parser_fetch_ex(p, "sdc_regulator",
		(int *)&mmcinfo->regulator, &type,
		sizeof(mmcinfo->regulator)/sizeof(int));
	if (r |= 0 || type != SCRIPT_PARSER_VALUE_TYPE_STRING ||
		      strcmp(mmcinfo->regulator, "none") == 0) {
		/* No regulator, clear all of the UHS features support */
		mmcinfo->caps &= ~(MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
				   MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_DDR50);
		mmcinfo->regulator[0] = 0;
	} else
		mmcinfo->regulator[sizeof(mmcinfo->regulator) - 1] = 0;

	return 0;
fail:
	SMC_MSG(NULL, "Not using mmc%d due to script.bin parse failure\n", i);
	mmcinfo->used = 0;
	return -1;
}

static int __init sw_mci_init(void)
{
	int i;
	int sdc_used = 0;
	int boot_card = 0;
	int io_used = 0;
	struct sunxi_mmc_platform_data* mmcinfo;

	SMC_MSG(NULL, "sw_mci_init\n");
	/* get devices information from sys_config.fex */
	for (i = 0; i < sw_host_num; i++)
		sw_mci_get_mmcinfo(i);

	/*
	 * Here we check whether there is a boot card. If the boot card exists,
	 * we register it firstly to make it be associatiated with the device
	 * node 'mmcblk0'. Then the applicantions of Android can fix the boot,
	 * system, data patitions on mmcblk0p1, mmcblk0p2... etc.
	 */
	for (i = 0; i < sw_host_num; i++) {
		mmcinfo = &sw_mci_pdata[i];
		if (mmcinfo->used) {
			sdc_used |= 1 << i;
			if (mmcinfo->cdmode == CARD_ALWAYS_PRESENT)
				boot_card |= 1 << i;
			if (mmcinfo->isiodev)
				io_used |= 1 << i;
		}
	}

	SMC_MSG(NULL, "MMC host used card: 0x%x, boot card: 0x%x, io_card %d\n",
					sdc_used, boot_card, io_used);
	/* register boot card firstly */
	for (i = 0; i < sw_host_num; i++) {
		if (boot_card & (1 << i))
			platform_device_register(&sw_mci_device[i]);
	}
	/* register other cards */
	for (i = 0; i < sw_host_num; i++) {
		if (boot_card & (1 << i))
			continue;
		if (sdc_used & (1 << i))
			platform_device_register(&sw_mci_device[i]);
	}

	return platform_driver_register(&sw_mci_driver);
}

static void __exit sw_mci_exit(void)
{
	SMC_MSG(NULL, "sw_mci_exit\n");
	platform_driver_unregister(&sw_mci_driver);
}


module_init(sw_mci_init);
module_exit(sw_mci_exit);

MODULE_DESCRIPTION("Winner's SD/MMC Card Controller Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Aaron.maoye<leafy.myeh@reuuimllatech.com>");
MODULE_ALIAS("platform:sunxi-mmc");
