/*
 * Copyright (C) 2010 Information System Products Co.,Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AMI_HW_H
#define AMI_HW_H

#define	AMI_I2C_BUS_NUM			2

#ifdef	AMI304_MODEL
#define AMI_I2C_ADDRESS			0x0F
#else
#define AMI_I2C_ADDRESS			0x0E
#endif

#define AMI_GPIO_INT			152
#define AMI_GPIO_DRDY			153

/* AMI-Sensor Internal Register Address
 *(Please refer to AMI-Sensor Specifications)
 */
#define AMI_MOREINFO_CMDCODE		0x0d
#define AMI_WHOIAM_CMDCODE		0x0f
#define AMI_REG_DATAX			0x10
#define AMI_REG_DATAY			0x12
#define AMI_REG_DATAZ			0x14
#define AMI_REG_STA1			0x18
#define AMI_REG_CTRL1			0x1b
#define AMI_REG_CTRL2			0x1c
#define AMI_REG_CTRL3			0x1d
#define AMI_REG_B0X			0x20
#define AMI_REG_B0Y			0x22
#define AMI_REG_B0Z			0x24
#define AMI_REG_CTRL5			0x40
#define AMI_REG_CTRL4			0x5c
#define AMI_REG_TEMP			0x60
#define AMI_REG_DELAYX			0x68
#define AMI_REG_DELAYY			0x6e
#define AMI_REG_DELAYZ			0x74
#define AMI_REG_OFFX			0x6c
#define AMI_REG_OFFY			0x72
#define AMI_REG_OFFZ			0x78
#define AMI_FINEOUTPUT_X		0x90
#define AMI_FINEOUTPUT_Y		0x92
#define AMI_FINEOUTPUT_Z		0x94
#define AMI_REG_SENX			0x96
#define AMI_REG_SENY			0x98
#define AMI_REG_SENZ			0x9a
#define AMI_REG_GAINX			0x9c
#define AMI_REG_GAINY			0x9e
#define AMI_REG_GAINZ			0xa0
#define AMI_GETVERSION_CMDCODE		0xe8
#define AMI_SERIALNUMBER_CMDCODE	0xea
#define AMI_REG_B0OTPX			0xa2
#define AMI_REG_B0OTPY			0xb8
#define AMI_REG_B0OTPZ			0xce
#define AMI_REG_OFFOTPX			0xf8
#define AMI_REG_OFFOTPY			0xfa
#define AMI_REG_OFFOTPZ			0xfc

/* AMI-Sensor Control Bit  (Please refer to AMI-Sensor Specifications) */
#define AMI_CTRL1_PC1			0x80
#define AMI_CTRL1_FS1_FORCE		0x02
#define AMI_CTRL1_ODR1			0x10
#define AMI_CTRL2_DREN			0x08
#define AMI_CTRL2_DRP			0x04
#define AMI_CTRL3_FORCE_BIT		0x40
#define AMI_CTRL3_B0_LO_BIT		0x10
#define AMI_CTRL3_SRST_BIT		0x80
#define AMI_CTRL4_HS			0xa07e
#define AMI_CTRL4_AB			0x0001
#define AMI_STA1_DRDY_BIT		0x40
#define AMI_STA1_DOR_BIT		0x20

#endif
