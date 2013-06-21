/*
 * arch/arm/mach-sun7i/include/mach/spi.h
 * Copyright (C) 2012 - 2016 Reuuimlla Limited
 * Pan Nan <pannan@reuuimllatech.com>
 * James Deng <csjamesdeng@reuuimllatech.com>
 *
 * SUN7I SPI Register Definition
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#ifndef _SUN7I_SPI_H_
#define _SUN7I_SPI_H_

#define SPI_MODULE_NUM      (4)
#define SPI_FIFO_DEPTH      (64)

#define SPI0_BASE_ADDR      (0x01C05000)
#define SPI1_BASE_ADDR      (0x01C06000)
#define SPI2_BASE_ADDR      (0x01C17000)
#define SPI3_BASE_ADDR      (0x01C1f000)

/* SPI Registers offsets from peripheral base address */
#define SPI_RXDATA_REG          (0x00) /* rx data register */
#define SPI_TXDATA_REG          (0x04) /* tx data register */
#define SPI_CTL_REG             (0x08) /* control register */
#define SPI_INT_CTL_REG         (0x0C) /* interrupt control register */
#define SPI_STATUS_REG          (0x10) /* status register */
#define SPI_DMA_CTL_REG         (0x14) /* dma control register */
#define SPI_WAIT_REG            (0x18) /* wait clock counter register */
#define SPI_CLK_RATE_REG        (0x1C) /* clock rate control register */
#define SPI_BC_REG              (0x20) /* burst counter register   */
#define SPI_TC_REG              (0x24) /* transmit counter register */
#define SPI_FIFO_STA_REG        (0x28) /* fifo status register */

/* SPI Rx data register,default value: 0x0000_0000 */
/* readonly
 * 8-bits: accessed in   byte    rxFIFO decreased by 1.
 *         accessed in half-word rxFIFO decreased by 2.
 *         accessed in   word    rxFIFO decreased by 4.
 */
/* SPI Tx data register,default value: 0x0000_0000 */
/* write only
 * same as Rx data register
 */

/* SPI Control Register Bit Fields & Masks,defualt value:0x0002_001C */
#define SPI_CTL_EN          (0x1 << 0)  /* SPI module enable control 1:enable;0:disable;default:0 */
#define SPI_CTL_FUNC_MODE   (0x1 << 1)  /* SPI function mode select 1:master;0:slave;default:0 */
/* default work mode3: pol = 1,pha = 1; */
#define SPI_CTL_PHA         (0x1 << 2)  /* SPI Clock polarity control,  0: phase0,1: phase1;default:1  */
#define SPI_CTL_POL         (0x1 << 3)  /* SPI Clock/Data phase control,0:low level idle,1:high level idle;default:1 */
#define SPI_POL_PHA_BIT_POS (2)

#define SPI_CTL_SSPOL       (0x1 << 4)  /* SPI Chip select signal polarity control,default: 1,low effective like this:~~|_____~~ */
#define SPI_CTL_DMAMOD      (0x1 << 5)  /* SPI dma mode select: 0-NDMA,1-DDMA */
#define SPI_CTL_LMTF        (0x1 << 6)  /* LSB/MSB transfer first select 0:MSB,1:LSB,default 0:MSB first */
#define SPI_CTL_SSCTL       (0x1 << 7)  /* SPI chip select control,default 0:SPI_SSx remains asserted between SPI bursts,1:negate SPI_SSx between SPI bursts */
#define SPI_CTL_RST_TXFIFO  (0x1 << 8)  /* SPI reset rxFIFO write 1 automatic clear 0*/
#define SPI_CTL_RST_RXFIFO  (0x1 << 9)  /* SPI reset txFIFO write 1 automatic clear 0*/
#define SPI_CTL_XCH         (0x1 << 10) /* Exchange burst default 0:idle,1:start exchange;when BC is zero,this bit cleared by SPI controller*/
#define SPI_CTL_RAPIDS      (0x1 << 11) /* Rapids transfer mode */ // modified by yemao, for aw1623, define as below, 2011-5-27 14:02:58

#define SPI_CTL_SS_MASK     (0x3 << 12) /* SPI chip select:00-SPI_SS0;01-SPI_SS1;10-SPI_SS2;11-SPI_SS3*/
#define SPI_SS_BIT_POS      (12)

#define SPI_CTL_DDB         (0x1 << 14) /* Dummy burst Type,default 0: dummy spi burst is zero;1:dummy spi burst is one */
#define SPI_CTL_DHB         (0x1 << 15) /* Discard Hash Burst,default 0:receiving all spi burst in BC period 1:discard unused,fectch WTC bursts */

#define SPI_CTL_SS_CTRL     (0x1 << 16) /* SS output mode select default is 0:automatic output SS;1:manual output SS */
#define SPI_CTL_SS_LEVEL    (0x1 << 17) /* defautl is 1:set SS to high;0:set SS to low */

#define SPI_CTL_T_PAUSE_EN  (0x1 << 18) /* Transmit Pause Enable;Master mode: 1-stop when RXFIFO full;0-ignore rxFIFO */
#define SPI_CTL_MASTER_SDC  (0x1 << 19) /* master sample data control, 1: delay--high speed operation;0:no delay. */
#define SPI_CTL_MASTER_SDM  (0x1 << 20) /* master sample data mode, 1: normal sample mode; 0:delay sample mode */
/* aw1620 control register 31-20bit reserved bit */

/* SPI Interrupt Register Bit Fields & Masks,default value:0x0000_0000 */
#define SPI_INTEN_RR        (0x1 << 0)  /* rxFIFO Ready Interrupt Enable,---used for immediately received,0:disable;1:enable */
#define SPI_INTEN_RH        (0x1 << 1)  /* rxFIFO Half Full Interrupt Enable ---used for IRQ received */
#define SPI_INTEN_RF        (0x1 << 2)  /* rxFIFO Full Interrupt Enable ---seldom used */
#define SPI_INTEN_QTR_RF    (0x1 << 3)  /* rxFIFO 1/4 Full Interrupt Enable */
#define SPI_INTEN_3QTR_RF   (0x1 << 4)  /* rxFIFO 3/4 Full Interrupt Enable */
#define SPI_INTEN_RO        (0x1 << 5)  /* rxFIFO Overflow Interrupt Enable ---used for error detect */
#define SPI_INTEN_RU        (0x1 << 6)  /* rxFIFO Underrun Interrupt Enable ---used for error detect */
/* 7 bit reserved */

#define SPI_INTEN_TE        (0x1 << 8)  /* txFIFO Empty Interrupt Enable ---seldom used */
#define SPI_INTEN_TH        (0x1 << 9)  /* txFIFO Half Empty Interrupt Enable ---used  for IRQ tx */
#define SPI_INTEN_TF        (0x1 << 10) /* txFIFO Full Interrupt Enable ---seldom used */
#define SPI_INTEN_QTR_TE    (0x1 << 11) /* txFIFO FIFO 1/4 Empty Interrupt Enable;0-disable;1-enable */
#define SPI_INTEN_3QTR_TE   (0x1 << 12) /* txFIFO FIFO 3/4 Empty Interrupt Enable;0-disable;1-enable */
#define SPI_INTEN_TO        (0x1 << 13) /* txFIFO Overflow Interrupt Enable ---used for error detect */
#define SPI_INTEN_TU        (0x1 << 14) /* txFIFO Underrun Interrupt Enable ---not happened */
/* 15 bit reserved */

#define SPI_INTEN_TC        (0x1 << 16) /* Transfer Completed Interrupt Enable  ---used */
#define SPI_INTEN_SSI       (0x1 << 17) /* SSI interrupt Enable,chip select from valid state to invalid state,for slave used only */
/* 31:18 bit reserved */
#define SPI_INTEN_ERR       (SPI_INTEN_TO|SPI_INTEN_RU|SPI_INTEN_RO) //NO txFIFO underrun
#define SPI_INTEN_MASK      (0x7f|(0x7f<<8)|(0x3<<16))

/* SPI Status Register Bit Fields & Masks,default value:0x0000_1B00 all bits are written 1 to clear 0 */
#define SPI_STAT_RR         (0x1 << 0)  /* rxFIFO ready, 0:no valid data;1:more than 1 word in rxfifo */
#define SPI_STAT_RHF        (0x1 << 1)  /* rxFIFO half full,0:less than 4 words;1:four or more than 4 words in rxfifo */
#define SPI_STAT_RF         (0x1 << 2)  /* rxFIFO full,0:not full;1:full */
#define SPI_STAT_QTR_RF     (0x1 << 3)  /* rxFIFO 1/4 Full, 0:not 1/4 full;1:1/4 full */
#define SPI_STAT_3QTR_RF    (0x1 << 4)  /* rxFIFO 3/4 Full, 0:not 1/4 full;1:1/4 full */
#define SPI_STAT_RO         (0x1 << 5)  /* rxFIFO overflow, 0: rxfifo is available;1:rxfifo has overflowed! */
#define SPI_STAT_RU         (0x1 << 6)  /* rxFIFO underrun,fectch data with no data available in FIFO */
/* 7bit reserved */

#define SPI_STAT_TE         (0x1 << 8)  /* txFIFO empty,0:txfifo contains one or more words;1:txfifo is empty.default value:1 */
#define SPI_STAT_THE        (0x1 << 9)  /* txFIFO half empty,0:more than half words;1: half or fewer words.defualt value: 1 */
#define SPI_STAT_TF         (0x1 << 10) /* txFIFO Full */
#define SPI_STAT_QTR_TE     (0x1 << 11) /* txFIFO 1/4 empty.default is 1:more than 1/4 empty */
#define SPI_STAT_3QTR_TE    (0x1 << 12) /* txFIFO 3/4 empty.default is 1:more than 3/4 empty */
#define SPI_STAT_TO         (0x1 << 13) /* txFIFO overflow 0:not overflow;1:overflow */
#define SPI_STAT_TU         (0x1 << 14) /* txFIFO underrun 0:not underrun;1:undrrun */
/* 15bit reserved */

#define SPI_STAT_TC         (0x1 << 16) /* Transfer Complete, 0:BUSY;1:transfer completed */
#define SPI_STAT_SSI        (0x1 << 17) /* SS Invalid Interrupt ,for slave used only */
/* 31-18bits reserved */
#define SPI_STAT_MASK       (0x7f|(0x7f<<8)|(0x3<<16))

#define SPI_STAT_ERR        (SPI_STAT_TO|SPI_STAT_RU|SPI_STAT_RO) //Slave mode,no SPI_STAT_TU

/* SPI DMA Control Register Bit Fields & Masks defuatl:0x0000_0000 */
#define SPI_DRQEN_RR        (0x1 << 0)  /* rxFIFO Ready DMA Request Enable,when one or more than one words in RXFIFO */
#define SPI_DRQEN_RHF       (0x1 << 1)  /* rXFIFO Half Full DMA Request Enable,when 4 or more than 4 words in RXFIFO */
#define SPI_DRQEN_RF        (0x1 << 2)  /* rxFIFO Full DMA Request Enable */
#define SPI_DRQEN_QTR_RF    (0x1 << 3)  /* rxFIFO 1/4 Full DMA Request Enable */
#define SPI_DRQEN_3QTR_RF   (0x1 << 4)  /* rxFIFO 3/4 Full DMA Request Enable */
/* 7:5 bit reserved */

#define SPI_DRQEN_TE        (0x1 << 8)  /* txFIFO Empty DMA Request Enable,when no words in TXFIFO */
#define SPI_DRQEN_THE       (0x1 << 9)  /* txFIFO Half Empty DMA Request Enable,when 4 or less than 4 words in TXFIFO */
#define SPI_DRQEN_TNF       (0x1 << 10) /* txFIFO Not Full DMA Request Enable,asserted when more than one free room for burst */
#define SPI_DRQEN_QTR_TE    (0x1 << 11) /* txFIFO 1/4 Empty DMA Request Enable */
#define SPI_DRQEN_3QTR_TE   (0x1 << 12) /* txFIFO 3/4 Empty DMA Request Enable */
/* 31:13 bits reserved */
#define SPI_DRQEN_MASK      (0x1f|(0x1f<<8))


/* SPI Wait Clock Register Bit Fields & Masks,default value:0x0000_0000 */
#define SPI_WAIT_CLK_MASK   (0xFFFF << 0)   /* used only in master mode: Wait Between Transactions */
/* 31:16bit reserved */


/* SPI Wait Clock Register Bit Fields & Masks,default:0x0000_0002 */
#define SPI_CLKCTL_CDR2     (0xFF << 0)  /* Clock Divide Rate 2,master mode only : SPI_CLK = AHB_CLK/(2*(n+1)) */
#define SPI_CLKCTL_CDR1     (0xF  << 8)  /* Clock Divide Rate 1,master mode only : SPI_CLK = AHB_CLK/2^(n+1) */
#define SPI_CLKCTL_DRS      (0x1  << 12) /* Divide rate select,default,0:rate 1;1:rate 2 */
#define SPI_CLK_SCOPE       (SPI_CLKCTL_CDR2+1)
/* 31:13bits reserved */

/* SPI Burst Counter Register Bit Fields & Masks,default value: 0x0000_0000 */
/* master mode: when SMC = 1,BC specifies total burst number, Max length is 16Mbytes */
#define SPI_BC_BC_MASK      (0xFFFFFF << 0) /* Total Burst Counter,tx length + rx length ,SMC=1 */
#define SPI_TRANSFER_SIZE   (SPI_BC_BC_MASK)

/* SPI Transmit Counter reigster default:0x0000_0000,Max length is 16Mbytes */
#define SPI_TC_WTC_MASK     (0xFFFFFF << 0) /* Write Transmit Counter,tx length, NOT rx length!!! */


/* SPI FIFO status register default is 0x0000_0000 */
#define SPI_FIFO_RXCNT      (0x7F << 0)     /* rxFIFO counter,how many bytes in the rxFIFO */
#define SPI_RXCNT_BIT_POS   (0)
/* 15:7bits reserved */

#define SPI_FIFO_TXCNT      (0x7F << 16)    /* txFIFO counter,how many bytes in the txFIFO */
#define SPI_TXCNT_BIT_POS   (16)


/* 设置config的bit位
 * 必须跟linux的spi参数设置一致
 * 工作模式，包括4种：
 * 0: 工作模式0，POL=0,PAL=0;
 * 1: 工作模式1，POL=0,PAL=1;
 * 2: 工作模式2，POL=1,PAL=0;
 * 3: 工作模式3，POL=1,PAL=1;
 */
#define SPI_PHA_ACTIVE_         (0x01)
#define SPI_POL_ACTIVE_         (0x02)

#define SPI_MODE_0_ACTIVE_      (0|0)
#define SPI_MODE_1_ACTIVE_      (0|SPI_PHA_ACTIVE_)
#define SPI_MODE_2_ACTIVE_      (SPI_POL_ACTIVE_|0)
#define SPI_MODE_3_ACTIVE_      (SPI_POL_ACTIVE_|SPI_PHA_ACTIVE_)   /*默认为模式3*/
/* 下面属性少用 */
#define SPI_CS_HIGH_ACTIVE_     (0x04)  /*默认为片选低电平有效，即低电平选中片选*/
#define SPI_LSB_FIRST_ACTIVE_   (0x08)  /*默认为先发送MSB，即先发送最低位*/

#define SPI_DUMMY_ONE_ACTIVE_   (0x10)  /*默认为接收时spi控制器自动填充0放在txFIFO */
#define SPI_RECEIVE_ALL_ACTIVE_ (0x20)  /*默认为放弃无用的burst，即发送的时候放弃rxFIFO接收到数据 */

/* can modify to adapt the application */
#define BULK_DATA_BOUNDARY      64

/* spi controller just suppport 20Mhz */
#define SPI_MAX_FREQUENCY       80000000

/* distinguish sdram and sram address */
#define SPI_RAM_BOUNDAY         (0x80000000)

/* function mode select */
#define SPI_MASTER_MODE         (0x1)
#define SPI_SLAVE_MODE          (0x0)

struct sun7i_spi_platform_data {
    int cs_bitmap;          // cs0-0x1,cs1-0x2,cs0&cs1-0x3
    int num_cs;             // number of cs
    const char *clk_name;   // ahb clk name
};

/* spi device controller state, alloc */
struct sun7i_spi_config {
    int bits_per_word;      // 8bit
    int max_speed_hz;       // 80MHz
    int mode;               // pha,pol,LSB,etc..
};

/* spi device data, used in dual spi mode */
struct sun7i_dual_mode_dev_data {
    int dual_mode;          // dual SPI mode, 0-single mode, 1-dual mode
    int single_cnt;         // single mode transmit counter
    int dummy_cnt;          // dummy counter should be sent before receive in dual mode
};

#endif
