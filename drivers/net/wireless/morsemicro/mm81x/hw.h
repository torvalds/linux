/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_HW_H_
#define _MM81X_HW_H_

#include <linux/gpio/consumer.h>
#include "core.h"
#include "command_defs.h"

/* This should be at a fixed location for a family of chipset */
#define MM8108_REG_CHIP_ID 0x00002d20

#define MM81X_SDIO_RW_ADDR_BOUNDARY_MASK ((u32)0xFFFF0000)

#define MM81X_CONFIG_ACCESS_1BYTE 0
#define MM81X_CONFIG_ACCESS_2BYTE 1
#define MM81X_CONFIG_ACCESS_4BYTE 2

#define MM81X_REG_TRGR_BASE(mors) ((mors)->regs->trgr_base_address)
#define MM81X_REG_TRGR1_STS(mors) (MM81X_REG_TRGR_BASE(mors) + 0x00)
#define MM81X_REG_TRGR1_SET(mors) (MM81X_REG_TRGR_BASE(mors) + 0x04)
#define MM81X_REG_TRGR1_CLR(mors) (MM81X_REG_TRGR_BASE(mors) + 0x08)
#define MM81X_REG_TRGR1_EN(mors) (MM81X_REG_TRGR_BASE(mors) + 0x0C)
#define MM81X_REG_TRGR2_STS(mors) (MM81X_REG_TRGR_BASE(mors) + 0x10)
#define MM81X_REG_TRGR2_SET(mors) (MM81X_REG_TRGR_BASE(mors) + 0x14)
#define MM81X_REG_TRGR2_CLR(mors) (MM81X_REG_TRGR_BASE(mors) + 0x18)
#define MM81X_REG_TRGR2_EN(mors) (MM81X_REG_TRGR_BASE(mors) + 0x1C)

#define MM81X_REG_INT_BASE(mors) ((mors)->regs->irq_base_address)
#define MM81X_REG_INT1_STS(mors) (MM81X_REG_INT_BASE(mors) + 0x00)
#define MM81X_REG_INT1_SET(mors) (MM81X_REG_INT_BASE(mors) + 0x04)
#define MM81X_REG_INT1_CLR(mors) (MM81X_REG_INT_BASE(mors) + 0x08)
#define MM81X_REG_INT1_EN(mors) (MM81X_REG_INT_BASE(mors) + 0x0C)
#define MM81X_REG_INT2_STS(mors) (MM81X_REG_INT_BASE(mors) + 0x10)
#define MM81X_REG_INT2_SET(mors) (MM81X_REG_INT_BASE(mors) + 0x14)
#define MM81X_REG_INT2_CLR(mors) (MM81X_REG_INT_BASE(mors) + 0x18)
#define MM81X_REG_INT2_EN(mors) (MM81X_REG_INT_BASE(mors) + 0x1C)

#define MM81X_REG_CHIP_ID(mors) ((mors)->regs->chip_id_address)

#define MM81X_REG_MSI(mors) ((mors)->regs->msi_address)
#define MM81X_REG_MSI_HOST_INT(mors) ((mors)->regs->msi_value)

#define MM81X_REG_HOST_MAGIC_VALUE(mors) ((mors)->regs->magic_num_value)

#define MM81X_REG_RESET(mors) ((mors)->regs->cpu_reset_address)
#define MM81X_REG_RESET_VALUE(mors) ((mors)->regs->cpu_reset_value)

#define MM81X_REG_HOST_MANIFEST_PTR(mors) ((mors)->regs->manifest_ptr_address)

#define MM81X_REG_EARLY_CLK_CTRL_VALUE(mors) \
	((mors)->regs->early_clk_ctrl_value)

#define MM81X_REG_CLK_CTRL(mors) ((mors)->regs->clk_ctrl_address)
#define MM81X_REG_CLK_CTRL_VALUE(mors) ((mors)->regs->clk_ctrl_value)

#define MM81X_REG_BOOT_ADDR(mors) ((mors)->regs->boot_address)
#define MM81X_REG_BOOT_ADDR_VALUE(mors) ((mors)->regs->boot_value)

#define MM81X_REG_AON_ADDR(mors) ((mors)->regs->aon)
#define MM81X_REG_AON_COUNT(mors) ((mors)->regs->aon_count)
#define MM81X_REG_AON_LATCH_ADDR(mors) ((mors)->regs->aon_latch)
#define MM81X_REG_AON_LATCH_MASK(mors) ((mors)->regs->aon_latch_mask)
#define MM81X_REG_AON_USB_RESET(mors) ((mors)->regs->aon_reset_usb_value)

/* Bit 17 to 24 reserved for the beacon VIF 0 to 7 interrupts */
#define MM81X_INT_BEACON_VIF_MASK_ALL (GENMASK(24, 17))
#define MM81X_INT_BEACON_BASE_NUM (17)

/* PV0 NDP probe interrupts (VIF 0 and 1). */
#define MM81X_INT_NDP_PROBE_REQ_PV0_VIF_MASK_ALL (GENMASK(26, 25))
#define MM81X_INT_NDP_PROBE_REQ_PV0_BASE_NUM (25)

/* Bit 27 Chip to Host stop notify */
#define MM81X_INT_HW_STOP_NOTIFICATION_NUM (27)
#define MM81X_INT_HW_STOP_NOTIFICATION BIT(MM81X_INT_HW_STOP_NOTIFICATION_NUM)

/* Chip IDs */
#define CHIP_ID_MM8108 0x809

/*
 * Minimum time we must wait between attempting to reload the HW after a
 * stop notification
 */
#define HW_RELOAD_AFTER_STOP_WINDOW 5

enum host_table_firmware_flags {
	MM81X_FW_FLAGS_SUPPORT_S1G = BIT(0),
	MM81X_FW_FLAGS_BUSY_ACTIVE_LOW = BIT(1),
	MM81X_FW_FLAGS_REPORTS_TX_BEACON_COMPLETION = BIT(2),
	MM81X_FW_FLAGS_SUPPORT_HW_SCAN = BIT(3),
	MM81X_FW_FLAGS_SUPPORT_CHIP_HALT_IRQ = BIT(4),
};

struct host_table {
	__le32 magic_number;
	__le32 fw_version_number;
	__le32 host_flags;
	__le32 fw_flags;
	__le32 memcmd_cmd_addr;
	__le32 memcmd_resp_addr;
	__le32 ext_host_tbl_addr;
} __packed;

struct mm81x_regs {
	u32 chip_id_address;
	u32 irq_base_address;
	u32 trgr_base_address;
	u32 cpu_reset_address;
	u32 cpu_reset_value;
	u32 msi_address;
	u32 msi_value;
	u32 manifest_ptr_address;
	u32 magic_num_value;
	u32 clk_ctrl_address;
	u32 clk_ctrl_value;
	u32 early_clk_ctrl_value;
	u32 boot_address;
	u32 boot_value;
	u32 pager_base_address;
	u32 aon_latch;
	u32 aon_latch_mask;
	u32 aon_reset_usb_value;
	u32 aon;
	u8 aon_count;
};

int mm81x_hw_otp_get_board_type(struct mm81x *mors);
bool mm81x_hw_otp_valid_board_type(u32 board_type);
int mm81x_hw_otp_get_mac_addr(struct mm81x *mors);

void mm81x_hw_irq_enable(struct mm81x *mors, u32 irq, bool enable);
int mm81x_hw_irq_handle(struct mm81x *mors);
void mm81x_hw_irq_clear(struct mm81x *mors);
void mm81x_hw_toggle_aon_latch(struct mm81x *mors);
void mm81x_hw_enable_burst_mode(struct mm81x *mors, const u8 burst_mode);
int mm81x_hw_digital_reset(struct mm81x *mors);
void mm81x_hw_pre_firmware_ndr_hook(struct mm81x *mors);
void mm81x_hw_post_firmware_ndr_hook(struct mm81x *mors);

enum sdio_burst_mode {
	SDIO_WORD_BURST_DISABLE =
		0, /* Intentionally duplicate to make it clear it's disabled */
	SDIO_WORD_BURST_SIZE_0 = 0, /* 000: no bursting (single 32bit word) */
	SDIO_WORD_BURST_SIZE_2 = 1, /* 001: bursts of 2 words */
	SDIO_WORD_BURST_SIZE_4 = 2, /* 010: bursts of 4 words */
	SDIO_WORD_BURST_SIZE_8 = 3, /* 011: bursts of 8 words */
	SDIO_WORD_BURST_SIZE_16 = 4, /* 100: bursts of 16 words */
	SDIO_WORD_BURST_MASK = 7,
};

extern const struct mm81x_regs mm8108_regs;

void mm81x_hw_enable_stop_notifications(struct mm81x *mors, bool enable);

#endif /* !_MM81X_HW_H_ */
