/*
 * drivers/amlogic/mmc/sdio.c
 *
 * SDIO Driver
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
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <asm/dma-mapping.h>
#include <mach/power_gate.h>
#include <linux/clk.h>
#include <mach/register.h>
// #include <mach/gpio.h>
#include <mach/pinmux.h>
#include <mach/irqs.h>
#include <linux/irq.h>
#include <mach/sd.h>
#include "amlsd.h"
#include <linux/mmc/emmc_partitions.h>

#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/amlogic/wifi_dt.h>

struct mmc_host *sdio_host = NULL;
static struct mmc_claim aml_sdio_claim;
#define     sdio_cmd_busy_bit     4
int CMD_PROCESS_JIT ;
int SDIO_IRQ_SUPPORT = 0;
static int aml_sdio_timeout_cmd(struct amlsd_host *host);
void aml_sdio_send_stop(struct amlsd_host* host);

static unsigned int sdio_error_flag = 0;
static unsigned int sdio_debug_flag = 0;
static unsigned int sdio_err_bak;

void sdio_debug_irqstatus(struct sdio_status_irq* irqs, struct cmd_send* send)
{    
    switch (sdio_debug_flag)
    {
        case 1:
            irqs->sdio_response_crc7_ok = 0;
            send->response_do_not_have_crc7 = 0;
            
            sdhc_err("Force sdio cmd response crc error here\n");
            break;
        case 2:
            irqs->sdio_data_read_crc16_ok = 0;
            irqs->sdio_data_write_crc16_ok = 0;
            sdhc_err("Force sdio data crc here\n");
            break;  
                                    
        default:            
            break;
    }
    
    //only enable once for debug
    sdio_debug_flag = 0;
}

static void aml_sdio_soft_reset(struct amlsd_host* host)
{
    struct sdio_irq_config irqc={0};
    /*soft reset*/
    irqc.soft_reset = 1;
    writel(*(u32*)&irqc, host->base + SDIO_IRQC);
    udelay(2);
}

/*
 * init sdio reg
 */
static void aml_sdio_init_param(struct amlsd_host* host)
{
    struct sdio_status_irq irqs={0};
    struct sdio_config conf={0};

    // aml_sdio_soft_reset(host);

    /*write 1 clear bit8,9*/
    irqs.sdio_if_int = 1;
    irqs.sdio_cmd_int = 1;
    writel(*(u32*)&irqs, host->base + SDIO_IRQS);

    /*setup config*/
    conf.sdio_write_crc_ok_status = 2;
    conf.sdio_write_nwr = 2;
    conf.m_endian = 3;
    conf.cmd_argument_bits = 39;
    conf.cmd_out_at_posedge = 0;
    conf.cmd_disable_crc = 0;
    conf.data_latch_at_negedge = 0;
    conf.cmd_clk_divide = CLK_DIV;
    writel(*(u32*)&conf, host->base + SDIO_CONF);
}

static bool is_card_last_block (struct amlsd_platform * pdata, u32 lba, u32 cnt)
{
    if (!pdata->card_capacity)
        pdata->card_capacity = mmc_capacity(pdata->mmc->card);

    return (lba + cnt) == pdata->card_capacity;
}

/*
read response from REG0_ARGU(136bit or 48bit)
*/
void aml_sdio_read_response(struct amlsd_platform * pdata, struct mmc_request *mrq)
{
    int i, resp[4];
    struct amlsd_host* host = pdata->host;
    u32 vmult = readl(host->base + SDIO_MULT);
    struct sdio_mult_config* mult = (void*)&vmult;
    struct mmc_command *cmd = mrq->cmd;

    mult->write_read_out_index = 1;
    mult->response_read_index = 0;
    writel(vmult, host->base + SDIO_MULT);

    if(cmd->flags & MMC_RSP_136){
        for(i=0;i<=3;i++)
            resp[3-i] = readl(host->base + SDIO_ARGU);
        cmd->resp[0] = (resp[0]<<8)|((resp[1]>>24)&0xff);
        cmd->resp[1] = (resp[1]<<8)|((resp[2]>>24)&0xff);
        cmd->resp[2] = (resp[2]<<8)|((resp[3]>>24)&0xff);
        cmd->resp[3] = (resp[3]<<8);
        // sdio_dbg(AMLSD_DBG_RESP,"Cmd %d ,Resp %x-%x-%x-%x\n",
                    // cmd->opcode, cmd->resp[0], cmd->resp[1],
                    // cmd->resp[2], cmd->resp[3]);
    }
    else if(cmd->flags & MMC_RSP_PRESENT){
        cmd->resp[0] = readl(host->base + SDIO_ARGU);
        // sdio_dbg(AMLSD_DBG_RESP,"Cmd %d ,Resp 0x%x\n", cmd->opcode, cmd->resp[0]);
        
        /* Now in sdio controller, something is wrong. 
         * When we read last block in multi-blocks-mode, 
         * it will cause "ADDRESS_OUT_OF_RANGE" error in card status, we must clear it.
         */
        if ((cmd->resp[0] & R1_OUT_OF_RANGE) // status error: address out of range
                && (mrq->data) && (is_card_last_block(pdata, cmd->arg, mrq->data->blocks))) {
            cmd->resp[0] &= (~R1_OUT_OF_RANGE); // clear the error
        }
    }
}

/*copy buffer from data->sg to dma buffer, set dma addr to reg*/
void aml_sdio_prepare_dma(struct amlsd_host *host, struct mmc_request *mrq)
{
    struct mmc_data *data = mrq->data;

    if(data->flags & MMC_DATA_WRITE){
        aml_sg_copy_buffer(data->sg, data->sg_len,
            host->bn_buf, data->blksz*data->blocks, 1);
        sdio_dbg(AMLSD_DBG_WR_DATA,"W Cmd %d, %x-%x-%x-%x\n",
            mrq->cmd->opcode,
            host->bn_buf[0], host->bn_buf[1],
            host->bn_buf[2], host->bn_buf[3]);
    }
    // host->dma_addr = host->bn_dma_buf;
}

void aml_sdio_set_port_ios(struct mmc_host* mmc)
{
    struct amlsd_platform* pdata = mmc_priv(mmc);
    struct amlsd_host* host = pdata->host;
    u32 vconf = readl(host->base + SDIO_CONF);
    struct sdio_config* conf = (void*)&vconf;

        if(aml_card_type_sdio(pdata) && (pdata->mmc->actual_clock > 50000000))
        { // if > 50MHz  
            conf->data_latch_at_negedge = 1; //[19] //0     
            conf->do_not_delay_data = 1; //[18]        
            conf->cmd_out_at_posedge =0; //[11]    
        } 
        else 
        { 
            conf->data_latch_at_negedge = 0; //[19] //0       
            conf->do_not_delay_data = 0; //[18]       
            conf->cmd_out_at_posedge =0; //[11]    
        }
        writel(vconf, host->base+SDIO_CONF);

    if ((conf->cmd_clk_divide == pdata->clkc) && (conf->bus_width == pdata->width))
        return ;

    /*Setup Clock*/
    conf->cmd_clk_divide = pdata->clkc;
    /*Setup Bus Width*/
    conf->bus_width = pdata->width;
    writel(vconf, host->base+SDIO_CONF);
}

static void aml_sdio_enable_irq(struct mmc_host *mmc, int enable)
{
    struct amlsd_platform* pdata = mmc_priv(mmc);
    struct amlsd_host* host = pdata->host;
    u32 virqc ;
    struct sdio_irq_config* irqc ;
    u32 virqs;
    struct sdio_status_irq* irqs;
    u32 vmult;
    struct sdio_mult_config* mult; 		
    unsigned long flags;
	
    spin_lock_irqsave(&host->mrq_lock, flags);
    virqc = readl(host->base + SDIO_IRQC);
    irqc = (void*)&virqc;
    virqs = readl(host->base + SDIO_IRQS);
    irqs = (void*)&virqs;
    vmult = readl(host->base + SDIO_MULT);
    mult = (void*)&vmult;
	
    //u32 vmult = readl(host->base + SDIO_MULT);
    //struct sdio_mult_config* mult = (void*)&vmult;

    /*enable if int irq*/
	if(enable ){
    		irqc->arc_if_int_en = 1;
	}		
	else{
    		irqc->arc_if_int_en = 0;
	}

    /*clear pending*/
    irqs->sdio_if_int = 1;
	
    mult->sdio_port_sel = pdata->port;
    writel(vmult, host->base + SDIO_MULT);
   // mult->sdio_port_sel = pdata->port;
   // writel(vmult, host->base + SDIO_MULT);
    writel(virqs, host->base + SDIO_IRQS);
    writel(virqc, host->base + SDIO_IRQC);
    spin_unlock_irqrestore(&host->mrq_lock, flags);
	

}

/*set to register, start xfer*/
void aml_sdio_start_cmd(struct mmc_host* mmc, struct mmc_request* mrq)
{
    u32 pack_size;
    struct amlsd_platform* pdata = mmc_priv(mmc);
    struct amlsd_host* host = pdata->host;
    struct cmd_send send={0};
    struct sdio_extension ext={0};
    u32 virqc = readl(host->base + SDIO_IRQC);
    struct sdio_irq_config* irqc = (void*)&virqc;
    u32 virqs = readl(host->base + SDIO_IRQS);
    struct sdio_status_irq* irqs = (void*)&virqs;
    u32 vmult = readl(host->base + SDIO_MULT);
    struct sdio_mult_config* mult = (void*)&vmult;

    switch (mmc_resp_type(mrq->cmd)) {
        case MMC_RSP_R1:
        case MMC_RSP_R1B:
        case MMC_RSP_R3:
            /*7(cmd)+32(respnse)+7(crc)-1 data*/
            send.cmd_response_bits = 45;
            break;
        case MMC_RSP_R2:
            /*7(cmd)+120(respnse)+7(crc)-1 data*/
            send.cmd_response_bits = 133;
            send.response_crc7_from_8 = 1;
            break;
        default:
            /*no response*/
            break;
    }

    if(!(mrq->cmd->flags & MMC_RSP_CRC))
        send.response_do_not_have_crc7 = 1;
    if(mrq->cmd->flags & MMC_RSP_BUSY)
        send.check_busy_on_dat0 = 1;

    if(mrq->data){
        /*total package num*/
        send.repeat_package_times = mrq->data->blocks - 1;
        BUG_ON(mrq->data->blocks > 256);
        /*package size*/
        if(pdata->width) /*0: 1bit, 1: 4bit*/
           pack_size = mrq->data->blksz*8 + (16-1)*4;
        else
           pack_size = mrq->data->blksz*8 + (16-1);
        ext.data_rw_number = pack_size;
        if(mrq->data->flags & MMC_DATA_WRITE)
            send.cmd_send_data = 1;
        else
            send.response_have_data = 1;
    }
    /*cmd index*/
    send.cmd_command = 0x40|mrq->cmd->opcode;

    aml_sdio_soft_reset(host);

    /*enable cmd irq*/
    irqc->arc_cmd_int_en = 1;

    /*clear pending*/
    irqs->sdio_cmd_int = 1;

    aml_sdio_set_port_ios(host->mmc);

    mult->sdio_port_sel = pdata->port;
    writel(vmult, host->base + SDIO_MULT);
    writel(virqs, host->base + SDIO_IRQS);
    writel(virqc, host->base + SDIO_IRQC);
    //setup all reg to send cmd
    writel(mrq->cmd->arg, host->base + SDIO_ARGU);
    writel(*(u32*)&ext, host->base + SDIO_EXT);
    writel(*(u32*)&send, host->base + SDIO_SEND);
}

/*
 * clear struct & call mmc_request_done
 */
void aml_sdio_request_done(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);
    struct amlsd_host* host = pdata->host;
    unsigned long flags;
    struct mmc_command *cmd;
    // u32 virqs = readl(host->base + SDIO_IRQS);
    // u32 virqc =readl(host->base + SDIO_IRQC);
    // struct sdio_irq_config* irqc = (void*)&virqc;

    /* Disable Command-Done-Interrupt
     * It will be enabled again in the next cmd.
     */
    // irqc->arc_cmd_int_en = 0;   // disable cmd irq
    // writel(virqc, host->base + SDIO_IRQC);
    // writel(virqs, host->base + SDIO_IRQS);     // clear pending

    /*
        * del timer before mmc_request_done,
        * if fail, it call aml_sdio_request again & mod_timer again
        */
    // if (!in_atomic()){
        // del_timer_sync(&host->timeout_tlist); // it may sleep
    // } else {
        // del_timer(&host->timeout_tlist);
    // }
    //del_timer(&host->timeout_tlist);
    if(delayed_work_pending(&host->timeout))
    		cancel_delayed_work(&host->timeout);
  //  cancel_delayed_work(&host->timeout_cmd);

    spin_lock_irqsave(&host->mrq_lock, flags);
    WARN_ON(!host->mrq->cmd);
    BUG_ON(host->xfer_step == XFER_FINISHED);
    aml_sdio_read_response(pdata, host->mrq);

    cmd = host->mrq->cmd; // for debug

    host->mrq = NULL;
    host->xfer_step = XFER_FINISHED;
    spin_unlock_irqrestore(&host->mrq_lock, flags);

    if(cmd->flags & MMC_RSP_136){
        sdio_dbg(AMLSD_DBG_RESP,"Cmd %d ,Resp %x-%x-%x-%x\n",
                    cmd->opcode, cmd->resp[0], cmd->resp[1],
                    cmd->resp[2], cmd->resp[3]);
    }
    else if(cmd->flags & MMC_RSP_PRESENT){
        sdio_dbg(AMLSD_DBG_RESP,"Cmd %d ,Resp 0x%x\n", cmd->opcode, cmd->resp[0]);
    }

#ifdef      CONFIG_MMC_AML_DEBUG
    host->req_cnt--;

    aml_dbg_verify_pinmux(pdata);
    aml_dbg_verify_pull_up(pdata);
#endif

    if (pdata->xfer_post)
        pdata->xfer_post(pdata);
		
	//if((SDIO_IRQ_SUPPORT)
	//	&&(mmc->sdio_irq_pending != true))
	//	mmc->ops->enable_sdio_irq(mmc, 1);
	
    mmc_request_done(host->mmc, mrq);
}

static void aml_sdio_print_err (struct amlsd_host *host, char *msg)
{
    struct amlsd_platform * pdata = mmc_priv(host->mmc);
    u32 virqs = readl(host->base + SDIO_IRQS);
    u32 virqc = readl(host->base + SDIO_IRQC);
    u32 vconf = readl(host->base + SDIO_CONF);
    struct sdio_config* conf = (void*)&vconf;
    struct clk* clk_src = clk_get_sys("clk81", NULL);
    u32 clk_rate = clk_get_rate(clk_src)/2;

    sdio_err("%s: %s, Cmd%d arg %#x, Xfer %d Bytes, "
            "host->xfer_step=%d, host->cmd_is_stop=%d, pdata->port=%d, "
            "virqs=%#0x, virqc=%#0x, conf->cmd_clk_divide=%d, pdata->clkc=%d, "
            "conf->bus_width=%d, pdata->width=%d, conf=%#x, clock=%d\n",
            mmc_hostname(host->mmc),
            msg,
            host->mrq->cmd->opcode,
            host->mrq->cmd->arg,
            host->mrq->data?host->mrq->data->blksz*host->mrq->data->blocks:0,
            host->xfer_step,
            host->cmd_is_stop,
            pdata->port,
            virqs, virqc,
            conf->cmd_clk_divide,
            pdata->clkc,
            conf->bus_width,
            pdata->width,
            vconf,
            clk_rate / (conf->cmd_clk_divide + 1));    
}

/*setup delayed workstruct in aml_sdio_request*/
static void aml_sdio_timeout(struct work_struct *work)
{
    static int timeout_cnt = 0;
    struct amlsd_host *host = container_of(work, struct amlsd_host, timeout.work);
    // struct mmc_request *mrq = host->mrq;
    //u32 virqs = readl(host->base + SDIO_IRQS);
    //struct sdio_status_irq* irqs = (void*)&virqs;
    //u32 virqc =readl(host->base + SDIO_IRQC);
    //struct sdio_irq_config* irqc = (void*)&virqc;
	u32 virqs;
    struct sdio_status_irq* irqs;
    u32 virqc;
    struct sdio_irq_config* irqc;
    unsigned long flags;
//#ifdef      CONFIG_MMC_AML_DEBUG
    struct amlsd_platform * pdata = mmc_priv(host->mmc);
//#endif
	static int timeout_cmd_cnt = 0;
	int is_mmc_stop = 0;

//	struct timeval ts_current;
	unsigned long time_start_cnt = READ_CBUS_REG(ISA_TIMERE);
	
    
    time_start_cnt = (time_start_cnt - host->time_req_sta) / 1000;

//	if(host->mmc->caps & MMC_CAP_SDIO_IRQ){
	if(SDIO_IRQ_SUPPORT && !is_mmc_stop)
	{
		if(aml_sdio_timeout_cmd(host))
			return;
		if(timeout_cmd_cnt++ < 20){
			schedule_delayed_work(&host->timeout, CMD_PROCESS_JIT/20);
			return;
		} else{
			timeout_cmd_cnt = 0;
		}
	}

   virqs = readl(host->base + SDIO_IRQS);
   irqs = (void*)&virqs;
   virqc =readl(host->base + SDIO_IRQC);
   irqc = (void*)&virqc;

    spin_lock_irqsave(&host->mrq_lock, flags);
    if(host->xfer_step == XFER_FINISHED){
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        sdio_err("timeout after xfer finished\n");
        return;
    }
    if((irqs->sdio_cmd_int)                             // irq have been occured
            || (host->xfer_step == XFER_IRQ_OCCUR)){    // isr have been run
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        //mod_timer(&host->timeout_tlist, jiffies + 10);
        schedule_delayed_work(&host->timeout, 50);
        host->time_req_sta = READ_CBUS_REG(ISA_TIMERE);    
        
        if(irqs->sdio_cmd_int) {
            timeout_cnt++;
            if (timeout_cnt > 30)
                goto timeout_handle;
            sdio_err("%s: cmd%d, ISR have been run, xfer_step=%d, time_start_cnt=%ldmS, timeout_cnt=%d\n",
                mmc_hostname(host->mmc), host->mrq->cmd->opcode, host->xfer_step, time_start_cnt, timeout_cnt);        
        }
        else
            sdio_err("%s: isr have been run\n",  mmc_hostname(host->mmc));
            
        return;
    } else {
        spin_unlock_irqrestore(&host->mrq_lock, flags);
    }
timeout_handle:
    timeout_cnt = 0;

    if (!(irqc->arc_cmd_int_en)) {
        sdio_err("%s: arc_cmd_int_en is not enable\n",  mmc_hostname(host->mmc));
    }

    /* Disable Command-Done-Interrupt to avoid irq occurs
     * It will be enabled again in the next cmd.
     */
    irqc->arc_cmd_int_en = 0;   // disable cmd irq
    writel(virqc, host->base + SDIO_IRQC);

    spin_lock_irqsave(&host->mrq_lock, flags);
    
    //do not retry for sdcard
    if(!aml_card_type_mmc(pdata)){
        sdio_error_flag |= (1<<30);
        host->mrq->cmd->retries = 0;
    }else if(((sdio_error_flag & (1<<3))== 0) && (host->mrq->data != NULL) 
            && pdata->is_in ){  //set cmd retry cnt when first error.
        sdio_error_flag |= (1<<3);
        host->mrq->cmd->retries = AML_TIMEOUT_RETRY_COUNTER; 
    }   
    
    //here clear error flags after error retried
    if(sdio_error_flag && (host->mrq->cmd->retries == 0)){
        sdio_error_flag |= (1<<30);
    } 
    
    host->xfer_step = XFER_TIMEDOUT;
    host->mrq->cmd->error = -ETIMEDOUT;
    spin_unlock_irqrestore(&host->mrq_lock, flags);
    
#if defined(CONFIG_MMC_AML_DEBUG)
    sdio_err("time_start_cnt:%ld\n", time_start_cnt);
    aml_sdio_print_err(host, "Timeout error");
#endif
    // if (pdata->port == MESON_SDIO_PORT_A) {
        // sdio_err("power_on_pin=%d\n",
                // amlogic_get_value(185, "sdio_wifi")); // G24-113, G33-185
    // }
    // if (pdata->port == MESON_SDIO_PORT_B) {
// #ifdef CONFIG_ARCH_MESON6
        // if ((pdata->port == MESON_SDIO_PORT_B) && (pdata->gpio_power != 0))
            // sdio_err("power_on_pin=%d\n", amlogic_get_value(pdata->gpio_power, MODULE_NAME));
// #endif
    // }

#ifdef      CONFIG_MMC_AML_DEBUG
    aml_dbg_verify_pinmux(pdata);
    aml_dbg_verify_pull_up(pdata);
    aml_sdio_print_reg(host);
    // aml_dbg_print_pinmux();
#endif

    if(host->mrq->stop && aml_card_type_mmc(pdata) && !host->cmd_is_stop){
        // sdio_err("Send stop cmd before timeout retry..\n");
        spin_lock_irqsave(&host->mrq_lock, flags);
        aml_sdio_send_stop(host);                
        spin_unlock_irqrestore(&host->mrq_lock, flags);
		is_mmc_stop = 1;
        schedule_delayed_work(&host->timeout, 50);
    }
    else{
            /*request done*/
        // sdio_err("Just stop without retry..\n");

        if (host->cmd_is_stop)
            host->cmd_is_stop = 0;

        aml_sdio_request_done(host->mmc, host->mrq);
    }
            

    // spin_lock_irqsave(&host->mrq_lock, flags);
    // WARN_ON(!host->mrq->cmd);
    // BUG_ON(host->xfer_step == XFER_FINISHED);
    // host->mrq = NULL;
    // host->xfer_step = XFER_FINISHED;
    // spin_unlock_irqrestore(&host->mrq_lock, flags);

    // mmc_request_done(host->mmc, mrq);

    /*print reg*/
    // aml_sdio_print_reg(host);
    // sdio_err("Timeout out func\n");
}


static int aml_sdio_timeout_cmd(struct amlsd_host *host)
{
   // static int timeout_cnt = 0;
   // struct amlsd_host *host = container_of(work, struct amlsd_host, timeout_cmd);
    u32 virqs = readl(host->base + SDIO_IRQS);
    struct sdio_status_irq* irqs = (void*)&virqs;
    u32 vsend = readl(host->base + SDIO_SEND);
    struct cmd_send* send = (void*)&vsend;	
    unsigned long flags;
    struct mmc_request* mrq;
    enum aml_mmc_waitfor    xfer_step;
    struct amlsd_platform * pdata = mmc_priv(host->mmc);

    //spin_lock_irqsave(&host->mrq_lock, flags);

	
    if((virqs >> sdio_cmd_busy_bit) & 0x1) 
    {      
    	//spin_unlock_irqrestore(&host->mrq_lock, flags);
		return 0;
    }	else{
 //  if (delayed_work_pending(&host->timeout)) 
 //   	 cancel_delayed_work(&host->timeout);

    spin_lock_irqsave(&host->mrq_lock, flags);
 
    if(host->xfer_step == XFER_FINISHED ||
        host->xfer_step == XFER_TIMEDOUT){
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        return 0;
    }		
	
    if(((host->xfer_step != XFER_AFTER_START   )
		&&( host->xfer_step != XFER_START))
		||(! host->mrq)){
	spin_unlock_irqrestore(&host->mrq_lock, flags);
	return 0;	
    }      

    mrq = host->mrq;
    xfer_step = host->xfer_step;

    if(host->cmd_is_stop){
        host->cmd_is_stop = 0;
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        aml_sdio_request_done(host->mmc, mrq);
        return 1;
    }

    host->xfer_step = XFER_TASKLET_DATA;
	
    if(!mrq->data){
        if(irqs->sdio_response_crc7_ok || send->response_do_not_have_crc7)
            mrq->cmd->error = 0;
        else {
            mrq->cmd->error = -EILSEQ;
            aml_sdio_print_err(host, "cmd crc7 error");
        }
       spin_unlock_irqrestore(&host->mrq_lock, flags);
        aml_sdio_request_done(host->mmc, mrq);
    }else{
        if(irqs->sdio_data_read_crc16_ok||irqs->sdio_data_write_crc16_ok)
            mrq->cmd->error = 0;
        else {
            mrq->cmd->error = -EILSEQ;
            if((sdio_error_flag == 0) && aml_card_type_mmc(pdata)){  //set cmd retry cnt when first error.
                sdio_error_flag |= (1<<0);
                mrq->cmd->retries = AML_ERROR_RETRY_COUNTER; 
            }  
            aml_sdio_print_err(host, "data crc16 error");
        }
        mrq->data->bytes_xfered = mrq->data->blksz*mrq->data->blocks;
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        if(mrq->data->flags & MMC_DATA_READ){
            aml_sg_copy_buffer(mrq->data->sg, mrq->data->sg_len,
                host->bn_buf, mrq->data->blksz*mrq->data->blocks, 0);
            sdio_dbg(AMLSD_DBG_RD_DATA, "R Cmd %d, %x-%x-%x-%x\n",
                host->mrq->cmd->opcode,
                host->bn_buf[0], host->bn_buf[1],
                host->bn_buf[2], host->bn_buf[3]);
        }
        spin_lock_irqsave(&host->mrq_lock, flags);

        if((mrq->cmd->error == 0) 
            || (sdio_error_flag && (mrq->cmd->retries == 0))){
            sdio_error_flag |= (1<<30);
        }
                
        if(mrq->stop){
            aml_sdio_send_stop(host);
            spin_unlock_irqrestore(&host->mrq_lock, flags);
        }
        else{
            spin_unlock_irqrestore(&host->mrq_lock, flags);
            aml_sdio_request_done(host->mmc, mrq);
        }
    }
    return 1;
    }

 
}

/*
 * aml handle request
 * 1. setup data
 * 2. send cmd
 * 3. return (aml_sdio_request_done in irq function)
*/
void aml_sdio_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
    struct amlsd_platform * pdata;
    struct amlsd_host *host;
    unsigned long flags;
    unsigned int timeout;

    BUG_ON(!mmc);
    BUG_ON(!mrq);

    pdata = mmc_priv(mmc);
    host = (void*)pdata->host;

    if (aml_check_unsupport_cmd(mmc, mrq))
        return;

    //only for SDCARD hotplag
    if(!pdata->is_in || (!host->init_flag && aml_card_type_sd(pdata))){
        spin_lock_irqsave(&host->mrq_lock, flags);
        mrq->cmd->error = -ENOMEDIUM;
        mrq->cmd->retries = 0;
        host->mrq = NULL;
        host->xfer_step = XFER_FINISHED;        
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        
        //aml_sdio_request_done(mmc, mrq);
        mmc_request_done(mmc, mrq);
        return;
    }

#ifdef      CONFIG_MMC_AML_DEBUG
    if (host->req_cnt) {
        sdio_err("Reentry error! host->req_cnt=%d\n", host->req_cnt);
    }
    host->req_cnt++;
#endif

    if (mrq->cmd->opcode == 0) {
        host->init_flag = 1;
    }

    sdio_dbg(AMLSD_DBG_REQ ,"%s: starting CMD%u arg %08x flags %08x\n",
        mmc_hostname(mmc), mrq->cmd->opcode,
        mrq->cmd->arg, mrq->cmd->flags);

    if(mrq->data) {
        /*Copy data to dma buffer for write request*/
        aml_sdio_prepare_dma(host, mrq);
        writel(host->bn_dma_buf, host->base + SDIO_ADDR);

        sdio_dbg(AMLSD_DBG_REQ ,"%s: blksz %d blocks %d flags %08x "
            "tsac %d ms nsac %d\n",
            mmc_hostname(mmc), mrq->data->blksz,
            mrq->data->blocks, mrq->data->flags,
            mrq->data->timeout_ns / 1000000,
            mrq->data->timeout_clks);
    }

    /*clear pinmux & set pinmux*/
    if(pdata->xfer_pre)
        pdata->xfer_pre(pdata);
		
//	if(SDIO_IRQ_SUPPORT)
	if((mmc->caps & MMC_CAP_SDIO_IRQ)
		&&(mmc->ops->enable_sdio_irq))
		mmc->ops->enable_sdio_irq(mmc, 0);
#ifdef      CONFIG_MMC_AML_DEBUG
    aml_dbg_verify_pull_up(pdata);
    aml_dbg_verify_pinmux(pdata);
#endif

    if(!mrq->data)
        timeout = 100; //mod_timer(&host->timeout_tlist, jiffies + 100); // 1s
    else
        timeout = 500;//mod_timer(&host->timeout_tlist,
                //jiffies + 500/*10*nsecs_to_jiffies(mrq->data->timeout_ns)*/); // 5s
//	if(mmc->caps & MMC_CAP_SDIO_IRQ){
	if(SDIO_IRQ_SUPPORT){
		schedule_delayed_work(&host->timeout, timeout/20); 
	}else{
		schedule_delayed_work(&host->timeout, timeout);
	}
	
    //cmd_process = 0;
    CMD_PROCESS_JIT = timeout;
    spin_lock_irqsave(&host->mrq_lock, flags);
    if(host->xfer_step != XFER_FINISHED && host->xfer_step != XFER_INIT)
        sdio_err("host->xfer_step %d\n", host->xfer_step);

    //clear error flag if last command retried failed here    
    if(sdio_error_flag & (1<<30)){
        sdio_error_flag = 0;
    }
    
    host->mrq = mrq;
    host->mmc = mmc;
    host->xfer_step = XFER_START;
    host->opcode = mrq->cmd->opcode;
    host->arg = mrq->cmd->arg;
    host->time_req_sta = READ_CBUS_REG(ISA_TIMERE);    

    // if(mrq->data){
        // sdio_dbg(AMLSD_DBG_REQ ,"%s: blksz %d blocks %d flags %08x "
            // "tsac %d ms nsac %d\n",
            // mmc_hostname(mmc), mrq->data->blksz,
            // mrq->data->blocks, mrq->data->flags,
            // mrq->data->timeout_ns / 1000000,
            // mrq->data->timeout_clks);
        // writel(host->bn_dma_buf, host->base + SDIO_ADDR);
    // }

    aml_sdio_start_cmd(mmc, mrq);
    host->xfer_step = XFER_AFTER_START;
    spin_unlock_irqrestore(&host->mrq_lock, flags);
}

struct mmc_command aml_sdio_cmd = {
    .opcode = MMC_STOP_TRANSMISSION,
    .flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC,
};
struct mmc_request aml_sdio_stop = {
    .cmd = &aml_sdio_cmd,
};

void aml_sdio_send_stop(struct amlsd_host* host)
{
    /*Already in mrq_lock*/
    host->cmd_is_stop = 1;
    sdio_err_bak = host->mrq->cmd->error;
    host->mrq->cmd->error = 0;
    aml_sdio_start_cmd(host->mmc, &aml_sdio_stop);
}

/*
 * enable cmd & data irq, call tasket, do aml_sdio_request_done
 */
static irqreturn_t aml_sdio_irq(int irq, void *dev_id)
{
    struct amlsd_host* host = (void*)dev_id;
    u32 virqs = readl(host->base + SDIO_IRQS);
    struct sdio_status_irq* irqs = (void*)&virqs;
    // int is_stop;
    struct mmc_request* mrq;
    unsigned long flags;
    int sdio_cmd_int = 0;

    spin_lock_irqsave(&host->mrq_lock, flags);
    mrq = host->mrq;
   // if(!mrq){
   //     sdio_err("CMD%u, arg %08x, virqs=%08x, NULL mrq in aml_sdio_irq xfer_step %d\n",
   //            host->opcode, host->arg, virqs, host->xfer_step);
    if(!mrq && !irqs->sdio_if_int){

        if(host->xfer_step == XFER_FINISHED ||
            host->xfer_step == XFER_TIMEDOUT){
            spin_unlock_irqrestore(&host->mrq_lock, flags);
            return IRQ_HANDLED;
        }
        WARN_ON(!mrq);
        aml_sdio_print_reg(host);
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        return IRQ_HANDLED;
    }

    if(irqs->sdio_cmd_int  && mrq){

        // writel(virqs, host->base + SDIO_IRQS); // clear irq
        if(host->cmd_is_stop)
            host->xfer_step = XFER_IRQ_TASKLET_BUSY;
        else
            host->xfer_step = XFER_IRQ_OCCUR;
        // host->time_req_sta = READ_CBUS_REG(ISA_TIMERE);
        spin_unlock_irqrestore(&host->mrq_lock, flags);
	 if(irqs->sdio_if_int && SDIO_IRQ_SUPPORT)
        	sdio_cmd_int = 1;
	 else
	 	return IRQ_WAKE_THREAD;
    }else
        spin_unlock_irqrestore(&host->mrq_lock, flags);
	
    if(irqs->sdio_if_int){
	 	if(host->mmc->ops->enable_sdio_irq)
			host->mmc->ops->enable_sdio_irq(host->mmc, 0);     
		if((host->mmc->sdio_irq_thread)
			&&(!atomic_read(& host->mmc->sdio_irq_thread_abort)))
	            mmc_signal_sdio_irq(host->mmc);
    }
	
    if(SDIO_IRQ_SUPPORT && sdio_cmd_int)
   		return IRQ_WAKE_THREAD;
    //sdio_err("irq ignore!\n");
    // writel(virqs, host->base + SDIO_IRQS);
    //if cmd has stop, call aml_sdio_send_stop
    return IRQ_HANDLED;
}

irqreturn_t aml_sdio_irq_thread(int irq, void *data)
{
    struct amlsd_host* host = (void*)data;
    u32 virqs = readl(host->base + SDIO_IRQS);
    struct sdio_status_irq* irqs = (void*)&virqs;
    u32 vsend = readl(host->base + SDIO_SEND);
    struct cmd_send* send = (void*)&vsend;
    unsigned long flags;
    struct mmc_request* mrq;
    enum aml_mmc_waitfor    xfer_step;
    struct amlsd_platform * pdata = mmc_priv(host->mmc);
    // u32 time = READ_CBUS_REG(ISA_TIMERE);

    // time = time - host->time_req_sta;
    // if ((time > 100000) && (host->opcode == 52)){
        // printk(KERN_DEBUG "TIME_STAMP: %8d[%s]\n", READ_CBUS_REG(ISA_TIMERE), __FUNCTION__);
        // printk(KERN_DEBUG "Time spend: %8d, CMD%u, arg %08x\n", time, host->opcode, host->arg);
    // }
    spin_lock_irqsave(&host->mrq_lock, flags);
    mrq = host->mrq;
    xfer_step = host->xfer_step;

    if ((xfer_step == XFER_FINISHED) || (xfer_step == XFER_TIMER_TIMEOUT)) {
        sdhc_err("Warning: xfer_step=%d\n", xfer_step);
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        return IRQ_HANDLED;
    }

    if(!mrq){
        sdio_err("CMD%u, arg %08x, mrq NULL xfer_step %d\n", host->opcode, host->arg, xfer_step);
        if(xfer_step == XFER_FINISHED ||
            xfer_step == XFER_TIMEDOUT){
            spin_unlock_irqrestore(&host->mrq_lock, flags);
            sdio_err("[aml_sdio_irq_thread] out\n");
            return IRQ_HANDLED;
        }
		//BUG();
		spin_unlock_irqrestore(&host->mrq_lock, flags);
        return IRQ_HANDLED;
    }

    if((SDIO_IRQ_SUPPORT )
		&& (host->xfer_step == XFER_TASKLET_DATA)){
	  spin_unlock_irqrestore(&host->mrq_lock, flags);
        return IRQ_HANDLED;
    }	
	
    if(host->cmd_is_stop){
        host->cmd_is_stop = 0;
        mrq->cmd->error = sdio_err_bak;
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        aml_sdio_request_done(host->mmc, mrq);
        return IRQ_HANDLED;
    }
    host->xfer_step = XFER_TASKLET_DATA;

    if(!mrq->data){
        if(irqs->sdio_response_crc7_ok || send->response_do_not_have_crc7) {
            mrq->cmd->error = 0;
            spin_unlock_irqrestore(&host->mrq_lock, flags);
        } else {
            mrq->cmd->error = -EILSEQ;
            spin_unlock_irqrestore(&host->mrq_lock, flags);
            aml_sdio_print_err(host, "cmd crc7 error");
        }
        aml_sdio_request_done(host->mmc, mrq);
    }else{
        if(irqs->sdio_data_read_crc16_ok||irqs->sdio_data_write_crc16_ok) {
            mrq->cmd->error = 0;
            spin_unlock_irqrestore(&host->mrq_lock, flags);
        } else {
            mrq->cmd->error = -EILSEQ;
            if((sdio_error_flag == 0) && aml_card_type_mmc(pdata)){  //set cmd retry cnt when first error.
                sdio_error_flag |= (1<<0);
                mrq->cmd->retries = AML_ERROR_RETRY_COUNTER; 
            }
            spin_unlock_irqrestore(&host->mrq_lock, flags);
            aml_sdio_print_err(host, "data crc16 error");
        }
		spin_lock_irqsave(&host->mrq_lock, flags);
        mrq->data->bytes_xfered = mrq->data->blksz*mrq->data->blocks;
       
        if((mrq->cmd->error == 0) || (sdio_error_flag && (mrq->cmd->retries == 0))){
            sdio_error_flag |= (1<<30);
        } 
               
        spin_unlock_irqrestore(&host->mrq_lock, flags);
        if(mrq->data->flags & MMC_DATA_READ){
            aml_sg_copy_buffer(mrq->data->sg, mrq->data->sg_len,
                host->bn_buf, mrq->data->blksz*mrq->data->blocks, 0);
            sdio_dbg(AMLSD_DBG_RD_DATA, "R Cmd %d, %x-%x-%x-%x\n",
                host->mrq->cmd->opcode,
                host->bn_buf[0], host->bn_buf[1],
                host->bn_buf[2], host->bn_buf[3]);
        }
        spin_lock_irqsave(&host->mrq_lock, flags);
        if(mrq->stop){
            aml_sdio_send_stop(host);
            spin_unlock_irqrestore(&host->mrq_lock, flags);
        }
        else{
            spin_unlock_irqrestore(&host->mrq_lock, flags);
            aml_sdio_request_done(host->mmc, mrq);
        }
    }
    return IRQ_HANDLED;
}

/*
1. clock valid range
2. clk config enable
3. select clock source
4. set clock divide
*/
static void aml_sdio_set_clk_rate(struct amlsd_platform* pdata, u32 clk_ios)
{
    struct amlsd_host* host = (void*)pdata->host;
    u32 vconf = readl(host->base + SDIO_CONF);
    struct sdio_config* conf = (void*)&vconf;
    struct clk* clk_src = clk_get_sys("clk81", NULL);
    u32 clk_rate = clk_get_rate(clk_src)/2;
    // u32 clk_rate = 159000000/2; //tmp for 3.10
    u32 clk_div;

    // aml_sdio_init_param(pdata);

    if(clk_ios > pdata->f_max)
        clk_ios = pdata->f_max;
    if(clk_ios < pdata->f_min)
        clk_ios = pdata->f_min;

    BUG_ON(!clk_ios);

    /*0: dont set it, 1:div2, 2:div3, 3:div4...*/
    clk_div = clk_rate / clk_ios - !(clk_rate%clk_ios);

   if(aml_card_type_sdio(pdata) && (pdata->f_max > 50000000)) // if > 50MHz
          clk_div = 0;

    conf->cmd_clk_divide = clk_div;
    pdata->clkc = clk_div;
    pdata->mmc->actual_clock = clk_rate / (clk_div + 1);
    writel(vconf, host->base + SDIO_CONF);

    sdio_dbg(AMLSD_DBG_IOS, "Clk IOS %d, Clk Src %d, Host Max Clk %d, clk_divide=%d\n",
            clk_ios, (clk_rate*2), pdata->f_max, clk_div);
    // sdio_err("Clk IOS %d, Clk Src %d, Host Max Clk %d, clk_divide=%d, actual_clock=%d\n",
            // clk_ios, (clk_rate*2), pdata->f_max, clk_div, pdata->mmc->actual_clock);
}

static void aml_sdio_set_bus_width(struct amlsd_platform* pdata, u32 busw_ios)
{
    u32 bus_width=0;
    struct amlsd_host* host = (void*)pdata->host;
    u32 vconf = readl(host->base + SDIO_CONF);
    struct sdio_config* conf = (void*)&vconf;

    switch(busw_ios)
    {
        case MMC_BUS_WIDTH_1:
            bus_width = 0;
            break;
        case MMC_BUS_WIDTH_4:
            bus_width = 1;
            break;
        case MMC_BUS_WIDTH_8:
        default:
            sdio_err("SDIO Controller Can Not Support 8bit Data Bus\n");
            break;
    }

    conf->bus_width = bus_width;
    pdata->width = bus_width;
    writel(vconf, host->base + SDIO_CONF);
    sdio_dbg(AMLSD_DBG_IOS, "Bus Width Ios %d\n", bus_width);
}


static void aml_sdio_set_power(struct amlsd_platform* pdata, u32 power_mode)
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

/* Routine to configure clock values. Exposed API to core */
static void aml_sdio_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);

    if(!pdata->is_in)
        return;

    /*set power*/
    aml_sdio_set_power(pdata, ios->power_mode);

    /*set clock*/
    if(ios->clock)
        aml_sdio_set_clk_rate(pdata, ios->clock);

    /*set bus width*/
    aml_sdio_set_bus_width(pdata, ios->bus_width);

    if (ios->chip_select == MMC_CS_HIGH) {
        aml_cs_high(pdata);
    } else if (ios->chip_select == MMC_CS_DONTCARE) {
        aml_cs_dont_care(pdata);
    } else { // MMC_CS_LOW
        /* Nothing to do */
    }
}

static int aml_sdio_get_ro(struct mmc_host *mmc)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);
    u32 ro = 0;

    if(pdata->ro)
        ro = pdata->ro(pdata);
    return ro;
}

int aml_sdio_get_cd(struct mmc_host *mmc)
{
    struct amlsd_platform * pdata = mmc_priv(mmc);
    return pdata->is_in; // 0: no inserted  1: inserted
}

#ifdef CONFIG_PM

static int aml_sdio_suspend(struct platform_device *pdev, pm_message_t state)
{
    int ret = 0;
    int i;
    struct amlsd_host *host = platform_get_drvdata(pdev);
    struct mmc_host* mmc;
    struct amlsd_platform* pdata;

    printk("***Entered %s:%s\n", __FILE__,__func__);
    i = 0;
    list_for_each_entry(pdata, &host->sibling, sibling){
        mmc = pdata->mmc;
        //mmc_power_save_host(mmc);
        ret = mmc_suspend_host(mmc);
        if (ret)
            break;

        // clear detect information when suspend
        // if (!(pdata->caps & MMC_CAP_NONREMOVABLE)) {
            // aml_sd_uart_detect_clr(pdata);
        // }

        i++;
    }

    if (ret) {
        list_for_each_entry(pdata, &host->sibling, sibling) {
            i--;
            if (i < 0) {
                break;
            }
            // if(aml_card_type_sdio(pdata)) { // sdio_wifi
                // wifi_setup_dt();
            // }
            if (!(pdata->caps & MMC_CAP_NONREMOVABLE)) {
                aml_sd_uart_detect(pdata);
            }
            mmc = pdata->mmc;
            mmc_resume_host(mmc);
        }
    }
    printk("***Exited %s:%s\n", __FILE__,__func__);

    return ret;
}

static int aml_sdio_resume(struct platform_device *pdev)
{
    int ret = 0;
    struct amlsd_host *host = platform_get_drvdata(pdev);
    struct mmc_host* mmc;
    struct amlsd_platform* pdata;

    printk("***Entered %s:%s\n", __FILE__,__func__);
    list_for_each_entry(pdata, &host->sibling, sibling){
        // if(pdata->port == MESON_SDIO_PORT_A) { // sdio_wifi
        // if(aml_card_type_sdio(pdata)) {
            // wifi_setup_dt();
        // }

        // detect if a card is exist or not if it is removable
        if (!(pdata->caps & MMC_CAP_NONREMOVABLE)) {
            aml_sd_uart_detect(pdata);
        }
        mmc = pdata->mmc;
        //mmc_power_restore_host(mmc);
        ret = mmc_resume_host(mmc);
        if (ret)
            break;
    }
    printk("***Exited %s:%s\n", __FILE__,__func__);
    return ret;
}

#else

#define aml_sdio_suspend    NULL
#define aml_sdio_resume        NULL

#endif

static const struct mmc_host_ops aml_sdio_ops = {
    .request = aml_sdio_request,
    .set_ios = aml_sdio_set_ios,
    .enable_sdio_irq = aml_sdio_enable_irq,
    .get_cd = aml_sdio_get_cd,
    .get_ro = aml_sdio_get_ro,
	.hw_reset = aml_emmc_hw_reset,
};

static ssize_t sdio_debug_func(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
//    struct amlsd_host *host = container_of(class, struct amlsd_host, debug);
    
    sscanf(buf, "%x", &sdio_debug_flag);    
    printk("sdio_debug_flag: %d\n", sdio_debug_flag);

    return count;
}

static ssize_t show_sdio_debug(struct class *class,
                    struct class_attribute *attr,	char *buf)
{
//    struct amlsd_host *host = container_of(class, struct amlsd_host, debug);
    
    printk("sdio_debug_flag: %d\n", sdio_debug_flag);
    printk("1 : Force sdio cmd crc error \n");
    printk("2 : Force sdio data crc error \n");
    printk("9 : Force sdio irq timeout error \n");

    return 0;
}

static struct class_attribute sdio_class_attrs[] = {
    __ATTR(debug,  S_IRUGO | S_IWUSR , show_sdio_debug, sdio_debug_func),
    __ATTR_NULL
};

static struct amlsd_host* aml_sdio_init_host(void)
{
    struct amlsd_host *host;

    spin_lock_init(&aml_sdio_claim.lock);
    init_waitqueue_head(&aml_sdio_claim.wq);

    host = kzalloc(sizeof(struct amlsd_host), GFP_KERNEL);

    if(request_threaded_irq(INT_SDIO, (irq_handler_t)aml_sdio_irq,
            aml_sdio_irq_thread, IRQF_DISABLED, "sdio", (void*)host)){
        sdio_err("Request SDIO Irq Error!\n");
        return NULL;
    }

    host->bn_buf = dma_alloc_coherent(NULL, SDIO_BOUNCE_REQ_SIZE,
                            &host->bn_dma_buf, GFP_KERNEL);
    if(NULL == host->bn_buf){
        sdio_err("Dma alloc Fail!\n");
        return NULL;
    }
    //setup_timer(&host->timeout_tlist, aml_sdio_timeout, (ulong)host);
    INIT_DELAYED_WORK(&host->timeout, aml_sdio_timeout);

    spin_lock_init(&host->mrq_lock);
    host->xfer_step = XFER_INIT;

    INIT_LIST_HEAD(&host->sibling);
    
    host->version = AML_MMC_VERSION;
    host->storage_flag = storage_flag;
    host->pinctrl = NULL;
    
    host->init_flag = 1;

#ifdef      CONFIG_MMC_AML_DEBUG
    host->req_cnt = 0;
    sdio_err("CONFIG_MMC_AML_DEBUG is on!\n");
#endif

	host->debug.name = kzalloc(strlen((const char*)AML_SDIO_MAGIC)+1, GFP_KERNEL);
	strcpy((char *)(host->debug.name), (const char*)AML_SDIO_MAGIC);
	host->debug.class_attrs = sdio_class_attrs;
	if(class_register(&host->debug))
		printk(" class register nand_class fail!\n");
		
    return host;
}

static int aml_sdio_probe(struct platform_device *pdev)
{
    struct mmc_host *mmc = NULL;
    struct amlsd_host *host = NULL;
    struct amlsd_platform* pdata;
    int ret = 0, i;

    // print_tmp("%s() begin!\n", __FUNCTION__);

    aml_mmc_ver_msg_show();

    host = aml_sdio_init_host();
    if(!host)
        goto fail_init_host;

    if(amlsd_get_reg_base(pdev, host))
        goto fail_init_host;
    
    //init sdio reg here
    aml_sdio_init_param(host);

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

        // if(pdata->parts){
        if (pdata->port == PORT_SDIO_C) {
            if (is_emmc_exist(host)) {
                mmc->is_emmc_port = 1;
                // add_part_table(pdata->parts, pdata->nr_parts);
                // mmc->add_part = add_emmc_partition;
            } else { // there is not eMMC/tsd
                printk("[%s]: there is not eMMC/tsd, skip sdio_c dts config!\n", __FUNCTION__);
                i++; // skip the port written in the dts
                memset(pdata, 0, sizeof(struct amlsd_platform));
                if(amlsd_get_platform_data(pdev, pdata, mmc, i)) {
                    mmc_free_host(mmc);
                    break;
                }
            }
        }
        dev_set_name(&mmc->class_dev, "%s", pdata->pinname);
        if (pdata->caps & MMC_CAP_NONREMOVABLE) {
            pdata->is_in = true;
        }
       if (pdata->caps & MMC_PM_KEEP_POWER)
            mmc->pm_caps |= MMC_PM_KEEP_POWER;

		if(pdata->caps& MMC_CAP_SDIO_IRQ){
			SDIO_IRQ_SUPPORT = 1;
		}
		
        pdata->host = host;
        // host->pdata = pdata; // should not do this here, it will conflict with aml_sdio_request
        // host->mmc = mmc;
        pdata->mmc = mmc;
        pdata->is_fir_init = true;

        mmc->index = i;
        mmc->ops = &aml_sdio_ops;
        mmc->alldev_claim = &aml_sdio_claim;
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

        if (aml_card_type_sdio(pdata)) { // if sdio_wifi
            mmc->host_rescan_disable = true;
			mmc->rescan_entered = 1; // do NOT run mmc_rescan for the first time
        } else {
            mmc->host_rescan_disable = false;
			mmc->rescan_entered = 0; 
        }
         
        if(pdata->port_init)
            pdata->port_init(pdata);

        aml_sduart_pre(pdata);

        ret = mmc_add_host(mmc);
        if (ret) { // error
            sdhc_err("Failed to add mmc host.\n");
            goto probe_free_host;
        } else { // ok
            if (aml_card_type_sdio(pdata)) { // if sdio_wifi
                sdio_host = mmc;
                //mmc->rescan_entered = 1; // do NOT run mmc_rescan for the first time
            }
        }

        aml_sdio_init_debugfs(mmc);
        /*Add each mmc host pdata to this controller host list*/
        INIT_LIST_HEAD(&pdata->sibling);
        list_add_tail(&pdata->sibling, &host->sibling);

        /*Register card detect irq : plug in & unplug*/
        if(pdata->irq_in && pdata->irq_out){
            pdata->irq_init(pdata);
            ret = request_threaded_irq(pdata->irq_in+INT_GPIO_0,
                    (irq_handler_t)aml_sd_irq_cd, aml_irq_cd_thread,
                    IRQF_DISABLED, "mmc_in", (void*)pdata);
            if (ret) {
                sdio_err("Failed to request mmc IN detect\n");
                goto probe_free_host;
            }
            ret |= request_threaded_irq(pdata->irq_out+INT_GPIO_0,
                    (irq_handler_t)aml_sd_irq_cd, aml_irq_cd_thread,
                    IRQF_DISABLED, "mmc_out", (void*)pdata);
            //ret = request_irq(pdata->irq_in+INT_GPIO_0, aml_sd_irq_cd, IRQF_DISABLED, "mmc_in", pdata);
            //ret |= request_irq(pdata->irq_out+INT_GPIO_0, aml_sd_irq_cd, IRQF_DISABLED, "mmc_out", pdata);
            if (ret) {
                sdio_err("Failed to request mmc OUT detect\n");
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
    free_irq(INT_SDIO, host);
    dma_free_coherent(NULL, SDIO_BOUNCE_REQ_SIZE, host->bn_buf,
            (dma_addr_t)host->bn_dma_buf);
    kfree(host);
    print_tmp("aml_sdio_probe() fail!\n");
    return ret;
}

int aml_sdio_remove(struct platform_device *pdev)
{
    struct amlsd_host* host = platform_get_drvdata(pdev);
    struct mmc_host* mmc;
    struct amlsd_platform* pdata;

    dma_free_coherent(NULL, SDIO_BOUNCE_REQ_SIZE, host->bn_buf,
            (dma_addr_t )host->bn_dma_buf);

    free_irq(INT_SDIO, host);
    iounmap(host->base);

    list_for_each_entry(pdata, &host->sibling, sibling){
        mmc = pdata->mmc;
        mmc_remove_host(mmc);
        mmc_free_host(mmc);
    }
    
    aml_devm_pinctrl_put(host);

    kfree(host);
    return 0;
}

static const struct of_device_id aml_sdio_dt_match[]={
     {
          .compatible = "amlogic,aml_sdio",
     },
     {},
};

MODULE_DEVICE_TABLE(of, aml_sdio_dt_match);

static struct platform_driver aml_sdio_driver = {
    .probe         = aml_sdio_probe,
    .remove        = aml_sdio_remove,
    .suspend    = aml_sdio_suspend,
    .resume        = aml_sdio_resume,
    .driver        = {
        .name = "aml_sdio",
        .owner = THIS_MODULE,
        .of_match_table=aml_sdio_dt_match,
    },
};

static int __init aml_sdio_init(void)
{
    return platform_driver_register(&aml_sdio_driver);
}

static void __exit aml_sdio_cleanup(void)
{
    platform_driver_unregister(&aml_sdio_driver);
}

module_init(aml_sdio_init);
module_exit(aml_sdio_cleanup);

MODULE_DESCRIPTION("Amlogic SDIO Controller driver");
MODULE_LICENSE("GPL");



