/*
 * drivers/mmc/sunxi-host/sdxc.c
 * (C) Copyright 2007-2011
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Aaron.Maoye <leafy.myeh@allwinnertech.com>
 *
 * sdxc.c - operation for register level control of mmc controller
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */
#include "host_op.h"
#include "sdxc.h"
#include "smc_syscall.h"

extern unsigned int smc_debug;

/******************************************************************************************************
 *                                       SD3.0 controller operation                                   *
 ******************************************************************************************************/

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_fifo_reset(struct sunxi_mmc_host* smc_host)
{
    writel(readl(SDXC_REG_GCTRL)|SDXC_FIFOReset, SDXC_REG_GCTRL);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_dma_reset(struct sunxi_mmc_host* smc_host)
{
    writel(readl(SDXC_REG_GCTRL)|SDXC_DMAReset, SDXC_REG_GCTRL);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
void sdxc_int_enable(struct sunxi_mmc_host* smc_host)
{
    writel(readl(SDXC_REG_GCTRL)|SDXC_INTEnb, SDXC_REG_GCTRL);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
void sdxc_int_disable(struct sunxi_mmc_host* smc_host)
{
    writel(readl(SDXC_REG_GCTRL)&(~SDXC_INTEnb), SDXC_REG_GCTRL);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_dma_enable(struct sunxi_mmc_host* smc_host)
{
    writel(readl(SDXC_REG_GCTRL)|SDXC_DMAEnb, SDXC_REG_GCTRL);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_dma_disable(struct sunxi_mmc_host* smc_host)
{
    writel(readl(SDXC_REG_GCTRL)|SDXC_DMAReset, SDXC_REG_GCTRL);
    writel(readl(SDXC_REG_GCTRL)&(~SDXC_DMAEnb), SDXC_REG_GCTRL);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_idma_reset(struct sunxi_mmc_host* smc_host)
{
    writel(SDXC_IDMACSoftRST, SDXC_REG_DMAC);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_idma_on(struct sunxi_mmc_host* smc_host)
{
    writel(SDXC_IDMACFixBurst | SDXC_IDMACIDMAOn, SDXC_REG_DMAC);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_idma_off(struct sunxi_mmc_host* smc_host)
{
    writel(0, SDXC_REG_DMAC);
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_idma_int_enable(struct sunxi_mmc_host* smc_host, u32 int_mask)
{
    writel(readl(SDXC_REG_IDIE)|int_mask, SDXC_REG_IDIE);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_idma_int_disable(struct sunxi_mmc_host* smc_host, u32 int_mask)
{
    writel(readl(SDXC_REG_IDIE) & (~int_mask), SDXC_REG_IDIE);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_cd_debounce_on(struct sunxi_mmc_host* smc_host)
{
    writel(readl(SDXC_REG_GCTRL)|SDXC_DebounceEnb, SDXC_REG_GCTRL);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_cd_debounce_off(struct sunxi_mmc_host* smc_host)
{
    writel(readl(SDXC_REG_GCTRL)&(~SDXC_DebounceEnb), SDXC_REG_GCTRL);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_sel_access_mode(struct sunxi_mmc_host* smc_host, u32 access_mode)
{
    writel((readl(SDXC_REG_GCTRL)&(~SDXC_ACCESS_BY_AHB)) | access_mode, SDXC_REG_GCTRL);
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline u32 sdxc_enable_imask(struct sunxi_mmc_host* smc_host, u32 imask)
{
	u32 newmask = readl(SDXC_REG_IMASK) | imask;
	if (!(imask & SDXC_SDIOInt))
		writew(newmask, SDXC_REG_IMASK);
	else
		writel(newmask, SDXC_REG_IMASK);
	return newmask;
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline u32 sdxc_disable_imask(struct sunxi_mmc_host* smc_host, u32 imask)
{
    u32 newmask = readl(SDXC_REG_IMASK) & (~imask);
	if (!(imask & SDXC_SDIOInt))
		writew(newmask, SDXC_REG_IMASK);
	else
		writel(newmask, SDXC_REG_IMASK);
	return newmask;
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static __inline void sdxc_clear_imask(struct sunxi_mmc_host* smc_host)
{
	/* use 16bit read/write operation to enable/disable other interrupt bits
	 * sdio/card-det bits are controlled by it self operation.
	 * If you want use 32bit operation, you must do an atomic operation on
	 * this read and write back operation.
	 */
	writew(0, SDXC_REG_IMASK);
//	writel(readl(SDXC_REG_IMASK)&(SDXC_SDIOInt|SDXC_CardInsert|SDXC_CardRemove), SDXC_REG_IMASK);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
void sdxc_enable_sdio_irq(struct sunxi_mmc_host* smc_host, u32 enable)
{
    if (enable)
        sdxc_enable_imask(smc_host, SDXC_SDIOInt);
    else
        sdxc_disable_imask(smc_host, SDXC_SDIOInt);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
void sdxc_sel_ddr_mode(struct sunxi_mmc_host* smc_host)
{
    writel(readl(SDXC_REG_GCTRL) | SDXC_DDR_MODE, SDXC_REG_GCTRL);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
void sdxc_sel_sdr_mode(struct sunxi_mmc_host* smc_host)
{
    writel(readl(SDXC_REG_GCTRL) & (~SDXC_DDR_MODE), SDXC_REG_GCTRL);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
void sdxc_set_buswidth(struct sunxi_mmc_host* smc_host, u32 width)
{
    switch(width)
    {
        case 1:
            writel(SDXC_WIDTH1, SDXC_REG_WIDTH);
            break;
        case 4:
            writel(SDXC_WIDTH4, SDXC_REG_WIDTH);
            break;
        case 8:
            writel(SDXC_WIDTH8, SDXC_REG_WIDTH);
            break;
    }
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
s32 sdxc_reset(struct sunxi_mmc_host* smc_host)
{
    u32 rval = readl(SDXC_REG_GCTRL) | SDXC_SoftReset | SDXC_FIFOReset | SDXC_DMAReset;
    s32 time = 0xffff;

    writel(rval, SDXC_REG_GCTRL);
    while((readl(SDXC_REG_GCTRL) & 0x7) && time--);
    if (time <= 0)
    {
        SMC_ERR("sdc %d reset failed\n", smc_host->pdev->id);
        return -1;
    }
    return 0;
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
s32 sdxc_program_clk(struct sunxi_mmc_host* smc_host)
{
  	u32 rval;
  	s32 time = 0xf000;
  	s32 ret = 0;

	//disable command done interrupt
	sdxc_disable_imask(smc_host, SDXC_CmdDone);

  	rval = SDXC_Start|SDXC_UPCLKOnly|SDXC_WaitPreOver;
  	writel(rval, SDXC_REG_CMDR);

	do {
	    rval = readl(SDXC_REG_CMDR);
	    time--;
	} while(time && (rval & SDXC_Start));

	if (time <= 0)
	{
		ret = -1;
	}

	//clear command cone flag
	rval = readl(SDXC_REG_RINTR) & (~SDXC_SDIOInt);
	writel(rval, SDXC_REG_RINTR);

	//enable command done interrupt
	sdxc_enable_imask(smc_host, SDXC_CmdDone);

	return ret;
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
s32 sdxc_update_clk(struct sunxi_mmc_host* smc_host, u32 sclk, u32 cclk)
{
    u32 rval;
    u32 clk_div;
    u32 real_clk;

    //caculate new clock divider
    clk_div = (sclk / cclk)>>1;
    real_clk = clk_div ? sclk/(clk_div<<1) : sclk;
    while (real_clk > cclk)
    {
        clk_div++;
        real_clk = sclk/(clk_div<<1);
    }

    SMC_DBG("sdc %d change clock over, src_clk %d, req_clk %d, real_clk %d, div %d\n", smc_host->pdev->id, sclk, cclk, real_clk, clk_div);

    //update new clock
    //disable clock
    rval = readl(SDXC_REG_CLKCR) & (~SDXC_CardClkOn) & (~SDXC_LowPowerOn);
    writel(rval, SDXC_REG_CLKCR);
    if (-1 == sdxc_program_clk(smc_host))
    {
        SMC_ERR("clock program failed in step 1\n");
        return -1;
    }

    //update divider
    rval = readl(SDXC_REG_CLKCR);
    rval &= ~0xff;
    rval |= clk_div & 0xff;
    writel(rval, SDXC_REG_CLKCR);
    if (-1 == sdxc_program_clk(smc_host))
    {
        SMC_ERR("clock program failed in step 2\n");
        return -1;
    }

    //re-enable clock
    rval = readl(SDXC_REG_CLKCR) | SDXC_CardClkOn ;//| SDXC_LowPowerOn;
    writel(rval, SDXC_REG_CLKCR);
    if (-1 == sdxc_program_clk(smc_host))
    {
        SMC_ERR("clock program failed in step 3\n");
        return -1;
    }

    smc_host->real_cclk = real_clk;
    return real_clk;
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static void sdxc_send_cmd(struct sunxi_mmc_host* smc_host, struct mmc_command* cmd)
{
    u32 imask;
    u32 cmd_val = SDXC_Start|(cmd->opcode&0x3f);

    imask = SDXC_CmdDone|SDXC_IntErrBit|SDXC_WaitPreOver;

    if (cmd->opcode == MMC_GO_IDLE_STATE)
    {
        cmd_val |= SDXC_SendInitSeq;
        smc_host->wait = SDC_WAIT_CMD_DONE;
    }
    else
    {
        if ((cmd->flags & MMC_CMD_MASK) != MMC_CMD_BC) //with response
        {
            cmd_val |= SDXC_RspExp;

            if (cmd->flags & MMC_RSP_136)                                   //long response
                cmd_val |= SDXC_LongRsp;

            if (cmd->flags & MMC_RSP_CRC)                                   //check response CRC
                cmd_val |= SDXC_CheckRspCRC;

            smc_host->wait = SDC_WAIT_CMD_DONE;

            if ((cmd->flags & MMC_CMD_MASK) == MMC_CMD_ADTC)                //with data transfer
            {
                cmd_val |= SDXC_DataExp | SDXC_WaitPreOver;
                smc_host->wait = SDC_WAIT_DATA_OVER;
                imask |= SDXC_DataOver;

                if (cmd->data->flags & MMC_DATA_STREAM)        //sequence mode
                {
                    imask |= SDXC_AutoCMDDone;
                    cmd_val |= SDXC_Seqmod | SDXC_SendAutoStop;
                    smc_host->wait = SDC_WAIT_AUTOCMD_DONE;
                }

                if (smc_host->with_autostop)
                {
                    imask |= SDXC_AutoCMDDone;
                    cmd_val |= SDXC_SendAutoStop;
                    smc_host->wait = SDC_WAIT_AUTOCMD_DONE;
                }

                if (cmd->data->flags & MMC_DATA_WRITE)           //read
                {
                    cmd_val |= SDXC_Write;
                    if (!smc_host->dodma) {
//                        SMC_MSG("SDXC_TxDataReq\n");
                        imask |= SDXC_TxDataReq;
                    }
                }
                else
                {
                    if (!smc_host->dodma) {
                        //imask &= ~(SDXC_AutoCMDDone | SDXC_DataOver);
                        imask |= SDXC_RxDataReq;
                        smc_host->wait = SDC_WAIT_READ_DONE;
//                        SMC_MSG("SDXC_RxDataReq\n");
                    }
                }
            }
        }
    }

    sdxc_enable_imask(smc_host, imask);

	//SMC_INFO("smc %d send cmd %d(%08x), imask = 0x%08x, wait = %d\n", smc_host->pdev->id, cmd_val&0x3f, cmd_val, imask, smc_host->wait);

    writel(cmd->arg, SDXC_REG_CARG);
    writel(cmd_val, SDXC_REG_CMDR);
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static void  sdxc_init_idma_des(struct sunxi_mmc_host* smc_host, struct mmc_data* data)
{
    struct sunxi_mmc_idma_des* pdes = smc_host->pdes;
    u32 des_idx = 0;
    u32 buff_frag_num = 0;
    u32 remain;
    u32 i, j;

    /* 初始化IDMA Descriptor */
    #if SDXC_DES_MODE == 0      //chain mode
    for (i=0; i<data->sg_len; i++)
    {
        buff_frag_num = data->sg[i].length >> SDXC_DES_NUM_SHIFT;   //SDXC_DES_NUM_SHIFT == 13, num = len/8192 = len>>13
        remain = data->sg[i].length & (SDXC_DES_BUFFER_MAX_LEN-1);
        if (remain)
        {
            buff_frag_num ++;
        }
        else
        {
            remain = SDXC_DES_BUFFER_MAX_LEN;
        }

        eLIBs_CleanFlushDCacheRegion(sg_virt(&data->sg[i]), data->sg[i].length);
        for (j=0; j < buff_frag_num; j++, des_idx++)
        {
			memset((void*)&pdes[des_idx], 0, sizeof(struct sunxi_mmc_idma_des));
            pdes[des_idx].des_chain = 1;
            pdes[des_idx].own = 1;
            pdes[des_idx].dic = 1;
            if (buff_frag_num > 1 && j != buff_frag_num-1)
            {
                pdes[des_idx].data_buf1_sz = 0x1fff & SDXC_DES_BUFFER_MAX_LEN;
            }
            else
            {
                pdes[des_idx].data_buf1_sz = remain;
            }

            pdes[des_idx].buf_addr_ptr1 = sg_dma_address(&data->sg[i]) + j * SDXC_DES_BUFFER_MAX_LEN;
            if (i==0 && j==0)
            {
                pdes[des_idx].first_des = 1;
            }

            if ((i == data->sg_len-1) && (j == buff_frag_num-1))
            {
                pdes[des_idx].dic = 0;
                pdes[des_idx].last_des = 1;
                pdes[des_idx].end_of_ring = 1;
                pdes[des_idx].buf_addr_ptr2 = 0;
            }
            else
            {
                pdes[des_idx].buf_addr_ptr2 = __pa(&pdes[des_idx+1]);
            }
			/*
            SMC_INFO("sg %d, frag %d, remain %d, des[%d](%08x): [0] = %08x, [1] = %08x, [2] = %08x, [3] = %08x\n", i, j, remain,
                                                                             des_idx, (u32)&pdes[des_idx],
                                                                             (u32)((u32*)&pdes[des_idx])[0], (u32)((u32*)&pdes[des_idx])[1],
                                                                             (u32)((u32*)&pdes[des_idx])[2], (u32)((u32*)&pdes[des_idx])[3]);
			*/
        }
    }
    #else      //fix length skip mode

    #endif

    eLIBs_CleanFlushDCacheRegion(pdes, sizeof(struct sunxi_mmc_idma_des) * (des_idx+1));

    return;
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
static int sdxc_prepare_dma(struct sunxi_mmc_host* smc_host, struct mmc_data* data)
{
    u32 dma_len;
    u32 i;

	if (smc_host->pdes == NULL)
	{
		return -ENOMEM;
	}

	dma_len = dma_map_sg(mmc_dev(smc_host->mmc), data->sg, data->sg_len, (data->flags & MMC_DATA_WRITE) ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
	if (dma_len == 0)
	{
		SMC_ERR("no dma map memory\n");
		return -ENOMEM;
	}

    for (i=0; i<data->sg_len; i++)
    {
        if (sg_dma_address(&data->sg[i]) & 3)
        {
		    SMC_ERR("unaligned dma address[%d] %p\n", i, (void*)sg_dma_address(&data->sg[i]));
			return -EINVAL;
        }
    }

    sdxc_init_idma_des(smc_host, data);
	sdxc_dma_enable(smc_host);
    sdxc_dma_reset(smc_host);
    sdxc_idma_reset(smc_host);
    sdxc_idma_on(smc_host);
    sdxc_idma_int_disable(smc_host, SDXC_IDMACTransmitInt|SDXC_IDMACReceiveInt);
    if (data->flags & MMC_DATA_WRITE)
    {
        sdxc_idma_int_enable(smc_host, SDXC_IDMACTransmitInt);
    }
    else
    {
        sdxc_idma_int_enable(smc_host, SDXC_IDMACReceiveInt);
    }

    //write descriptor address to register
    writel(__pa(smc_host->pdes), SDXC_REG_DLBA);

    //write water level
    writel((2U<<28)|(7<<16)|8, SDXC_REG_FTRGL);

    return 0;
}

static inline int sdxc_get_data_buffer(struct sunxi_mmc_host *smc_host, u32 *bytes, u32 **pointer)
{
	struct scatterlist *sg;

	if (smc_host->pio_active == XFER_NONE)
		return -EINVAL;

	if ((!smc_host->mrq) || (!smc_host->mrq->data))
		return -EINVAL;

	if (smc_host->pio_sgptr >= smc_host->mrq->data->sg_len) {
		SMC_DBG("no more buffers (%i/%i)\n",
		      smc_host->pio_sgptr, smc_host->mrq->data->sg_len);
		return -EBUSY;
	}
	sg = &smc_host->mrq->data->sg[smc_host->pio_sgptr];

	*bytes = sg->length;
	*pointer = sg_virt(sg);

	smc_host->pio_sgptr++;

	SMC_DBG("new buffer (%i/%i)\n", smc_host->pio_sgptr, smc_host->mrq->data->sg_len);

	return 0;
}

void sdxc_do_pio_read(struct sunxi_mmc_host* smc_host)
{
	int res;
	u32 fifo;
	u32 fifo_count;
	u32 *ptr;
	u32 fifo_words;
	void __iomem *from_ptr;

	from_ptr = SDXC_REG_FIFO;

    fifo_count = (readl(SDXC_REG_STAS)>>17)&0x1f;
	while (fifo_count) {
	    fifo = fifo_count << 2;
		if (!smc_host->pio_bytes) {
			res = sdxc_get_data_buffer(smc_host, &smc_host->pio_bytes, &smc_host->pio_ptr);
			if (res) {
				smc_host->pio_active = XFER_NONE;
				smc_host->wait = SDC_WAIT_FINALIZE;

				SMC_DBG("pio_read(): complete (no more data).\n");
				return;
			}

			SMC_DBG("pio_read(): new target: [%i]@[%p]\n",
			    smc_host->pio_bytes, smc_host->pio_ptr);
		}

//		SMC_DBG("pio_read(): fifo:[%02i] buffer:[%03i] dcnt:[%08X]\n",
//		    fifo, smc_host->pio_bytes, readl(SDXC_REG_BBCR));

		/* If we have reached the end of the block, we can
		 * read a word and get 1 to 3 bytes.  If we in the
		 * middle of the block, we have to read full words,
		 * otherwise we will write garbage, so round down to
		 * an even multiple of 4. */
		if (fifo >= smc_host->pio_bytes)
			fifo = smc_host->pio_bytes;
		else
			fifo -= fifo & 3;

		smc_host->pio_bytes -= fifo;
		smc_host->pio_count += fifo;

		fifo_words = fifo >> 2;
		ptr = smc_host->pio_ptr;
		while (fifo_words--)
			*ptr++ = readl(from_ptr);
		smc_host->pio_ptr = ptr;

		if (fifo & 3) {
			u32 n = fifo & 3;
			u32 data = readl(from_ptr);
			u8 *p = (u8 *)smc_host->pio_ptr;

			while (n--) {
				*p++ = data;
				data >>= 8;
			}
		}

        fifo_count = (readl(SDXC_REG_STAS)>>17)&0x1f;
	}

	if (!smc_host->pio_bytes) {
		res = sdxc_get_data_buffer(smc_host, &smc_host->pio_bytes, &smc_host->pio_ptr);
		if (res) {
			SMC_DBG("pio_read(): complete (no more buffers).\n");
			smc_host->pio_active = XFER_NONE;
			smc_host->wait = SDC_WAIT_FINALIZE;
			return;
		}
	}

	sdxc_enable_imask(smc_host, SDXC_RxDataReq);
}

void sdxc_do_pio_write(struct sunxi_mmc_host *smc_host)
{
	void __iomem *to_ptr;
	int ret;
	u32 fifo;
	u32 fifo_free = 0;
	u32 *ptr;

	to_ptr = SDXC_REG_FIFO;

    fifo_free = SDXC_FIFO_SIZE - ((readl(SDXC_REG_STAS)>>17)&0x1f);
	while (fifo_free) {
	    fifo = fifo_free << 2;
		if (!smc_host->pio_bytes) {
			ret = sdxc_get_data_buffer(smc_host, &smc_host->pio_bytes, &smc_host->pio_ptr);
			if (ret) {
				SMC_DBG("pio_write(): complete (no more data).\n");
				smc_host->pio_active = XFER_NONE;

				return;
			}

			SMC_DBG("pio_write(): new source: [%i]@[%p]\n", smc_host->pio_bytes, smc_host->pio_ptr);
		}

		/* If we have reached the end of the block, we have to
		 * write exactly the remaining number of bytes.  If we
		 * in the middle of the block, we have to write full
		 * words, so round down to an even multiple of 4. */
		if (fifo >= smc_host->pio_bytes)
			fifo = smc_host->pio_bytes;
		else
			fifo -= fifo & 3;

		smc_host->pio_bytes -= fifo;
		smc_host->pio_count += fifo;

		fifo = (fifo + 3) >> 2;
		ptr = smc_host->pio_ptr;
		while (fifo--)
			writel(*ptr++, to_ptr);
		smc_host->pio_ptr = ptr;

        fifo_free = SDXC_FIFO_SIZE - ((readl(SDXC_REG_STAS)>>17)&0x1f);
	}

	sdxc_enable_imask(smc_host, SDXC_TxDataReq);
}

#define BOTH_DIR (MMC_DATA_WRITE | MMC_DATA_READ)
static int sdxc_prepare_pio(struct sunxi_mmc_host* smc_host, struct mmc_data* data)
{
	int rw = (data->flags & MMC_DATA_WRITE) ? 1 : 0;

	if ((data->flags & BOTH_DIR) == BOTH_DIR)
		return -EINVAL;

	smc_host->pio_sgptr = 0;
	smc_host->pio_bytes = 0;
	smc_host->pio_count = 0;
	smc_host->pio_active = rw ? XFER_WRITE : XFER_READ;

	if (rw)
		sdxc_do_pio_write(smc_host);

	return 0;
}

int sdxc_check_r1_ready(struct sunxi_mmc_host* smc_host)
{
    return readl(SDXC_REG_STAS) & SDXC_CardDataBusy ? 0 : 1;
}

int sdxc_send_manual_stop(struct sunxi_mmc_host* smc_host, struct mmc_request* request)
{
	struct mmc_data* data = request->data;
	u32 cmd_val = SDXC_Start | SDXC_RspExp | SDXC_CheckRspCRC | MMC_STOP_TRANSMISSION;
	u32 iflags = 0;
	int ret = 0;

	if (!data || !data->stop)
	{
		SMC_ERR("no stop cmd request\n");
		return -1;
	}

	sdxc_int_disable(smc_host);

	writel(0, SDXC_REG_CARG);
	writel(cmd_val, SDXC_REG_CMDR);
	do {
		iflags = readl(SDXC_REG_RINTR);
	} while(!(iflags & (SDXC_CmdDone | SDXC_IntErrBit)));

	if (iflags & SDXC_IntErrBit)
	{
		SMC_ERR("sdc %d send stop command failed\n", smc_host->pdev->id);
		data->stop->error = ETIMEDOUT;
		ret = -1;
	}

	writel(iflags & (~SDXC_SDIOInt), SDXC_REG_RINTR);
    data->stop->resp[0] = readl(SDXC_REG_RESP0);

	sdxc_int_enable(smc_host);

	return ret;
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
void sdxc_request(struct sunxi_mmc_host* smc_host, struct mmc_request* request)
{
    struct mmc_command* cmd = request->cmd;
    struct mmc_data* data = request->data;
    struct scatterlist* sg = NULL;
    u32 byte_cnt = 0;
    int ret;

    smc_host->mrq = request;
    smc_host->int_sum = 0;
    SMC_DBG("smc %d, cmd %d, arg %08x\n", smc_host->pdev->id, cmd->opcode, cmd->arg);
    if (data)
    {
        sg = data->sg;
        byte_cnt = data->blksz * data->blocks;

        writel(data->blksz, SDXC_REG_BLKSZ);
        writel(byte_cnt, SDXC_REG_BCNTR);

        SMC_DBG("-> with data %d bytes, sg_len %d\n", byte_cnt, data->sg_len);
        if (byte_cnt > 0)
        {
//            SMC_MSG("-> trans by dma\n");
            sdxc_sel_access_mode(smc_host, SDXC_ACCESS_BY_DMA);
            smc_host->todma = 0;
            ret = sdxc_prepare_dma(smc_host, data);
            if (ret < 0)
            {
                SMC_ERR("smc %d prepare DMA failed\n", smc_host->pdev->id);
		        smc_host->dodma = 0;

                SMC_ERR("data prepare error %d\n", ret);
    			cmd->error = ret;
    			cmd->data->error = ret;
    			mmc_request_done(smc_host->mmc, request);
    			return;
            }
            smc_host->dodma = 1;
        }
        else
        {
//            SMC_MSG("-> trans by ahb\n");
            sdxc_sel_access_mode(smc_host, SDXC_ACCESS_BY_AHB);
            smc_host->todma = 0;
            ret = sdxc_prepare_pio(smc_host, data);
            if (ret < 0)
            {
                SMC_ERR("smc %d prepare ahb failed\n", smc_host->pdev->id);
    			cmd->error = ret;
    			cmd->data->error = ret;
    			mmc_request_done(smc_host->mmc, request);
    			return;
            }
		    smc_host->dodma = 0;
        }

        if (data->stop)
            smc_host->with_autostop = 1;
        else
            smc_host->with_autostop = 0;
    }

    /* disable card detect debounce */
    sdxc_cd_debounce_off(smc_host);
    sdxc_send_cmd(smc_host, cmd);
}


/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
void sdxc_check_status(struct sunxi_mmc_host* smc_host)
{
    u32 raw_int;
    u32 msk_int;
	u32 idma_inte;
    u32 idma_int;

    sdxc_int_disable(smc_host);

    idma_int = readl(SDXC_REG_IDST);
    idma_inte = readl(SDXC_REG_IDIE);
    raw_int = readl(SDXC_REG_RINTR);
    msk_int = readl(SDXC_REG_MISTA);

    smc_host->int_sum |= raw_int;
    //SMC_INFO("smc %d int, ri %08x(%08x) mi %08x ie %08x idi %08x\n", smc_host->pdev->id, raw_int, smc_host->int_sum, msk_int, idma_inte, idma_int);

	if (msk_int & SDXC_SDIOInt)
	{
		smc_host->sdio_int = 1;
		writel(SDXC_SDIOInt, SDXC_REG_RINTR);
	}

	if (smc_host->cd_gpio == CARD_DETECT_BY_DATA3)
    {
        if (msk_int&SDXC_CardInsert)
        {
    	    SMC_DBG("card detect insert\n");
    	    smc_host->present = 1;
    	    smc_host->change = 1;
    	    writel(SDXC_CardInsert, SDXC_REG_RINTR);
    		goto irq_out;
    	}
    	if (msk_int&SDXC_CardRemove)
    	{
    	    SMC_DBG("card detect remove\n");
    	    smc_host->present = 0;
    	    smc_host->change = 1;
    	    writel(SDXC_CardRemove, SDXC_REG_RINTR);
    		goto irq_out;
    	}
    }

    if (smc_host->wait == SDC_WAIT_NONE && !smc_host->sdio_int)
    {
    	SMC_ERR("smc %x, nothing to complete, raw_int = %08x, mask_int = %08x\n", smc_host->pdev->id, raw_int, msk_int);
    	sdxc_clear_imask(smc_host);
		goto irq_normal_out;
    }

    if ((raw_int & SDXC_IntErrBit) || (idma_int & SDXC_IDMA_ERR))
    {
        smc_host->error = raw_int & SDXC_IntErrBit;
        smc_host->wait = SDC_WAIT_FINALIZE;
        goto irq_normal_out;
    }

	if (!smc_host->dodma) {
		if ((smc_host->pio_active == XFER_WRITE) && (raw_int & SDXC_TxDataReq)) {
			sdxc_disable_imask(smc_host, SDXC_TxDataReq);
			tasklet_schedule(&smc_host->tasklet);
		}

		if ((smc_host->pio_active == XFER_READ) && (raw_int & SDXC_RxDataReq)) {
			sdxc_disable_imask(smc_host, SDXC_RxDataReq);
			tasklet_schedule(&smc_host->tasklet);
		}

		if (msk_int&SDXC_DataOver) {
		    sdxc_disable_imask(smc_host, SDXC_TxDataReq|SDXC_RxDataReq);
			tasklet_schedule(&smc_host->tasklet);
		}

	}

	if (smc_host->wait == SDC_WAIT_AUTOCMD_DONE && (msk_int&SDXC_AutoCMDDone))
	{
	    smc_host->wait = SDC_WAIT_FINALIZE;
	}
	else if (smc_host->wait == SDC_WAIT_DATA_OVER && (msk_int&SDXC_DataOver))
	{
	    smc_host->wait = SDC_WAIT_FINALIZE;
	}
	else if (smc_host->wait == SDC_WAIT_CMD_DONE && (msk_int&SDXC_CmdDone) && !(smc_host->int_sum&SDXC_IntErrBit))
	{
	    smc_host->wait = SDC_WAIT_FINALIZE;
	}

irq_normal_out:
    writel((~SDXC_SDIOInt) & msk_int, SDXC_REG_RINTR);
	writel(idma_int, SDXC_REG_IDST);

irq_out:

    sdxc_int_enable(smc_host);
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
s32 sdxc_request_done(struct sunxi_mmc_host* smc_host)
{
    struct mmc_request* req = smc_host->mrq;
    u32 temp;
    s32 ret = 0;

    if (smc_host->int_sum & SDXC_IntErrBit)
    {
        SMC_ERR("smc %d err, cmd %d, %s%s%s%s%s%s%s%s%s%s !!\n",
            smc_host->pdev->id, req->cmd->opcode,
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

        if (req->data)
        {
            SMC_ERR("In data %s operation\n", req->data->flags & MMC_DATA_WRITE ? "write" : "read");
        }
    	ret = -1;
        goto _out_;
    }

    if (req->cmd)
    {
        if (req->cmd->flags & MMC_RSP_136)
    	{
    		req->cmd->resp[0] = readl(SDXC_REG_RESP3);
    		req->cmd->resp[1] = readl(SDXC_REG_RESP2);
    		req->cmd->resp[2] = readl(SDXC_REG_RESP1);
    		req->cmd->resp[3] = readl(SDXC_REG_RESP0);
    	}
    	else
    	{
    		req->cmd->resp[0] = readl(SDXC_REG_RESP0);
    	}
    }

_out_:
    if (req->data)
    {
        if (!(req->data->flags & MMC_DATA_WRITE) && (readl(SDXC_REG_STAS) & SDXC_DataFSMBusy))
        {
            if ((readl(SDXC_REG_STAS) & SDXC_DataFSMBusy)
                && (readl(SDXC_REG_STAS) & SDXC_DataFSMBusy)
                && (readl(SDXC_REG_STAS) & SDXC_DataFSMBusy)
                && (readl(SDXC_REG_STAS) & SDXC_DataFSMBusy)
                && (readl(SDXC_REG_STAS) & SDXC_DataFSMBusy))
                    SMC_DBG("mmc %d fsm busy 0x%x len %d\n",
                        smc_host->pdev->id, readl(SDXC_REG_STAS), req->data->blksz * req->data->blocks);
        }
        if (smc_host->dodma)
        {
    		smc_host->dma_done = 0;
            writel(0x337, SDXC_REG_IDST);
            writel(0, SDXC_REG_IDIE);
            sdxc_idma_off(smc_host);
            sdxc_dma_disable(smc_host);
        }

        sdxc_fifo_reset(smc_host);
        #if 0
        if (smc_host->pdev->id == 3)
        {
            int i = 0;
            char* dirstr = req->data->flags & MMC_DATA_WRITE ? "== Tx Data ==" : "== Rx Data ==";
            u32 sg_len = req->data->sg_len;

            SMC_MSG("%s\n", dirstr);
            for (i=0; i<sg_len; i++) {
                char sgstr[8] = {0};
                sprintf(sgstr, "sg[%d]", i);
                hexdump(sgstr, sg_virt(&req->data->sg[i]), req->data->sg[i].length > 64 ? 64 : req->data->sg[i].length);
            }
        }
        #endif
    }

    temp = readl(SDXC_REG_STAS);
    if ((temp & SDXC_DataFSMBusy) || (smc_host->int_sum & (SDXC_RespErr | SDXC_HardWLocked | SDXC_RespTimeout)))
    {
        SMC_DBG("sdc %d abnormal status: %s %s\n", smc_host->pdev->id,
                                                  temp & SDXC_DataFSMBusy ? "DataFSMBusy" : "",
                                                  smc_host->int_sum & SDXC_HardWLocked ? "HardWLocked" : "");
        sdxc_reset(smc_host);
        sdxc_program_clk(smc_host);
    }

    writel(0xffff, SDXC_REG_RINTR);
    sdxc_clear_imask(smc_host);
    //re-enable card detect debounce
    if (smc_host->cd_gpio == CARD_DETECT_BY_DATA3)
    {
        sdxc_cd_debounce_on(smc_host);
    }

    SMC_DBG("smc %d done, resp %08x %08x %08x %08x\n", smc_host->pdev->id, req->cmd->resp[0], req->cmd->resp[1], req->cmd->resp[2], req->cmd->resp[3]);

	if (req->data && req->data->stop && (smc_host->int_sum & SDXC_IntErrBit))
	{
		SMC_MSG("found data error, need to send stop command !!\n");
		sdxc_send_manual_stop(smc_host, req);
	}

    return ret;
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
void sdxc_regs_save(struct sunxi_mmc_host* smc_host)
{
	struct sunximmc_ctrl_regs* bak_regs = &smc_host->bak_regs;

	bak_regs->gctrl		= readl(SDXC_REG_GCTRL);
	bak_regs->clkc		= readl(SDXC_REG_CLKCR);
	bak_regs->timeout	= readl(SDXC_REG_TMOUT);
	bak_regs->buswid	= readl(SDXC_REG_WIDTH);
	bak_regs->waterlvl	= readl(SDXC_REG_FTRGL);
	bak_regs->funcsel	= readl(SDXC_REG_FUNS);
	bak_regs->debugc	= readl(SDXC_REG_DBGC);
	bak_regs->idmacc	= readl(SDXC_REG_DMAC);
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
void sdxc_regs_restore(struct sunxi_mmc_host* smc_host)
{
	struct sunximmc_ctrl_regs* bak_regs = &smc_host->bak_regs;

    writel(bak_regs->gctrl   , SDXC_REG_GCTRL);
    writel(bak_regs->clkc    , SDXC_REG_CLKCR);
    writel(bak_regs->timeout , SDXC_REG_TMOUT);
    writel(bak_regs->buswid  , SDXC_REG_WIDTH);
    writel(bak_regs->waterlvl, SDXC_REG_FTRGL);
    writel(bak_regs->funcsel , SDXC_REG_FUNS );
    writel(bak_regs->debugc  , SDXC_REG_DBGC );
    writel(bak_regs->idmacc  , SDXC_REG_DMAC );
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
s32 sdxc_init(struct sunxi_mmc_host* smc_host)
{
	struct sunxi_mmc_idma_des* pdes = NULL;

    /* reset controller */
    if (-1 == sdxc_reset(smc_host))
    {
        return -1;
    }

    writel(SDXC_PosedgeLatchData, SDXC_REG_GCTRL);

    /* config DMA/Interrupt Trigger threshold */
    writel(0x70008, SDXC_REG_FTRGL);

    /* config timeout register */
    writel(0xffffffff, SDXC_REG_TMOUT);

    /* clear interrupt flags */
    writel(0xffffffff, SDXC_REG_RINTR);

    writel(0xdeb, SDXC_REG_DBGC);
    writel(0xceaa0000, SDXC_REG_FUNS);

    sdxc_int_enable(smc_host);

   	/* alloc idma descriptor structure */
	pdes = (struct sunxi_mmc_idma_des*)kmalloc(sizeof(struct sunxi_mmc_idma_des) * SDXC_MAX_DES_NUM, GFP_DMA | GFP_KERNEL);
	if (pdes == NULL)
	{
	    SMC_ERR("alloc dma des failed\n");
	    return -1;
	}
	smc_host->pdes = pdes;
    return 0;
}

/*
 * Method	  :
 * Description:
 * Parameters :
 *
 * Returns    :
 * Note       :
 */
s32 sdxc_exit(struct sunxi_mmc_host* smc_host)
{
	/* free idma descriptor structrue */
	if (smc_host->pdes)
	{
    	kfree((void*)smc_host->pdes);
		smc_host->pdes = NULL;
	}

    /* reset controller */
    if (-1 == sdxc_reset(smc_host))
    {
        return -1;
    }

    return 0;
}

