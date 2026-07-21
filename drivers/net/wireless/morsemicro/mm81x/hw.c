// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2026 Morse Micro
 */
#include <linux/firmware.h>
#include <linux/delay.h>
#include <linux/stringify.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/gpio.h>
#include "hif.h"
#include "mac.h"
#include "bus.h"
#include "core.h"
#include "fw.h"
#include "yaps.h"

#define MM8108_REG_HOST_MAGIC_VALUE 0xDEADBEEF
#define MM8108_REG_RESET_VALUE 0xDEAD

#define MM8108_REG_SDIO_DEVICE_ADDR 0x0000207C

#define MM8108_REG_SDIO_DEVICE_BURST_OFFSET 9
#define MM8108_REG_TRGR_BASE 0x00003c00
#define MM8108_REG_INT_BASE 0x00003c50
#define MM8108_REG_MSI_ADDRESS 0x00004100
#define MM8108_REG_MSI_VALUE 0x1
#define MM8108_REG_MANIFEST_PTR_ADDRESS 0x00002d40
#define MM8108_REG_APPS_BOOT_ADDR 0x00002084
#define MM8108_REG_RESET 0x000020AC
#define MM8108_REG_AON_COUNT 2

#define MM8108_REG_AON_ADDR 0x00002114
#define MM8108_REG_AON_LATCH_ADDR 0x00405020
#define MM8108_REG_AON_LATCH_MASK 0x1
#define MM8108_REG_AON_RESET_USB_VALUE 0x8
#define MM8108_APPS_MAC_DMEM_ADDR_START 0x00100000

#define MM8108_REG_RC_CLK_POWER_OFF_ADDR 0x00405020
#define MM8108_REG_RC_CLK_POWER_OFF_MASK 0x00000040
#define MM8108_SLOW_RC_POWER_ON_DELAY_MS 2

#define MM8108_RESET_DELAY_TIME_MS 400

#define MM8108_REG_OTPCTRL_PLDO 0x00004014
#define MM8108_REG_OTPCTRL_PENVDD2 0x00004010
#define MM8108_REG_OTPCTRL_PDSTB 0x00004018
#define MM8108_REG_OTPCTRL_PTM 0x0000401c
#define MM8108_REG_OTPCTRL_PCE 0x00004020
#define MM8108_REG_OTPCTRL_PA 0x00004034
#define MM8108_REG_OTPCTRL_PECCRDB 0x00004048
#define MM8108_REG_OTPCTRL_ACTION_AUTO_RD_START 0x0000400c
#define MM8108_REG_OTPCTRL_PDOUT 0x00004040

#define MM81X_OTP_MAC_ADDR_2_BANK_NUM 27
#define MM81X_OTP_MAC_ADDR_1_BANK_NUM 26
#define MM81X_OTP_MAC_ADDR_1_MASK GENMASK(31, 16)
#define MM81X_OTP_BOARD_TYPE_BANK_NUM 26
#define MM81X_OTP_BOARD_TYPE_MASK GENMASK(15, 0)

#define MM810X_BOARD_TYPE_MAX_VALUE (MM81X_OTP_BOARD_TYPE_MASK - 1)

static void mm81x_hw_otp_power_up(struct mm81x *mors)
{
	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PENVDD2, 1);
	udelay(2);

	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PLDO, 1);
	usleep_range(10, 20);

	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PDSTB, 1);
	udelay(3);
}

static void mm81x_hw_otp_power_down(struct mm81x *mors)
{
	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PDSTB, 0);
	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PLDO, 0);
	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PENVDD2, 0);
}

static void mm81x_hw_otp_read_enable(struct mm81x *mors)
{
	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PTM, 0);
	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PCE, 1);
	usleep_range(10, 20);
}

static void mm81x_hw_otp_read_disable(struct mm81x *mors)
{
	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PCE, 0);
	udelay(1);
}

static int mm81x_hw_otp_read(struct mm81x *mors, u8 bank_num, u32 *buf,
			     u8 ignore_ecc)
{
	u32 auto_rd_start_tmp;
	u32 auto_rd_start = 1;
	int i;

	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PA, bank_num);
	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_PECCRDB, ignore_ecc);

	mm81x_reg32_read(mors, MM8108_REG_OTPCTRL_ACTION_AUTO_RD_START,
			 &auto_rd_start_tmp);
	auto_rd_start_tmp &= 0xfffffffe;

	mm81x_reg32_write(mors, MM8108_REG_OTPCTRL_ACTION_AUTO_RD_START,
			  auto_rd_start | auto_rd_start_tmp);

	/* Attempt reading up to 5 times. */
	for (i = 0; i < 5 && auto_rd_start; i++) {
		usleep_range(15, 20);
		mm81x_reg32_read(mors, MM8108_REG_OTPCTRL_ACTION_AUTO_RD_START,
				 &auto_rd_start_tmp);
		auto_rd_start = auto_rd_start_tmp & 0x1;
	}

	if (i == 5)
		return -EIO;

	mm81x_reg32_read(mors, MM8108_REG_OTPCTRL_PDOUT, buf);

	return 0;
}

int mm81x_hw_otp_get_board_type(struct mm81x *mors)
{
	int board_type = 0;
	u32 otp_word = 0;
	int ret;

	mm81x_claim_bus(mors);
	mm81x_hw_otp_power_up(mors);
	mm81x_hw_otp_read_enable(mors);

	ret = mm81x_hw_otp_read(mors, MM81X_OTP_BOARD_TYPE_BANK_NUM, &otp_word,
				1);

	mm81x_hw_otp_read_disable(mors);
	mm81x_hw_otp_power_down(mors);
	mm81x_release_bus(mors);

	if (ret)
		return -EINVAL;

	board_type = otp_word & MM81X_OTP_BOARD_TYPE_MASK;

	return board_type;
}

bool mm81x_hw_otp_valid_board_type(u32 board_type)
{
	return board_type > 0 && board_type < MM810X_BOARD_TYPE_MAX_VALUE;
}

int mm81x_hw_otp_get_mac_addr(struct mm81x *mors)
{
	u32 mac1 = 0;
	u32 mac2 = 0;
	int ret = 0;

	mm81x_claim_bus(mors);
	mm81x_hw_otp_power_up(mors);
	mm81x_hw_otp_read_enable(mors);

	ret = mm81x_hw_otp_read(mors, MM81X_OTP_MAC_ADDR_1_BANK_NUM, &mac1, 1);
	if (ret)
		goto exit;

	ret = mm81x_hw_otp_read(mors, MM81X_OTP_MAC_ADDR_2_BANK_NUM, &mac2, 1);
	if (ret)
		goto exit;

	put_unaligned_le16((mac1 & MM81X_OTP_MAC_ADDR_1_MASK) >> 16,
			   &mors->macaddr[0]);
	put_unaligned_le32(mac2, &mors->macaddr[2]);

exit:
	mm81x_hw_otp_read_disable(mors);
	mm81x_hw_otp_power_down(mors);
	mm81x_release_bus(mors);

	return ret;
}

void mm81x_hw_irq_enable(struct mm81x *mors, u32 irq, bool enable)
{
	u32 irq_en, irq_en_addr = irq < 32 ? MM81X_REG_INT1_EN(mors) :
					     MM81X_REG_INT2_EN(mors);
	u32 irq_clr_addr = irq < 32 ? MM81X_REG_INT1_CLR(mors) :
				      MM81X_REG_INT2_CLR(mors);
	u32 mask = irq < 32 ? (1 << irq) : (1 << (irq - 32));

	mm81x_claim_bus(mors);
	mm81x_reg32_read(mors, irq_en_addr, &irq_en);
	if (enable)
		irq_en |= (mask);
	else
		irq_en &= ~(mask);
	mm81x_reg32_write(mors, irq_clr_addr, mask);
	mm81x_reg32_write(mors, irq_en_addr, irq_en);
	mm81x_release_bus(mors);
}

int mm81x_hw_irq_handle(struct mm81x *mors)
{
	u32 status1 = 0;

	mm81x_reg32_read(mors, MM81X_REG_INT1_STS(mors), &status1);

	if (status1 & MM81X_HIF_IRQ_MASK_ALL)
		mm81x_hif_handle_irq(mors, status1);

	if (status1 & MM81X_INT_BEACON_VIF_MASK_ALL)
		mm81x_mac_beacon_irq_handle(mors, status1);

	mm81x_reg32_write(mors, MM81X_REG_INT1_CLR(mors), status1);

	return status1 ? 1 : 0;
}
EXPORT_SYMBOL_GPL(mm81x_hw_irq_handle);

void mm81x_hw_irq_clear(struct mm81x *mors)
{
	mm81x_claim_bus(mors);
	mm81x_reg32_write(mors, MM81X_REG_INT1_CLR(mors), 0xFFFFFFFF);
	mm81x_reg32_write(mors, MM81X_REG_INT2_CLR(mors), 0xFFFFFFFF);
	mm81x_release_bus(mors);
}

void mm81x_hw_toggle_aon_latch(struct mm81x *mors)
{
	u32 address = MM81X_REG_AON_LATCH_ADDR(mors);
	u32 mask = MM81X_REG_AON_LATCH_MASK(mors);
	u32 latch;

	mm81x_reg32_read(mors, address, &latch);
	mm81x_reg32_write(mors, address, latch & ~(mask));
	mdelay(5);
	mm81x_reg32_write(mors, address, latch | mask);
	mdelay(5);
	mm81x_reg32_write(mors, address, latch & ~(mask));
	mdelay(5);
}

void mm81x_hw_enable_stop_notifications(struct mm81x *mors, bool enable)
{
	mm81x_hw_irq_enable(mors, MM81X_INT_HW_STOP_NOTIFICATION_NUM, enable);
}

void mm81x_hw_enable_burst_mode(struct mm81x *mors, const u8 burst_mode)
{
	u32 reg32_value;

	mm81x_claim_bus(mors);
	if (mm81x_reg32_read(mors, MM8108_REG_SDIO_DEVICE_ADDR, &reg32_value))
		goto end;

	reg32_value &= ~(u32)(SDIO_WORD_BURST_MASK
			      << MM8108_REG_SDIO_DEVICE_BURST_OFFSET);
	reg32_value |= (u32)(burst_mode << MM8108_REG_SDIO_DEVICE_BURST_OFFSET);

	dev_dbg(mors->dev,
		"Setting Burst mode to %d Writing 0x%08X to the register",
		burst_mode, reg32_value);

	if (mm81x_reg32_write(mors, MM8108_REG_SDIO_DEVICE_ADDR, reg32_value))
		goto end;

end:
	mm81x_release_bus(mors);
}
EXPORT_SYMBOL_GPL(mm81x_hw_enable_burst_mode);

static int mm81x_hw_enable_internal_slow_clock(struct mm81x *mors)
{
	u32 rc_clock_reg_value;
	int ret = 0;

	dev_dbg(mors->dev, "Enabling internal slow clock");

	ret = mm81x_reg32_read(mors, MM8108_REG_RC_CLK_POWER_OFF_ADDR,
			       &rc_clock_reg_value);
	if (ret)
		goto exit;

	rc_clock_reg_value &= ~MM8108_REG_RC_CLK_POWER_OFF_MASK;
	ret = mm81x_reg32_write(mors, MM8108_REG_RC_CLK_POWER_OFF_ADDR,
				rc_clock_reg_value);
	if (ret)
		goto exit;

	mm81x_hw_toggle_aon_latch(mors);

	/* Wait for the clock to turn on and settle */
	mdelay(MM8108_SLOW_RC_POWER_ON_DELAY_MS);
exit:
	return ret;
}

int mm81x_hw_digital_reset(struct mm81x *mors)
{
	int ret = 0;

	mm81x_claim_bus(mors);

	/* This should be the first step in digital reset, do not reorder */
	ret = mm81x_hw_enable_internal_slow_clock(mors);
	if (ret)
		goto exit;

	if (mors->bus_type == MM81X_BUS_TYPE_USB) {
		ret = mm81x_bus_digital_reset(mors);
		goto usb_done;
	}

	if (MM81X_REG_RESET(mors) != 0)
		ret = mm81x_reg32_write(mors, MM81X_REG_RESET(mors),
					MM81X_REG_RESET_VALUE(mors));

usb_done:
	msleep(MM8108_RESET_DELAY_TIME_MS);
exit:
	mm81x_release_bus(mors);

	if (!ret)
		mors->chip_was_reset = true;

	return ret;
}

void mm81x_hw_pre_firmware_ndr_hook(struct mm81x *mors)
{
	/* We need disable bursting for firmware download/init procedure */
	mm81x_bus_config_burst_mode(mors, false);
}

void mm81x_hw_post_firmware_ndr_hook(struct mm81x *mors)
{
	/* We are safe here to re-enable bursting again, if supported */
	mm81x_bus_config_burst_mode(mors, true);
}

const struct mm81x_regs mm8108_regs = {
	.chip_id_address = MM8108_REG_CHIP_ID,
	.irq_base_address = MM8108_REG_INT_BASE,
	.trgr_base_address = MM8108_REG_TRGR_BASE,
	.cpu_reset_address = MM8108_REG_RESET,
	.cpu_reset_value = MM8108_REG_RESET_VALUE,
	.manifest_ptr_address = MM8108_REG_MANIFEST_PTR_ADDRESS,
	.msi_address = MM8108_REG_MSI_ADDRESS,
	.msi_value = MM8108_REG_MSI_VALUE,
	.magic_num_value = MM8108_REG_HOST_MAGIC_VALUE,
	.early_clk_ctrl_value = 0,
	.pager_base_address = MM8108_APPS_MAC_DMEM_ADDR_START,
	.aon_latch = MM8108_REG_AON_LATCH_ADDR,
	.aon_latch_mask = MM8108_REG_AON_LATCH_MASK,
	.aon_reset_usb_value = MM8108_REG_AON_RESET_USB_VALUE,
	.aon = MM8108_REG_AON_ADDR,
	.aon_count = MM8108_REG_AON_COUNT,
	.boot_address = MM8108_REG_APPS_BOOT_ADDR,
};

MODULE_FIRMWARE(MM81X_FW_DIR "/v" __stringify(MM81X_FW_VER_MAX) "/"
		MM8108_FW_BASE MM81X_FW_EXT);
