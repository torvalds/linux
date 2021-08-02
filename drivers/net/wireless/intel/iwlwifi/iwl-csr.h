/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014, 2018-2021 Intel Corporation
 * Copyright (C) 2013-2014 Intel Mobile Communications GmbH
 * Copyright (C) 2016 Intel Deutschland GmbH
 */
#ifndef __iwl_csr_h__
#define __iwl_csr_h__
/*
 * CSR (control and status registers)
 *
 * CSR registers are mapped directly into PCI bus space, and are accessible
 * whenever platform supplies power to device, even when device is in
 * low power states due to driver-invoked device resets
 * (e.g. CSR_RESET_REG_FLAG_SW_RESET) or uCode-driven power-saving modes.
 *
 * Use iwl_write32() and iwl_read32() family to access these registers;
 * these provide simple PCI bus access, without waking up the MAC.
 * Do not use iwl_write_direct32() family for these registers;
 * no need to "grab nic access" via CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ.
 * The MAC (uCode processor, etc.) does not need to be powered up for accessing
 * the CSR registers.
 *
 * NOTE:  Device does need to be awake in order to read this memory
 *        via CSR_EEPROM and CSR_OTP registers
 */
#define CSR_BASE    (0x000)

#define CSR_HW_IF_CONFIG_REG    (CSR_BASE+0x000) /* hardware interface config */
#define CSR_INT_COALESCING      (CSR_BASE+0x004) /* accum ints, 32-usec units */
#define CSR_INT                 (CSR_BASE+0x008) /* host interrupt status/ack */
#define CSR_INT_MASK            (CSR_BASE+0x00c) /* host interrupt enable */
#define CSR_FH_INT_STATUS       (CSR_BASE+0x010) /* busmaster int status/ack*/
#define CSR_GPIO_IN             (CSR_BASE+0x018) /* read external chip pins */
#define CSR_RESET               (CSR_BASE+0x020) /* busmaster enable, NMI, etc*/
#define CSR_GP_CNTRL            (CSR_BASE+0x024)

/* 2nd byte of CSR_INT_COALESCING, not accessible via iwl_write32()! */
#define CSR_INT_PERIODIC_REG	(CSR_BASE+0x005)

/*
 * Hardware revision info
 * Bit fields:
 * 31-16:  Reserved
 *  15-4:  Type of device:  see CSR_HW_REV_TYPE_xxx definitions
 *  3-2:  Revision step:  0 = A, 1 = B, 2 = C, 3 = D
 *  1-0:  "Dash" (-) value, as in A-1, etc.
 */
#define CSR_HW_REV              (CSR_BASE+0x028)

/*
 * RF ID revision info
 * Bit fields:
 * 31:24: Reserved (set to 0x0)
 * 23:12: Type
 * 11:8:  Step (A - 0x0, B - 0x1, etc)
 * 7:4:   Dash
 * 3:0:   Flavor
 */
#define CSR_HW_RF_ID		(CSR_BASE+0x09c)

/*
 * EEPROM and OTP (one-time-programmable) memory reads
 *
 * NOTE:  Device must be awake, initialized via apm_ops.init(),
 *        in order to read.
 */
#define CSR_EEPROM_REG          (CSR_BASE+0x02c)
#define CSR_EEPROM_GP           (CSR_BASE+0x030)
#define CSR_OTP_GP_REG   	(CSR_BASE+0x034)

#define CSR_GIO_REG		(CSR_BASE+0x03C)
#define CSR_GP_UCODE_REG	(CSR_BASE+0x048)
#define CSR_GP_DRIVER_REG	(CSR_BASE+0x050)

/*
 * UCODE-DRIVER GP (general purpose) mailbox registers.
 * SET/CLR registers set/clear bit(s) if "1" is written.
 */
#define CSR_UCODE_DRV_GP1       (CSR_BASE+0x054)
#define CSR_UCODE_DRV_GP1_SET   (CSR_BASE+0x058)
#define CSR_UCODE_DRV_GP1_CLR   (CSR_BASE+0x05c)
#define CSR_UCODE_DRV_GP2       (CSR_BASE+0x060)

#define CSR_MBOX_SET_REG	(CSR_BASE + 0x88)

#define CSR_LED_REG             (CSR_BASE+0x094)
#define CSR_DRAM_INT_TBL_REG	(CSR_BASE+0x0A0)
#define CSR_MAC_SHADOW_REG_CTRL		(CSR_BASE + 0x0A8) /* 6000 and up */
#define CSR_MAC_SHADOW_REG_CTRL_RX_WAKE	BIT(20)
#define CSR_MAC_SHADOW_REG_CTL2		(CSR_BASE + 0x0AC)
#define CSR_MAC_SHADOW_REG_CTL2_RX_WAKE	0xFFFF

/* LTR control (since IWL_DEVICE_FAMILY_22000) */
#define CSR_LTR_LONG_VAL_AD			(CSR_BASE + 0x0D4)
#define CSR_LTR_LONG_VAL_AD_NO_SNOOP_REQ	0x80000000
#define CSR_LTR_LONG_VAL_AD_NO_SNOOP_SCALE	0x1c000000
#define CSR_LTR_LONG_VAL_AD_NO_SNOOP_VAL	0x03ff0000
#define CSR_LTR_LONG_VAL_AD_SNOOP_REQ		0x00008000
#define CSR_LTR_LONG_VAL_AD_SNOOP_SCALE		0x00001c00
#define CSR_LTR_LONG_VAL_AD_SNOOP_VAL		0x000003ff
#define CSR_LTR_LONG_VAL_AD_SCALE_USEC		2

/* GIO Chicken Bits (PCI Express bus link power management) */
#define CSR_GIO_CHICKEN_BITS    (CSR_BASE+0x100)

/* host chicken bits */
#define CSR_HOST_CHICKEN	(CSR_BASE + 0x204)
#define CSR_HOST_CHICKEN_PM_IDLE_SRC_DIS_SB_PME	BIT(19)

/* Analog phase-lock-loop configuration  */
#define CSR_ANA_PLL_CFG         (CSR_BASE+0x20c)

/*
 * CSR HW resources monitor registers
 */
#define CSR_MONITOR_CFG_REG		(CSR_BASE+0x214)
#define CSR_MONITOR_STATUS_REG		(CSR_BASE+0x228)
#define CSR_MONITOR_XTAL_RESOURCES	(0x00000010)

/*
 * CSR Hardware Revision Workaround Register.  Indicates hardware rev;
 * "step" determines CCK backoff for txpower calculation.
 * See also CSR_HW_REV register.
 * Bit fields:
 *  3-2:  0 = A, 1 = B, 2 = C, 3 = D step
 *  1-0:  "Dash" (-) value, as in C-1, etc.
 */
#define CSR_HW_REV_WA_REG		(CSR_BASE+0x22C)

#define CSR_DBG_HPET_MEM_REG		(CSR_BASE+0x240)
#define CSR_DBG_LINK_PWR_MGMT_REG	(CSR_BASE+0x250)

/* Bits for CSR_HW_IF_CONFIG_REG */
#define CSR_HW_IF_CONFIG_REG_MSK_MAC_DASH	(0x00000003)
#define CSR_HW_IF_CONFIG_REG_MSK_MAC_STEP	(0x0000000C)
#define CSR_HW_IF_CONFIG_REG_BIT_MONITOR_SRAM	(0x00000080)
#define CSR_HW_IF_CONFIG_REG_MSK_BOARD_VER	(0x000000C0)
#define CSR_HW_IF_CONFIG_REG_BIT_MAC_SI		(0x00000100)
#define CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI	(0x00000200)
#define CSR_HW_IF_CONFIG_REG_D3_DEBUG		(0x00000200)
#define CSR_HW_IF_CONFIG_REG_MSK_PHY_TYPE	(0x00000C00)
#define CSR_HW_IF_CONFIG_REG_MSK_PHY_DASH	(0x00003000)
#define CSR_HW_IF_CONFIG_REG_MSK_PHY_STEP	(0x0000C000)

#define CSR_HW_IF_CONFIG_REG_POS_MAC_DASH	(0)
#define CSR_HW_IF_CONFIG_REG_POS_MAC_STEP	(2)
#define CSR_HW_IF_CONFIG_REG_POS_BOARD_VER	(6)
#define CSR_HW_IF_CONFIG_REG_POS_PHY_TYPE	(10)
#define CSR_HW_IF_CONFIG_REG_POS_PHY_DASH	(12)
#define CSR_HW_IF_CONFIG_REG_POS_PHY_STEP	(14)

#define CSR_HW_IF_CONFIG_REG_BIT_HAP_WAKE_L1A	(0x00080000)
#define CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM	(0x00200000)
#define CSR_HW_IF_CONFIG_REG_BIT_NIC_READY	(0x00400000) /* PCI_OWN_SEM */
#define CSR_HW_IF_CONFIG_REG_BIT_NIC_PREPARE_DONE (0x02000000) /* ME_OWN */
#define CSR_HW_IF_CONFIG_REG_PREPARE		  (0x08000000) /* WAKE_ME */
#define CSR_HW_IF_CONFIG_REG_ENABLE_PME		  (0x10000000)
#define CSR_HW_IF_CONFIG_REG_PERSIST_MODE	  (0x40000000) /* PERSISTENCE */

#define CSR_MBOX_SET_REG_OS_ALIVE		BIT(5)

#define CSR_INT_PERIODIC_DIS			(0x00) /* disable periodic int*/
#define CSR_INT_PERIODIC_ENA			(0xFF) /* 255*32 usec ~ 8 msec*/

/* interrupt flags in INTA, set by uCode or hardware (e.g. dma),
 * acknowledged (reset) by host writing "1" to flagged bits. */
#define CSR_INT_BIT_FH_RX        (1 << 31) /* Rx DMA, cmd responses, FH_INT[17:16] */
#define CSR_INT_BIT_HW_ERR       (1 << 29) /* DMA hardware error FH_INT[31] */
#define CSR_INT_BIT_RX_PERIODIC	 (1 << 28) /* Rx periodic */
#define CSR_INT_BIT_FH_TX        (1 << 27) /* Tx DMA FH_INT[1:0] */
#define CSR_INT_BIT_SCD          (1 << 26) /* TXQ pointer advanced */
#define CSR_INT_BIT_SW_ERR       (1 << 25) /* uCode error */
#define CSR_INT_BIT_RF_KILL      (1 << 7)  /* HW RFKILL switch GP_CNTRL[27] toggled */
#define CSR_INT_BIT_CT_KILL      (1 << 6)  /* Critical temp (chip too hot) rfkill */
#define CSR_INT_BIT_SW_RX        (1 << 3)  /* Rx, command responses */
#define CSR_INT_BIT_WAKEUP       (1 << 1)  /* NIC controller waking up (pwr mgmt) */
#define CSR_INT_BIT_ALIVE        (1 << 0)  /* uCode interrupts once it initializes */

#define CSR_INI_SET_MASK	(CSR_INT_BIT_FH_RX   | \
				 CSR_INT_BIT_HW_ERR  | \
				 CSR_INT_BIT_FH_TX   | \
				 CSR_INT_BIT_SW_ERR  | \
				 CSR_INT_BIT_RF_KILL | \
				 CSR_INT_BIT_SW_RX   | \
				 CSR_INT_BIT_WAKEUP  | \
				 CSR_INT_BIT_ALIVE   | \
				 CSR_INT_BIT_RX_PERIODIC)

/* interrupt flags in FH (flow handler) (PCI busmaster DMA) */
#define CSR_FH_INT_BIT_ERR       (1 << 31) /* Error */
#define CSR_FH_INT_BIT_HI_PRIOR  (1 << 30) /* High priority Rx, bypass coalescing */
#define CSR_FH_INT_BIT_RX_CHNL1  (1 << 17) /* Rx channel 1 */
#define CSR_FH_INT_BIT_RX_CHNL0  (1 << 16) /* Rx channel 0 */
#define CSR_FH_INT_BIT_TX_CHNL1  (1 << 1)  /* Tx channel 1 */
#define CSR_FH_INT_BIT_TX_CHNL0  (1 << 0)  /* Tx channel 0 */

#define CSR_FH_INT_RX_MASK	(CSR_FH_INT_BIT_HI_PRIOR | \
				CSR_FH_INT_BIT_RX_CHNL1 | \
				CSR_FH_INT_BIT_RX_CHNL0)

#define CSR_FH_INT_TX_MASK	(CSR_FH_INT_BIT_TX_CHNL1 | \
				CSR_FH_INT_BIT_TX_CHNL0)

/* GPIO */
#define CSR_GPIO_IN_BIT_AUX_POWER                   (0x00000200)
#define CSR_GPIO_IN_VAL_VAUX_PWR_SRC                (0x00000000)
#define CSR_GPIO_IN_VAL_VMAIN_PWR_SRC               (0x00000200)

/* RESET */
#define CSR_RESET_REG_FLAG_NEVO_RESET                (0x00000001)
#define CSR_RESET_REG_FLAG_FORCE_NMI                 (0x00000002)
#define CSR_RESET_REG_FLAG_SW_RESET		     (0x00000080)
#define CSR_RESET_REG_FLAG_MASTER_DISABLED           (0x00000100)
#define CSR_RESET_REG_FLAG_STOP_MASTER               (0x00000200)
#define CSR_RESET_LINK_PWR_MGMT_DISABLED             (0x80000000)

/*
 * GP (general purpose) CONTROL REGISTER
 * Bit fields:
 *    27:  HW_RF_KILL_SW
 *         Indicates state of (platform's) hardware RF-Kill switch
 * 26-24:  POWER_SAVE_TYPE
 *         Indicates current power-saving mode:
 *         000 -- No power saving
 *         001 -- MAC power-down
 *         010 -- PHY (radio) power-down
 *         011 -- Error
 *    10:  XTAL ON request
 *   9-6:  SYS_CONFIG
 *         Indicates current system configuration, reflecting pins on chip
 *         as forced high/low by device circuit board.
 *     4:  GOING_TO_SLEEP
 *         Indicates MAC is entering a power-saving sleep power-down.
 *         Not a good time to access device-internal resources.
 *     3:  MAC_ACCESS_REQ
 *         Host sets this to request and maintain MAC wakeup, to allow host
 *         access to device-internal resources.  Host must wait for
 *         MAC_CLOCK_READY (and !GOING_TO_SLEEP) before accessing non-CSR
 *         device registers.
 *     2:  INIT_DONE
 *         Host sets this to put device into fully operational D0 power mode.
 *         Host resets this after SW_RESET to put device into low power mode.
 *     0:  MAC_CLOCK_READY
 *         Indicates MAC (ucode processor, etc.) is powered up and can run.
 *         Internal resources are accessible.
 *         NOTE:  This does not indicate that the processor is actually running.
 *         NOTE:  This does not indicate that device has completed
 *                init or post-power-down restore of internal SRAM memory.
 *                Use CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP as indication that
 *                SRAM is restored and uCode is in normal operation mode.
 *                Later devices (5xxx/6xxx/1xxx) use non-volatile SRAM, and
 *                do not need to save/restore it.
 *         NOTE:  After device reset, this bit remains "0" until host sets
 *                INIT_DONE
 */
#define CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY	     (0x00000001)
#define CSR_GP_CNTRL_REG_FLAG_INIT_DONE		     (0x00000004)
#define CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ	     (0x00000008)
#define CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP	     (0x00000010)
#define CSR_GP_CNTRL_REG_FLAG_XTAL_ON		     (0x00000400)

#define CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN	     (0x00000001)

#define CSR_GP_CNTRL_REG_MSK_POWER_SAVE_TYPE         (0x07000000)
#define CSR_GP_CNTRL_REG_FLAG_RFKILL_WAKE_L1A_EN     (0x04000000)
#define CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW          (0x08000000)


/* HW REV */
#define CSR_HW_REV_DASH(_val)          (((_val) & 0x0000003) >> 0)
#define CSR_HW_REV_STEP(_val)          (((_val) & 0x000000C) >> 2)
#define CSR_HW_REV_TYPE(_val)          (((_val) & 0x000FFF0) >> 4)

/* HW RFID */
#define CSR_HW_RFID_FLAVOR(_val)       (((_val) & 0x000000F) >> 0)
#define CSR_HW_RFID_DASH(_val)         (((_val) & 0x00000F0) >> 4)
#define CSR_HW_RFID_STEP(_val)         (((_val) & 0x0000F00) >> 8)
#define CSR_HW_RFID_TYPE(_val)         (((_val) & 0x0FFF000) >> 12)
#define CSR_HW_RFID_IS_CDB(_val)       (((_val) & 0x10000000) >> 28)
#define CSR_HW_RFID_IS_JACKET(_val)    (((_val) & 0x20000000) >> 29)

/**
 *  hw_rev values
 */
enum {
	SILICON_A_STEP = 0,
	SILICON_B_STEP,
	SILICON_C_STEP,
};


#define CSR_HW_REV_TYPE_MSK		(0x000FFF0)
#define CSR_HW_REV_TYPE_5300		(0x0000020)
#define CSR_HW_REV_TYPE_5350		(0x0000030)
#define CSR_HW_REV_TYPE_5100		(0x0000050)
#define CSR_HW_REV_TYPE_5150		(0x0000040)
#define CSR_HW_REV_TYPE_1000		(0x0000060)
#define CSR_HW_REV_TYPE_6x00		(0x0000070)
#define CSR_HW_REV_TYPE_6x50		(0x0000080)
#define CSR_HW_REV_TYPE_6150		(0x0000084)
#define CSR_HW_REV_TYPE_6x05		(0x00000B0)
#define CSR_HW_REV_TYPE_6x30		CSR_HW_REV_TYPE_6x05
#define CSR_HW_REV_TYPE_6x35		CSR_HW_REV_TYPE_6x05
#define CSR_HW_REV_TYPE_2x30		(0x00000C0)
#define CSR_HW_REV_TYPE_2x00		(0x0000100)
#define CSR_HW_REV_TYPE_105		(0x0000110)
#define CSR_HW_REV_TYPE_135		(0x0000120)
#define CSR_HW_REV_TYPE_7265D		(0x0000210)
#define CSR_HW_REV_TYPE_NONE		(0x00001F0)
#define CSR_HW_REV_TYPE_QNJ		(0x0000360)
#define CSR_HW_REV_TYPE_QNJ_B0		(0x0000364)
#define CSR_HW_REV_TYPE_QU_B0		(0x0000334)
#define CSR_HW_REV_TYPE_QU_C0		(0x0000338)
#define CSR_HW_REV_TYPE_QUZ		(0x0000354)
#define CSR_HW_REV_TYPE_HR_CDB		(0x0000340)
#define CSR_HW_REV_TYPE_SO		(0x0000370)
#define CSR_HW_REV_TYPE_TY		(0x0000420)

/* RF_ID value */
#define CSR_HW_RF_ID_TYPE_JF		(0x00105100)
#define CSR_HW_RF_ID_TYPE_HR		(0x0010A000)
#define CSR_HW_RF_ID_TYPE_HR1		(0x0010c100)
#define CSR_HW_RF_ID_TYPE_HRCDB		(0x00109F00)
#define CSR_HW_RF_ID_TYPE_GF		(0x0010D000)
#define CSR_HW_RF_ID_TYPE_GF4		(0x0010E000)

/* HW_RF CHIP STEP  */
#define CSR_HW_RF_STEP(_val) (((_val) >> 8) & 0xF)

/* EEPROM REG */
#define CSR_EEPROM_REG_READ_VALID_MSK	(0x00000001)
#define CSR_EEPROM_REG_BIT_CMD		(0x00000002)
#define CSR_EEPROM_REG_MSK_ADDR		(0x0000FFFC)
#define CSR_EEPROM_REG_MSK_DATA		(0xFFFF0000)

/* EEPROM GP */
#define CSR_EEPROM_GP_VALID_MSK		(0x00000007) /* signature */
#define CSR_EEPROM_GP_IF_OWNER_MSK	(0x00000180)
#define CSR_EEPROM_GP_BAD_SIGNATURE_BOTH_EEP_AND_OTP	(0x00000000)
#define CSR_EEPROM_GP_BAD_SIG_EEP_GOOD_SIG_OTP		(0x00000001)
#define CSR_EEPROM_GP_GOOD_SIG_EEP_LESS_THAN_4K		(0x00000002)
#define CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_THAN_4K		(0x00000004)

/* One-time-programmable memory general purpose reg */
#define CSR_OTP_GP_REG_DEVICE_SELECT	(0x00010000) /* 0 - EEPROM, 1 - OTP */
#define CSR_OTP_GP_REG_OTP_ACCESS_MODE	(0x00020000) /* 0 - absolute, 1 - relative */
#define CSR_OTP_GP_REG_ECC_CORR_STATUS_MSK          (0x00100000) /* bit 20 */
#define CSR_OTP_GP_REG_ECC_UNCORR_STATUS_MSK        (0x00200000) /* bit 21 */

/* GP REG */
#define CSR_GP_REG_POWER_SAVE_STATUS_MSK            (0x03000000) /* bit 24/25 */
#define CSR_GP_REG_NO_POWER_SAVE            (0x00000000)
#define CSR_GP_REG_MAC_POWER_SAVE           (0x01000000)
#define CSR_GP_REG_PHY_POWER_SAVE           (0x02000000)
#define CSR_GP_REG_POWER_SAVE_ERROR         (0x03000000)


/* CSR GIO */
#define CSR_GIO_REG_VAL_L0S_DISABLED	(0x00000002)

/*
 * UCODE-DRIVER GP (general purpose) mailbox register 1
 * Host driver and uCode write and/or read this register to communicate with
 * each other.
 * Bit fields:
 *     4:  UCODE_DISABLE
 *         Host sets this to request permanent halt of uCode, same as
 *         sending CARD_STATE command with "halt" bit set.
 *     3:  CT_KILL_EXIT
 *         Host sets this to request exit from CT_KILL state, i.e. host thinks
 *         device temperature is low enough to continue normal operation.
 *     2:  CMD_BLOCKED
 *         Host sets this during RF KILL power-down sequence (HW, SW, CT KILL)
 *         to release uCode to clear all Tx and command queues, enter
 *         unassociated mode, and power down.
 *         NOTE:  Some devices also use HBUS_TARG_MBX_C register for this bit.
 *     1:  SW_BIT_RFKILL
 *         Host sets this when issuing CARD_STATE command to request
 *         device sleep.
 *     0:  MAC_SLEEP
 *         uCode sets this when preparing a power-saving power-down.
 *         uCode resets this when power-up is complete and SRAM is sane.
 *         NOTE:  device saves internal SRAM data to host when powering down,
 *                and must restore this data after powering back up.
 *                MAC_SLEEP is the best indication that restore is complete.
 *                Later devices (5xxx/6xxx/1xxx) use non-volatile SRAM, and
 *                do not need to save/restore it.
 */
#define CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP             (0x00000001)
#define CSR_UCODE_SW_BIT_RFKILL                     (0x00000002)
#define CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED           (0x00000004)
#define CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT      (0x00000008)
#define CSR_UCODE_DRV_GP1_BIT_D3_CFG_COMPLETE       (0x00000020)

/* GP Driver */
#define CSR_GP_DRIVER_REG_BIT_RADIO_SKU_MSK	    (0x00000003)
#define CSR_GP_DRIVER_REG_BIT_RADIO_SKU_3x3_HYB	    (0x00000000)
#define CSR_GP_DRIVER_REG_BIT_RADIO_SKU_2x2_HYB	    (0x00000001)
#define CSR_GP_DRIVER_REG_BIT_RADIO_SKU_2x2_IPA	    (0x00000002)
#define CSR_GP_DRIVER_REG_BIT_CALIB_VERSION6	    (0x00000004)
#define CSR_GP_DRIVER_REG_BIT_6050_1x2		    (0x00000008)

#define CSR_GP_DRIVER_REG_BIT_RADIO_IQ_INVER	    (0x00000080)

/* GIO Chicken Bits (PCI Express bus link power management) */
#define CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX  (0x00800000)
#define CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER  (0x20000000)

/* LED */
#define CSR_LED_BSM_CTRL_MSK (0xFFFFFFDF)
#define CSR_LED_REG_TURN_ON (0x60)
#define CSR_LED_REG_TURN_OFF (0x20)

/* ANA_PLL */
#define CSR50_ANA_PLL_CFG_VAL        (0x00880300)

/* HPET MEM debug */
#define CSR_DBG_HPET_MEM_REG_VAL	(0xFFFF0000)

/* DRAM INT TABLE */
#define CSR_DRAM_INT_TBL_ENABLE		(1 << 31)
#define CSR_DRAM_INIT_TBL_WRITE_POINTER	(1 << 28)
#define CSR_DRAM_INIT_TBL_WRAP_CHECK	(1 << 27)

/*
 * SHR target access (Shared block memory space)
 *
 * Shared internal registers can be accessed directly from PCI bus through SHR
 * arbiter without need for the MAC HW to be powered up. This is possible due to
 * indirect read/write via HEEP_CTRL_WRD_PCIEX_CTRL (0xEC) and
 * HEEP_CTRL_WRD_PCIEX_DATA (0xF4) registers.
 *
 * Use iwl_write32()/iwl_read32() family to access these registers. The MAC HW
 * need not be powered up so no "grab inc access" is required.
 */

/*
 * Registers for accessing shared registers (e.g. SHR_APMG_GP1,
 * SHR_APMG_XTAL_CFG). For example, to read from SHR_APMG_GP1 register (0x1DC),
 * first, write to the control register:
 * HEEP_CTRL_WRD_PCIEX_CTRL[15:0] = 0x1DC (offset of the SHR_APMG_GP1 register)
 * HEEP_CTRL_WRD_PCIEX_CTRL[29:28] = 2 (read access)
 * second, read from the data register HEEP_CTRL_WRD_PCIEX_DATA[31:0].
 *
 * To write the register, first, write to the data register
 * HEEP_CTRL_WRD_PCIEX_DATA[31:0] and then:
 * HEEP_CTRL_WRD_PCIEX_CTRL[15:0] = 0x1DC (offset of the SHR_APMG_GP1 register)
 * HEEP_CTRL_WRD_PCIEX_CTRL[29:28] = 3 (write access)
 */
#define HEEP_CTRL_WRD_PCIEX_CTRL_REG	(CSR_BASE+0x0ec)
#define HEEP_CTRL_WRD_PCIEX_DATA_REG	(CSR_BASE+0x0f4)

/*
 * HBUS (Host-side Bus)
 *
 * HBUS registers are mapped directly into PCI bus space, but are used
 * to indirectly access device's internal memory or registers that
 * may be powered-down.
 *
 * Use iwl_write_direct32()/iwl_read_direct32() family for these registers;
 * host must "grab nic access" via CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ
 * to make sure the MAC (uCode processor, etc.) is powered up for accessing
 * internal resources.
 *
 * Do not use iwl_write32()/iwl_read32() family to access these registers;
 * these provide only simple PCI bus access, without waking up the MAC.
 */
#define HBUS_BASE	(0x400)

/*
 * Registers for accessing device's internal SRAM memory (e.g. SCD SRAM
 * structures, error log, event log, verifying uCode load).
 * First write to address register, then read from or write to data register
 * to complete the job.  Once the address register is set up, accesses to
 * data registers auto-increment the address by one dword.
 * Bit usage for address registers (read or write):
 *  0-31:  memory address within device
 */
#define HBUS_TARG_MEM_RADDR     (HBUS_BASE+0x00c)
#define HBUS_TARG_MEM_WADDR     (HBUS_BASE+0x010)
#define HBUS_TARG_MEM_WDAT      (HBUS_BASE+0x018)
#define HBUS_TARG_MEM_RDAT      (HBUS_BASE+0x01c)

/* Mailbox C, used as workaround alternative to CSR_UCODE_DRV_GP1 mailbox */
#define HBUS_TARG_MBX_C         (HBUS_BASE+0x030)
#define HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED         (0x00000004)

/*
 * Registers for accessing device's internal peripheral registers
 * (e.g. SCD, BSM, etc.).  First write to address register,
 * then read from or write to data register to complete the job.
 * Bit usage for address registers (read or write):
 *  0-15:  register address (offset) within device
 * 24-25:  (# bytes - 1) to read or write (e.g. 3 for dword)
 */
#define HBUS_TARG_PRPH_WADDR    (HBUS_BASE+0x044)
#define HBUS_TARG_PRPH_RADDR    (HBUS_BASE+0x048)
#define HBUS_TARG_PRPH_WDAT     (HBUS_BASE+0x04c)
#define HBUS_TARG_PRPH_RDAT     (HBUS_BASE+0x050)

/* Used to enable DBGM */
#define HBUS_TARG_TEST_REG	(HBUS_BASE+0x05c)

/*
 * Per-Tx-queue write pointer (index, really!)
 * Indicates index to next TFD that driver will fill (1 past latest filled).
 * Bit usage:
 *  0-7:  queue write index
 * 11-8:  queue selector
 */
#define HBUS_TARG_WRPTR         (HBUS_BASE+0x060)

/**********************************************************
 * CSR values
 **********************************************************/
 /*
 * host interrupt timeout value
 * used with setting interrupt coalescing timer
 * the CSR_INT_COALESCING is an 8 bit register in 32-usec unit
 *
 * default interrupt coalescing timer is 64 x 32 = 2048 usecs
 */
#define IWL_HOST_INT_TIMEOUT_MAX	(0xFF)
#define IWL_HOST_INT_TIMEOUT_DEF	(0x40)
#define IWL_HOST_INT_TIMEOUT_MIN	(0x0)
#define IWL_HOST_INT_OPER_MODE		BIT(31)

/*****************************************************************************
 *                        7000/3000 series SHR DTS addresses                 *
 *****************************************************************************/

/* Diode Results Register Structure: */
enum dtd_diode_reg {
	DTS_DIODE_REG_DIG_VAL			= 0x000000FF, /* bits [7:0] */
	DTS_DIODE_REG_VREF_LOW			= 0x0000FF00, /* bits [15:8] */
	DTS_DIODE_REG_VREF_HIGH			= 0x00FF0000, /* bits [23:16] */
	DTS_DIODE_REG_VREF_ID			= 0x03000000, /* bits [25:24] */
	DTS_DIODE_REG_PASS_ONCE			= 0x80000000, /* bits [31:31] */
	DTS_DIODE_REG_FLAGS_MSK			= 0xFF000000, /* bits [31:24] */
/* Those are the masks INSIDE the flags bit-field: */
	DTS_DIODE_REG_FLAGS_VREFS_ID_POS	= 0,
	DTS_DIODE_REG_FLAGS_VREFS_ID		= 0x00000003, /* bits [1:0] */
	DTS_DIODE_REG_FLAGS_PASS_ONCE_POS	= 7,
	DTS_DIODE_REG_FLAGS_PASS_ONCE		= 0x00000080, /* bits [7:7] */
};

/*****************************************************************************
 *                        MSIX related registers                             *
 *****************************************************************************/

#define CSR_MSIX_BASE			(0x2000)
#define CSR_MSIX_FH_INT_CAUSES_AD	(CSR_MSIX_BASE + 0x800)
#define CSR_MSIX_FH_INT_MASK_AD		(CSR_MSIX_BASE + 0x804)
#define CSR_MSIX_HW_INT_CAUSES_AD	(CSR_MSIX_BASE + 0x808)
#define CSR_MSIX_HW_INT_MASK_AD		(CSR_MSIX_BASE + 0x80C)
#define CSR_MSIX_AUTOMASK_ST_AD		(CSR_MSIX_BASE + 0x810)
#define CSR_MSIX_RX_IVAR_AD_REG		(CSR_MSIX_BASE + 0x880)
#define CSR_MSIX_IVAR_AD_REG		(CSR_MSIX_BASE + 0x890)
#define CSR_MSIX_PENDING_PBA_AD		(CSR_MSIX_BASE + 0x1000)
#define CSR_MSIX_RX_IVAR(cause)		(CSR_MSIX_RX_IVAR_AD_REG + (cause))
#define CSR_MSIX_IVAR(cause)		(CSR_MSIX_IVAR_AD_REG + (cause))

#define MSIX_FH_INT_CAUSES_Q(q)		(q)

/*
 * Causes for the FH register interrupts
 */
enum msix_fh_int_causes {
	MSIX_FH_INT_CAUSES_Q0			= BIT(0),
	MSIX_FH_INT_CAUSES_Q1			= BIT(1),
	MSIX_FH_INT_CAUSES_D2S_CH0_NUM		= BIT(16),
	MSIX_FH_INT_CAUSES_D2S_CH1_NUM		= BIT(17),
	MSIX_FH_INT_CAUSES_S2D			= BIT(19),
	MSIX_FH_INT_CAUSES_FH_ERR		= BIT(21),
};

/* The low 16 bits are for rx data queue indication */
#define MSIX_FH_INT_CAUSES_DATA_QUEUE 0xffff

/*
 * Causes for the HW register interrupts
 */
enum msix_hw_int_causes {
	MSIX_HW_INT_CAUSES_REG_ALIVE		= BIT(0),
	MSIX_HW_INT_CAUSES_REG_WAKEUP		= BIT(1),
	MSIX_HW_INT_CAUSES_REG_IML              = BIT(1),
	MSIX_HW_INT_CAUSES_REG_RESET_DONE	= BIT(2),
	MSIX_HW_INT_CAUSES_REG_CT_KILL		= BIT(6),
	MSIX_HW_INT_CAUSES_REG_RF_KILL		= BIT(7),
	MSIX_HW_INT_CAUSES_REG_PERIODIC		= BIT(8),
	MSIX_HW_INT_CAUSES_REG_SW_ERR		= BIT(25),
	MSIX_HW_INT_CAUSES_REG_SCD		= BIT(26),
	MSIX_HW_INT_CAUSES_REG_FH_TX		= BIT(27),
	MSIX_HW_INT_CAUSES_REG_HW_ERR		= BIT(29),
	MSIX_HW_INT_CAUSES_REG_HAP		= BIT(30),
};

#define MSIX_MIN_INTERRUPT_VECTORS		2
#define MSIX_AUTO_CLEAR_CAUSE			0
#define MSIX_NON_AUTO_CLEAR_CAUSE		BIT(7)

/*****************************************************************************
 *                     HW address related registers                          *
 *****************************************************************************/

#define CSR_ADDR_BASE(trans)			((trans)->cfg->mac_addr_from_csr)
#define CSR_MAC_ADDR0_OTP(trans)		(CSR_ADDR_BASE(trans) + 0x00)
#define CSR_MAC_ADDR1_OTP(trans)		(CSR_ADDR_BASE(trans) + 0x04)
#define CSR_MAC_ADDR0_STRAP(trans)		(CSR_ADDR_BASE(trans) + 0x08)
#define CSR_MAC_ADDR1_STRAP(trans)		(CSR_ADDR_BASE(trans) + 0x0c)

#endif /* !__iwl_csr_h__ */
