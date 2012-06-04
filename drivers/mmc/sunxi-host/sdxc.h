/*
 * drivers/mmc/sunxi-host/sdxc.h
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
#ifndef _SUNXI_SDXC_H_
#define _SUNXI_SDXC_H_

/******************************************************************************************************
 *                                   Define For SD3.0 Controller                                      *
 ******************************************************************************************************/
/* register offset define */
#define SDXC_REG_o_GCTRL              (0x00)              // SMC Global Control Register
#define SDXC_REG_o_CLKCR              (0x04)              // SMC Clock Control Register
#define SDXC_REG_o_TMOUT              (0x08)              // SMC Time Out Register
#define SDXC_REG_o_WIDTH              (0x0C)              // SMC Bus Width Register
#define SDXC_REG_o_BLKSZ              (0x10)              // SMC Block Size Register
#define SDXC_REG_o_BCNTR              (0x14)              // SMC Byte Count Register
#define SDXC_REG_o_CMDR               (0x18)              // SMC Command Register
#define SDXC_REG_o_CARG               (0x1C)              // SMC Argument Register
#define SDXC_REG_o_RESP0              (0x20)              // SMC Response Register 0
#define SDXC_REG_o_RESP1              (0x24)              // SMC Response Register 1
#define SDXC_REG_o_RESP2              (0x28)              // SMC Response Register 2
#define SDXC_REG_o_RESP3              (0x2C)              // SMC Response Register 3
#define SDXC_REG_o_IMASK              (0x30)              // SMC Interrupt Mask Register
#define SDXC_REG_o_MISTA              (0x34)              // SMC Masked Interrupt Status Register
#define SDXC_REG_o_RINTR              (0x38)              // SMC Raw Interrupt Status Register
#define SDXC_REG_o_STAS               (0x3C)              // SMC Status Register
#define SDXC_REG_o_FTRGL              (0x40)              // SMC FIFO Threshold Watermark Register
#define SDXC_REG_o_FUNS               (0x44)              // SMC Function Select Register
#define SDXC_REG_o_CBCR               (0x48)              // SMC CIU Byte Count Register
#define SDXC_REG_o_BBCR               (0x4C)              // SMC BIU Byte Count Register
#define SDXC_REG_o_DBGC               (0x50)              // SMC Debug Enable Register
#define SDXC_REG_o_DMAC               (0x80)              // SMC IDMAC Control Register
#define SDXC_REG_o_DLBA               (0x84)              // SMC IDMAC Descriptor List Base Address Register
#define SDXC_REG_o_IDST               (0x88)              // SMC IDMAC Status Register
#define SDXC_REG_o_IDIE               (0x8C)              // SMC IDMAC Interrupt Enable Register
#define SDXC_REG_o_CHDA               (0x90)
#define SDXC_REG_o_CBDA               (0x94)
#define SDXC_REG_o_FIFO               (0x100)             // SMC FIFO Access Address

#define SDXC_REG_GCTRL                  (smc_host->smc_base + SDXC_REG_o_GCTRL)
#define SDXC_REG_CLKCR                  (smc_host->smc_base + SDXC_REG_o_CLKCR)
#define SDXC_REG_TMOUT                  (smc_host->smc_base + SDXC_REG_o_TMOUT)
#define SDXC_REG_WIDTH                  (smc_host->smc_base + SDXC_REG_o_WIDTH)
#define SDXC_REG_BLKSZ                  (smc_host->smc_base + SDXC_REG_o_BLKSZ)
#define SDXC_REG_BCNTR                  (smc_host->smc_base + SDXC_REG_o_BCNTR)
#define SDXC_REG_CMDR                   (smc_host->smc_base + SDXC_REG_o_CMDR )
#define SDXC_REG_CARG                   (smc_host->smc_base + SDXC_REG_o_CARG )
#define SDXC_REG_RESP0                  (smc_host->smc_base + SDXC_REG_o_RESP0)
#define SDXC_REG_RESP1                  (smc_host->smc_base + SDXC_REG_o_RESP1)
#define SDXC_REG_RESP2                  (smc_host->smc_base + SDXC_REG_o_RESP2)
#define SDXC_REG_RESP3                  (smc_host->smc_base + SDXC_REG_o_RESP3)
#define SDXC_REG_IMASK                  (smc_host->smc_base + SDXC_REG_o_IMASK)
#define SDXC_REG_MISTA                  (smc_host->smc_base + SDXC_REG_o_MISTA)
#define SDXC_REG_RINTR                  (smc_host->smc_base + SDXC_REG_o_RINTR)
#define SDXC_REG_STAS                   (smc_host->smc_base + SDXC_REG_o_STAS )
#define SDXC_REG_FTRGL                  (smc_host->smc_base + SDXC_REG_o_FTRGL)
#define SDXC_REG_FUNS                   (smc_host->smc_base + SDXC_REG_o_FUNS )
#define SDXC_REG_CBCR                   (smc_host->smc_base + SDXC_REG_o_CBCR )
#define SDXC_REG_BBCR                   (smc_host->smc_base + SDXC_REG_o_BBCR )
#define SDXC_REG_DBGC                   (smc_host->smc_base + SDXC_REG_o_DBGC )
#define SDXC_REG_DMAC                   (smc_host->smc_base + SDXC_REG_o_DMAC )
#define SDXC_REG_DLBA                   (smc_host->smc_base + SDXC_REG_o_DLBA )
#define SDXC_REG_IDST                   (smc_host->smc_base + SDXC_REG_o_IDST )
#define SDXC_REG_IDIE                   (smc_host->smc_base + SDXC_REG_o_IDIE )
#define SDXC_REG_CHDA                   (smc_host->smc_base + SDXC_REG_o_CHDA )
#define SDXC_REG_CBDA                   (smc_host->smc_base + SDXC_REG_o_CBDA )
#define SDXC_REG_FIFO                   (smc_host->smc_base + SDXC_REG_o_FIFO )

/* bit field for registers */
/* global control register */
#define SDXC_SoftReset                (0x1U<<0)
#define SDXC_FIFOReset                (0x1U<<1)
#define SDXC_DMAReset                 (0x1U<<2)
#define SDXC_INTEnb                   (0x1U<<4)
#define SDXC_DMAEnb                   (0x1U<<5)
#define SDXC_DebounceEnb              (0x1U<<8)
#define SDXC_PosedgeLatchData         (0x1U<<9)
#define SDXC_NegedgeLatchData         (0x0U<<9)
#define SDXC_DDR_MODE                 (0x1U<<10)
#define SDXC_ACCESS_BY_AHB            (0x1U<<31)
#define SDXC_ACCESS_BY_DMA            (0x0U<<31)
/* Clock control */
#define SDXC_CardClkOn                (1U<<16)
#define SDXC_LowPowerOn               (1U<<17)
/* bus width */
#define SDXC_WIDTH1                   (0)
#define SDXC_WIDTH4                   (1)
#define SDXC_WIDTH8                   (2)
/* Struct for SMC Commands */
#define SDXC_RspExp                   (0x1U<<6)  //0x40
#define SDXC_LongRsp                  (0x1U<<7)  //0x80
#define SDXC_CheckRspCRC              (0x1U<<8)  //0x100
#define SDXC_DataExp                  (0x1U<<9)  //0x200
#define SDXC_Read                     (0x0U<<10) //0x000
#define SDXC_Write                    (0x1U<<10) //0x400
#define SDXC_Blockmod                 (0x0U<<11) //0x000
#define SDXC_Seqmod                   (0x1U<<11) //0x800
#define SDXC_SendAutoStop             (0x1U<<12) //0x1000
#define SDXC_WaitPreOver              (0x1U<<13) //0x2000
#define SDXC_StopAbortCMD             (0x1U<<14) //0x4000
#define SDXC_SendInitSeq              (0x1U<<15) //0x8000
#define SDXC_UPCLKOnly                (0x1U<<21) //0x200000
#define SDXC_RdCEATADev               (0x1U<<22) //0x400000
#define SDXC_CCSExp                   (0x1U<<23) //0x800000
#define SDXC_EnbBoot                  (0x1U<<24) //0x1000000
#define SDXC_AltBootOpt               (0x1U<<25) //0x2000000
#define SDXC_MandBootOpt              (0x0U<<25) //0x0000000
#define SDXC_BootACKExp               (0x1U<<26) //0x4000000
#define SDXC_DisableBoot              (0x1U<<27) //0x8000000
#define SDXC_VolSwitch                (0x1U<<28) //0x10000000
#define SDXC_Start                    (0x1U<<31) //0x80000000
/* Struct for Intrrrupt Information */
#define SDXC_RespErr                  (0x1U<<1)  //0x2
#define SDXC_CmdDone                  (0x1U<<2)  //0x4
#define SDXC_DataOver                 (0x1U<<3)  //0x8
#define SDXC_TxDataReq                (0x1U<<4)  //0x10
#define SDXC_RxDataReq                (0x1U<<5)  //0x20
#define SDXC_RespCRCErr               (0x1U<<6)  //0x40
#define SDXC_DataCRCErr               (0x1U<<7)  //0x80
#define SDXC_RespTimeout              (0x1U<<8)  //0x100
#define SDXC_ACKRcv                   (0x1U<<8)  //0x100
#define SDXC_DataTimeout              (0x1U<<9)  //0x200
#define SDXC_BootStart                (0x1U<<9)  //0x200
#define SDXC_DataStarve               (0x1U<<10) //0x400
#define SDXC_VolChgDone               (0x1U<<10) //0x400
#define SDXC_FIFORunErr               (0x1U<<11) //0x800
#define SDXC_HardWLocked              (0x1U<<12) //0x1000
#define SDXC_StartBitErr              (0x1U<<13) //0x2000
#define SDXC_AutoCMDDone              (0x1U<<14) //0x4000
#define SDXC_EndBitErr                (0x1U<<15) //0x8000
#define SDXC_SDIOInt                  (0x1U<<16) //0x10000
#define SDXC_CardInsert               (0x1U<<30) //0x40000000
#define SDXC_CardRemove               (0x1U<<31) //0x80000000
#define SDXC_IntErrBit                (SDXC_RespErr | SDXC_RespCRCErr | SDXC_DataCRCErr | SDXC_RespTimeout | SDXC_DataTimeout  \
                                        | SDXC_FIFORunErr | SDXC_HardWLocked | SDXC_StartBitErr | SDXC_EndBitErr)  //0xbfc2
/* status */
#define SDXC_RXWLFlag                 (0x1U<<0)
#define SDXC_TXWLFlag                 (0x1U<<1)
#define SDXC_FIFOEmpty                (0x1U<<2)
#define SDXC_FIFOFull                 (0x1U<<3)
#define SDXC_CardPresent              (0x1U<<8)
#define SDXC_CardDataBusy             (0x1U<<9)
#define SDXC_DataFSMBusy              (0x1U<<10)
#define SDXC_DMAReq                   (0x1U<<31)
#define SDXC_FIFO_SIZE                (16)
/* Function select */
#define SDXC_CEATAOn                  (0xceaaU<<16)
#define SDXC_SendIrqRsp               (0x1U<<0)
#define SDXC_SDIORdWait               (0x1U<<1)
#define SDXC_AbtRdData                (0x1U<<2)
#define SDXC_SendCCSD                 (0x1U<<8)
#define SDXC_SendAutoStopCCSD         (0x1U<<9)
#define SDXC_CEATADevIntEnb           (0x1U<<10)
/* IDMA controller bus mod bit field */
#define SDXC_IDMACSoftRST             (0x1U<<0)
#define SDXC_IDMACFixBurst            (0x1U<<1)
#define SDXC_IDMACIDMAOn              (0x1U<<7)
#define SDXC_IDMACRefetchDES          (0x1U<<31)
/* IDMA status bit field */
#define SDXC_IDMACTransmitInt         (0x1U<<0)
#define SDXC_IDMACReceiveInt          (0x1U<<1)
#define SDXC_IDMACFatalBusErr         (0x1U<<2)
#define SDXC_IDMACDesInvalid          (0x1U<<4)
#define SDXC_IDMACCardErrSum          (0x1U<<5)
#define SDXC_IDMACNormalIntSum        (0x1U<<8)
#define SDXC_IDMACAbnormalIntSum      (0x1U<<9)
#define SDXC_IDMACHostAbtInTx         (0x1U<<10)
#define SDXC_IDMACHostAbtInRx         (0x1U<<10)
#define SDXC_IDMACIdle                (0x0U<<13)
#define SDXC_IDMACSuspend             (0x1U<<13)
#define SDXC_IDMACDESCRd              (0x2U<<13)
#define SDXC_IDMACDESCCheck           (0x3U<<13)
#define SDXC_IDMACRdReqWait           (0x4U<<13)
#define SDXC_IDMACWrReqWait           (0x5U<<13)
#define SDXC_IDMACRd                  (0x6U<<13)
#define SDXC_IDMACWr                  (0x7U<<13)
#define SDXC_IDMACDESCClose           (0x8U<<13)

#define SDXC_IDMA_OVER       (SDXC_IDMACTransmitInt|SDXC_IDMACReceiveInt|SDXC_IDMACNormalIntSum)
#define SDXC_IDMA_ERR        (SDXC_IDMACFatalBusErr|SDXC_IDMACDesInvalid|SDXC_IDMACCardErrSum|SDXC_IDMACAbnormalIntSum)

/*
 * IDMA描述符中支持最大的buffer长度为8192，如果单次传输>8192需要将buffer指定到不同的描述符中
 * 这里支持最大1024个描述符，即支持单次传输的最大长度为1024*8192 = 8M数据，在传输中一次性分配
 * 描述符空间，避免多次分配使效率低下，描述符共占用1024*16 = 16K空间，传输完毕后释放
 */
#define SDXC_DES_BUFFER_MAX_LEN       (1 << SUNXI_MMC_MAX_DMA_DES_BIT) //16bits in aw1625, 13bit in aw1623
#define SDXC_DES_NUM_SHIFT            (SUNXI_MMC_MAX_DMA_DES_BIT)  //65536 == 1<<16; change to 16bits, 13bit used in aw1623
#define SDXC_MAX_DES_NUM              (1024)
#define SDXC_DES_MODE                 0 //0-chain mode, 1-fix length skip

struct sunxi_mmc_idma_des{
    u32                     :1,
            dic             :1,     //disable interrupt on completion
            last_des        :1,     //1-this data buffer is the last buffer
            first_des       :1,     //1-data buffer is the first buffer, 0-data buffer contained in the next descriptor is the first data buffer
            des_chain       :1,     //1-the 2nd address in the descriptor is the next descriptor address
            end_of_ring     :1,     //1-last descriptor flag when using dual data buffer in descriptor
                            :24,
            card_err_sum    :1,     //transfer error flag
            own             :1;     //des owner:1-idma owns it, 0-host owns it

    u32     data_buf1_sz    :SUNXI_MMC_MAX_DMA_DES_BIT,    //change to 16bits, 13bit used in aw1623
            data_buf2_sz    :SUNXI_MMC_MAX_DMA_DES_BIT,    //change to 16bits, 13bit used in aw1623
                            :SUNXI_MMC_DMA_DES_BIT_LEFT;
    u32     buf_addr_ptr1;
    u32     buf_addr_ptr2;
};

struct sunxi_mmc_host;

s32 sdxc_init(struct sunxi_mmc_host* smc_host);
s32 sdxc_exit(struct sunxi_mmc_host* smc_host);
s32 sdxc_reset(struct sunxi_mmc_host* smc_host);
void sdxc_int_enable(struct sunxi_mmc_host* smc_host);
void sdxc_int_disable(struct sunxi_mmc_host* smc_host);
s32 sdxc_program_clk(struct sunxi_mmc_host* smc_host);
s32 sdxc_update_clk(struct sunxi_mmc_host* smc_host, u32 sclk, u32 cclk);
void sdxc_request(struct sunxi_mmc_host* smc_host, struct mmc_request* request);
void sdxc_check_status(struct sunxi_mmc_host* smc_host);
s32 sdxc_request_done(struct sunxi_mmc_host* smc_host);
void sdxc_set_buswidth(struct sunxi_mmc_host* smc_host, u32 width);
void sdxc_sel_ddr_mode(struct sunxi_mmc_host* smc_host);
void sdxc_sel_sdr_mode(struct sunxi_mmc_host* smc_host);
u32 sdxc_check_card_busy(struct sunxi_mmc_host* smc_host);
void sdxc_enable_sdio_irq(struct sunxi_mmc_host* smc_host, u32 enable);
void sdxc_regs_save(struct sunxi_mmc_host* smc_host);
void sdxc_regs_restore(struct sunxi_mmc_host* smc_host);
int sdxc_check_r1_ready(struct sunxi_mmc_host* smc_host);
void sdxc_do_pio_read(struct sunxi_mmc_host* smc_host);
void sdxc_do_pio_write(struct sunxi_mmc_host *smc_host);

#endif
