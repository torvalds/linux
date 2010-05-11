/*
 * arch/arm/mach-rk2818/include/mach/dma.h
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

#ifndef __ASM_ARCH_RK2818_DMA_H
#define __ASM_ARCH_RK2818_DMA_H

#include <asm/mach/dma.h>
#include <asm/dma.h>



/******dam registers*******/
//cfg low word
#define         B_CFGL_CH_PRIOR(P)       ((P)<<5)//pri = 0~2
#define         B_CFGL_CH_SUSP           (1<<8)
#define         B_CFGL_FIFO_EMPTY        (1<<9)
#define         B_CFGL_H_SEL_DST         (0<<10)
#define         B_CFGL_S_SEL_DST         (1<<10)
#define         B_CFGL_H_SEL_SRC         (0<<11)
#define         B_CFGL_S_SEL_SRC         (1<<11)
#define         B_CFGL_LOCK_CH_L_OTF     (0<<12)
#define         B_CFGL_LOCK_CH_L_OBT     (1<<12)
#define         B_CFGL_LOCK_CH_L_OTN     (2<<12)
#define         B_CFGL_LOCK_B_L_OTF      (0<<14)
#define         B_CFGL_LOCK_B_L_OBT      (1<<14)
#define         B_CFGL_LOCK_B_L_OTN      (2<<14)
#define         B_CFGL_LOCK_CH_EN        (0<<16)
#define         B_CFGL_LOCK_B_EN         (0<<17)
#define         B_CFGL_DST_HS_POL_H      (0<<18)
#define         B_CFGL_DST_HS_POL_L      (1<<18)
#define         B_CFGL_SRC_HS_POL_H      (0<<19)
#define         B_CFGL_SRC_HS_POL_L      (1<<19)
#define         B_CFGL_RELOAD_SRC        (1<<30)
#define         B_CFGL_RELOAD_DST        (1<<31)
//cfg high word
#define         B_CFGH_FCMODE            (1<<0)
#define         B_CFGH_FIFO_MODE         (1<<1)
#define         B_CFGH_PROTCTL           (1<<2)
#define         B_CFGH_DS_UPD_EN         (1<<5)
#define         B_CFGH_SS_UPD_EN         (1<<6)
#define         B_CFGH_SRC_PER(HS)       ((HS)<<7)
#define         B_CFGH_DST_PER(HS)       ((HS)<<11)
    
//ctl low word
#define         B_CTLL_INT_EN            (1<<0)
#define         B_CTLL_DST_TR_WIDTH_8    (0<<1)
#define         B_CTLL_DST_TR_WIDTH_16   (1<<1)
#define         B_CTLL_DST_TR_WIDTH_32   (2<<1)
#define         B_CTLL_DST_TR_WIDTH(W)   ((W)<<1)
#define         B_CTLL_SRC_TR_WIDTH_8    (0<<4)
#define         B_CTLL_SRC_TR_WIDTH_16   (1<<4)
#define         B_CTLL_SRC_TR_WIDTH_32   (2<<4)
#define         B_CTLL_SRC_TR_WIDTH(W)   ((W)<<4)
#define         B_CTLL_DINC_INC          (0<<7)
#define         B_CTLL_DINC_DEC          (1<<7)
#define         B_CTLL_DINC_UNC          (2<<7)
#define         B_CTLL_DINC(W)           ((W)<<7)
#define         B_CTLL_SINC_INC          (0<<9)
#define         B_CTLL_SINC_DEC          (1<<9)
#define         B_CTLL_SINC_UNC          (2<<9)
#define         B_CTLL_SINC(W)           ((W)<<9)
#define         B_CTLL_DST_MSIZE_1       (0<<11)
#define         B_CTLL_DST_MSIZE_4       (1<<11)
#define         B_CTLL_DST_MSIZE_8       (2<<11)
#define         B_CTLL_DST_MSIZE_16      (3<<11)
#define         B_CTLL_DST_MSIZE_32      (4<<11)
#define         B_CTLL_SRC_MSIZE_1       (0<<14)
#define         B_CTLL_SRC_MSIZE_4       (1<<14)
#define         B_CTLL_SRC_MSIZE_8       (2<<14)
#define         B_CTLL_SRC_MSIZE_16      (3<<14)
#define         B_CTLL_SRC_MSIZE_32      (4<<14)
#define         B_CTLL_SRC_GATHER        (1<<17)
#define         B_CTLL_DST_SCATTER       (1<<18)
#define         B_CTLL_MEM2MEM_DMAC      (0<<20)
#define         B_CTLL_MEM2PER_DMAC      (1<<20)
#define         B_CTLL_PER2MEM_DMAC      (2<<20)
#define         B_CTLL_PER2MEM_PER       (4<<20)
#define         B_CTLL_DMS_EXP           (0<<23)
#define         B_CTLL_DMS_ARMD          (1<<23)
#define         B_CTLL_SMS_EXP           (0<<25)
#define         B_CTLL_SMS_ARMD          (1<<25)
#define         B_CTLL_LLP_DST_EN        (1<<27)
#define         B_CTLL_LLP_SRC_EN        (1<<28)

#define         DWDMA_SAR(chn)      0x00+0x58*(chn)
#define         DWDMA_DAR(chn)      0x08+0x58*(chn)
#define         DWDMA_LLP(chn)      0x10+0x58*(chn)
#define         DWDMA_CTLL(chn)     0x18+0x58*(chn)
#define         DWDMA_CTLH(chn)     0x1c+0x58*(chn)
#define         DWDMA_SSTAT(chn)    0x20+0x58*(chn)
#define         DWDMA_DSTAT(chn)    0x28+0x58*(chn)
#define         DWDMA_SSTATAR(chn)  0x30+0x58*(chn)
#define         DWDMA_DSTATAR(chn)  0x38+0x58*(chn)
#define         DWDMA_CFGL(chn)     0x40+0x58*(chn)
#define         DWDMA_CFGH(chn)     0x44+0x58*(chn)
#define         DWDMA_SGR(chn)      0x48+0x58*(chn)
#define         DWDMA_DSR(chn)      0x50+0x58*(chn)

#define         DWDMA_RawTfr        0x2c0
#define         DWDMA_RawBlock      0x2c8
#define         DWDMA_RawSrcTran    0x2d0
#define         DWDMA_RawDstTran    0x2d8
#define         DWDMA_RawErr        0x2e0
#define         DWDMA_StatusTfr     0x2e8
#define         DWDMA_StatusBlock   0x2f0
#define         DWDMA_StatusSrcTran 0x2f8
#define         DWDMA_StatusDstTran 0x300
#define         DWDMA_StatusErr     0x308
#define         DWDMA_MaskTfr       0x310
#define         DWDMA_MaskBlock     0x318
#define         DWDMA_MaskSrcTran   0x320
#define         DWDMA_MaskDstTran   0x328
#define         DWDMA_MaskErr       0x330
#define         DWDMA_ClearTfr      0x338
#define         DWDMA_ClearBlock    0x340
#define         DWDMA_ClearSrcTran  0x348
#define         DWDMA_ClearDstTran  0x350
#define         DWDMA_ClearErr      0x358
#define         DWDMA_StatusInt     0x360
#define         DWDMA_ReqSrcReg     0x368
#define         DWDMA_ReqDstReg     0x370
#define         DWDMA_SglReqSrcReg  0x378
#define         DWDMA_SglReqDstReg  0x380
#define         DWDMA_LstSrcReg     0x388
#define         DWDMA_LstDstReg     0x390
#define         DWDMA_DmaCfgReg     0x398
#define         DWDMA_ChEnReg       0x3a0
#define         DWDMA_DmaIdReg      0x3a8
#define         DWDMA_DmaTestReg    0x3b0
/**************************/

#define write_dma_reg(addr, val)        __raw_writel(val, addr+RK2818_DWDMA_BASE) 
#define read_dma_reg(addr)              __raw_readl(addr+RK2818_DWDMA_BASE)    
#define mask_dma_reg(addr, msk, val)    write_dma_reg(addr, (val)|((~(msk))&read_dma_reg(addr)))

   /* clear interrupt */
#define CLR_DWDMA_INTR(dma_ch)            write_dma_reg(DWDMA_ClearTfr, 0x101<<dma_ch)

   /* Unmask interrupt */
#define UN_MASK_DWDMA_ALL_TRF_INTR        write_dma_reg(DWDMA_MaskTfr, 0x3f3f)//mask_dma_reg(DWDMA_MaskTfr, 0x101<<dma_ch, 0x101<<dma_ch)

   /* Mask interrupt */
#define MASK_DWDMA_ALL_TRF_INTR           write_dma_reg(DWDMA_MaskTfr, 0x3f00)//mask_dma_reg(DWDMA_MaskTfr, 0x101<<dma_ch, 0x100<<dma_ch)

   /* Enable channel */
#define ENABLE_DWDMA(dma_ch)              mask_dma_reg(DWDMA_ChEnReg, 0x101<<dma_ch, 0x101<<dma_ch)

   /* Disable channel */
#define DISABLE_DWDMA(dma_ch)             mask_dma_reg(DWDMA_ChEnReg, 0x101<<dma_ch, 0x100<<dma_ch)

   /* Disable channel */
#define GET_DWDMA_STATUS(dma_ch)          read_dma_reg(DWDMA_ChEnReg) & (0x001<<dma_ch)

/**************************/

#define RK28_DMA_IRQ_NUM   0
#define RK28_MAX_DMA_LLPS      64 /*max dma sg count*/

#define RK28_DMA_CH0A1_MAX_LEN      4095U
#define RK28_DMA_CH2_MAX_LEN        2047U



struct rk28_dma_llp;
typedef struct rk28_dma_llp llp_t;

struct rk28_dma_llp {
    unsigned int      sar;
    unsigned int      dar;
    llp_t   *llp;
    unsigned int      ctll;
    unsigned int      size; 
	unsigned int	  sstat;
	unsigned int	  dstat;
};

struct rk28_dma_dev {
    unsigned int      hd_if_r;   /* hardware interface for reading */
    unsigned int      hd_if_w;   /* hardware interface for writing */
    unsigned int      dev_addr_r;   /* device basic addresss for reading */
    unsigned int      dev_addr_w;   /* device basic addresss for reading */
    unsigned int      fifo_width;  /* fifo width of device */
};

struct rk2818_dma {
    dma_t  dma_t;
	struct rk28_dma_dev *dev_info;/* basic address of sg in memory */
    struct rk28_dma_llp *dma_llp_vir;  /* virtual cpu addrress of linked list */
    unsigned int dma_llp_phy;                   /* physical bus address of linked list */
	unsigned int length;     /* current transfer block */ 
	unsigned int residue;     /* residue block of current dma transfer */
	spinlock_t		lock;
};

//#define test_dma

/*devicd id list*/
#define RK28_DMA_SD_MMC        0
#define RK28_DMA_URAT2         1
#define RK28_DMA_URAT3         2
#define RK28_DMA_SDIO          3
#define RK28_DMA_I2S           4
#define RK28_DMA_SPI_M         5
#define RK28_DMA_SPI_S         6
#define RK28_DMA_URAT0         7
#define RK28_DMA_URAT1         8

#ifdef test_dma
#define RK28_DMA_SDRAM         9
#define RK28_DMA_DEV_NUM_MAX   10 /*max number of device that support dwdma*/

#define BUF_READ_ARRR          0x66000000         
#define BUF_WRITE_ARRR         0x66000000
#else
#define RK28_DMA_DEV_NUM_MAX   9 /*max number of device that support dwdma*/
#endif


/*device hardware interface to dwdma*/
#define RK28_DMA_SD_MMC0       0
#define RK28_DMA_URAT2_TXD     1
#define RK28_DMA_URAT2_RXD     2
#define RK28_DMA_URAT3_TXD     3
#define RK28_DMA_URAT3_RXD     4
#define RK28_DMA_SD_MMC1       5
#define RK28_DMA_I2S_TXD       6
#define RK28_DMA_I2S_RXD       7
#define RK28_DMA_SPI_M_TXD     8
#define RK28_DMA_SPI_M_RXD     9
#define RK28_DMA_SPI_S_TXD     10
#define RK28_DMA_SPI_S_RXD     11
#define RK28_DMA_URAT0_TXD     12
#define RK28_DMA_URAT0_RXD     13
#define RK28_DMA_URAT1_TXD     14
#define RK28_DMA_URAT1_RXD     15





#endif	/* _ASM_ARCH_RK28_DMA_H */
