/* SPDX-License-Identifier: ISC */
/*
 * Copyright (C) 2016 Felix Fietkau <nbd@nbd.name>
 */

#ifndef __MT76x2_MCU_H
#define __MT76x2_MCU_H

#include "../mt76x02_mcu.h"

/* Register definitions */
#define MT_MCU_CPU_CTL			0x0704
#define MT_MCU_CLOCK_CTL		0x0708
#define MT_MCU_PCIE_REMAP_BASE1		0x0740
#define MT_MCU_PCIE_REMAP_BASE2		0x0744
#define MT_MCU_PCIE_REMAP_BASE3		0x0748

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

int mt76x2_mcu_tssi_comp(struct mt76x02_dev *dev,
			 struct mt76x2_tssi_comp *tssi_data);
int mt76x2_mcu_init_gain(struct mt76x02_dev *dev, u8 channel, u32 gain,
			 bool force);

#endif
