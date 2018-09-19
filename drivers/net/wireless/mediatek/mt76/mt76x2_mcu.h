/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __MT76x2_MCU_H
#define __MT76x2_MCU_H

#include "mt76x02_mcu.h"

/* Register definitions */
#define MT_MCU_CPU_CTL			0x0704
#define MT_MCU_CLOCK_CTL		0x0708
#define MT_MCU_PCIE_REMAP_BASE1		0x0740
#define MT_MCU_PCIE_REMAP_BASE2		0x0744
#define MT_MCU_PCIE_REMAP_BASE3		0x0748

#define MT_LED_CTRL			0x0770
#define MT_LED_CTRL_REPLAY(_n)		BIT(0 + (8 * (_n)))
#define MT_LED_CTRL_POLARITY(_n)	BIT(1 + (8 * (_n)))
#define MT_LED_CTRL_TX_BLINK_MODE(_n)	BIT(2 + (8 * (_n)))
#define MT_LED_CTRL_KICK(_n)		BIT(7 + (8 * (_n)))

#define MT_LED_TX_BLINK_0		0x0774
#define MT_LED_TX_BLINK_1		0x0778

#define MT_LED_S0_BASE			0x077C
#define MT_LED_S0(_n)			(MT_LED_S0_BASE + 8 * (_n))
#define MT_LED_S1_BASE			0x0780
#define MT_LED_S1(_n)			(MT_LED_S1_BASE + 8 * (_n))
#define MT_LED_STATUS_OFF_MASK		GENMASK(31, 24)
#define MT_LED_STATUS_OFF(_v)		(((_v) << __ffs(MT_LED_STATUS_OFF_MASK)) & \
					 MT_LED_STATUS_OFF_MASK)
#define MT_LED_STATUS_ON_MASK		GENMASK(23, 16)
#define MT_LED_STATUS_ON(_v)		(((_v) << __ffs(MT_LED_STATUS_ON_MASK)) & \
					 MT_LED_STATUS_ON_MASK)
#define MT_LED_STATUS_DURATION_MASK	GENMASK(15, 8)
#define MT_LED_STATUS_DURATION(_v)	(((_v) << __ffs(MT_LED_STATUS_DURATION_MASK)) & \
					 MT_LED_STATUS_DURATION_MASK)

#define MT_MCU_ROM_PATCH_OFFSET		0x80000
#define MT_MCU_ROM_PATCH_ADDR		0x90000

#define MT_MCU_ILM_OFFSET		0x80000

#define MT_MCU_DLM_OFFSET		0x100000
#define MT_MCU_DLM_ADDR			0x90000
#define MT_MCU_DLM_ADDR_E3		0x90800

enum mcu_calibration {
	MCU_CAL_R = 1,
	MCU_CAL_TEMP_SENSOR,
	MCU_CAL_RXDCOC,
	MCU_CAL_RC,
	MCU_CAL_SX_LOGEN,
	MCU_CAL_LC,
	MCU_CAL_TX_LOFT,
	MCU_CAL_TXIQ,
	MCU_CAL_TSSI,
	MCU_CAL_TSSI_COMP,
	MCU_CAL_DPD,
	MCU_CAL_RXIQC_FI,
	MCU_CAL_RXIQC_FD,
	MCU_CAL_PWRON,
	MCU_CAL_TX_SHAPING,
};

enum mt76x2_mcu_cr_mode {
	MT_RF_CR,
	MT_BBP_CR,
	MT_RF_BBP_CR,
	MT_HL_TEMP_CR_UPDATE,
};

struct mt76x2_tssi_comp {
	u8 pa_mode;
	u8 cal_mode;
	u16 pad;

	u8 slope0;
	u8 slope1;
	u8 offset0;
	u8 offset1;
} __packed __aligned(4);

int mt76x2_mcu_tssi_comp(struct mt76x2_dev *dev, struct mt76x2_tssi_comp *tssi_data);
int mt76x2_mcu_init_gain(struct mt76x2_dev *dev, u8 channel, u32 gain,
			 bool force);

#endif
