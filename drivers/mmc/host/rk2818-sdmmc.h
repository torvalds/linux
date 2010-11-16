/* drivers/mmc/host/rk2818-sdmmc.h
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

#ifndef __RK2818_SDMMC_H
#define __RK2818_SDMMC_H

#define MAX_SG_CHN	2


#define SDMMC_CTRL            (0x000)
#define SDMMC_PWREN           (0x004)
#define SDMMC_CLKDIV          (0x008)
#define SDMMC_CLKSRC          (0x00c)
#define SDMMC_CLKENA          (0x010)
#define SDMMC_TMOUT           (0x014)
#define SDMMC_CTYPE           (0x018)
#define SDMMC_BLKSIZ          (0x01c)
#define SDMMC_BYTCNT          (0x020)
#define SDMMC_INTMASK         (0x024)
#define SDMMC_CMDARG          (0x028)
#define SDMMC_CMD             (0x02c)
#define SDMMC_RESP0           (0x030)
#define SDMMC_RESP1           (0x034)
#define SDMMC_RESP2           (0x038)
#define SDMMC_RESP3           (0x03c)
#define SDMMC_MINTSTS         (0x040)
#define SDMMC_RINTSTS         (0x044)
#define SDMMC_STATUS          (0x048)
#define SDMMC_FIFOTH          (0x04c)
#define SDMMC_CDETECT         (0x050)
#define SDMMC_WRTPRT          (0x054)
#define SDMMC_TCBCNT          (0x05c)
#define SDMMC_TBBCNT          (0x060)
#define SDMMC_DEBNCE          (0x064)

#define SDMMC_DATA            (0x100)

#define RK2818_BIT(n)				(1<<(n))

/* Control register defines */
#define SDMMC_CTRL_OD_PULLUP	  RK2818_BIT(24)
#define SDMMC_CTRL_ABRT_READ_DATA RK2818_BIT(8)
#define SDMMC_CTRL_SEND_IRQ_RESP  RK2818_BIT(7)
#define SDMMC_CTRL_READ_WAIT      RK2818_BIT(6)
#define SDMMC_CTRL_DMA_ENABLE     RK2818_BIT(5)
#define SDMMC_CTRL_INT_ENABLE     RK2818_BIT(4)
#define SDMMC_CTRL_DMA_RESET      RK2818_BIT(2)
#define SDMMC_CTRL_FIFO_RESET     RK2818_BIT(1)
#define SDMMC_CTRL_RESET          RK2818_BIT(0)
/* Clock Enable register defines */
#define SDMMC_CLKEN_LOW_PWR      RK2818_BIT(16)
#define SDMMC_CLKEN_ENABLE       RK2818_BIT(0)
/* time-out register defines */
#define SDMMC_TMOUT_DATA(n)      _SBF(8, (n))
#define SDMMC_TMOUT_DATA_MSK     0xFFFFFF00
#define SDMMC_TMOUT_RESP(n)      ((n) & 0xFF)
#define SDMMC_TMOUT_RESP_MSK     0xFF
/* card-type register defines */
#define SDMMC_CTYPE_8BIT         RK2818_BIT(16)
#define SDMMC_CTYPE_4BIT         RK2818_BIT(0)
/* Interrupt status & mask register defines */
#define SDMMC_INT_SDIO          RK2818_BIT(16)
#define SDMMC_INT_EBE           RK2818_BIT(15)
#define SDMMC_INT_ACD           RK2818_BIT(14)
#define SDMMC_INT_SBE           RK2818_BIT(13)
#define SDMMC_INT_HLE           RK2818_BIT(12)
#define SDMMC_INT_FRUN          RK2818_BIT(11)
#define SDMMC_INT_HTO           RK2818_BIT(10)
#define SDMMC_INT_DRTO          RK2818_BIT(9)
#define SDMMC_INT_RTO           RK2818_BIT(8)
#define SDMMC_INT_DCRC          RK2818_BIT(7)
#define SDMMC_INT_RCRC          RK2818_BIT(6)
#define SDMMC_INT_RXDR          RK2818_BIT(5)
#define SDMMC_INT_TXDR          RK2818_BIT(4)
#define SDMMC_INT_DTO			RK2818_BIT(3)
#define SDMMC_INT_CMD_DONE		RK2818_BIT(2)
#define SDMMC_INT_RE	        RK2818_BIT(1)
#define SDMMC_INT_CD            RK2818_BIT(0)

/* Command register defines */
#define SDMMC_CMD_START         RK2818_BIT(31)
#define SDMMC_CMD_CCS_EXP       RK2818_BIT(23)
#define SDMMC_CMD_CEATA_RD      RK2818_BIT(22)
#define SDMMC_CMD_UPD_CLK       RK2818_BIT(21)
#define SDMMC_CMD_INIT          RK2818_BIT(15)
#define SDMMC_CMD_STOP          RK2818_BIT(14)
#define SDMMC_CMD_PRV_DAT_WAIT  RK2818_BIT(13)
#define SDMMC_CMD_SEND_STOP     RK2818_BIT(12)
#define SDMMC_CMD_STRM_MODE     RK2818_BIT(11)
#define SDMMC_CMD_DAT_WR        RK2818_BIT(10)
#define SDMMC_CMD_DAT_EXP       RK2818_BIT(9)
#define SDMMC_CMD_RESP_CRC      RK2818_BIT(8)
#define SDMMC_CMD_RESP_LONG     RK2818_BIT(7)
#define SDMMC_CMD_RESP_EXP      RK2818_BIT(6)
#define SDMMC_CMD_INDX(n)       ((n) & 0x1F)
/* Status register defines */
#define SDMMC_STAUTS_RESP_INDEX	RK2818_BIT(11)
#define SDMMC_STAUTS_MC_BUSY	RK2818_BIT(10)
#define SDMMC_STAUTS_DATA_BUSY	RK2818_BIT(9)
#define SDMMC_STAUTS_CARD_PRESENT	RK2818_BIT(8)
#define SDMMC_STAUTS_FIFO_FULL	RK2818_BIT(3)
#define SDMMC_STAUTS_FIFO_EMPTY	RK2818_BIT(2)
#define SDMMC_STAUTS_FIFO_TX_WATERMARK	RK2818_BIT(1)
#define SDMMC_STAUTS_FIFO_RX_WATERMARK	RK2818_BIT(0)




#define SDMMC_GET_FCNT(x)       (((x)>>17) & 0x1FF)
#define SDMMC_FIFO_SZ           32

#define SDMMC_WRITE_PROTECT		RK2818_BIT(0)
#define SDMMC_CARD_DETECT_N		RK2818_BIT(0)

/* Specifies how often in millisecs to poll for card removal-insertion changes
 * when the timer switch is open */
#define RK_SDMMC0_SWITCH_POLL_DELAY 35


#endif
