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

#include <linux/bitops.h>

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
#define SDMMC_DATA            (0x100)   //FIFO data read write
#else
#define SDMMC_VERID           (0x06c)   //Version ID register
#define SDMMC_UHS_REG         (0x074)   //UHS-I register
#define SDMMC_RST_n           (0x078)   //Hardware reset register
#define SDMMC_BMOD		      (0x080)   //Bus mode register, control IMAC
#define SDMMC_PLDMND		  (0x084)   //Poll Demand Register
#define SDMMC_DBADDR		  (0x088)   //Descriptor List Base Address Register for 32-bit.
#define SDMMC_IDSTS		      (0x08c)   //Internal DMAC Status register
#define SDMMC_IDINTEN		  (0x090)   //Internal DMAC Interrupt Enable Register
#define SDMMC_DSCADDR		  (0x094)   //Current Host Descriptor Address Register for 32-bit
#define SDMMC_BUFADDR		  (0x098)   //Current Buffer Descriptor Address Register for 32-bit
#define SDMMC_CARDTHRCTL      (0x100)   //Card Read Threshold Enable
#define SDMMC_BACK_END_POWER  (0x104)   //Back-end Power
#define SDMMC_FIFO_BASE       (0x200)   //FIFO data read write

#define SDMMC_DATA            SDMMC_FIFO_BASE
#endif

struct sdmmc_reg
{
  u32      addr;
  char   * name;
};

static struct sdmmc_reg rk_sdmmc_regs[] =
{
  { 0x0000, "      CTRL" },
  { 0x0004, "     PWREN" },
  { 0x0008, "    CLKDIV" },
  { 0x000C, "    CLKSRC" },
  { 0x0010, "    CLKENA" },
  { 0x0014, "     TMOUT" },
  { 0x0018, "     CTYPE" },
  { 0x001C, "    BLKSIZ" },
  { 0x0020, "    BYTCNT" },
  { 0x0024, "    INTMSK" },
  { 0x0028, "    CMDARG" },
  { 0x002C, "       CMD" },
  { 0x0030, "     RESP0" },
  { 0x0034, "     RESP1" },
  { 0x0038, "     RESP2" },
  { 0x003C, "     RESP3" },
  { 0x0040, "    MINSTS" },
  { 0x0044, "   RINTSTS" },
  { 0x0048, "    STATUS" },
  { 0x004C, "    FIFOTH" },
  { 0x0050, "   CDETECT" },
  { 0x0054, "    WRTPRT" },
  { 0x0058, "      GPIO" },
  { 0x005C, "    TCBCNT" },
  { 0x0060, "    TBBCNT" },
  { 0x0064, "    DEBNCE" },
  { 0x0068, "     USRID" },
#if !defined(CONFIG_ARCH_RK29)  
  { 0x006C, "     VERID" },
  { 0x0070, "      HCON" },
  { 0x0074, "   UHS_REG" },
  { 0x0078, "     RST_n" },
  { 0x0080, "      BMOD" },
  { 0x0084, "    PLDMND" },
  { 0x0088, "    DBADDR" },
  { 0x008C, "     IDSTS" },
  { 0x0090, "   IDINTEN" },
  { 0x0094, "   DSCADDR" },
  { 0x0098, "   BUFADDR" },
  { 0x0100, "CARDTHRCTL" },
  { 0x0104, "BackEndPwr" },
#endif  
  { 0, 0 }
};

#define BIT(n)				(1<<(n))
#define RK_CLEAR_BIT(n)		        (0<<(n))


/* Control register defines (base+ 0x00)*/
#define SDMMC_CTRL_USE_IDMAC	    BIT(25)
#define SDMMC_CTRL_OD_PULLUP	    BIT(24)
#define SDMMC_CTRL_CEATA_INT_EN	    BIT(11)
#define SDMMC_CTRL_SEND_AS_CCSD	    BIT(10)
#define SDMMC_CTRL_SEND_CCSD		BIT(9)
#define SDMMC_CTRL_ABRT_READ_DATA	BIT(8)
#define SDMMC_CTRL_SEND_IRQ_RESP	BIT(7)
#define SDMMC_CTRL_READ_WAIT		BIT(6)
#define SDMMC_CTRL_DMA_ENABLE       BIT(5)
#define SDMMC_CTRL_INT_ENABLE       BIT(4)
#define SDMMC_CTRL_DMA_RESET        BIT(2)
#define SDMMC_CTRL_FIFO_RESET       BIT(1)
#define SDMMC_CTRL_RESET            BIT(0)

/* Power Enable Register(base+ 0x04) */
#define POWER_ENABLE             BIT(0)             //Power enable
#define POWER_DISABLE            RK_CLEAR_BIT(0)    //Power off

/* SDMMC Clock source Register(base+ 0x0C) */
#define CLK_DIV_SRC_0         (0x0)    //clock divider 0 selected
#define CLK_DIV_SRC_1         (0x1)    //clock divider 1 selected
#define CLK_DIV_SRC_2         (0x2)    //clock divider 2 selected
#define CLK_DIV_SRC_3         (0x3)    //clock divider 3 selected


/* Clock Enable register defines(base+0x10) */
#define SDMMC_CLKEN_LOW_PWR      BIT(16)
#define SDMMC_CLKEN_NO_LOW_PWR   RK_CLEAR_BIT(16)   //low-power mode disabled
#define SDMMC_CLKEN_ENABLE       BIT(0)
#define SDMMC_CLKEN_DISABLE      RK_CLEAR_BIT(0)   //clock disabled

/* time-out register defines(base+0x14) */
#define SDMMC_TMOUT_DATA(n)      _SBF(8, (n))
#define SDMMC_TMOUT_DATA_MSK     0xFFFFFF00
#define SDMMC_TMOUT_RESP(n)      ((n) & 0xFF)
#define SDMMC_TMOUT_RESP_MSK     0xFF

/* card-type register defines(base+0x18) */
#define SDMMC_CTYPE_8BIT         BIT(16)
#define SDMMC_CTYPE_4BIT         BIT(0)
#define SDMMC_CTYPE_1BIT         RK_CLEAR_BIT(0)

/* Interrupt status & mask register defines(base+0x24) */
#if defined(CONFIG_ARCH_RK29)
#define SDMMC_INT_SDIO          BIT(16)      //SDIO interrupt
#else
#define SDMMC_INT_SDIO          BIT(24)      //SDIO interrupt
#define SDMMC_INT_UNBUSY        BIT(16)      //data no busy interrupt
#endif

#define SDMMC_INT_EBE           BIT(15)      //End Bit Error(read)/Write no CRC
#define SDMMC_INT_ACD           BIT(14)      //Auto Command Done
#define SDMMC_INT_SBE           BIT(13)      //Start Bit Error
#define SDMMC_INT_HLE           BIT(12)      //Hardware Locked Write Error
#define SDMMC_INT_FRUN          BIT(11)      //FIFO Underrun/Overrun Error
#define SDMMC_INT_HTO           BIT(10)      //Data Starvation by Host Timeout
#define SDMMC_INT_VSI           SDMMC_INT_HTO   // VSI => Voltage Switch Interrupt,Volt_Switch_int
#define SDMMC_INT_DRTO          BIT(9)       //Data Read TimeOut
#define SDMMC_INT_RTO           BIT(8)       //Response TimeOut
#define SDMMC_INT_DCRC          BIT(7)       //Data CRC Error
#define SDMMC_INT_RCRC          BIT(6)       //Response CRC Error
#define SDMMC_INT_RXDR          BIT(5)       //Receive FIFO Data Request
#define SDMMC_INT_TXDR          BIT(4)       //Transmit FIFO Data Request
#define SDMMC_INT_DTO			BIT(3)       //Data Transfer Over
#define SDMMC_INT_CMD_DONE		BIT(2)       //Command Done
#define SDMMC_INT_RE	        BIT(1)       //Response Error
#define SDMMC_INT_CD            BIT(0)       //Card Detect

/* Command register defines(base+0x2C) */
#define SDMMC_CMD_START         BIT(31)      //start command
#if !defined(CONFIG_ARCH_RK29)
#define SDMMC_CMD_USE_HOLD_REG      BIT(29)      //Use hold register.
#define SDMMC_CMD_VOLT_SWITCH       BIT(28)      //Voltage switch bit
#define SDMMC_CMD_BOOT_MODE         BIT(27)      //set boot mode.
#define SDMMC_CMD_DISABLE_BOOT      BIT(26)      //disable boot.
#define SDMMC_CMD_EXPECT_BOOT_ACK   BIT(25)      //Expect Boot Acknowledge.
#define SDMMC_CMD_ENABLE_BOOT       BIT(24)      //be set only for mandatory boot mode.
#define SDMMC_CMD_CCS_EXP		    BIT(23)      //expect Command Completion Signal(CCS) from the CE-ATA device.
#define SDMMC_CMD_CEATA_RD		    BIT(22)      //software should set this bit to indicate that CE-ATA device
#endif
#define SDMMC_CMD_UPD_CLK           BIT(21)             //update clock register only
#define SDMMC_CMD_INIT              BIT(15)             //send initialization sequence
#define SDMMC_CMD_STOP              BIT(14)             //stop abort command
#define SDMMC_CMD_PRV_DAT_NO_WAIT   RK_CLEAR_BIT(13)    //not wait previous data transfer complete, send command at once
#define SDMMC_CMD_PRV_DAT_WAIT      BIT(13)             //wait previous data transfer complete
#define SDMMC_CMD_SEND_STOP         BIT(12)             //send auto stop command at end of data transfer
#define SDMMC_CMD_BLOCK_MODE        RK_CLEAR_BIT(11)    //block data transfer command
#define SDMMC_CMD_STRM_MODE         BIT(11)             //stream data transfer command
#define SDMMC_CMD_DAT_READ          RK_CLEAR_BIT(10)    //read from card
#define SDMMC_CMD_DAT_WRITE         BIT(10)             //write to card; 
#define SDMMC_CMD_DAT_WR            BIT(10)             //write to card;
#define SDMMC_CMD_DAT_NO_EXP        RK_CLEAR_BIT(9)     //no data transfer expected
#define SDMMC_CMD_DAT_EXP           BIT(9)              //data transfer expected
#define SDMMC_CMD_RESP_NO_CRC       RK_CLEAR_BIT(8)     //do not check response crc
#define SDMMC_CMD_RESP_CRC          BIT(8)              //check response crc
#define SDMMC_CMD_RESP_CRC_NOCARE   SDMMC_CMD_RESP_CRC  //not care response crc
#define SDMMC_CMD_RESP_SHORT        RK_CLEAR_BIT(7)         //short response expected from card
#define SDMMC_CMD_RESP_LONG         BIT(7)                  //long response expected from card;
#define SDMMC_CMD_RESP_NOCARE       SDMMC_CMD_RESP_SHORT    //not care response length
#define SDMMC_CMD_RESP_NO_EXP       RK_CLEAR_BIT(6)         //no response expected from card
#define SDMMC_CMD_RESP_EXP          BIT(6)                  //response expected from card
#define SDMMC_CMD_INDX(n)           ((n) & 0x1F)


/* Status register defines (base+0x48)*/
#define SDMMC_STAUTS_MC_BUSY	    BIT(10)
#define SDMMC_STAUTS_DATA_BUSY	    BIT(9)          //Card busy
#define SDMMC_CMD_FSM_MASK		    (0x0F << 4)	    //Command FSM status mask
#define SDMMC_CMD_FSM_IDLE          (0x00)			//CMD FSM is IDLE
#define SDMMC_STAUTS_FIFO_FULL	    BIT(3)          //FIFO is full status
#define SDMMC_STAUTS_FIFO_EMPTY	    BIT(2)          //FIFO is empty status

/* Status register defines */
#define SDMMC_GET_FCNT(x)           (((x)>>17) & 0x1FFF)//fifo_count, numbers of filled locations in FIFO
#define SDMMC_FIFO_SZ               32
#define PIO_DATA_SHIFT              2


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

#define FIFO_THRESHOLD_WATERMASK    (SD_MSIZE_16 |(RX_WMARK << RX_WMARK_SHIFT)|(TX_WMARK << TX_WMARK_SHIFT))

/* CDETECT register defines (base+0x50)*/
#define SDMMC_CARD_DETECT_N		BIT(0)      //0--represents presence of card.

/* WRIPRT register defines (base+0x54)*/
#define SDMMC_WRITE_PROTECT		BIT(0)      // 1--represents write protect

/* Control SDMMC_UHS_REG defines (base+ 0x74)*/
#define SDMMC_UHS_DDR_MODE      BIT(16)     // 0--Non DDR Mode; 1--DDR mode 
#define SDMMC_UHS_VOLT_REG_18   BIT(0)      // 0--3.3v; 1--1.8V


// #ifdef IDMAC_SUPPORT

/* Bus Mode Register (base + 0x80) */
#define  BMOD_SWR 		    BIT(0)	    // Software Reset: Auto cleared after one clock cycle                                0
#define  BMOD_FB 		    BIT(1)	    // Fixed Burst Length: when set SINGLE/INCR/INCR4/INCR8/INCR16 used at the start     1 
#define  BMOD_DE 		    BIT(7)	    // Idmac Enable: When set IDMAC is enabled                                           7
#define	 BMOD_DSL_MSK		0x0000007C	// Descriptor Skip length: In Number of Words                                      6:2 
#define	 BMOD_DSL_Shift		2	        // Descriptor Skip length Shift value
#define	 BMOD_DSL_ZERO      0x00000000	// No Gap between Descriptors
#define	 BMOD_DSL_TWO       0x00000008	// 2 Words Gap between Descriptors
#define  BMOD_PBL		    0x00000400	// MSIZE in FIFOTH Register 

/* Internal DMAC Status Register(base + 0x8c)*/
/* Internal DMAC Interrupt Enable Register Bit Definitions */
#define  IDMAC_AI			    0x00000200   // Abnormal Interrupt Summary Enable/ Status                                       9
#define  IDMAC_NI    		   	0x00000100   // Normal Interrupt Summary Enable/ Status                                         8
#define  IDMAC_CES				0x00000020   // Card Error Summary Interrupt Enable/ status                                     5
#define  IDMAC_DU				0x00000010   // Descriptor Unavailabe Interrupt Enable /Status                                  4
#define  IDMAC_FBE				0x00000004   // Fata Bus Error Enable/ Status                                                   2
#define  IDMAC_RI				0x00000002   // Rx Interrupt Enable/ Status                                                     1
#define  IDMAC_TI				0x00000001   // Tx Interrupt Enable/ Status                                                     0

#define  IDMAC_EN_INT_ALL   	0x00000337   // Enables all interrupts 

#define IDMAC_HOST_ABORT_TX     0x00000400   // Host Abort received during Transmission                                     12:10 
#define IDMAC_HOST_ABORT_RX     0x00000800   // Host Abort received during Reception                                        12:10 

/* IDMAC FSM States */
#define IDMAC_DMA_IDLE          0x00000000   // DMA is in IDLE state                                                        
#define IDMAC_DMA_SUSPEND       0x00002000   // DMA is in SUSPEND state                                                     
#define IDMAC_DESC_RD           0x00004000   // DMA is in DESC READ or FETCH State                                          
#define IDMAC_DESC_CHK          0x00006000   // DMA is checking the Descriptor for Correctness                              
#define IDMAC_DMA_RD_REQ_WAIT   0x00008000   // DMA is in this state till dma_req is asserted (Read operation)              
#define IDMAC_DMA_WR_REQ_WAIT   0x0000A000   // DMA is in this state till dma_req is asserted (Write operation)             
#define IDMAC_DMA_RD            0x0000C000   // DMA is in Read mode                                                         
#define IDMAC_DMA_WR            0x0000E000   // DMA is in Write mode                                                         
#define IDMAC_DESC_CLOSE        0x00010000   // DMA is closing the Descriptor                                               

#define FIFOTH_MSIZE_1		0x00000000   // Multiple Trans. Size is 1
#define FIFOTH_MSIZE_4		0x10000000   // Multiple Trans. Size is 4
#define FIFOTH_MSIZE_8   	0x20000000   // Multiple Trans. Size is 8
#define FIFOTH_MSIZE_16		0x30000000   // Multiple Trans. Size is 16
#define FIFOTH_MSIZE_32		0x40000000   // Multiple Trans. Size is 32
#define FIFOTH_MSIZE_64		0x50000000   // Multiple Trans. Size is 64
#define FIFOTH_MSIZE_128	0x60000000   // Multiple Trans. Size is 128
#define FIFOTH_MSIZE_256	0x70000000   // Multiple Trans. Size is 256
// #endif //#endif --#ifdef IDMAC_SUPPORT


/**********************************************************************
**  Misc Defines 
**********************************************************************/
#define SDMMC_MAX_BUFF_SIZE_IDMAC       8192
#define SDMMC_DEFAULT_DEBNCE_VAL        0x0FFFFFF

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

#if defined(CONFIG_ARCH_RK29)
#define SDMMC_USE_INT_UNBUSY     0
#else
#define SDMMC_USE_INT_UNBUSY     0///1 
#endif

/*
** You can set the macro to true, if some module wants to use this feature, which is about SDIO suspend-resume.
** As the following example.
** added by xbw at 2013-05-08
*/
#if defined(CONFIG_MTK_COMBO_DRIVER_VERSION_JB2)
#define RK_SDMMC_USE_SDIO_SUSPEND_RESUME    1
#else
#define RK_SDMMC_USE_SDIO_SUSPEND_RESUME    0
#endif

#define RK29_SDMMC_ERROR_FLAGS		(SDMMC_INT_FRUN | SDMMC_INT_HLE )

#if defined(CONFIG_SDMMC0_RK29_SDCARD_DET_FROM_GPIO)
    #if SDMMC_USE_INT_UNBUSY
    #define RK29_SDMMC_INTMASK_USEDMA   (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | SDMMC_INT_UNBUSY |RK29_SDMMC_ERROR_FLAGS )
    #define RK29_SDMMC_INTMASK_USEIO    (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | SDMMC_INT_UNBUSY |RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_TXDR | SDMMC_INT_RXDR )
    #else
    #define RK29_SDMMC_INTMASK_USEDMA   (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS )
    #define RK29_SDMMC_INTMASK_USEIO    (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_TXDR | SDMMC_INT_RXDR )
    #endif
#else
    #if SDMMC_USE_INT_UNBUSY
    #define RK29_SDMMC_INTMASK_USEDMA   (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | SDMMC_INT_UNBUSY |RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_CD)
    #define RK29_SDMMC_INTMASK_USEIO    (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | SDMMC_INT_UNBUSY |RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_CD| SDMMC_INT_TXDR | SDMMC_INT_RXDR )
    #else
    #define RK29_SDMMC_INTMASK_USEDMA   (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_CD)
    #define RK29_SDMMC_INTMASK_USEIO    (SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_CD| SDMMC_INT_TXDR | SDMMC_INT_RXDR )
    #endif
#endif

#define RK29_SDMMC_SEND_START_TIMEOUT   3000  //The time interval from the time SEND_CMD to START_CMD_BIT cleared.
#define RK29_ERROR_PRINTK_INTERVAL      200   //The time interval between the two printk for the same error. 
#define RK29_SDMMC_WAIT_DTO_INTERNVAL   4500  //The time interval from the CMD_DONE_INT to DTO_INT
#define RK29_SDMMC_REMOVAL_DELAY        2000  //The time interval from the CD_INT to detect_timer react.

//#define RK29_SDMMC_VERSION "Ver.6.00 The last modify date is 2013-08-02"

#if !defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)	
#define RK29_CTRL_SDMMC_ID   0  //mainly used by SDMMC
#define RK29_CTRL_SDIO1_ID   1  //mainly used by sdio-wifi
#define RK29_CTRL_SDIO2_ID   2  //mainly used by sdio-card
#else
#define RK29_CTRL_SDMMC_ID   5  
#define RK29_CTRL_SDIO1_ID   1  
#define RK29_CTRL_SDIO2_ID   2  
#endif

#define SDMMC_CLOCK_TEST     0

#define RK29_SDMMC_NOTIFY_REMOVE_INSERTION /* use sysfs to notify the removal or insertion of sd-card*/
//#define RK29_SDMMC_LIST_QUEUE            /* use list-queue for multi-card*/

//support Internal DMA 
#if 0 //Sometime in the future to enable
#define DRIVER_SDMMC_USE_IDMA 1
#else
#define DRIVER_SDMMC_USE_IDMA 0
#endif


enum {
	EVENT_CMD_COMPLETE = 0,
	EVENT_DATA_COMPLETE,
	EVENT_DATA_UNBUSY,
	EVENT_DATA_ERROR,
	EVENT_XFER_ERROR
};

enum rk29_sdmmc_state {
	STATE_IDLE = 0,
	STATE_SENDING_CMD,
	STATE_DATA_BUSY,
	STATE_DATA_UNBUSY,
	STATE_DATA_END,
	STATE_SENDING_STOP,
};

struct rk29_sdmmc_dma_info {
	enum dma_ch chn;
	char *name;
	struct rk29_dma_client client;
};

static struct rk29_sdmmc_dma_info rk29_sdmmc_dma_infos[]= {
	{
		.chn = DMACH_SDMMC,
		.client = {
			.name = "rk29-dma-sdmmc0",
		}
	},
	{
		.chn = DMACH_SDIO,
		.client = {
			.name = "rk29-dma-sdio1",
		}
	},

	{
		.chn = DMACH_EMMC,
		.client = {
			.name = "rk29-dma-sdio2",
		}
	},
};


/* Interrupt Information */
typedef struct TagSDC_INT_INFO
{
    u32     transLen;               //the length of data sent.
    u32     desLen;                 //the total length of the all data.
    u32    *pBuf;                   //the data buffer for interrupt read or write.
}SDC_INT_INFO_T;


struct rk29_sdmmc {
	spinlock_t		lock;
	void __iomem	*regs;
	struct clk 		*clk;

	struct mmc_request	*mrq;
	struct mmc_request	*new_mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;	
	struct scatterlist	*sg;
	unsigned int		pio_offset;

	dma_addr_t		dma_addr;;
	unsigned int	use_dma:1;
	char			dma_name[8];
	u32			cmd_status;
	u32			data_status;
	u32			stop_cmdr;

    u32         old_div;
	u32			cmdr;   //the value setted into command-register
	u32			dodma;  //sign the DMA used for transfer.
	u32         errorstep;//record the error point.
	int         timeout_times;  //use to force close the sdmmc0 when the timeout_times exceeds the limit.
	u32         *pbuf;
	SDC_INT_INFO_T    intInfo; 
    struct rk29_sdmmc_dma_info 	dma_info;
    int         irq;
	int error_times;
	u32 old_cmd;
	
	struct tasklet_struct	tasklet;
	unsigned long		pending_events;
	unsigned long		completed_events;
	enum rk29_sdmmc_state	state;

#ifdef RK29_SDMMC_LIST_QUEUE
	struct list_head	queue;
	struct list_head	queue_node;
#endif

	u32			bus_hz;
	struct platform_device	*pdev;
	struct mmc_host		*mmc;
	u32			ctype;
	unsigned int		clock;
	unsigned long		flags;
	
#define RK29_SDMMC_CARD_PRESENT	0

	int			id;

	struct timer_list	detect_timer; 
	struct timer_list	request_timer; //the timer for INT_CMD_DONE
	struct timer_list	DTO_timer;     //the timer for INT_DTO
	struct mmc_command	stopcmd;
    struct rksdmmc_gpio det_pin;

	/* flag for current bus settings */
    u32 bus_mode;

    unsigned int            oldstatus;
    unsigned int            complete_done;
    unsigned int            retryfunc;
    
    int gpio_irq;
	int gpio_power_en;
	int gpio_power_en_level;
	struct delayed_work		work;

#ifdef CONFIG_RK29_SDIO_IRQ_FROM_GPIO
    unsigned int sdio_INT_gpio;
    unsigned int sdio_irq;
    unsigned long trigger_level;
#endif

#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT) || defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
    int write_protect;
    int protect_level;
#endif

	bool			irq_state;
    void (*set_iomux)(int device_id, unsigned int bus_width);

    /* FIFO push and pull */
    int         data_shift;
    void (*push_data)(struct rk29_sdmmc *host, void *buf, int cnt);
    void (*pull_data)(struct rk29_sdmmc *host, void *buf, int cnt);
};


#ifdef RK29_SDMMC_NOTIFY_REMOVE_INSERTION
static struct rk29_sdmmc    *globalSDhost[3];
#endif

#define rk29_sdmmc_test_and_clear_pending(host, event)		\
	test_and_clear_bit(event, &host->pending_events)
#define rk29_sdmmc_test_pending(host, event)		\
	test_bit(event, &host->pending_events)
#define rk29_sdmmc_test_completed(host, event)           \
        test_bit(event, &host->completed_events)       
#define rk29_sdmmc_set_completed(host, event)			\
	set_bit(event, &host->completed_events)
#define rk29_sdmmc_set_pending(host, event)				\
	set_bit(event, &host->pending_events)



#endif
