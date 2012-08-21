/* drivers/mmc/host/rk29_sdmmc.h
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
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

#ifndef __RK2918_SDMMC_H
#define __RK2918_SDMMC_H

#define MAX_SG_CHN	2


#define SDMMC_CTRL            (0x000)   //SDMMC Control register
#define SDMMC_PWREN           (0x004)   //Power enable register
#define SDMMC_CLKDIV          (0x008)   //Clock divider register
#define SDMMC_CLKSRC          (0x00c)   //Clock source register
#define SDMMC_CLKENA          (0x010)   //Clock enable register
#define SDMMC_TMOUT           (0x014)   //Time out register
#define SDMMC_CTYPE           (0x018)   //Card type register
#define SDMMC_BLKSIZ          (0x01c)   //Block size register
#define SDMMC_BYTCNT          (0x020)   //Byte count register
#define SDMMC_INTMASK         (0x024)   //Interrupt mask register
#define SDMMC_CMDARG          (0x028)   //Command argument register
#define SDMMC_CMD             (0x02c)   //Command register
#define SDMMC_RESP0           (0x030)   //Response 0 register
#define SDMMC_RESP1           (0x034)   //Response 1 register
#define SDMMC_RESP2           (0x038)   //Response 2 register
#define SDMMC_RESP3           (0x03c)   //Response 3 register
#define SDMMC_MINTSTS         (0x040)   //Masked interrupt status register
#define SDMMC_RINTSTS         (0x044)   //Raw interrupt status register
#define SDMMC_STATUS          (0x048)   //Status register
#define SDMMC_FIFOTH          (0x04c)   //FIFO threshold register
#define SDMMC_CDETECT         (0x050)   //Card detect register
#define SDMMC_WRTPRT          (0x054)   //Write protect register
#define SDMMC_TCBCNT          (0x05c)   //Transferred CIU card byte count
#define SDMMC_TBBCNT          (0x060)   //Transferred host/DMA to/from BIU_FIFO byte count
#define SDMMC_DEBNCE          (0x064)   //Card detect debounce register
#define SDMMC_USRID           (0x068)   //User ID register

#if defined(CONFIG_ARCH_RK29)
#define SDMMC_DATA            (0x100)
#else
#define SDMMC_VERID           (0x06c)   //Version ID register
#define SDMMC_UHS_REG         (0x074)   //UHS-I register
#define SDMMC_RST_n           (0x078)   //Hardware reset register
#define SDMMC_CARDTHRCTL      (0x100)   //Card Read Threshold Enable
#define SDMMC_BACK_END_POWER  (0x104)   //Back-end Power
#define SDMMC_FIFO_BASE       (0x200)   //

#define SDMMC_DATA            SDMMC_FIFO_BASE
#endif

#define RK2818_BIT(n)				(1<<(n))
#define RK_CLEAR_BIT(n)		        (0<<(n))


/* Control register defines (base+ 0x00)*/
#define SDMMC_CTRL_OD_PULLUP	  RK2818_BIT(24)
#define SDMMC_CTRL_DMA_ENABLE     RK2818_BIT(5)
#define SDMMC_CTRL_INT_ENABLE     RK2818_BIT(4)
#define SDMMC_CTRL_DMA_RESET      RK2818_BIT(2)
#define SDMMC_CTRL_FIFO_RESET     RK2818_BIT(1)
#define SDMMC_CTRL_RESET          RK2818_BIT(0)

/* Power Enable Register(base+ 0x04) */
#define POWER_ENABLE             RK2818_BIT(0)      //Power enable
#define POWER_DISABLE            RK_CLEAR_BIT(0)    //Power off

/* SDMMC Clock source Register(base+ 0x0C) */
#define CLK_DIV_SRC_0         (0x0)    //clock divider 0 selected
#define CLK_DIV_SRC_1         (0x1)    //clock divider 1 selected
#define CLK_DIV_SRC_2         (0x2)    //clock divider 2 selected
#define CLK_DIV_SRC_3         (0x3)    //clock divider 3 selected


/* Clock Enable register defines(base+0x10) */
#define SDMMC_CLKEN_LOW_PWR      RK2818_BIT(16)
#define SDMMC_CLKEN_NO_LOW_PWR   RK_CLEAR_BIT(16)   //low-power mode disabled
#define SDMMC_CLKEN_ENABLE       RK2818_BIT(0)
#define SDMMC_CLKEN_DISABLE      RK_CLEAR_BIT(16)   //clock disabled

/* time-out register defines(base+0x14) */
#define SDMMC_TMOUT_DATA(n)      _SBF(8, (n))
#define SDMMC_TMOUT_DATA_MSK     0xFFFFFF00
#define SDMMC_TMOUT_RESP(n)      ((n) & 0xFF)
#define SDMMC_TMOUT_RESP_MSK     0xFF

/* card-type register defines(base+0x18) */
#define SDMMC_CTYPE_8BIT         RK2818_BIT(16)
#define SDMMC_CTYPE_4BIT         RK2818_BIT(0)
#define SDMMC_CTYPE_1BIT         RK_CLEAR_BIT(0)

/* Interrupt status & mask register defines(base+0x24) */
#if defined(CONFIG_ARCH_RK29)
#define SDMMC_INT_SDIO          RK2818_BIT(16)      //SDIO interrupt
#else
#define SDMMC_INT_SDIO          RK2818_BIT(24)      //SDIO interrupt
#define SDMMC_INT_UNBUSY        RK2818_BIT(16)      //data no busy interrupt
#endif

#define SDMMC_INT_EBE           RK2818_BIT(15)      //End Bit Error(read)/Write no CRC
#define SDMMC_INT_ACD           RK2818_BIT(14)      //Auto Command Done
#define SDMMC_INT_SBE           RK2818_BIT(13)      //Start Bit Error
#define SDMMC_INT_HLE           RK2818_BIT(12)      //Hardware Locked Write Error
#define SDMMC_INT_FRUN          RK2818_BIT(11)      //FIFO Underrun/Overrun Error
#define SDMMC_INT_HTO           RK2818_BIT(10)      //Data Starvation by Host Timeout
#define SDMMC_INT_DRTO          RK2818_BIT(9)       //Data Read TimeOut
#define SDMMC_INT_RTO           RK2818_BIT(8)       //Response TimeOut
#define SDMMC_INT_DCRC          RK2818_BIT(7)       //Data CRC Error
#define SDMMC_INT_RCRC          RK2818_BIT(6)       //Response CRC Error
#define SDMMC_INT_RXDR          RK2818_BIT(5)       //Receive FIFO Data Request
#define SDMMC_INT_TXDR          RK2818_BIT(4)       //Transmit FIFO Data Request
#define SDMMC_INT_DTO			RK2818_BIT(3)       //Data Transfer Over
#define SDMMC_INT_CMD_DONE		RK2818_BIT(2)       //Command Done
#define SDMMC_INT_RE	        RK2818_BIT(1)       //Response Error
#define SDMMC_INT_CD            RK2818_BIT(0)       //Card Detect

/* Command register defines(base+0x2C) */
#define SDMMC_CMD_START         RK2818_BIT(31)      //start command
#if !defined(CONFIG_ARCH_RK29)
#define SDMMC_CMD_USE_HOLD_REG      RK2818_BIT(29)      //Use hold register.
#define SDMMC_CMD_VOLT_SWITCH       RK2818_BIT(28)      //Voltage switch bit
#define SDMMC_CMD_BOOT_MODE         RK2818_BIT(27)      //set boot mode.
#define SDMMC_CMD_DISABLE_BOOT      RK2818_BIT(26)      //disable boot.
#define SDMMC_CMD_EXPECT_BOOT_ACK   RK2818_BIT(25)      //Expect Boot Acknowledge.
#define SDMMC_CMD_ENABLE_BOOT       RK2818_BIT(24)      //be set only for mandatory boot mode.
#endif
#define SDMMC_CMD_UPD_CLK       RK2818_BIT(21)      //update clock register only
#define SDMMC_CMD_INIT          RK2818_BIT(15)      //send initialization sequence
#define SDMMC_CMD_STOP          RK2818_BIT(14)      //stop abort command
#define SDMMC_CMD_PRV_DAT_NO_WAIT  RK_CLEAR_BIT(13) //not wait previous data transfer complete, send command at once
#define SDMMC_CMD_PRV_DAT_WAIT  RK2818_BIT(13)      //wait previous data transfer complete
#define SDMMC_CMD_SEND_STOP     RK2818_BIT(12)      //send auto stop command at end of data transfer
#define SDMMC_CMD_BLOCK_MODE    RK_CLEAR_BIT(11)    //block data transfer command
#define SDMMC_CMD_STRM_MODE     RK2818_BIT(11)      //stream data transfer command
#define SDMMC_CMD_DAT_READ      RK_CLEAR_BIT(10)    //read from card
#define SDMMC_CMD_DAT_WRITE     RK2818_BIT(10)      //write to card; 
#define SDMMC_CMD_DAT_WR        RK2818_BIT(10)      //write to card;
#define SDMMC_CMD_DAT_NO_EXP    RK_CLEAR_BIT(9)     //no data transfer expected
#define SDMMC_CMD_DAT_EXP       RK2818_BIT(9)       //data transfer expected
#define SDMMC_CMD_RESP_NO_CRC   RK_CLEAR_BIT(8)     //do not check response crc
#define SDMMC_CMD_RESP_CRC      RK2818_BIT(8)       //check response crc
#define SDMMC_CMD_RESP_CRC_NOCARE   SDMMC_CMD_RESP_CRC  //not care response crc
#define SDMMC_CMD_RESP_SHORT    RK_CLEAR_BIT(7)     //short response expected from card
#define SDMMC_CMD_RESP_LONG     RK2818_BIT(7)       //long response expected from card;
#define SDMMC_CMD_RESP_NOCARE   SDMMC_CMD_RESP_SHORT    //not care response length
#define SDMMC_CMD_RESP_NO_EXP   RK_CLEAR_BIT(6)     //no response expected from card
#define SDMMC_CMD_RESP_EXP      RK2818_BIT(6)       //response expected from card
#define SDMMC_CMD_INDX(n)       ((n) & 0x1F)


/* Status register defines (base+0x48)*/
#define SDMMC_STAUTS_MC_BUSY	RK2818_BIT(10)
#define SDMMC_STAUTS_DATA_BUSY	RK2818_BIT(9)       //Card busy
#define SDMMC_CMD_FSM_MASK		(0x0F << 4)	//Command FSM status mask
#define SDMMC_CMD_FSM_IDLE      (0x00)			//CMD FSM is IDLE
#define SDMMC_STAUTS_FIFO_FULL	RK2818_BIT(3)       //FIFO is full status
#define SDMMC_STAUTS_FIFO_EMPTY	RK2818_BIT(2)       //FIFO is empty status

#define SDMMC_GET_FCNT(x)       (((x)>>17) & 0x1FF)//fifo_count, numbers of filled locations in FIFO
#define SDMMC_FIFO_SZ           32


/* FIFO Register (base + 0x4c)*/
#define SD_MSIZE_1        (0x0 << 28)  //DW_DMA_Multiple_Transaction_Size
#define SD_MSIZE_4        (0x1 << 28)
#define SD_MSIZE_8        (0x1 << 28)
#define SD_MSIZE_16       (0x3 << 28)
#define SD_MSIZE_32       (0x4 << 28)
#define SD_MSIZE_64       (0x5 << 28)
#define SD_MSIZE_128      (0x6 << 28)
#define SD_MSIZE_256      (0x7 << 28)

#if defined(CONFIG_ARCH_RK29)
#define FIFO_DEPTH        (0x20)       //FIFO depth = 32 word
#define RX_WMARK_SHIFT    (16)
#define TX_WMARK_SHIFT    (0)

/* FIFO watermark */
#define RX_WMARK          (0xF)        //RX watermark level set to 15
#define TX_WMARK          (0x10)       //TX watermark level set to 16

#else
#define FIFO_DEPTH        (0x100)       //FIFO depth = 256 word
#define RX_WMARK_SHIFT    (16)
#define TX_WMARK_SHIFT    (0)

/* FIFO watermark */
#define RX_WMARK          (FIFO_DEPTH/2-1)     //RX watermark level set to 127
#define TX_WMARK          (FIFO_DEPTH/2)       //TX watermark level set to  128
#endif

/* CDETECT register defines (base+0x50)*/
#define SDMMC_CARD_DETECT_N		RK2818_BIT(0)        //0--represents presence of card.

/* WRIPRT register defines (base+0x54)*/
#define SDMMC_WRITE_PROTECT		RK2818_BIT(0)       // 1--represents write protect



/* Specifies how often in millisecs to poll for card removal-insertion changes
 * when the timer switch is open */
#define RK_SDMMC0_SWITCH_POLL_DELAY 35



/* SDMMC progress  return value */
#define SDM_SUCCESS              (0)                    
#define SDM_FALSE                (1)             
#define SDM_PARAM_ERROR          (2)              
#define SDM_RESP_ERROR           (3)             
#define SDM_RESP_TIMEOUT         (4)              
#define SDM_DATA_CRC_ERROR       (5)              
#define SDM_DATA_READ_TIMEOUT    (6)              
#define SDM_END_BIT_ERROR        (7)              
#define SDM_START_BIT_ERROR      (8)              
#define SDM_BUSY_TIMEOUT         (9)            
#define SDM_ERROR                (10)             //SDMMC host controller error 
#define SDM_START_CMD_FAIL       (11)  
#define SDM_WAIT_FOR_CMDSTART_TIMEOUT   (12)  
#define SDM_WAIT_FOR_FIFORESET_TIMEOUT  (13)  



#define FALSE			0
#define TRUE			1


#define DEBOUNCE_TIME         (25)     //uint is ms, recommend 5--25ms


#endif
