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

#define _BIT(n)				(1<<(n))

/* Control register defines */
#define SDMMC_CTRL_ABRT_READ_DATA _BIT(8)
#define SDMMC_CTRL_SEND_IRQ_RESP  _BIT(7)
#define SDMMC_CTRL_READ_WAIT      _BIT(6)
#define SDMMC_CTRL_DMA_ENABLE     _BIT(5)
#define SDMMC_CTRL_INT_ENABLE     _BIT(4)
#define SDMMC_CTRL_DMA_RESET      _BIT(2)
#define SDMMC_CTRL_FIFO_RESET     _BIT(1)
#define SDMMC_CTRL_RESET          _BIT(0)
/* Clock Enable register defines */
#define SDMMC_CLKEN_LOW_PWR      _BIT(16)
#define SDMMC_CLKEN_ENABLE       _BIT(0)
/* time-out register defines */
#define SDMMC_TMOUT_DATA(n)      _SBF(8, (n))
#define SDMMC_TMOUT_DATA_MSK     0xFFFFFF00
#define SDMMC_TMOUT_RESP(n)      ((n) & 0xFF)
#define SDMMC_TMOUT_RESP_MSK     0xFF
/* card-type register defines */
#define SDMMC_CTYPE_8BIT         _BIT(16)
#define SDMMC_CTYPE_4BIT         _BIT(0)
/* Interrupt status & mask register defines */
#define SDMMC_INT_SDIO          _BIT(16)
#define SDMMC_INT_EBE           _BIT(15)
#define SDMMC_INT_ACD           _BIT(14)
#define SDMMC_INT_SBE           _BIT(13)
#define SDMMC_INT_HLE           _BIT(12)
#define SDMMC_INT_FRUN          _BIT(11)
#define SDMMC_INT_HTO           _BIT(10)
#define SDMMC_INT_DRTO          _BIT(9)
#define SDMMC_INT_RTO           _BIT(8)
#define SDMMC_INT_DCRC          _BIT(7)
#define SDMMC_INT_RCRC          _BIT(6)
#define SDMMC_INT_RXDR          _BIT(5)
#define SDMMC_INT_TXDR          _BIT(4)
#define SDMMC_INT_DTO			_BIT(3)
#define SDMMC_INT_CMD_DONE		_BIT(2)
#define SDMMC_INT_RE	        _BIT(1)
#define SDMMC_INT_CD            _BIT(0)

/* Command register defines */
#define SDMMC_CMD_START         _BIT(31)
#define SDMMC_CMD_CCS_EXP       _BIT(23)
#define SDMMC_CMD_CEATA_RD      _BIT(22)
#define SDMMC_CMD_UPD_CLK       _BIT(21)
#define SDMMC_CMD_INIT          _BIT(15)
#define SDMMC_CMD_STOP          _BIT(14)
#define SDMMC_CMD_PRV_DAT_WAIT  _BIT(13)
#define SDMMC_CMD_SEND_STOP     _BIT(12)
#define SDMMC_CMD_STRM_MODE     _BIT(11)
#define SDMMC_CMD_DAT_WR        _BIT(10)
#define SDMMC_CMD_DAT_EXP       _BIT(9)
#define SDMMC_CMD_RESP_CRC      _BIT(8)
#define SDMMC_CMD_RESP_LONG     _BIT(7)
#define SDMMC_CMD_RESP_EXP      _BIT(6)
#define SDMMC_CMD_INDX(n)       ((n) & 0x1F)
/* Status register defines */
#define SDMMC_GET_FCNT(x)       (((x)>>17) & 0x1FF)
#define SDMMC_FIFO_SZ           32

#define SDMMC_WRITE_PROTECT		_BIT(0)
#define SDMMC_CARD_DETECT_N		_BIT(0)
#endif
