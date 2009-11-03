/*
 * Copyright 2008-2009 Analog Devices Inc.
 *
 * Licensed under the ADI BSD license or the GPL-2 (or later)
 */

#ifndef _DEF_BF514_H
#define _DEF_BF514_H

/* Include all Core registers and bit definitions */
#include <asm/def_LPBlackfin.h>

/* SYSTEM & MMR ADDRESS DEFINITIONS FOR ADSP-BF514 */

/* Include defBF51x_base.h for the set of #defines that are common to all ADSP-BF51x processors */
#include "defBF51x_base.h"

/* The following are the #defines needed by ADSP-BF514 that are not in the common header */

/* SDH Registers */

#define SDH_PWR_CTL                    0xFFC03900 /* SDH Power Control */
#define SDH_CLK_CTL                    0xFFC03904 /* SDH Clock Control */
#define SDH_ARGUMENT                   0xFFC03908 /* SDH Argument */
#define SDH_COMMAND                    0xFFC0390C /* SDH Command */
#define SDH_RESP_CMD                   0xFFC03910 /* SDH Response Command */
#define SDH_RESPONSE0                  0xFFC03914 /* SDH Response0 */
#define SDH_RESPONSE1                  0xFFC03918 /* SDH Response1 */
#define SDH_RESPONSE2                  0xFFC0391C /* SDH Response2 */
#define SDH_RESPONSE3                  0xFFC03920 /* SDH Response3 */
#define SDH_DATA_TIMER                 0xFFC03924 /* SDH Data Timer */
#define SDH_DATA_LGTH                  0xFFC03928 /* SDH Data Length */
#define SDH_DATA_CTL                   0xFFC0392C /* SDH Data Control */
#define SDH_DATA_CNT                   0xFFC03930 /* SDH Data Counter */
#define SDH_STATUS                     0xFFC03934 /* SDH Status */
#define SDH_STATUS_CLR                 0xFFC03938 /* SDH Status Clear */
#define SDH_MASK0                      0xFFC0393C /* SDH Interrupt0 Mask */
#define SDH_MASK1                      0xFFC03940 /* SDH Interrupt1 Mask */
#define SDH_FIFO_CNT                   0xFFC03948 /* SDH FIFO Counter */
#define SDH_FIFO                       0xFFC03980 /* SDH Data FIFO */
#define SDH_E_STATUS                   0xFFC039C0 /* SDH Exception Status */
#define SDH_E_MASK                     0xFFC039C4 /* SDH Exception Mask */
#define SDH_CFG                        0xFFC039C8 /* SDH Configuration */
#define SDH_RD_WAIT_EN                 0xFFC039CC /* SDH Read Wait Enable */
#define SDH_PID0                       0xFFC039D0 /* SDH Peripheral Identification0 */
#define SDH_PID1                       0xFFC039D4 /* SDH Peripheral Identification1 */
#define SDH_PID2                       0xFFC039D8 /* SDH Peripheral Identification2 */
#define SDH_PID3                       0xFFC039DC /* SDH Peripheral Identification3 */
#define SDH_PID4                       0xFFC039E0 /* SDH Peripheral Identification4 */
#define SDH_PID5                       0xFFC039E4 /* SDH Peripheral Identification5 */
#define SDH_PID6                       0xFFC039E8 /* SDH Peripheral Identification6 */
#define SDH_PID7                       0xFFC039EC /* SDH Peripheral Identification7 */

/* Removable Storage Interface Registers */

#define RSI_PWR_CONTROL                0xFFC03800 /* RSI Power Control Register */
#define RSI_CLK_CONTROL                0xFFC03804 /* RSI Clock Control Register */
#define RSI_ARGUMENT                   0xFFC03808 /* RSI Argument Register */
#define RSI_COMMAND                    0xFFC0380C /* RSI Command Register */
#define RSI_RESP_CMD                   0xFFC03810 /* RSI Response Command Register */
#define RSI_RESPONSE0                  0xFFC03814 /* RSI Response Register */
#define RSI_RESPONSE1                  0xFFC03818 /* RSI Response Register */
#define RSI_RESPONSE2                  0xFFC0381C /* RSI Response Register */
#define RSI_RESPONSE3                  0xFFC03820 /* RSI Response Register */
#define RSI_DATA_TIMER                 0xFFC03824 /* RSI Data Timer Register */
#define RSI_DATA_LGTH                  0xFFC03828 /* RSI Data Length Register */
#define RSI_DATA_CONTROL               0xFFC0382C /* RSI Data Control Register */
#define RSI_DATA_CNT                   0xFFC03830 /* RSI Data Counter Register */
#define RSI_STATUS                     0xFFC03834 /* RSI Status Register */
#define RSI_STATUSCL                   0xFFC03838 /* RSI Status Clear Register */
#define RSI_MASK0                      0xFFC0383C /* RSI Interrupt 0 Mask Register */
#define RSI_MASK1                      0xFFC03840 /* RSI Interrupt 1 Mask Register */
#define RSI_FIFO_CNT                   0xFFC03848 /* RSI FIFO Counter Register */
#define RSI_CEATA_CONTROL              0xFFC0384C /* RSI CEATA Register */
#define RSI_FIFO                       0xFFC03880 /* RSI Data FIFO Register */
#define RSI_ESTAT                      0xFFC038C0 /* RSI Exception Status Register */
#define RSI_EMASK                      0xFFC038C4 /* RSI Exception Mask Register */
#define RSI_CONFIG                     0xFFC038C8 /* RSI Configuration Register */
#define RSI_RD_WAIT_EN                 0xFFC038CC /* RSI Read Wait Enable Register */
#define RSI_PID0                       0xFFC03FE0 /* RSI Peripheral ID Register 0 */
#define RSI_PID1                       0xFFC03FE4 /* RSI Peripheral ID Register 1 */
#define RSI_PID2                       0xFFC03FE8 /* RSI Peripheral ID Register 2 */
#define RSI_PID3                       0xFFC03FEC /* RSI Peripheral ID Register 3 */
#define RSI_PID4                       0xFFC03FF0 /* RSI Peripheral ID Register 4 */
#define RSI_PID5                       0xFFC03FF4 /* RSI Peripheral ID Register 5 */
#define RSI_PID6                       0xFFC03FF8 /* RSI Peripheral ID Register 6 */
#define RSI_PID7                       0xFFC03FFC /* RSI Peripheral ID Register 7 */

/* ********************************************************** */
/*     SINGLE BIT MACRO PAIRS (bit mask and negated one)      */
/*     and MULTI BIT READ MACROS                              */
/* ********************************************************** */

/* Bit masks for SDH_COMMAND */

#define                   CMD_IDX  0x3f       /* Command Index */
#define                   CMD_RSP  0x40       /* Response */
#define                 CMD_L_RSP  0x80       /* Long Response */
#define                 CMD_INT_E  0x100      /* Command Interrupt */
#define                CMD_PEND_E  0x200      /* Command Pending */
#define                     CMD_E  0x400      /* Command Enable */

/* Bit masks for SDH_PWR_CTL */

#define                    PWR_ON  0x3        /* Power On */
#if 0
#define                       TBD  0x3c       /* TBD */
#endif
#define                 SD_CMD_OD  0x40       /* Open Drain Output */
#define                   ROD_CTL  0x80       /* Rod Control */

/* Bit masks for SDH_CLK_CTL */

#define                    CLKDIV  0xff       /* MC_CLK Divisor */
#define                     CLK_E  0x100      /* MC_CLK Bus Clock Enable */
#define                  PWR_SV_E  0x200      /* Power Save Enable */
#define             CLKDIV_BYPASS  0x400      /* Bypass Divisor */
#define                  WIDE_BUS  0x800      /* Wide Bus Mode Enable */

/* Bit masks for SDH_RESP_CMD */

#define                  RESP_CMD  0x3f       /* Response Command */

/* Bit masks for SDH_DATA_CTL */

#define                     DTX_E  0x1        /* Data Transfer Enable */
#define                   DTX_DIR  0x2        /* Data Transfer Direction */
#define                  DTX_MODE  0x4        /* Data Transfer Mode */
#define                 DTX_DMA_E  0x8        /* Data Transfer DMA Enable */
#define              DTX_BLK_LGTH  0xf0       /* Data Transfer Block Length */

/* Bit masks for SDH_STATUS */

#define              CMD_CRC_FAIL  0x1        /* CMD CRC Fail */
#define              DAT_CRC_FAIL  0x2        /* Data CRC Fail */
#define               CMD_TIME_OUT  0x4        /* CMD Time Out */
#define               DAT_TIME_OUT  0x8        /* Data Time Out */
#define               TX_UNDERRUN  0x10       /* Transmit Underrun */
#define                RX_OVERRUN  0x20       /* Receive Overrun */
#define              CMD_RESP_END  0x40       /* CMD Response End */
#define                  CMD_SENT  0x80       /* CMD Sent */
#define                   DAT_END  0x100      /* Data End */
#define             START_BIT_ERR  0x200      /* Start Bit Error */
#define               DAT_BLK_END  0x400      /* Data Block End */
#define                   CMD_ACT  0x800      /* CMD Active */
#define                    TX_ACT  0x1000     /* Transmit Active */
#define                    RX_ACT  0x2000     /* Receive Active */
#define              TX_FIFO_STAT  0x4000     /* Transmit FIFO Status */
#define              RX_FIFO_STAT  0x8000     /* Receive FIFO Status */
#define              TX_FIFO_FULL  0x10000    /* Transmit FIFO Full */
#define              RX_FIFO_FULL  0x20000    /* Receive FIFO Full */
#define              TX_FIFO_ZERO  0x40000    /* Transmit FIFO Empty */
#define               RX_DAT_ZERO  0x80000    /* Receive FIFO Empty */
#define                TX_DAT_RDY  0x100000   /* Transmit Data Available */
#define               RX_FIFO_RDY  0x200000   /* Receive Data Available */

/* Bit masks for SDH_STATUS_CLR */

#define         CMD_CRC_FAIL_STAT  0x1        /* CMD CRC Fail Status */
#define         DAT_CRC_FAIL_STAT  0x2        /* Data CRC Fail Status */
#define          CMD_TIMEOUT_STAT  0x4        /* CMD Time Out Status */
#define          DAT_TIMEOUT_STAT  0x8        /* Data Time Out status */
#define          TX_UNDERRUN_STAT  0x10       /* Transmit Underrun Status */
#define           RX_OVERRUN_STAT  0x20       /* Receive Overrun Status */
#define         CMD_RESP_END_STAT  0x40       /* CMD Response End Status */
#define             CMD_SENT_STAT  0x80       /* CMD Sent Status */
#define              DAT_END_STAT  0x100      /* Data End Status */
#define        START_BIT_ERR_STAT  0x200      /* Start Bit Error Status */
#define          DAT_BLK_END_STAT  0x400      /* Data Block End Status */

/* Bit masks for SDH_MASK0 */

#define         CMD_CRC_FAIL_MASK  0x1        /* CMD CRC Fail Mask */
#define         DAT_CRC_FAIL_MASK  0x2        /* Data CRC Fail Mask */
#define          CMD_TIMEOUT_MASK  0x4        /* CMD Time Out Mask */
#define          DAT_TIMEOUT_MASK  0x8        /* Data Time Out Mask */
#define          TX_UNDERRUN_MASK  0x10       /* Transmit Underrun Mask */
#define           RX_OVERRUN_MASK  0x20       /* Receive Overrun Mask */
#define         CMD_RESP_END_MASK  0x40       /* CMD Response End Mask */
#define             CMD_SENT_MASK  0x80       /* CMD Sent Mask */
#define              DAT_END_MASK  0x100      /* Data End Mask */
#define        START_BIT_ERR_MASK  0x200      /* Start Bit Error Mask */
#define          DAT_BLK_END_MASK  0x400      /* Data Block End Mask */
#define              CMD_ACT_MASK  0x800      /* CMD Active Mask */
#define               TX_ACT_MASK  0x1000     /* Transmit Active Mask */
#define               RX_ACT_MASK  0x2000     /* Receive Active Mask */
#define         TX_FIFO_STAT_MASK  0x4000     /* Transmit FIFO Status Mask */
#define         RX_FIFO_STAT_MASK  0x8000     /* Receive FIFO Status Mask */
#define         TX_FIFO_FULL_MASK  0x10000    /* Transmit FIFO Full Mask */
#define         RX_FIFO_FULL_MASK  0x20000    /* Receive FIFO Full Mask */
#define         TX_FIFO_ZERO_MASK  0x40000    /* Transmit FIFO Empty Mask */
#define          RX_DAT_ZERO_MASK  0x80000    /* Receive FIFO Empty Mask */
#define           TX_DAT_RDY_MASK  0x100000   /* Transmit Data Available Mask */
#define          RX_FIFO_RDY_MASK  0x200000   /* Receive Data Available Mask */

/* Bit masks for SDH_FIFO_CNT */

#define                FIFO_COUNT  0x7fff     /* FIFO Count */

/* Bit masks for SDH_E_STATUS */

#define              SDIO_INT_DET  0x2        /* SDIO Int Detected */
#define               SD_CARD_DET  0x10       /* SD Card Detect */

/* Bit masks for SDH_E_MASK */

#define                  SDIO_MSK  0x2        /* Mask SDIO Int Detected */
#define                   SCD_MSK  0x40       /* Mask Card Detect */

/* Bit masks for SDH_CFG */

#define                   CLKS_EN  0x1        /* Clocks Enable */
#define                      SD4E  0x4        /* SDIO 4-Bit Enable */
#define                       MWE  0x8        /* Moving Window Enable */
#define                    SD_RST  0x10       /* SDMMC Reset */
#define                 PUP_SDDAT  0x20       /* Pull-up SD_DAT */
#define                PUP_SDDAT3  0x40       /* Pull-up SD_DAT3 */
#define                 PD_SDDAT3  0x80       /* Pull-down SD_DAT3 */

/* Bit masks for SDH_RD_WAIT_EN */

#define                       RWR  0x1        /* Read Wait Request */

#endif /* _DEF_BF514_H */
