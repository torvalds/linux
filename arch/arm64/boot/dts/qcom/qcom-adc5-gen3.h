/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#ifndef __DTS_ARM64_QCOM_ADC5_GEN3_H__
#define __DTS_ARM64_QCOM_ADC5_GEN3_H__

/* ADC channels for PMIC5 Gen3 */

#define VIRT_CHAN(sid, chan)			((sid) << 8 | (chan))

#define ADC5_GEN3_REF_GND(sid)			VIRT_CHAN(sid, 0x00)
#define ADC5_GEN3_1P25VREF(sid)			VIRT_CHAN(sid, 0x01)
#define ADC5_GEN3_VREF_VADC(sid)		VIRT_CHAN(sid, 0x02)
#define ADC5_GEN3_DIE_TEMP(sid)			VIRT_CHAN(sid, 0x03)

#define ADC5_GEN3_AMUX1_THM(sid)		VIRT_CHAN(sid, 0x04)
#define ADC5_GEN3_AMUX2_THM(sid)		VIRT_CHAN(sid, 0x05)
#define ADC5_GEN3_AMUX3_THM(sid)		VIRT_CHAN(sid, 0x06)
#define ADC5_GEN3_AMUX4_THM(sid)		VIRT_CHAN(sid, 0x07)
#define ADC5_GEN3_AMUX5_THM(sid)		VIRT_CHAN(sid, 0x08)
#define ADC5_GEN3_AMUX6_THM(sid)		VIRT_CHAN(sid, 0x09)
#define ADC5_GEN3_AMUX1_GPIO(sid)		VIRT_CHAN(sid, 0x0a)
#define ADC5_GEN3_AMUX2_GPIO(sid)		VIRT_CHAN(sid, 0x0b)
#define ADC5_GEN3_AMUX3_GPIO(sid)		VIRT_CHAN(sid, 0x0c)
#define ADC5_GEN3_AMUX4_GPIO(sid)		VIRT_CHAN(sid, 0x0d)

#define ADC5_GEN3_CHG_TEMP(sid)			VIRT_CHAN(sid, 0x10)
#define ADC5_GEN3_USB_SNS_V_16(sid)		VIRT_CHAN(sid, 0x11)
#define ADC5_GEN3_VIN_DIV16_MUX(sid)		VIRT_CHAN(sid, 0x12)
#define ADC5_GEN3_VREF_BAT_THERM(sid)		VIRT_CHAN(sid, 0x15)
#define ADC5_GEN3_IIN_FB(sid)			VIRT_CHAN(sid, 0x17)
#define ADC5_GEN3_TEMP_ALARM_LITE(sid)		VIRT_CHAN(sid, 0x18)
#define ADC5_GEN3_IIN_SMB(sid)			VIRT_CHAN(sid, 0x19)
#define ADC5_GEN3_ICHG_SMB(sid)			VIRT_CHAN(sid, 0x1b)
#define ADC5_GEN3_ICHG_FB(sid)			VIRT_CHAN(sid, 0xa1)

/* 30k pull-up */
#define ADC5_GEN3_AMUX1_THM_30K_PU(sid)		VIRT_CHAN(sid, 0x24)
#define ADC5_GEN3_AMUX2_THM_30K_PU(sid)		VIRT_CHAN(sid, 0x25)
#define ADC5_GEN3_AMUX3_THM_30K_PU(sid)		VIRT_CHAN(sid, 0x26)
#define ADC5_GEN3_AMUX4_THM_30K_PU(sid)		VIRT_CHAN(sid, 0x27)
#define ADC5_GEN3_AMUX5_THM_30K_PU(sid)		VIRT_CHAN(sid, 0x28)
#define ADC5_GEN3_AMUX6_THM_30K_PU(sid)		VIRT_CHAN(sid, 0x29)
#define ADC5_GEN3_AMUX1_GPIO_30K_PU(sid)	VIRT_CHAN(sid, 0x2a)
#define ADC5_GEN3_AMUX2_GPIO_30K_PU(sid)	VIRT_CHAN(sid, 0x2b)
#define ADC5_GEN3_AMUX3_GPIO_30K_PU(sid)	VIRT_CHAN(sid, 0x2c)
#define ADC5_GEN3_AMUX4_GPIO_30K_PU(sid)	VIRT_CHAN(sid, 0x2d)

/* 100k pull-up */
#define ADC5_GEN3_AMUX1_THM_100K_PU(sid)	VIRT_CHAN(sid, 0x44)
#define ADC5_GEN3_AMUX2_THM_100K_PU(sid)	VIRT_CHAN(sid, 0x45)
#define ADC5_GEN3_AMUX3_THM_100K_PU(sid)	VIRT_CHAN(sid, 0x46)
#define ADC5_GEN3_AMUX4_THM_100K_PU(sid)	VIRT_CHAN(sid, 0x47)
#define ADC5_GEN3_AMUX5_THM_100K_PU(sid)	VIRT_CHAN(sid, 0x48)
#define ADC5_GEN3_AMUX6_THM_100K_PU(sid)	VIRT_CHAN(sid, 0x49)
#define ADC5_GEN3_AMUX1_GPIO_100K_PU(sid)	VIRT_CHAN(sid, 0x4a)
#define ADC5_GEN3_AMUX2_GPIO_100K_PU(sid)	VIRT_CHAN(sid, 0x4b)
#define ADC5_GEN3_AMUX3_GPIO_100K_PU(sid)	VIRT_CHAN(sid, 0x4c)
#define ADC5_GEN3_AMUX4_GPIO_100K_PU(sid)	VIRT_CHAN(sid, 0x4d)

/* 400k pull-up */
#define ADC5_GEN3_AMUX1_THM_400K_PU(sid)	VIRT_CHAN(sid, 0x64)
#define ADC5_GEN3_AMUX2_THM_400K_PU(sid)	VIRT_CHAN(sid, 0x65)
#define ADC5_GEN3_AMUX3_THM_400K_PU(sid)	VIRT_CHAN(sid, 0x66)
#define ADC5_GEN3_AMUX4_THM_400K_PU(sid)	VIRT_CHAN(sid, 0x67)
#define ADC5_GEN3_AMUX5_THM_400K_PU(sid)	VIRT_CHAN(sid, 0x68)
#define ADC5_GEN3_AMUX6_THM_400K_PU(sid)	VIRT_CHAN(sid, 0x69)
#define ADC5_GEN3_AMUX1_GPIO_400K_PU(sid)	VIRT_CHAN(sid, 0x6a)
#define ADC5_GEN3_AMUX2_GPIO_400K_PU(sid)	VIRT_CHAN(sid, 0x6b)
#define ADC5_GEN3_AMUX3_GPIO_400K_PU(sid)	VIRT_CHAN(sid, 0x6c)
#define ADC5_GEN3_AMUX4_GPIO_400K_PU(sid)	VIRT_CHAN(sid, 0x6d)

/* 1/3 Divider */
#define ADC5_GEN3_AMUX1_GPIO_DIV3(sid)		VIRT_CHAN(sid, 0x8a)
#define ADC5_GEN3_AMUX2_GPIO_DIV3(sid)		VIRT_CHAN(sid, 0x8b)
#define ADC5_GEN3_AMUX3_GPIO_DIV3(sid)		VIRT_CHAN(sid, 0x8c)
#define ADC5_GEN3_AMUX4_GPIO_DIV3(sid)		VIRT_CHAN(sid, 0x8d)

#define ADC5_GEN3_VPH_PWR(sid)			VIRT_CHAN(sid, 0x8e)
#define ADC5_GEN3_VBAT_SNS_QBG(sid)		VIRT_CHAN(sid, 0x8f)

#define ADC5_GEN3_VBAT_SNS_CHGR(sid)		VIRT_CHAN(sid, 0x94)
#define ADC5_GEN3_VBAT_2S_MID_QBG(sid)		VIRT_CHAN(sid, 0x96)
#define ADC5_GEN3_VBAT_2S_MID_CHGR(sid)		VIRT_CHAN(sid, 0x9d)

#endif /* __DTS_ARM64_QCOM_ADC5_GEN3_H__ */
