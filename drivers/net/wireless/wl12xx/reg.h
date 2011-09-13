/*
 * This file is part of wl12xx
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __REG_H__
#define __REG_H__

#include <linux/bitops.h>

#define REGISTERS_BASE 0x00300000
#define DRPW_BASE      0x00310000

#define REGISTERS_DOWN_SIZE 0x00008800
#define REGISTERS_WORK_SIZE 0x0000b000

#define HW_ACCESS_ELP_CTRL_REG_ADDR         0x1FFFC
#define FW_STATUS_ADDR                      (0x14FC0 + 0xA000)

/* ELP register commands */
#define ELPCTRL_WAKE_UP             0x1
#define ELPCTRL_WAKE_UP_WLAN_READY  0x5
#define ELPCTRL_SLEEP               0x0
/* ELP WLAN_READY bit */
#define ELPCTRL_WLAN_READY          0x2

/*===============================================
   Host Software Reset - 32bit RW
 ------------------------------------------
    [31:1] Reserved
    0  SOFT_RESET Soft Reset  - When this bit is set,
    it holds the Wlan hardware in a soft reset state.
    This reset disables all MAC and baseband processor
    clocks except the CardBus/PCI interface clock.
    It also initializes all MAC state machines except
    the host interface. It does not reload the
    contents of the EEPROM. When this bit is cleared
    (not self-clearing), the Wlan hardware
    exits the software reset state.
===============================================*/
#define ACX_REG_SLV_SOFT_RESET         (REGISTERS_BASE + 0x0000)

#define WL1271_SLV_REG_DATA            (REGISTERS_BASE + 0x0008)
#define WL1271_SLV_REG_ADATA           (REGISTERS_BASE + 0x000c)
#define WL1271_SLV_MEM_DATA            (REGISTERS_BASE + 0x0018)

#define ACX_REG_INTERRUPT_TRIG         (REGISTERS_BASE + 0x0474)
#define ACX_REG_INTERRUPT_TRIG_H       (REGISTERS_BASE + 0x0478)

/*=============================================
  Host Interrupt Mask Register - 32bit (RW)
  ------------------------------------------
  Setting a bit in this register masks the
  corresponding interrupt to the host.
  0 - RX0		- Rx first dubble buffer Data Interrupt
  1 - TXD		- Tx Data Interrupt
  2 - TXXFR		- Tx Transfer Interrupt
  3 - RX1		- Rx second dubble buffer Data Interrupt
  4 - RXXFR		- Rx Transfer Interrupt
  5 - EVENT_A	- Event Mailbox interrupt
  6 - EVENT_B	- Event Mailbox interrupt
  7 - WNONHST	- Wake On Host Interrupt
  8 - TRACE_A	- Debug Trace interrupt
  9 - TRACE_B	- Debug Trace interrupt
 10 - CDCMP		- Command Complete Interrupt
 11 -
 12 -
 13 -
 14 - ICOMP		- Initialization Complete Interrupt
 16 - SG SE		- Soft Gemini - Sense enable interrupt
 17 - SG SD		- Soft Gemini - Sense disable interrupt
 18 -			-
 19 -			-
 20 -			-
 21-			-
 Default: 0x0001
*==============================================*/
#define ACX_REG_INTERRUPT_MASK         (REGISTERS_BASE + 0x04DC)

/*=============================================
  Host Interrupt Mask Set 16bit, (Write only)
  ------------------------------------------
 Setting a bit in this register sets
 the corresponding bin in ACX_HINT_MASK register
 without effecting the mask
 state of other bits (0 = no effect).
==============================================*/
#define ACX_REG_HINT_MASK_SET          (REGISTERS_BASE + 0x04E0)

/*=============================================
  Host Interrupt Mask Clear 16bit,(Write only)
  ------------------------------------------
 Setting a bit in this register clears
 the corresponding bin in ACX_HINT_MASK register
 without effecting the mask
 state of other bits (0 = no effect).
=============================================*/
#define ACX_REG_HINT_MASK_CLR          (REGISTERS_BASE + 0x04E4)

/*=============================================
  Host Interrupt Status Nondestructive Read
  16bit,(Read only)
  ------------------------------------------
 The host can read this register to determine
 which interrupts are active.
 Reading this register doesn't
 effect its content.
=============================================*/
#define ACX_REG_INTERRUPT_NO_CLEAR     (REGISTERS_BASE + 0x04E8)

/*=============================================
  Host Interrupt Status Clear on Read  Register
  16bit,(Read only)
  ------------------------------------------
 The host can read this register to determine
 which interrupts are active.
 Reading this register clears it,
 thus making all interrupts inactive.
==============================================*/
#define ACX_REG_INTERRUPT_CLEAR        (REGISTERS_BASE + 0x04F8)

/*=============================================
  Host Interrupt Acknowledge Register
  16bit,(Write only)
  ------------------------------------------
 The host can set individual bits in this
 register to clear (acknowledge) the corresp.
 interrupt status bits in the HINT_STS_CLR and
 HINT_STS_ND registers, thus making the
 assotiated interrupt inactive. (0-no effect)
==============================================*/
#define ACX_REG_INTERRUPT_ACK          (REGISTERS_BASE + 0x04F0)

#define RX_DRIVER_COUNTER_ADDRESS      (REGISTERS_BASE + 0x0538)

/* Device Configuration registers*/
#define SOR_CFG                        (REGISTERS_BASE + 0x0800)

/* Embedded ARM CPU Control */

/*===============================================
 Halt eCPU   - 32bit RW
 ------------------------------------------
 0 HALT_ECPU Halt Embedded CPU - This bit is the
 compliment of bit 1 (MDATA2) in the SOR_CFG register.
 During a hardware reset, this bit holds
 the inverse of MDATA2.
 When downloading firmware from the host,
 set this bit (pull down MDATA2).
 The host clears this bit after downloading the firmware into
 zero-wait-state SSRAM.
 When loading firmware from Flash, clear this bit (pull up MDATA2)
 so that the eCPU can run the bootloader code in Flash
 HALT_ECPU eCPU State
 --------------------
 1 halt eCPU
 0 enable eCPU
 ===============================================*/
#define ACX_REG_ECPU_CONTROL           (REGISTERS_BASE + 0x0804)

#define HI_CFG                         (REGISTERS_BASE + 0x0808)

/*===============================================
 EEPROM Burst Read Start  - 32bit RW
 ------------------------------------------
 [31:1] Reserved
 0  ACX_EE_START -  EEPROM Burst Read Start 0
 Setting this bit starts a burst read from
 the external EEPROM.
 If this bit is set (after reset) before an EEPROM read/write,
 the burst read starts at EEPROM address 0.
 Otherwise, it starts at the address
 following the address of the previous access.
 TheWlan hardware hardware clears this bit automatically.

 Default: 0x00000000
*================================================*/
#define ACX_REG_EE_START               (REGISTERS_BASE + 0x080C)

#define OCP_POR_CTR                    (REGISTERS_BASE + 0x09B4)
#define OCP_DATA_WRITE                 (REGISTERS_BASE + 0x09B8)
#define OCP_DATA_READ                  (REGISTERS_BASE + 0x09BC)
#define OCP_CMD                        (REGISTERS_BASE + 0x09C0)

#define WL1271_HOST_WR_ACCESS          (REGISTERS_BASE + 0x09F8)

#define CHIP_ID_B                      (REGISTERS_BASE + 0x5674)

#define CHIP_ID_1271_PG10              (0x4030101)
#define CHIP_ID_1271_PG20              (0x4030111)
#define CHIP_ID_1283_PG10              (0x05030101)
#define CHIP_ID_1283_PG20              (0x05030111)

#define ENABLE                         (REGISTERS_BASE + 0x5450)

/* Power Management registers */
#define ELP_CFG_MODE                   (REGISTERS_BASE + 0x5804)
#define ELP_CMD                        (REGISTERS_BASE + 0x5808)
#define PLL_CAL_TIME                   (REGISTERS_BASE + 0x5810)
#define CLK_REQ_TIME                   (REGISTERS_BASE + 0x5814)
#define CLK_BUF_TIME                   (REGISTERS_BASE + 0x5818)

#define CFG_PLL_SYNC_CNT               (REGISTERS_BASE + 0x5820)

/* Scratch Pad registers*/
#define SCR_PAD0                       (REGISTERS_BASE + 0x5608)
#define SCR_PAD1                       (REGISTERS_BASE + 0x560C)
#define SCR_PAD2                       (REGISTERS_BASE + 0x5610)
#define SCR_PAD3                       (REGISTERS_BASE + 0x5614)
#define SCR_PAD4                       (REGISTERS_BASE + 0x5618)
#define SCR_PAD4_SET                   (REGISTERS_BASE + 0x561C)
#define SCR_PAD4_CLR                   (REGISTERS_BASE + 0x5620)
#define SCR_PAD5                       (REGISTERS_BASE + 0x5624)
#define SCR_PAD5_SET                   (REGISTERS_BASE + 0x5628)
#define SCR_PAD5_CLR                   (REGISTERS_BASE + 0x562C)
#define SCR_PAD6                       (REGISTERS_BASE + 0x5630)
#define SCR_PAD7                       (REGISTERS_BASE + 0x5634)
#define SCR_PAD8                       (REGISTERS_BASE + 0x5638)
#define SCR_PAD9                       (REGISTERS_BASE + 0x563C)

/* Spare registers*/
#define SPARE_A1                       (REGISTERS_BASE + 0x0994)
#define SPARE_A2                       (REGISTERS_BASE + 0x0998)
#define SPARE_A3                       (REGISTERS_BASE + 0x099C)
#define SPARE_A4                       (REGISTERS_BASE + 0x09A0)
#define SPARE_A5                       (REGISTERS_BASE + 0x09A4)
#define SPARE_A6                       (REGISTERS_BASE + 0x09A8)
#define SPARE_A7                       (REGISTERS_BASE + 0x09AC)
#define SPARE_A8                       (REGISTERS_BASE + 0x09B0)
#define SPARE_B1                       (REGISTERS_BASE + 0x5420)
#define SPARE_B2                       (REGISTERS_BASE + 0x5424)
#define SPARE_B3                       (REGISTERS_BASE + 0x5428)
#define SPARE_B4                       (REGISTERS_BASE + 0x542C)
#define SPARE_B5                       (REGISTERS_BASE + 0x5430)
#define SPARE_B6                       (REGISTERS_BASE + 0x5434)
#define SPARE_B7                       (REGISTERS_BASE + 0x5438)
#define SPARE_B8                       (REGISTERS_BASE + 0x543C)

#define PLL_PARAMETERS                 (REGISTERS_BASE + 0x6040)
#define WU_COUNTER_PAUSE               (REGISTERS_BASE + 0x6008)
#define WELP_ARM_COMMAND               (REGISTERS_BASE + 0x6100)
#define DRPW_SCRATCH_START             (DRPW_BASE + 0x002C)


#define ACX_SLV_SOFT_RESET_BIT   BIT(1)
#define ACX_REG_EEPROM_START_BIT BIT(1)

/* Command/Information Mailbox Pointers */

/*===============================================
  Command Mailbox Pointer - 32bit RW
 ------------------------------------------
 This register holds the start address of
 the command mailbox located in the Wlan hardware memory.
 The host must read this pointer after a reset to
 find the location of the command mailbox.
 The Wlan hardware initializes the command mailbox
 pointer with the default address of the command mailbox.
 The command mailbox pointer is not valid until after
 the host receives the Init Complete interrupt from
 the Wlan hardware.
 ===============================================*/
#define REG_COMMAND_MAILBOX_PTR				(SCR_PAD0)

/*===============================================
  Information Mailbox Pointer - 32bit RW
 ------------------------------------------
 This register holds the start address of
 the information mailbox located in the Wlan hardware memory.
 The host must read this pointer after a reset to find
 the location of the information mailbox.
 The Wlan hardware initializes the information mailbox pointer
 with the default address of the information mailbox.
 The information mailbox pointer is not valid
 until after the host receives the Init Complete interrupt from
 the Wlan hardware.
 ===============================================*/
#define REG_EVENT_MAILBOX_PTR				(SCR_PAD1)

/*===============================================
 EEPROM Read/Write Request 32bit RW
 ------------------------------------------
 1 EE_READ - EEPROM Read Request 1 - Setting this bit
 loads a single byte of data into the EE_DATA
 register from the EEPROM location specified in
 the EE_ADDR register.
 The Wlan hardware hardware clears this bit automatically.
 EE_DATA is valid when this bit is cleared.

 0 EE_WRITE  - EEPROM Write Request  - Setting this bit
 writes a single byte of data from the EE_DATA register into the
 EEPROM location specified in the EE_ADDR register.
 The Wlan hardware hardware clears this bit automatically.
*===============================================*/
#define ACX_EE_CTL_REG                      EE_CTL
#define EE_WRITE                            0x00000001ul
#define EE_READ                             0x00000002ul

/*===============================================
  EEPROM Address  - 32bit RW
  ------------------------------------------
  This register specifies the address
  within the EEPROM from/to which to read/write data.
  ===============================================*/
#define ACX_EE_ADDR_REG                     EE_ADDR

/*===============================================
  EEPROM Data  - 32bit RW
  ------------------------------------------
  This register either holds the read 8 bits of
  data from the EEPROM or the write data
  to be written to the EEPROM.
  ===============================================*/
#define ACX_EE_DATA_REG                     EE_DATA

/*===============================================
  EEPROM Base Address  - 32bit RW
  ------------------------------------------
  This register holds the upper nine bits
  [23:15] of the 24-bit Wlan hardware memory
  address for burst reads from EEPROM accesses.
  The EEPROM provides the lower 15 bits of this address.
  The MSB of the address from the EEPROM is ignored.
  ===============================================*/
#define ACX_EE_CFG                          EE_CFG

/*===============================================
  GPIO Output Values  -32bit, RW
  ------------------------------------------
  [31:16]  Reserved
  [15: 0]  Specify the output values (at the output driver inputs) for
  GPIO[15:0], respectively.
  ===============================================*/
#define ACX_GPIO_OUT_REG            GPIO_OUT
#define ACX_MAX_GPIO_LINES          15

/*===============================================
  Contention window  -32bit, RW
  ------------------------------------------
  [31:26]  Reserved
  [25:16]  Max (0x3ff)
  [15:07]  Reserved
  [06:00]  Current contention window value - default is 0x1F
  ===============================================*/
#define ACX_CONT_WIND_CFG_REG    CONT_WIND_CFG
#define ACX_CONT_WIND_MIN_MASK   0x0000007f
#define ACX_CONT_WIND_MAX        0x03ff0000

/*===============================================
  HI_CFG Interface Configuration Register Values
  ------------------------------------------
  ===============================================*/
#define HI_CFG_UART_ENABLE          0x00000004
#define HI_CFG_RST232_ENABLE        0x00000008
#define HI_CFG_CLOCK_REQ_SELECT     0x00000010
#define HI_CFG_HOST_INT_ENABLE      0x00000020
#define HI_CFG_VLYNQ_OUTPUT_ENABLE  0x00000040
#define HI_CFG_HOST_INT_ACTIVE_LOW  0x00000080
#define HI_CFG_UART_TX_OUT_GPIO_15  0x00000100
#define HI_CFG_UART_TX_OUT_GPIO_14  0x00000200
#define HI_CFG_UART_TX_OUT_GPIO_7   0x00000400

#define HI_CFG_DEF_VAL              \
	(HI_CFG_UART_ENABLE |        \
	HI_CFG_RST232_ENABLE |      \
	HI_CFG_CLOCK_REQ_SELECT |   \
	HI_CFG_HOST_INT_ENABLE)

#define REF_FREQ_19_2                       0
#define REF_FREQ_26_0                       1
#define REF_FREQ_38_4                       2
#define REF_FREQ_40_0                       3
#define REF_FREQ_33_6                       4
#define REF_FREQ_NUM                        5

#define LUT_PARAM_INTEGER_DIVIDER           0
#define LUT_PARAM_FRACTIONAL_DIVIDER        1
#define LUT_PARAM_ATTN_BB                   2
#define LUT_PARAM_ALPHA_BB                  3
#define LUT_PARAM_STOP_TIME_BB              4
#define LUT_PARAM_BB_PLL_LOOP_FILTER        5
#define LUT_PARAM_NUM                       6

#define ACX_EEPROMLESS_IND_REG              (SCR_PAD4)
#define USE_EEPROM                          0
#define SOFT_RESET_MAX_TIME                 1000000
#define SOFT_RESET_STALL_TIME               1000
#define NVS_DATA_BUNDARY_ALIGNMENT          4


/* Firmware image load chunk size */
#define CHUNK_SIZE          512

/* Firmware image header size */
#define FW_HDR_SIZE 8

#define ECPU_CONTROL_HALT					0x00000101


/******************************************************************************

    CHANNELS, BAND & REG DOMAINS definitions

******************************************************************************/


enum {
	RADIO_BAND_2_4GHZ = 0,  /* 2.4 Ghz band */
	RADIO_BAND_5GHZ = 1,    /* 5 Ghz band */
	RADIO_BAND_JAPAN_4_9_GHZ = 2,
	DEFAULT_BAND = RADIO_BAND_2_4GHZ,
	INVALID_BAND = 0xFE,
	MAX_RADIO_BANDS = 0xFF
};

#define SHORT_PREAMBLE_BIT   BIT(0) /* CCK or Barker depending on the rate */
#define OFDM_RATE_BIT        BIT(6)
#define PBCC_RATE_BIT        BIT(7)

enum {
	CCK_LONG = 0,
	CCK_SHORT = SHORT_PREAMBLE_BIT,
	PBCC_LONG = PBCC_RATE_BIT,
	PBCC_SHORT = PBCC_RATE_BIT | SHORT_PREAMBLE_BIT,
	OFDM = OFDM_RATE_BIT
};

/******************************************************************************

Transmit-Descriptor RATE-SET field definitions...

Define a new "Rate-Set" for TX path that incorporates the
Rate & Modulation info into a single 16-bit field.

TxdRateSet_t:
b15   - Indicates Preamble type (1=SHORT, 0=LONG).
	Notes:
	Must be LONG (0) for 1Mbps rate.
	Does not apply (set to 0) for RevG-OFDM rates.
b14   - Indicates PBCC encoding (1=PBCC, 0=not).
	Notes:
	Does not apply (set to 0) for rates 1 and 2 Mbps.
	Does not apply (set to 0) for RevG-OFDM rates.
b13    - Unused (set to 0).
b12-b0 - Supported Rate indicator bits as defined below.

******************************************************************************/


/*************************************************************************

    Interrupt Trigger Register (Host -> WiLink)

**************************************************************************/

/* Hardware to Embedded CPU Interrupts - first 32-bit register set */

/*
 * Host Command Interrupt. Setting this bit masks
 * the interrupt that the host issues to inform
 * the FW that it has sent a command
 * to the Wlan hardware Command Mailbox.
 */
#define INTR_TRIG_CMD       BIT(0)

/*
 * Host Event Acknowlegde Interrupt. The host
 * sets this bit to acknowledge that it received
 * the unsolicited information from the event
 * mailbox.
 */
#define INTR_TRIG_EVENT_ACK BIT(1)

/*
 * The host sets this bit to inform the Wlan
 * FW that a TX packet is in the XFER
 * Buffer #0.
 */
#define INTR_TRIG_TX_PROC0 BIT(2)

/*
 * The host sets this bit to inform the FW
 * that it read a packet from RX XFER
 * Buffer #0.
 */
#define INTR_TRIG_RX_PROC0 BIT(3)

#define INTR_TRIG_DEBUG_ACK BIT(4)

#define INTR_TRIG_STATE_CHANGED BIT(5)


/* Hardware to Embedded CPU Interrupts - second 32-bit register set */

/*
 * The host sets this bit to inform the FW
 * that it read a packet from RX XFER
 * Buffer #1.
 */
#define INTR_TRIG_RX_PROC1 BIT(17)

/*
 * The host sets this bit to inform the Wlan
 * hardware that a TX packet is in the XFER
 * Buffer #1.
 */
#define INTR_TRIG_TX_PROC1 BIT(18)

#endif
