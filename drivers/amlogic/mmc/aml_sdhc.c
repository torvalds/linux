/*
 * drivers/amlogic/mmc/aml_sdhc.c
 *
 * SDHC Driver
 *
 * Copyright (C) 2010 Amlogic, Inc.
*/
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/io.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <asm/dma-mapping.h>
#include <mach/power_gate.h>
#include <linux/clk.h>
#include <mach/register.h>
#include <mach/gpio.h>
#include <mach/gpio_data.h>
#include <mach/pinmux.h>
#include <mach/irqs.h>
#include <linux/irq.h>
#include <mach/sd.h>
#include "amlsd.h"
#include <linux/amlogic/aml_gpio_consumer.h>

/*soft reset after errors*/
void aml_sdhc_host_reset(struct amlsd_host* host)
{
    writel(SDHC_SRST_ALL, host->base+SDHC_SRST);
    udelay(5);

    writel(0, host->base+SDHC_SRST);
    udelay(10);
}

/*setup reg initial value*/
static void aml_sdhc_reg_init(struct amlsd_host* host)
{
    struct sdhc_ctrl ctrl = {0};
    struct sdhc_clkc clkc = {0};
    struct sdhc_pdma pdma = {0};
    struct sdhc_misc misc = {0};

    aml_sdhc_host_reset(host);

    ctrl.rx_timeout = 0x7f;
    ctrl.rx_period = 0xf;
    ctrl.rx_endian = 0x3;
    writel(*(u32*)&ctrl, host->base + SDHC_CTRL);

    clkc.clk_ctl_enable = 0;
    writel(*(u32*)&clkc, host->base + SDHC_CLKC);

    clkc.clk_ctl_enable = 1;
    clkc.clk_src_sel = SDHC_CLOCK_SRC_FCLK_DIV5;
    clkc.clk_en = 1;
    clkc.rx_clk_phase = 0; /*Rx clock phase 0*/
    writel(*(u32*)&clkc, host->base + SDHC_CLKC);

    pdma.dma_mode = 0;
    pdma.dma_urgent = 1;
    pdma.rd_burst = 3;
    pdma.wr_burst = 3;
    pdma.rxfifo_th = 7;
    pdma.txfifo_th = 0x18;
    host->pdma = *(u32*)&pdma;
    writel(*(u32*)&pdma, host->base + SDHC_PDMA);

    /*Send Stop Cmd manually*/
    misc.manual_stop = 0;
    misc.rxfifo_th = 0x1e;
	writel(*(u32*)&misc, host->base + SDHC_MISC);

    /*Disable All Irq*/
    writel(0, host->base + SDHC_ICTL);

    /*Write 1 Clear all irq status*/
    writel(SDHC_ISTA_W1C_ALL, host->base + SDHC_ISTA);
}

/*wait sdhc controller cmd send*/
int aml_sdhc_wait_ready(struct amlsd_host* host, u32 timeout)
{
    u32 i, vstat;
    struct sdhc_stat* stat;

    for(i=0; i< timeout; i++){
        vstat = readl(host->base + SDHC_STAT);
        stat = (struct sdhc_stat*)&vstat;
        if(!stat->cmd_busy)
            return 0;
        udelay(1);
    }

    aml_sdhc_print_reg(host);
    aml_sdhc_host_reset(host);
    WARN_ON(1);
    return -1;
}

/*
read response from REG0_ARGU(136bit or 48bit)
*/
int aml_sdhc_read_response(struct mmc_host *mmc, struct mmc_command *cmd)
{
    u32 i=0, j=0;
	struct amlsd_platform* pdata = mmc_priv(mmc);
	struct amlsd_host* host = pdata->host;
    u32 vpdma = readl(host->base+SDHC_PDMA);
    struct sdhc_pdma* pdma = (struct sdhc_pdma*)&vpdma;
    u32* presp = (u32*)cmd->resp;

    pdma->dma_mode = 0;
    if(cmd->flags & MMC_RSP_136) /*136 bit*/{
        for(i=MMC_RSP_136_NUM,j=0; i>=1; i--,j++){
            pdma->pio_rdresp = i;
            writel(vpdma ,host->base+SDHC_PDMA);
            presp[j] = readl(host->base+SDHC_ARGU);
            sdhc_dbg(AMLSD_DBG_RESP,"Cmd %d ,Resp[%d] 0x%x\n",
                    cmd->opcode, j, presp[j]);
        }
    }
    else/*48 bit*/{
        pdma->pio_rdresp = i;
        writel(vpdma ,host->base+SDHC_PDMA);
        presp[0] = readl(host->base+SDHC_ARGU);
        sdhc_dbg(AMLSD_DBG_RESP,"Cmd%d ,Resp 0x%x\n", cmd->opcode, presp[0]);
    }
    return 0;
}

/*clear fifo after transfer data*/
void aml_sdhc_clear_fifo(struct amlsd_host* host)
{
    struct sdhc_srst srst;

    srst.rxfifo = 1;
    srst.txfifo = 1;
    writel(*(u32*)&srst, host->base+SDHC_SRST);
    udelay(1);
}

/*enable irq bit in reg SDHC_ICTL*/
inline void aml_sdhc_enable_imask(struct amlsd_host* host, u32 irq)
{
    u32 ictl = readl(host->base+SDHC_ICTL);
    ictl |= irq;
    writel(ictl, host->base+SDHC_ICTL);
}

/*disable irq bit in reg SDHC_ICTL*/
inline void aml_sdhc_disable_imask(struct amlsd_host* host, u32 irq)
{
    u32 ictl = readl(host->base+SDHC_ICTL);
    ictl &= ~irq;
    writel(ictl, host->base+SDHC_ICTL);
}

//for m6
void aml_sdhc_swap_buf(const void* buf, int bytes)
{
    u64 *buf64 = (u64*)buf;
    u64 tmpbuf64;
    int i;

    for(i=0;i<(bytes>>3);i++){
        tmpbuf64 = __swab64(*buf64);
        *buf64++ = tmpbuf64;
    }
    return;
}

void aml_sdhc_set_pdma(struct mmc_host* mmc, struct mmc_request* mrq)
{
	struct amlsd_platform* pdata = mmc_priv(mmc);
	struct amlsd_host* host = pdata->host;
    struct sdhc_pdma* pdma = (struct sdhc_pdma*)&host->pdma;

    pdma->dma_mode = 1;
    BUG_ON(!mrq->data);
    if(mrq->data->flags & MMC_DATA_WRITE){
        if(mrq->data->blocks > 64){
            pdma->rd_burst = 7;
            pdma->wr_burst = 7;
            pdma->rxfifo_th = 7;
            pdma->txfifo_th = 0x18;
        }else{
            pdma->rd_burst = 3;
            pdma->wr_burst = 3;
            pdma->rxfifo_th = 0x7;
            pdma->txfifo_th = 0x7;
        }
    }else{
        if(mrq->data->blocks > 64){
            pdma->rd_burst = 7;
            pdma->wr_burst = 7;
            pdma->rxfifo_th = 0x18;
            pdma->txfifo_th = 7;
        }else{
            pdma->rd_burst = 3;
            pdma->wr_burst = 3;
            pdma->rxfifo_th = 0x7;
            pdma->txfifo_th = 0x7;
        }
    }
    writel(*(u32*)pdma, host->base+SDHC_PDMA);
}

/*copy buffer from data->sg to dma buffer, set dma addr to reg*/
void aml_sdhc_prepare_dma(struct amlsd_host *host, struct mmc_request *mrq)
{
    struct mmc_data *data = mrq->data;

    if(data->flags & MMC_DATA_WRITE){
        aml_sg_copy_buffer(data->sg, data->sg_len,
                host->bn_buf, data->blksz*data->blocks, 1);
        aml_sdhc_swap_buf(host->bn_buf, data->blksz*data->blocks);
        sdhc_dbg(AMLSD_DBG_WR_DATA,"W Cmd %d, %x-%x-%x-%x\n",
                mrq->cmd->opcode,
                host->bn_buf[0], host->bn_buf[1],
                host->bn_buf[2], host->bn_buf[3]);
    }
    host->dma_addr = host->bn_dma_buf;
}

/*
* set host->clkc_w for 8bit emmc write cmd as it would fail on TXFIFO EMPTY,
* we decrease the clock for write cmd, and set host->clkc for other cmd
*/
void aml_sdhc_set_clkc(struct mmc_host* mmc)
{
	struct amlsd_platform* pdata = mmc_priv(mmc);
	struct amlsd_host* host = pdata->host;
    struct sdhc_clkc* clkc = (struct sdhc_clkc*)&pdata->clkc;
    u32 vclkc = readl(host->base + SDHC_CLKC);

    if(pdata->clkc == vclkc){
        return;
    }

    /*Turn off before set it*/
    clkc->clk_ctl_enable = 0;
    writel(*(u32*)clkc, host->base + SDHC_CLKC);

    clkc->clk_ctl_enable = 1;

    sdhc_dbg(AMLSD_DBG_IOS, "vclkc %x pdata->clkc %x, clk_div %d\n",
                vclkc, pdata->clkc, clkc->clk_div);

    writel(*(u32*)clkc, host->base + SDHC_CLKC);
}

void aml_sdhc_start_cmd(struct mmc_host* mmc, struct mmc_request* mrq)
{
	struct amlsd_platform* pdata = mmc_priv(mmc);
	struct amlsd_host* host = pdata->host;
	struct sdhc_send send = {0};
	struct sdhc_ictl ictl = {0};
	u32 vctrl = readl(host->base + SDHC_CTRL);
    struct sdhc_ctrl* ctrl = (struct sdhc_ctrl*)&vctrl;

    /*Set Irq Control*/
    ictl.data_timeout = 1;
    ictl.data_err_crc = 1;
    ictl.rxfifo_full = 1;
    ictl.txfifo_empty = 1;
	ictl.resp_ok = 1;

    if(mrq->data){
        /*Command has data*/
        send.cmd_has_data = 1;

        /*Read/Write data direction*/
        if(mrq->data->flags & MMC_DATA_WRITE)
            send.data_dir = 1;

    	/*Set package size*/
        if(mrq->data->blksz < 512)
            ctrl->pack_len = mrq->data->blksz;
        else
            ctrl->pack_len = 0;

    	/*Set blocks in package*/
        send.total_pack = mrq->data->blocks - 1;
        
        ictl.resp_ok = 0;

    	if(mrq->data->blocks > 1){ /*R/W multi block*/
    		ictl.data_xfer_ok = 1;
        }else{ /*R/W single block*/
    		ictl.data_1pack_ok = 1;
        }
    }

    /* set bus width */
    ctrl->dat_type = pdata->width;

	if(mrq->cmd->flags & MMC_RSP_136)
		send.resp_len = 1;
	if(mrq->cmd->flags & MMC_RSP_PRESENT)
		send.cmd_has_resp = 1;
	if(!(mrq->cmd->flags & MMC_RSP_CRC)|| mrq->cmd->flags & MMC_RSP_136)
		send.resp_no_crc = 1;

    /*Command Index*/
	send.cmd_index = mrq->cmd->opcode;

    /*Set irq status: write 1 clear*/
	writel(SDHC_ISTA_W1C_ALL, host->base+SDHC_ISTA);

	writel(*(u32*)&ictl, host->base+SDHC_ICTL);
	writel(mrq->cmd->arg, host->base+SDHC_ARGU);
	writel(vctrl, host->base+SDHC_CTRL);
	writel(host->dma_addr, host->base+SDHC_ADDR);
	aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT); /*Wait command busy*/
    if(mrq->data)
	    aml_sdhc_set_pdma(mmc, mrq);/*Start dma transfer*/
	writel(*(u32*)&send, host->base+SDHC_SEND); /*Command send*/
}

/*mmc_request_done & do nothing in xfer_post*/
void aml_sdhc_request_done(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct amlsd_platform * pdata = mmc_priv(mmc);
	struct amlsd_host* host = pdata->host;

	host->xfer_step = XFER_FINISHED;
	aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT);
	if(pdata->xfer_post)
		pdata->xfer_post(pdata);
    // printk("[%s] ", __FUNCTION__);
	// aml_sdhc_print_reg(host); // add for debug
	mmc_request_done(host->mmc, mrq); // this should be the last action, otherwise things may wrong
}

/*error handler*/
void aml_sdhc_timeout(unsigned long data)
{
	struct amlsd_host* host = (void*)data;
	u32 vista = readl(host->base + SDHC_ISTA);
    struct sdhc_ista* ista = (struct sdhc_ista*)&vista;
	struct mmc_request* mrq;
	unsigned long flags;
    struct amlsd_platform * pdata = mmc_priv(host->mmc);
	u32 vctrl;
    struct sdhc_ctrl* ctrl;

	if(host->xfer_step == XFER_FINISHED){
		sdio_err("timeout after xfer finished\n");
		return;
	}

    if ((host->xfer_step == XFER_IRQ_TASKLET_DATA) || (host->xfer_step == XFER_IRQ_TASKLET_CMD) 
            || (host->xfer_step == XFER_IRQ_TASKLET_BUSY)) { // 
        mod_timer(&host->timeout_tlist, jiffies + 10);
        sdhc_err("%s: host->xfer_step=%d, irq have been occured and transfer is normal.\n",
                mmc_hostname(host->mmc), host->xfer_step);
        // return;
    }
    
	aml_sdhc_disable_imask(host, ICTL_ALL);

	BUG_ON(!mrq || !mrq->cmd);

	spin_lock_irqsave(&host->mrq_lock, flags);
	mrq = host->mrq;
	// sdhc_err("host->xfer_step %d\n", host->xfer_step);

    vctrl = readl(host->base + SDHC_CTRL);
    ctrl = (struct sdhc_ctrl*)&vctrl;
    sdhc_err("%s: Timeout Cmd%d arg %08x Xfer %d Bytes, "
            "host->xfer_step=%d, host->cmd_is_stop=%d, pdata->port=%d, "
            "SDHC_CLKC=%#x, pdata->clkc=%#x, ctrl->dat_type=%d, pdata->width=%d\n",
            mmc_hostname(host->mmc),
            host->mrq->cmd->opcode,
            host->mrq->cmd->arg,
            host->mrq->data?host->mrq->data->blksz*host->mrq->data->blocks:0,
            host->xfer_step, 
            host->cmd_is_stop,
            pdata->port,
            readl(host->base + SDHC_CLKC),
            pdata->clkc,
            ctrl->dat_type,
            pdata->width);
    // if (pdata->port == MESON_SDIO_PORT_XC_A) {
        // sdhc_err("power_on_pin=%d\n",
                // amlogic_get_value(185, "sdio_wifi")); // G24-113, G33-185
    // } else  
    if (pdata->port != MESON_SDIO_PORT_XC_A) {
#ifdef CONFIG_ARCH_MESON6
        if ((pdata->port == MESON_SDIO_PORT_XC_B) && (pdata->gpio_power != 0))
        sdhc_err("power_on_pin=%d\n", amlogic_get_value(pdata->gpio_power, MODULE_NAME));
#endif
        aml_dbg_print_pinmux();
        aml_sdhc_print_reg(host);
    }

    host->xfer_step = XFER_TIMEDOUT;
    spin_unlock_irqrestore(&host->mrq_lock, flags);

	/*read response, if error, set -EILSEQ, then retry in aml_sdhc_request_done*/
	aml_sdhc_read_response(host->mmc, mrq->cmd);
	sdhc_err("Cmd %d Resp 0x%x Xfer %d\n", host->mrq->cmd->opcode,
		host->mrq->cmd->resp[0],
		mrq->data?mrq->data->blksz*mrq->data->blocks:0);
	if(R1_STATUS(host->mrq->cmd->resp[0]) & R1_COM_CRC_ERROR ){
		mrq->cmd->error = -EILSEQ;
		sdhc_err("Cmd CRC error\n");
        	vista = readl(host->base+SDHC_ISTA);
		goto req_done;
	}

	mrq->cmd->error = -ETIMEDOUT;

	/*
        * response crc error & data crc error with xfer size >= 512,
        * ignore crc error with data transfer less then 512, for example 64B
        */
	if((ista->resp_err_crc && !mrq->data)||
        (ista->data_err_crc && mrq->data && mrq->data->blksz == 512)){
        mrq->cmd->error = -EILSEQ;
		sdhc_err("CRC error Cmd %d Ista 0x%x\n", mrq->cmd->opcode, vista);
		goto req_done;
	}

	/*get TXFIFO_EMPTY as a CRC error */
	if(ista->txfifo_empty){
		mrq->cmd->error = -EILSEQ;
		sdhc_err("TXFIFO_EMPTY Cmd %d Size 0x%x\n",
				mrq->cmd->opcode, mrq->data->blksz*mrq->data->blocks);
		goto req_done;
	}
	/*get RXFIFO_FULL as a CRC error */
	if(ista->rxfifo_full){
		mrq->cmd->error = -EILSEQ;
		sdhc_err("RXFIFO_FULL Cmd %d Size 0x%x\n",
				mrq->cmd->opcode, mrq->data->blksz*mrq->data->blocks);
		goto req_done;
	}

	if(ista->resp_timeout){
		sdhc_err("TIMEOUT Cmd %d Ista 0x%x\n", mrq->cmd->opcode, vista);
		mrq->cmd->error = -ETIMEDOUT;
		goto req_done;
	}
	/*timeout in CMD12(send stop), return CRC error(-EILSEQ), should retry*/
	if(ista->resp_no_busy && mrq->data && mrq->data->blocks>1){
		sdhc_err("DAT0_TURN_READY Ista 0x%x, arg %d\n",
			vista, mrq->cmd->arg);
		aml_sdhc_disable_imask(host, ISTA_DAT0_TURN_READY);
		aml_sdhc_host_reset(host);
		mrq->cmd->error = -EILSEQ;
		goto req_done;
	}

	sdhc_err("Cmd %d, Ista 0x%x Request done\n", mrq->cmd->opcode, vista);
	mrq->cmd->error = -1;

req_done:
	host->mrq = NULL;
	/*write 1 clear irq status reg*/
	writel(vista, host->base+SDHC_ISTA);
	aml_sdhc_request_done(host->mmc, mrq);
    return ;
}

#define     PORT_SDIO           PORT_SDHC_A // SDIO devices(for SDIO-WIFI)
#define     PORT_BLOCK          PORT_SDHC_B // Block devices(for SD/MMC card)
#define     PORT_BASE           PORT_SDHC_C // Base block device (for TSD/eMMC)
/*sdhc controller does not support wifi now, return*/
int aml_sdhc_check_unsupport_cmd(struct mmc_host* mmc, struct mmc_request* mrq)
{
	struct amlsd_platform * pdata = mmc_priv(mmc);

    if ((pdata->port != PORT_SDIO) && (mrq->cmd->opcode == SD_IO_SEND_OP_COND ||
        mrq->cmd->opcode == SD_IO_RW_DIRECT ||
        mrq->cmd->opcode == SD_IO_RW_EXTENDED)) {
        mrq->cmd->error = -EINVAL;
        mmc_request_done(mmc, mrq);
        return -EINVAL;
    }
    return 0;
}

/*cmd request interface*/
void aml_sdhc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct amlsd_platform * pdata;
    struct amlsd_host *host;
	u32 vista;
    unsigned long flags;

    BUG_ON(!mmc);
    BUG_ON(!mrq);

    pdata = mmc_priv(mmc);
    host = (void*)pdata->host;

    if(aml_sdhc_check_unsupport_cmd(mmc, mrq))
        return;
	if(pdata->eject){
        mrq->cmd->error = -ETIMEDOUT;
        mmc_request_done(mmc, mrq);
		return;
	}

	sdhc_dbg(AMLSD_DBG_REQ ,"%s: starting CMD%u arg %08x flags %08x\n",
		mmc_hostname(mmc), mrq->cmd->opcode,
		mrq->cmd->arg, mrq->cmd->flags);

	if(mrq->data) {
        /*Copy data to dma buffer for write request*/
		aml_sdhc_prepare_dma(host, mrq);

		sdhc_dbg(AMLSD_DBG_REQ ,"%s: blksz %d blocks %d flags %08x "
			"tsac %d ms nsac %d\n",
			mmc_hostname(mmc), mrq->data->blksz,
			mrq->data->blocks, mrq->data->flags,
			mrq->data->timeout_ns / 1000000,
			mrq->data->timeout_clks);
	}

    /*Set clock for each port, change clock before wait ready*/
	aml_sdhc_set_clkc(mmc);

	/*clear pinmux & set pinmux*/
	if(pdata->xfer_pre)
		pdata->xfer_pre(pdata);
		
	aml_sdhc_host_reset(host);

    vista = readl(host->base+SDHC_ISTA);
	writel(vista, host->base+SDHC_ISTA); // clear irqs

	if(!mrq->data)
		mod_timer(&host->timeout_tlist, jiffies + 100);
	else
		mod_timer(&host->timeout_tlist,
				jiffies + 500);

	spin_lock_irqsave(&host->mrq_lock, flags);
	if(host->xfer_step != XFER_FINISHED && host->xfer_step != XFER_INIT)
		sdhc_err("host->xfer_step %d\n", host->xfer_step);

	/*host->mrq, used in irq & tasklet*/
    host->mrq = mrq;
	host->mmc = mmc;
	host->xfer_step = XFER_START;
    host->opcode = mrq->cmd->opcode;
    host->arg = mrq->cmd->arg;

    // printk("[%s] ", __FUNCTION__);
	// aml_sdhc_print_reg(host); // add for debug

	/*setup reg for all cmd*/
	aml_sdhc_start_cmd(mmc, mrq);
	host->xfer_step = XFER_AFTER_START;
	spin_unlock_irqrestore(&host->mrq_lock, flags);
}

/*sdhc controller irq*/
static irqreturn_t aml_sdhc_irq(int irq, void *dev_id) 
{
	struct amlsd_host* host = dev_id;
	struct mmc_host* mmc;
	struct amlsd_platform* pdata;
	u32 victl = readl(host->base + SDHC_ICTL);
    u32 vista = readl(host->base + SDHC_ISTA);
    struct sdhc_ista* ista = (struct sdhc_ista*)&vista;
    struct sdhc_ictl* ictl = (struct sdhc_ictl*)&victl;
	struct mmc_request* mrq;
	unsigned long flags;

	spin_lock_irqsave(&host->mrq_lock, flags);
	mrq = host->mrq;
	mmc = host->mmc;
	pdata = mmc_priv(mmc);
	if(!mrq){
		sdhc_err("NULL mrq in aml_sdhc_irq step %d\n", host->xfer_step);
		if(host->xfer_step == XFER_FINISHED ||
			host->xfer_step == XFER_TIMEDOUT){
			spin_unlock_irqrestore(&host->mrq_lock, flags);
			return IRQ_HANDLED;
		}
		WARN_ON(!mrq);
		aml_sdhc_print_reg(host);
		spin_unlock_irqrestore(&host->mrq_lock, flags);
		return IRQ_HANDLED;
	}

	host->xfer_step = XFER_IRQ_OCCUR;

	if(victl & vista){
		/*TXFIFO EMPTY & RXFIFO FULL as CRC error*/
        if(ista->txfifo_empty){
			sdhc_err("%s Cmd %d TXFIFO_EMPTY!!! "
                "Xfer %d Bytes\n",
					pdata->pinname, mrq->cmd->opcode,
					mrq->data?mrq->data->blksz*mrq->data->blocks:0);
            host->xfer_step = XFER_IRQ_FIFO_ERR;
			spin_unlock_irqrestore(&host->mrq_lock, flags);
			return IRQ_WAKE_THREAD;
        }
		if(ista->rxfifo_full){
			sdhc_err("%s Cmd %d RXFIFO_FULL!!! "
                "Xfer %d Bytes\n",
					pdata->pinname, mrq->cmd->opcode,
					mrq->data?mrq->data->blksz*mrq->data->blocks:0);
            host->xfer_step = XFER_IRQ_FIFO_ERR;
			spin_unlock_irqrestore(&host->mrq_lock, flags);
			return IRQ_WAKE_THREAD;
        }
        if((ista->resp_err_crc && !mrq->data)||
                (ista->data_err_crc && mrq->data && mrq->data->blksz == 512)){
            mrq->cmd->error = -EILSEQ;
			sdhc_err("%s CRC ERR Cmd %d Ista 0x%x,"
                "Xfer %d Bytes\n",
				pdata->pinname, host->mrq->cmd->opcode, vista,
				mrq->data?mrq->data->blksz*mrq->data->blocks:0);
    		goto req_done;
	    }
		if(ista->data_timeout || ista->resp_timeout){
			BUG_ON(!pdata);
			sdhc_err("%s Timeout Cmd %d Ista 0x%x,"
                " Xfer %d Bytes\n",
					pdata->pinname, mrq->cmd->opcode, vista,
					mrq->data?mrq->data->blksz*mrq->data->blocks:0);
			mrq->cmd->error = -ETIMEDOUT;
			goto req_done;
		}
		if(ista->data_1pack_ok || ista->data_xfer_ok){
			host->xfer_step = XFER_IRQ_TASKLET_DATA;
			aml_sdhc_disable_imask(host, ICTL_DATA);
			writel(ISTA_DATA|ISTA_RESP, host->base+SDHC_ISTA);
			spin_unlock_irqrestore(&host->mrq_lock, flags);
			return IRQ_WAKE_THREAD;
		}else if(ictl->resp_ok && ista->resp_ok){/*cmd without data*/
			host->xfer_step = XFER_IRQ_TASKLET_CMD;
			aml_sdhc_disable_imask(host, ICRL_RESP);
			writel(ISTA_RESP, host->base+SDHC_ISTA);
			spin_unlock_irqrestore(&host->mrq_lock, flags);
			return IRQ_WAKE_THREAD;
		}
		/*after auto send stop in multi block transfer*/
		if(ista->resp_no_busy){
			host->xfer_step = XFER_IRQ_TASKLET_BUSY;
			aml_sdhc_disable_imask(host, ISTA_DAT0_TURN_READY);
			writel(ista, host->base+SDHC_ISTA);
			spin_unlock_irqrestore(&host->mrq_lock, flags);
			return IRQ_WAKE_THREAD;
		}
	}
    // else{
		host->xfer_step = XFER_IRQ_UNKNOWN_IRQ;
		sdhc_err("%s Unknown Irq Ictl 0x%x, Ista 0x%x,"
			"cmd %d, xfer %d bytes \n",
			pdata->pinname, victl, vista, mrq->cmd->opcode,
			mrq->data?mrq->data->blksz*mrq->data->blocks:0);
	// }
	spin_unlock_irqrestore(&host->mrq_lock, flags);
	return IRQ_HANDLED;

req_done:
	del_timer(&host->timeout_tlist);
	host->mrq = NULL;
	spin_unlock_irqrestore(&host->mrq_lock, flags);
	aml_sdhc_request_done(mmc, mrq);
	return IRQ_HANDLED;
}

irqreturn_t aml_sdhc_data_thread(int irq, void *data)
{
	struct amlsd_host* host = data;
	u32 xfer_bytes;
	struct mmc_request* mrq;
	enum aml_mmc_waitfor	xfer_step;
	unsigned long flags;

	if(host->xfer_step == XFER_TIMEDOUT){
		sdhc_err("Timeout Return\n");
		return IRQ_HANDLED;
	}
	if(host->xfer_step == XFER_INIT){
		return IRQ_HANDLED;
	}
	
	if(host->xfer_step == XFER_FINISHED){
		sdhc_err("XFER_FINISHED Return\n");
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&host->mrq_lock, flags);
	mrq = host->mrq;
	xfer_step = host->xfer_step;
	if(!mrq){
		sdhc_err("CMD%u, arg %08x, mrq NULL xfer_step %d\n", host->opcode, host->arg, xfer_step);
		if(xfer_step == XFER_FINISHED ||
			xfer_step == XFER_TIMEDOUT){
			spin_unlock_irqrestore(&host->mrq_lock, flags);
			sdhc_err("out\n");
			return IRQ_HANDLED;
		}
		BUG();
	}
	spin_unlock_irqrestore(&host->mrq_lock, flags);

	BUG_ON(!host->mrq->cmd);

	switch(xfer_step){
		case XFER_IRQ_TASKLET_DATA:
			BUG_ON(!mrq->data);

			xfer_bytes = mrq->data->blksz*mrq->data->blocks;
			/* copy buffer from dma to data->sg in read cmd*/
			if(host->mrq->data->flags & MMC_DATA_READ){
				aml_sg_copy_buffer(mrq->data->sg, mrq->data->sg_len, host->bn_buf,
							xfer_bytes, 0);
				sdhc_dbg(AMLSD_DBG_RD_DATA, "R Cmd %d, %x-%x-%x-%x\n",
					host->mrq->cmd->opcode,
					host->bn_buf[0], host->bn_buf[1],
					host->bn_buf[2], host->bn_buf[3]);
			}

			/*enable busy turn ready irq bit, sdhc controller would send stop automatically*/
			if(host->mrq->data->blocks > 1)
				aml_sdhc_enable_imask(host, ICTL_DAT0_TURN_READY);
			else{
				/*R/W single block, end_request here*/
				del_timer(&host->timeout_tlist);
				mrq->cmd->error = 0;
				mrq->data->bytes_xfered = xfer_bytes;
				host->xfer_step = XFER_TASKLET_DATA;
				host->mrq = NULL;
				aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT);
				aml_sdhc_read_response(host->mmc, mrq->cmd);
				aml_sdhc_clear_fifo(host);
				aml_sdhc_request_done(host->mmc, mrq);
			}
			break;
		case XFER_IRQ_TASKLET_CMD:
			if(!host->mrq->data){
				del_timer(&host->timeout_tlist);
				aml_sdhc_read_response(host->mmc, host->mrq->cmd);
				host->mrq->cmd->error = 0;
				host->xfer_step = XFER_TASKLET_CMD;
				host->mrq = NULL;
				aml_sdhc_request_done(host->mmc, mrq);
			} else {
                sdhc_err("XFER_IRQ_TASKLET_CMD error\n");
            }
			break;
		case XFER_IRQ_TASKLET_BUSY:
			BUG_ON(!host->mrq->data);
			del_timer(&host->timeout_tlist);
			aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT);
			aml_sdhc_read_response(host->mmc, host->mrq->cmd);
			aml_sdhc_clear_fifo(host);
			host->mrq->cmd->error = 0;
			xfer_bytes = host->mrq->data->blksz*host->mrq->data->blocks;
			host->mrq->data->bytes_xfered = xfer_bytes;
			host->xfer_step = XFER_TASKLET_BUSY;
			host->mrq = NULL;
			aml_sdhc_request_done(host->mmc, mrq);
			break;
		case XFER_IRQ_FIFO_ERR:
		case XFER_IRQ_CRC_ERR:
			del_timer(&host->timeout_tlist);
			aml_sdhc_wait_ready(host, STAT_POLL_TIMEOUT);
			aml_sdhc_read_response(host->mmc, host->mrq->cmd);
			aml_sdhc_host_reset(host);
			host->mrq->cmd->error = -EILSEQ;
			aml_sdhc_request_done(host->mmc, mrq);
			break;
		case XFER_IRQ_TIMEOUT_ERR:
			del_timer(&host->timeout_tlist);
			host->mrq->cmd->error = -ETIMEDOUT;
			aml_sdhc_host_reset(host);
			aml_sdhc_request_done(host->mmc, mrq);
			break;
		case XFER_INIT:
		case XFER_START:
		case XFER_IRQ_UNKNOWN_IRQ:
		case XFER_TIMER_TIMEOUT:
		case XFER_TASKLET_CMD:
		case XFER_TASKLET_DATA:
		case XFER_TASKLET_BUSY:
		case XFER_TIMEDOUT:
		case XFER_AFTER_START:
		case XFER_FINISHED:
		default:
			sdhc_err("BUG xfer_step %d\n", xfer_step);
			BUG();
			
	}
	
	return IRQ_HANDLED;
}

/*
1. clock valid range
2. clk config enable
3. select clock source
4. set clock divide
*/
static void aml_sdhc_set_clk_rate(struct mmc_host *mmc, unsigned int clk_ios)
{
    u32 clk_rate, clk_div, vclkc;
    struct clk *clk_src;
    struct amlsd_platform* pdata = mmc_priv(mmc);
    struct amlsd_host* host = (void*)pdata->host;
    struct sdhc_clkc* clkc;

    aml_sdhc_host_reset(host);

    clk_src = clk_get_sys("pll_fixed", NULL);
    clk_rate = clk_get_rate(clk_src)/5;

    /*
        *clk_ctl_enable: Every time parameters are set,
        * need turn it off and then turn it on
        */
    vclkc = readl(host->base + SDHC_CLKC);
    clkc = (struct sdhc_clkc*)&vclkc;
    clkc->clk_ctl_enable = 0;    
    writel(vclkc, host->base+SDHC_CLKC);

    sdhc_dbg(AMLSD_DBG_IOS, "Clk IOS %d, Clk Src %d, Host Max Clk %d\n",
        clk_ios, clk_rate, pdata->f_max);

    if(clk_ios > pdata->f_max)
        clk_ios = pdata->f_max;
    if(clk_ios < pdata->f_min)
        clk_ios = pdata->f_min;

    /*0: dont set it, 1:div2, 2:div3, 3:div4...*/
    clk_div = clk_rate / clk_ios - !(clk_rate%clk_ios);

    /*Set clock divide*/
    clkc->clk_div = clk_div;
    /*clk_ctrl_enable Turn it on*/
    clkc->clk_ctl_enable = 1;
    /*Set to platform data*/
    pdata->clkc = vclkc;
    // printk("%s pdata->clkc %x, clk_div %d\n",pdata->pinname, pdata->clkc, clkc->clk_div);
    writel(vclkc, host->base+SDHC_CLKC);

    /*Read Dummy Data from FIFO*/
    readl(host->base+SDHC_DATA);
    /*Disable All Irq*/
    writel(0, host->base+SDHC_ICTL);

    /*Wait for a while after clock setting*/
    udelay(100);
    return;
}

/*setup bus width, 1bit, 4bits, 8bits*/
static void aml_sdhc_set_bus_width(struct amlsd_platform* pdata, u32 busw_ios)
{
    struct amlsd_host* host = (void*)pdata->host;
    u32 vctrl = readl(host->base + SDHC_CTRL);
    struct sdhc_ctrl* ctrl = (struct sdhc_ctrl*)&vctrl;
    u32 width = 0;

    switch(busw_ios)
    {
        case MMC_BUS_WIDTH_1:
            width = 0;
            break;
        case MMC_BUS_WIDTH_4:
            width = 1;
            break;
        case MMC_BUS_WIDTH_8:
            width = 2;
            break;
        default:
            sdhc_err("Error Data Bus\n");
            break;
    }

    ctrl->dat_type = width;
    pdata->width = width;
    writel(vctrl, host->base+SDHC_CTRL);
    sdhc_dbg(AMLSD_DBG_IOS, "Bus Width Ios %d\n", busw_ios);
}

/*call by mmc, power on, power off ...*/
static void aml_sdhc_set_power(struct amlsd_platform* pdata, u32 power_mode)
{
    switch (power_mode) {
        case MMC_POWER_ON:
			if(pdata->pwr_pre)
				pdata->pwr_pre(pdata);
			if(pdata->pwr_on)
					pdata->pwr_on(pdata);
                break;
        case MMC_POWER_UP:
            break;
        case MMC_POWER_OFF:
        default:
            if(pdata->pwr_pre)
                pdata->pwr_pre(pdata);
            if(pdata->pwr_off)
                pdata->pwr_off(pdata);
                break;
    }
}

/*call by mmc, set ios: power, clk, bus width*/
static void aml_sdhc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);
    struct amlsd_host* host = (void*)pdata->host;
    u32 misc;

    if(pdata->eject)
        return;

    /*Set Power*/
    aml_sdhc_set_power(pdata, ios->power_mode);

    /*Set Clock*/
    if(ios->clock)
        aml_sdhc_set_clk_rate(mmc, ios->clock);

    /*Set Bus Width*/
    aml_sdhc_set_bus_width(pdata, ios->bus_width);

    misc = readl(host->base+SDHC_MISC);
    misc &= ~MISC_TX_EMPTY_MASK;
    writel(misc, host->base+SDHC_MISC);

    if (ios->chip_select == MMC_CS_HIGH) {
        aml_cs_high(pdata);
    } else if (ios->chip_select == MMC_CS_DONTCARE) {
        aml_cs_dont_care(pdata);
    } else { // MMC_CS_LOW
        /* Nothing to do */
    }
}

/*get readonly: 0 for rw, 1 for ro*/
static int aml_sdhc_get_ro(struct mmc_host *mmc)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);
    u32 ro = 0;

    if(pdata->ro)
        ro = pdata->ro(pdata);
    return ro;
}

/*get card detect: 1 for insert, 0 for removed*/
int aml_sdhc_get_cd(struct mmc_host *mmc)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);
    int ret = 1;

    if(pdata->cd)
        ret = pdata->cd(pdata);
    else
        ret = 0; //inserted
    pdata->eject = ret;
    return (ret?0:1);
}

static irqreturn_t aml_sdhc_irq_cd(int irq, void *dev_id)
{
    struct amlsd_platform *pdata = (struct amlsd_platform*)dev_id;

    mmc_detect_change(pdata->mmc, msecs_to_jiffies(500));
    aml_sdhc_get_cd(pdata->mmc);
    return IRQ_HANDLED;
}

#ifdef CONFIG_PM

static int aml_sdhc_suspend(struct platform_device *pdev, pm_message_t state)
{
    struct amlsd_host *host = platform_get_drvdata(pdev);
    struct mmc_host* mmc;
    struct amlsd_platform* pdata;

	printk("***Entered %s:%s\n", __FILE__,__func__);
    list_for_each_entry(pdata, &host->sibling, sibling){
        mmc = pdata->mmc;
        mmc_suspend_host(mmc);
    }
	printk("***Exited %s:%s\n", __FILE__,__func__);
    return 0;
}

static int aml_sdhc_resume(struct platform_device *pdev)
{
    struct amlsd_host *host = platform_get_drvdata(pdev);
    struct mmc_host* mmc;
    struct amlsd_platform* pdata;

	printk("***Entered %s:%s\n", __FILE__,__func__);
    list_for_each_entry(pdata, &host->sibling, sibling){
        mmc = pdata->mmc;
        mmc_resume_host(mmc);
    }
	printk("***Exited %s:%s\n", __FILE__,__func__);
    return 0;
}

#else

#define aml_mmc_suspend	NULL
#define aml_mmc_resume		NULL

#endif

static const struct mmc_host_ops aml_sdhc_ops = {
    .request = aml_sdhc_request,
    .set_ios = aml_sdhc_set_ios,
    .get_cd = aml_sdhc_get_cd,
    .get_ro = aml_sdhc_get_ro,
};
	
/*for multi host claim host*/
static struct mmc_claim aml_sdhc_claim;

static struct amlsd_host* aml_sdhc_init_host(void)
{
    struct amlsd_host *host;

    spin_lock_init(&aml_sdhc_claim.lock);
    init_waitqueue_head(&aml_sdhc_claim.wq);

    host = kzalloc(sizeof(struct amlsd_host), GFP_KERNEL);

    if(request_threaded_irq(INT_WIFI_WATCHDOG, aml_sdhc_irq,
            aml_sdhc_data_thread, IRQF_DISABLED, "sdhc", (void*)host)){
        sdhc_err("Request SDHC Irq Error!\n");
        return NULL;
    }

    host->bn_buf = dma_alloc_coherent(NULL, SDHC_BOUNCE_REQ_SIZE,
    						&host->bn_dma_buf, GFP_KERNEL);
    if(NULL == host->bn_buf){
        sdhc_err("Dma alloc Fail!\n");
        return NULL;
    }

    setup_timer(&host->timeout_tlist, aml_sdhc_timeout, (ulong)host);

    spin_lock_init(&host->mrq_lock);
    host->xfer_step = XFER_INIT;

    INIT_LIST_HEAD(&host->sibling);
    host->storage_flag = storage_flag;
    host->pinctrl = NULL;
    return host;
}

static int aml_sdhc_probe(struct platform_device *pdev)
{
    struct mmc_host *mmc = NULL;
    struct amlsd_host *host = NULL;
    struct amlsd_platform* pdata;
    int ret = 0, i;

    print_tmp("%s() begin!\n", __FUNCTION__);

    host = aml_sdhc_init_host();
    if(!host)
        goto fail_init_host;
    if(amlsd_get_reg_base(pdev, host))
        goto fail_init_host;

    aml_sdhc_reg_init(host);
    host->pdev = pdev;

    for(i=0;i<MMC_MAX_DEVICE;i++){
        /*malloc extra amlsd_platform*/
        mmc = mmc_alloc_host(sizeof(struct amlsd_platform), &pdev->dev);
        if (!mmc) {
            ret = -ENOMEM;
            goto probe_free_host;
        }

        pdata = mmc_priv(mmc);
        memset(pdata, 0, sizeof(struct amlsd_platform));
        if(amlsd_get_platform_data(pdev, pdata, mmc, i)) {
            mmc_free_host(mmc);
            break;
        }
        if (pdata->port == PORT_SDHC_C) {
            if (is_emmc_exist(host)) {
                mmc->is_emmc_port = 1;
            } else { // there is not eMMC/tsd
                printk("[%s]: there is not eMMC/tsd, skip sdhc_c dts config!\n", __FUNCTION__);
                i++; // skip the port written in the dts
                memset(pdata, 0, sizeof(struct amlsd_platform));
                if(amlsd_get_platform_data(pdev, pdata, mmc, i)) {
                    mmc_free_host(mmc);
                    break;
                }
            }
		}
        dev_set_name(&mmc->class_dev, "%s", pdata->pinname);

        pdata->host = host;
        pdata->mmc = mmc;

        mmc->index = i;
        mmc->ops = &aml_sdhc_ops;
        mmc->alldev_claim = &aml_sdhc_claim;
        mmc->ios.clock = 400000;
        mmc->ios.bus_width = MMC_BUS_WIDTH_1;
        mmc->max_blk_count = 4095;
        mmc->max_blk_size = 4095;
        mmc->max_req_size = pdata->max_req_size;
        mmc->max_seg_size = mmc->max_req_size;
        mmc->max_segs = 1024;
        mmc->ocr_avail = pdata->ocr_avail;
        mmc->ocr = pdata->ocr_avail;
        mmc->caps = pdata->caps;
        mmc->caps2 = pdata->caps2;
        mmc->f_min = pdata->f_min;
        mmc->f_max = pdata->f_max;

        if(pdata->port_init)
            pdata->port_init(pdata);

        ret = mmc_add_host(mmc);
        if (ret) {
            sdhc_err("Failed to add mmc host.\n");
            goto probe_free_host;
        }
        aml_sdhc_init_debugfs(mmc);
        /*Add each mmc host pdata to this controller host list*/
        INIT_LIST_HEAD(&pdata->sibling);
        list_add_tail(&pdata->sibling, &host->sibling);

        /*Register card detect irq : plug in & unplug*/
        if(pdata->irq_in && pdata->irq_out){
            pdata->irq_init(pdata);
            ret = request_irq(pdata->irq_in+INT_GPIO_0, aml_sdhc_irq_cd,
                        IRQF_DISABLED, "sdhc_mmc_in", pdata);
            ret |= request_irq(pdata->irq_out+INT_GPIO_0, aml_sdhc_irq_cd,
                        IRQF_DISABLED, "sdhc_mmc_out", pdata);
            if (ret) {
                sdhc_err("Failed to request mmc detect\n");
                goto fail_cd_irq_in;
            }
        }
    }

    print_tmp("%s() success!\n", __FUNCTION__);
    platform_set_drvdata(pdev, host);
    return 0;

fail_cd_irq_in:
    if(pdata->irq_in)
        free_irq(pdata->irq_in, pdata);
probe_free_host:
    list_for_each_entry(pdata, &host->sibling, sibling){
        mmc = pdata->mmc;
        mmc_remove_host(mmc);
        mmc_free_host(mmc);
    }
fail_init_host:
    iounmap(host->base);
    free_irq(INT_WIFI_WATCHDOG, host);
    dma_free_coherent(NULL, SDHC_BOUNCE_REQ_SIZE, host->bn_buf,
        (dma_addr_t)host->bn_dma_buf);
    kfree(host);	
    return ret;
}


int aml_sdhc_remove(struct platform_device *pdev)
{
    struct amlsd_host* host  = platform_get_drvdata(pdev);
	struct mmc_host* mmc;
	struct amlsd_platform* pdata;

    dma_free_coherent(NULL, SDHC_BOUNCE_REQ_SIZE, host->bn_buf,
        (dma_addr_t )host->bn_dma_buf);

    free_irq(INT_WIFI_WATCHDOG, host);
    iounmap(host->base);

    list_for_each_entry(pdata, &host->sibling, sibling){
        mmc = pdata->mmc;
        mmc_remove_host(mmc);
        mmc_free_host(mmc);
    }
    kfree(host);
    return 0;
}

static const struct of_device_id aml_sdhc_dt_match[]={
    {
        .compatible = "amlogic,aml_sdhc",
    },
    {},
};

MODULE_DEVICE_TABLE(of, aml_sdhc_dt_match);

static struct platform_driver aml_sdhc_driver = {
    .probe 		= aml_sdhc_probe,
    .remove		= aml_sdhc_remove,
    .suspend	= aml_sdhc_suspend,
    .resume		= aml_sdhc_resume,
    .driver		= {
        .name = "aml_sdhc",
        .owner = THIS_MODULE,
        .of_match_table=aml_sdhc_dt_match,
    },
};

static int __init aml_sdhc_init(void)
{
    return platform_driver_register(&aml_sdhc_driver);
}

static void __exit aml_sdhc_cleanup(void)
{
    platform_driver_unregister(&aml_sdhc_driver);
}

module_init(aml_sdhc_init);
module_exit(aml_sdhc_cleanup);

MODULE_DESCRIPTION("Amlogic Multimedia Card driver");
MODULE_LICENSE("GPL");

