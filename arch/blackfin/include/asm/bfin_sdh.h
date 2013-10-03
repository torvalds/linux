/*
 * Blackfin Secure Digital Host (SDH) definitions
 *
 * Copyright 2008-2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __BFIN_SDH_H__
#define __BFIN_SDH_H__

/* Platform resources */
struct bfin_sd_host {
	int dma_chan;
	int irq_int0;
	int irq_int1;
	u16 pin_req[7];
};

/* SDH_COMMAND bitmasks */
#define CMD_IDX            0x3f        /* Command Index */
#define CMD_RSP            (1 << 6)    /* Response */
#define CMD_L_RSP          (1 << 7)    /* Long Response */
#define CMD_INT_E          (1 << 8)    /* Command Interrupt */
#define CMD_PEND_E         (1 << 9)    /* Command Pending */
#define CMD_E              (1 << 10)   /* Command Enable */
#ifdef RSI_BLKSZ
#define CMD_CRC_CHECK_D    (1 << 11)   /* CRC Check is disabled */
#define CMD_DATA0_BUSY     (1 << 12)   /* Check for Busy State on the DATA0 pin */
#endif

/* SDH_PWR_CTL bitmasks */
#ifndef RSI_BLKSZ
#define PWR_ON             0x3         /* Power On */
#define SD_CMD_OD          (1 << 6)    /* Open Drain Output */
#define ROD_CTL            (1 << 7)    /* Rod Control */
#endif

/* SDH_CLK_CTL bitmasks */
#define CLKDIV             0xff        /* MC_CLK Divisor */
#define CLK_E              (1 << 8)    /* MC_CLK Bus Clock Enable */
#define PWR_SV_E           (1 << 9)    /* Power Save Enable */
#define CLKDIV_BYPASS      (1 << 10)   /* Bypass Divisor */
#define BUS_MODE_MASK      0x1800      /* Bus Mode Mask */
#define STD_BUS_1          0x000       /* Standard Bus 1 bit mode */
#define WIDE_BUS_4         0x800       /* Wide Bus 4 bit mode */
#define BYTE_BUS_8         0x1000      /* Byte Bus 8 bit mode */

/* SDH_RESP_CMD bitmasks */
#define RESP_CMD           0x3f        /* Response Command */

/* SDH_DATA_CTL bitmasks */
#define DTX_E              (1 << 0)    /* Data Transfer Enable */
#define DTX_DIR            (1 << 1)    /* Data Transfer Direction */
#define DTX_MODE           (1 << 2)    /* Data Transfer Mode */
#define DTX_DMA_E          (1 << 3)    /* Data Transfer DMA Enable */
#ifndef RSI_BLKSZ
#define DTX_BLK_LGTH       (0xf << 4)  /* Data Transfer Block Length */
#else

/* Bit masks for SDH_BLK_SIZE */
#define DTX_BLK_LGTH       0x1fff      /* Data Transfer Block Length */
#endif

/* SDH_STATUS bitmasks */
#define CMD_CRC_FAIL       (1 << 0)    /* CMD CRC Fail */
#define DAT_CRC_FAIL       (1 << 1)    /* Data CRC Fail */
#define CMD_TIME_OUT       (1 << 2)    /* CMD Time Out */
#define DAT_TIME_OUT       (1 << 3)    /* Data Time Out */
#define TX_UNDERRUN        (1 << 4)    /* Transmit Underrun */
#define RX_OVERRUN         (1 << 5)    /* Receive Overrun */
#define CMD_RESP_END       (1 << 6)    /* CMD Response End */
#define CMD_SENT           (1 << 7)    /* CMD Sent */
#define DAT_END            (1 << 8)    /* Data End */
#define START_BIT_ERR      (1 << 9)    /* Start Bit Error */
#define DAT_BLK_END        (1 << 10)   /* Data Block End */
#define CMD_ACT            (1 << 11)   /* CMD Active */
#define TX_ACT             (1 << 12)   /* Transmit Active */
#define RX_ACT             (1 << 13)   /* Receive Active */
#define TX_FIFO_STAT       (1 << 14)   /* Transmit FIFO Status */
#define RX_FIFO_STAT       (1 << 15)   /* Receive FIFO Status */
#define TX_FIFO_FULL       (1 << 16)   /* Transmit FIFO Full */
#define RX_FIFO_FULL       (1 << 17)   /* Receive FIFO Full */
#define TX_FIFO_ZERO       (1 << 18)   /* Transmit FIFO Empty */
#define RX_DAT_ZERO        (1 << 19)   /* Receive FIFO Empty */
#define TX_DAT_RDY         (1 << 20)   /* Transmit Data Available */
#define RX_FIFO_RDY        (1 << 21)   /* Receive Data Available */

/* SDH_STATUS_CLR bitmasks */
#define CMD_CRC_FAIL_STAT  (1 << 0)    /* CMD CRC Fail Status */
#define DAT_CRC_FAIL_STAT  (1 << 1)    /* Data CRC Fail Status */
#define CMD_TIMEOUT_STAT   (1 << 2)    /* CMD Time Out Status */
#define DAT_TIMEOUT_STAT   (1 << 3)    /* Data Time Out status */
#define TX_UNDERRUN_STAT   (1 << 4)    /* Transmit Underrun Status */
#define RX_OVERRUN_STAT    (1 << 5)    /* Receive Overrun Status */
#define CMD_RESP_END_STAT  (1 << 6)    /* CMD Response End Status */
#define CMD_SENT_STAT      (1 << 7)    /* CMD Sent Status */
#define DAT_END_STAT       (1 << 8)    /* Data End Status */
#define START_BIT_ERR_STAT (1 << 9)    /* Start Bit Error Status */
#define DAT_BLK_END_STAT   (1 << 10)   /* Data Block End Status */

/* SDH_MASK0 bitmasks */
#define CMD_CRC_FAIL_MASK  (1 << 0)    /* CMD CRC Fail Mask */
#define DAT_CRC_FAIL_MASK  (1 << 1)    /* Data CRC Fail Mask */
#define CMD_TIMEOUT_MASK   (1 << 2)    /* CMD Time Out Mask */
#define DAT_TIMEOUT_MASK   (1 << 3)    /* Data Time Out Mask */
#define TX_UNDERRUN_MASK   (1 << 4)    /* Transmit Underrun Mask */
#define RX_OVERRUN_MASK    (1 << 5)    /* Receive Overrun Mask */
#define CMD_RESP_END_MASK  (1 << 6)    /* CMD Response End Mask */
#define CMD_SENT_MASK      (1 << 7)    /* CMD Sent Mask */
#define DAT_END_MASK       (1 << 8)    /* Data End Mask */
#define START_BIT_ERR_MASK (1 << 9)    /* Start Bit Error Mask */
#define DAT_BLK_END_MASK   (1 << 10)   /* Data Block End Mask */
#define CMD_ACT_MASK       (1 << 11)   /* CMD Active Mask */
#define TX_ACT_MASK        (1 << 12)   /* Transmit Active Mask */
#define RX_ACT_MASK        (1 << 13)   /* Receive Active Mask */
#define TX_FIFO_STAT_MASK  (1 << 14)   /* Transmit FIFO Status Mask */
#define RX_FIFO_STAT_MASK  (1 << 15)   /* Receive FIFO Status Mask */
#define TX_FIFO_FULL_MASK  (1 << 16)   /* Transmit FIFO Full Mask */
#define RX_FIFO_FULL_MASK  (1 << 17)   /* Receive FIFO Full Mask */
#define TX_FIFO_ZERO_MASK  (1 << 18)   /* Transmit FIFO Empty Mask */
#define RX_DAT_ZERO_MASK   (1 << 19)   /* Receive FIFO Empty Mask */
#define TX_DAT_RDY_MASK    (1 << 20)   /* Transmit Data Available Mask */
#define RX_FIFO_RDY_MASK   (1 << 21)   /* Receive Data Available Mask */

/* SDH_FIFO_CNT bitmasks */
#define FIFO_COUNT         0x7fff      /* FIFO Count */

/* SDH_E_STATUS bitmasks */
#define SDIO_INT_DET       (1 << 1)    /* SDIO Int Detected */
#define SD_CARD_DET        (1 << 4)    /* SD Card Detect */
#define SD_CARD_BUSYMODE   (1 << 31)   /* Card is in Busy mode */
#define SD_CARD_SLPMODE    (1 << 30)   /* Card in Sleep Mode */
#define SD_CARD_READY      (1 << 17)   /* Card Ready */

/* SDH_E_MASK bitmasks */
#define SDIO_MSK           (1 << 1)    /* Mask SDIO Int Detected */
#define SCD_MSK            (1 << 4)    /* Mask Card Detect */
#define CARD_READY_MSK     (1 << 16)   /* Mask Card Ready */

/* SDH_CFG bitmasks */
#define CLKS_EN            (1 << 0)    /* Clocks Enable */
#define SD4E               (1 << 2)    /* SDIO 4-Bit Enable */
#define MWE                (1 << 3)    /* Moving Window Enable */
#define SD_RST             (1 << 4)    /* SDMMC Reset */
#define PUP_SDDAT          (1 << 5)    /* Pull-up SD_DAT */
#define PUP_SDDAT3         (1 << 6)    /* Pull-up SD_DAT3 */
#ifndef RSI_BLKSZ
#define PD_SDDAT3          (1 << 7)    /* Pull-down SD_DAT3 */
#else
#define PWR_ON             0x600       /* Power On */
#define SD_CMD_OD          (1 << 11)   /* Open Drain Output */
#define BOOT_EN            (1 << 12)   /* Boot Enable */
#define BOOT_MODE          (1 << 13)   /* Alternate Boot Mode */
#define BOOT_ACK_EN        (1 << 14)   /* Boot ACK is expected */
#endif

/* SDH_RD_WAIT_EN bitmasks */
#define RWR                (1 << 0)    /* Read Wait Request */

#endif
