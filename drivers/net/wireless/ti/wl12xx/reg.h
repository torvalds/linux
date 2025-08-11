/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * This file is part of wl12xx
 *
 * Copyright (C) 1998-2009 Texas Instruments. All rights reserved.
 * Copyright (C) 2009 Nokia Corporation
 *
 * Contact: Luciano Coelho <luciano.coelho@nokia.com>
 */

#ifndef __REG_H__
#define __REG_H__

#include <linux/bitops.h>

#define REGISTERS_BASE 0x00300000
#define DRPW_BASE      0x00310000

#define REGISTERS_DOWN_SIZE 0x00008800
#define REGISTERS_WORK_SIZE 0x0000b000

#define FW_STATUS_ADDR                      (0x14FC0 + 0xA000)

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
#define WL12XX_SLV_SOFT_RESET		(REGISTERS_BASE + 0x0000)

#define WL1271_SLV_REG_DATA            (REGISTERS_BASE + 0x0008)
#define WL1271_SLV_REG_ADATA           (REGISTERS_BASE + 0x000c)
#define WL1271_SLV_MEM_DATA            (REGISTERS_BASE + 0x0018)

#define WL12XX_REG_INTERRUPT_TRIG         (REGISTERS_BASE + 0x0474)
#define WL12XX_REG_INTERRUPT_TRIG_H       (REGISTERS_BASE + 0x0478)

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
#define WL12XX_REG_INTERRUPT_MASK         (REGISTERS_BASE + 0x04DC)

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
#define WL12XX_REG_INTERRUPT_NO_CLEAR     (REGISTERS_BASE + 0x04E8)

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
#define WL12XX_REG_INTERRUPT_ACK          (REGISTERS_BASE + 0x04F0)

#define WL12XX_REG_RX_DRIVER_COUNTER	(REGISTERS_BASE + 0x0538)

/* Device Configuration registers*/
#define SOR_CFG                        (REGISTERS_BASE + 0x0800)

/* Embedded ARM CPU Control */

/*===============================================
 Halt eCPU   - 32bit RW
 ------------------------------------------
 0 HALT_ECPU Halt Embedded CPU - This bit is the
 complement of bit 1 (MDATA2) in the SOR_CFG register.
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
#define WL12XX_REG_ECPU_CONTROL           (REGISTERS_BASE + 0x0804)

#define WL12XX_HI_CFG			(REGISTERS_BASE + 0x0808)

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
 TheWlan hardware clears this bit automatically.

 Default: 0x00000000
*================================================*/
#define ACX_REG_EE_START               (REGISTERS_BASE + 0x080C)

#define WL12XX_OCP_POR_CTR		(REGISTERS_BASE + 0x09B4)
#define WL12XX_OCP_DATA_WRITE		(REGISTERS_BASE + 0x09B8)
#define WL12XX_OCP_DATA_READ		(REGISTERS_BASE + 0x09BC)
#define WL12XX_OCP_CMD			(REGISTERS_BASE + 0x09C0)

#define WL12XX_HOST_WR_ACCESS		(REGISTERS_BASE + 0x09F8)

#define WL12XX_CHIP_ID_B		(REGISTERS_BASE + 0x5674)

#define WL12XX_ENABLE			(REGISTERS_BASE + 0x5450)

/* Power Management registers */
#define WL12XX_ELP_CFG_MODE		(REGISTERS_BASE + 0x5804)
#define WL12XX_ELP_CMD			(REGISTERS_BASE + 0x5808)
#define WL12XX_PLL_CAL_TIME		(REGISTERS_BASE + 0x5810)
#define WL12XX_CLK_REQ_TIME		(REGISTERS_BASE + 0x5814)
#define WL12XX_CLK_BUF_TIME		(REGISTERS_BASE + 0x5818)

#define WL12XX_CFG_PLL_SYNC_CNT		(REGISTERS_BASE + 0x5820)

/* Scratch Pad registers*/
#define WL12XX_SCR_PAD0			(REGISTERS_BASE + 0x5608)
#define WL12XX_SCR_PAD1			(REGISTERS_BASE + 0x560C)
#define WL12XX_SCR_PAD2			(REGISTERS_BASE + 0x5610)
#define WL12XX_SCR_PAD3			(REGISTERS_BASE + 0x5614)
#define WL12XX_SCR_PAD4			(REGISTERS_BASE + 0x5618)
#define WL12XX_SCR_PAD4_SET		(REGISTERS_BASE + 0x561C)
#define WL12XX_SCR_PAD4_CLR		(REGISTERS_BASE + 0x5620)
#define WL12XX_SCR_PAD5			(REGISTERS_BASE + 0x5624)
#define WL12XX_SCR_PAD5_SET		(REGISTERS_BASE + 0x5628)
#define WL12XX_SCR_PAD5_CLR		(REGISTERS_BASE + 0x562C)
#define WL12XX_SCR_PAD6			(REGISTERS_BASE + 0x5630)
#define WL12XX_SCR_PAD7			(REGISTERS_BASE + 0x5634)
#define WL12XX_SCR_PAD8			(REGISTERS_BASE + 0x5638)
#define WL12XX_SCR_PAD9			(REGISTERS_BASE + 0x563C)

/* Spare registers*/
#define WL12XX_SPARE_A1			(REGISTERS_BASE + 0x0994)
#define WL12XX_SPARE_A2			(REGISTERS_BASE + 0x0998)
#define WL12XX_SPARE_A3			(REGISTERS_BASE + 0x099C)
#define WL12XX_SPARE_A4			(REGISTERS_BASE + 0x09A0)
#define WL12XX_SPARE_A5			(REGISTERS_BASE + 0x09A4)
#define WL12XX_SPARE_A6			(REGISTERS_BASE + 0x09A8)
#define WL12XX_SPARE_A7			(REGISTERS_BASE + 0x09AC)
#define WL12XX_SPARE_A8			(REGISTERS_BASE + 0x09B0)
#define WL12XX_SPARE_B1			(REGISTERS_BASE + 0x5420)
#define WL12XX_SPARE_B2			(REGISTERS_BASE + 0x5424)
#define WL12XX_SPARE_B3			(REGISTERS_BASE + 0x5428)
#define WL12XX_SPARE_B4			(REGISTERS_BASE + 0x542C)
#define WL12XX_SPARE_B5			(REGISTERS_BASE + 0x5430)
#define WL12XX_SPARE_B6			(REGISTERS_BASE + 0x5434)
#define WL12XX_SPARE_B7			(REGISTERS_BASE + 0x5438)
#define WL12XX_SPARE_B8			(REGISTERS_BASE + 0x543C)

#define WL12XX_PLL_PARAMETERS		(REGISTERS_BASE + 0x6040)
#define WL12XX_WU_COUNTER_PAUSE		(REGISTERS_BASE + 0x6008)
#define WL12XX_WELP_ARM_COMMAND		(REGISTERS_BASE + 0x6100)
#define WL12XX_DRPW_SCRATCH_START	(DRPW_BASE + 0x002C)

#define WL12XX_CMD_MBOX_ADDRESS		0x407B4

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
#define WL12XX_REG_COMMAND_MAILBOX_PTR		(WL12XX_SCR_PAD0)

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
#define WL12XX_REG_EVENT_MAILBOX_PTR		(WL12XX_SCR_PAD1)

/*===============================================
 EEPROM Read/Write Request 32bit RW
 ------------------------------------------
 1 EE_READ - EEPROM Read Request 1 - Setting this bit
 loads a single byte of data into the EE_DATA
 register from the EEPROM location specified in
 the EE_ADDR register.
 The Wlan hardware clears this bit automatically.
 EE_DATA is valid when this bit is cleared.

 0 EE_WRITE  - EEPROM Write Request  - Setting this bit
 writes a single byte of data from the EE_DATA register into the
 EEPROM location specified in the EE_ADDR register.
 The Wlan hardware clears this bit automatically.
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

#define WL12XX_EEPROMLESS_IND		(WL12XX_SCR_PAD4)
#define USE_EEPROM                          0
#define NVS_DATA_BUNDARY_ALIGNMENT          4

/* Firmware image header size */
#define FW_HDR_SIZE 8

/******************************************************************************

    CHANNELS, BAND & REG DOMAINS definitions

******************************************************************************/

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

#define OCP_CMD_LOOP		32
#define OCP_CMD_WRITE		0x1
#define OCP_CMD_READ		0x2
#define OCP_READY_MASK		BIT(18)
#define OCP_STATUS_MASK		(BIT(16) | BIT(17))
#define OCP_STATUS_NO_RESP	0x00000
#define OCP_STATUS_OK		0x10000
#define OCP_STATUS_REQ_FAILED	0x20000
#define OCP_STATUS_RESP_ERROR	0x30000

#define OCP_REG_POLARITY     0x0064
#define OCP_REG_CLK_TYPE     0x0448
#define OCP_REG_CLK_POLARITY 0x0cb2
#define OCP_REG_CLK_PULL     0x0cb4

#define POLARITY_LOW         BIT(1)
#define NO_PULL              (BIT(14) | BIT(15))

#define FREF_CLK_TYPE_BITS     0xfffffe7f
#define CLK_REQ_PRCM           0x100
#define FREF_CLK_POLARITY_BITS 0xfffff8ff
#define CLK_REQ_OUTN_SEL       0x700

#define WU_COUNTER_PAUSE_VAL 0x3FF

/* PLL configuration algorithm for wl128x */
#define SYS_CLK_CFG_REG              0x2200
/* Bit[0]   -  0-TCXO,  1-FREF */
#define MCS_PLL_CLK_SEL_FREF         BIT(0)
/* Bit[3:2] - 01-TCXO, 10-FREF */
#define WL_CLK_REQ_TYPE_FREF         BIT(3)
#define WL_CLK_REQ_TYPE_PG2          (BIT(3) | BIT(2))
/* Bit[4]   -  0-TCXO,  1-FREF */
#define PRCM_CM_EN_MUX_WLAN_FREF     BIT(4)

#define TCXO_ILOAD_INT_REG           0x2264
#define TCXO_CLK_DETECT_REG          0x2266

#define TCXO_DET_FAILED              BIT(4)

#define FREF_ILOAD_INT_REG           0x2084
#define FREF_CLK_DETECT_REG          0x2086
#define FREF_CLK_DETECT_FAIL         BIT(4)

/* Use this reg for masking during driver access */
#define WL_SPARE_REG                 0x2320
#define WL_SPARE_VAL                 BIT(2)
/* Bit[6:5:3] -  mask wl write SYS_CLK_CFG[8:5:2:4] */
#define WL_SPARE_MASK_8526           (BIT(6) | BIT(5) | BIT(3))

#define PLL_LOCK_COUNTERS_REG        0xD8C
#define PLL_LOCK_COUNTERS_COEX       0x0F
#define PLL_LOCK_COUNTERS_MCS        0xF0
#define MCS_PLL_OVERRIDE_REG         0xD90
#define MCS_PLL_CONFIG_REG           0xD92
#define MCS_SEL_IN_FREQ_MASK         0x0070
#define MCS_SEL_IN_FREQ_SHIFT        4
#define MCS_PLL_CONFIG_REG_VAL       0x73
#define MCS_PLL_ENABLE_HP            (BIT(0) | BIT(1))

#define MCS_PLL_M_REG                0xD94
#define MCS_PLL_N_REG                0xD96
#define MCS_PLL_M_REG_VAL            0xC8
#define MCS_PLL_N_REG_VAL            0x07

#define SDIO_IO_DS                   0xd14

/* SDIO/wSPI DS configuration values */
enum {
	HCI_IO_DS_8MA = 0,
	HCI_IO_DS_4MA = 1, /* default */
	HCI_IO_DS_6MA = 2,
	HCI_IO_DS_2MA = 3,
};

/* end PLL configuration algorithm for wl128x */

/*
 * Host Command Interrupt. Setting this bit masks
 * the interrupt that the host issues to inform
 * the FW that it has sent a command
 * to the Wlan hardware Command Mailbox.
 */
#define WL12XX_INTR_TRIG_CMD		BIT(0)

/*
 * Host Event Acknowlegde Interrupt. The host
 * sets this bit to acknowledge that it received
 * the unsolicited information from the event
 * mailbox.
 */
#define WL12XX_INTR_TRIG_EVENT_ACK	BIT(1)

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

#define WL127X_REG_FUSE_DATA_2_1	0x050a
#define WL128X_REG_FUSE_DATA_2_1	0x2152
#define PG_VER_MASK			0x3c
#define PG_VER_OFFSET			2

#define WL127X_PG_MAJOR_VER_MASK	0x3
#define WL127X_PG_MAJOR_VER_OFFSET	0x0
#define WL127X_PG_MINOR_VER_MASK	0xc
#define WL127X_PG_MINOR_VER_OFFSET	0x2

#define WL128X_PG_MAJOR_VER_MASK	0xc
#define WL128X_PG_MAJOR_VER_OFFSET	0x2
#define WL128X_PG_MINOR_VER_MASK	0x3
#define WL128X_PG_MINOR_VER_OFFSET	0x0

#define WL127X_PG_GET_MAJOR(pg_ver) ((pg_ver & WL127X_PG_MAJOR_VER_MASK) >> \
				     WL127X_PG_MAJOR_VER_OFFSET)
#define WL127X_PG_GET_MINOR(pg_ver) ((pg_ver & WL127X_PG_MINOR_VER_MASK) >> \
				     WL127X_PG_MINOR_VER_OFFSET)
#define WL128X_PG_GET_MAJOR(pg_ver) ((pg_ver & WL128X_PG_MAJOR_VER_MASK) >> \
				     WL128X_PG_MAJOR_VER_OFFSET)
#define WL128X_PG_GET_MINOR(pg_ver) ((pg_ver & WL128X_PG_MINOR_VER_MASK) >> \
				     WL128X_PG_MINOR_VER_OFFSET)

#define WL12XX_REG_FUSE_BD_ADDR_1	0x00310eb4
#define WL12XX_REG_FUSE_BD_ADDR_2	0x00310eb8

#endif
