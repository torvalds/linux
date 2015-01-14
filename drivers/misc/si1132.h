/*
 * si1132.c - Support for Silabs si1132 combined ambient light and
 * proximity sensor.
 * Copyright (C) 2014 Hardkernel Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _SI1132_H
#define _SI1132_H

#include <linux/regmap.h>

#define SI1132_NAME		"si1132"

/* Registers */
#define SI1132_REG_PART_ID		0x00
#define SI1132_REG_REV_ID		0x01
#define SI1132_REG_SEQ_Ir		0x02
#define SI1132_REG_INT_CFG		0x03
#define SI1132_REG_IRQ_ENABLE		0x04
#define SI1132_REG_HW_KEY		0x07
#define SI1132_REG_MEAS_RATE		0x08
#define SI1132_REG_MEAS_RATE1		0x09
#define SI1132_REG_UCOEF0		0x13
#define SI1132_REG_UCOEF1		0x14
#define SI1132_REG_UCOEF2		0x15
#define SI1132_REG_UCOEF3		0x16
#define SI1132_REG_PARAM_WR		0x17
#define SI1132_REG_COMMAND		0x18
#define SI1132_REG_RESPONSE		0x20
#define SI1132_REG_IRQ_STATUS		0x21
#define SI1132_REG_ALSVIS_DATA0		0x22
#define SI1132_REG_ALSVIS_DATA1		0x23
#define SI1132_REG_ALSIR_DATA0		0x24
#define SI1132_REG_ALSIR_DATA1		0x25
#define SI1132_REG_AUX_DATA0		0x2c
#define SI1132_REG_AUX_DATA1		0x2d
#define SI1132_REG_PARAM_RD		0x2e
#define SI1132_REG_CHIP_STAT		0x30

/* Parameter offsets */
#define SI1132_PARAM_I2C_ADDR		0x00
#define SI1132_PARAM_CHLIST		0x01
#define SI1132_PARAM_ALS_ENCODING	0x06
#define SI1132_PARAM_ALSIR_ADC_MUX	0x0e
#define SI1132_PARAM_AUX_ADC_MUX	0x0f
#define SI1132_PARAM_ALSVIS_ADC_COUNTER	0x10
#define SI1132_PARAM_ALSVIS_ADC_GAIN	0x11
#define SI1132_PARAM_ALSVIS_ADC_MISC	0x12
#define SI1132_PARAM_ALSIR_ADC_COUNTER	0x1d
#define SI1132_PARAM_ALSIR_ADC_GAIN	0x1e
#define SI1132_PARAM_ALSIR_ADC_MISC	0x1f

/* Channel enable masks for CHLIST parameter */
#define SI1132_CHLIST_EN_ALSVIS		0x10
#define SI1132_CHLIST_EN_ALSIR		0x20
#define SI1132_CHLIST_EN_AUX		0x40
#define SI1132_CHLIST_EN_UV		0x80

/* Signal range mask for ADC_MISC parameter */
#define SI1132_MISC_RANGE		0x20

/* Commands for REG_COMMAND */
#define SI1132_COMMAND_NOP		0x00
#define SI1132_COMMAND_RESET		0x01
#define SI1132_COMMAND_BUSADDR		0x02
#define SI1132_COMMAND_ALS_FORCE	0x06
#define SI1132_COMMAND_ALS_PAUSE	0x0a
#define SI1132_COMMAND_ALS_AUTO		0x0e
#define SI1132_COMMAND_GET_CAL		0x12
#define SI1132_COMMAND_PARAM_QUERY	0x80
#define SI1132_COMMAND_PARAM_SET	0xa0
#define SI1132_COMMAND_PARAM_AND	0xc0
#define SI1132_COMMAND_PARAM_OR		0xe0

/* Interrupt configuration masks for INT_CFG register */
#define SI1132_INT_CFG_OE              0x01 /* enable interrupt */
#define SI1132_INT_CFG_MODE            0x02 /* auto reset interrupt pin */

/* Interrupt enable masks for IRQ_ENABLE register */
#define SI1132_CMD_IE                  0x20
#define SI1132_ALS_INT1_IE             0x02
#define SI1132_ALS_INT0_IE             0x01

#endif
