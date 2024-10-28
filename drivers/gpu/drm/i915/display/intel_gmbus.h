/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef __INTEL_GMBUS_H__
#define __INTEL_GMBUS_H__

#include <linux/types.h>

struct i2c_adapter;
struct intel_display;

#define GMBUS_PIN_DISABLED	0
#define GMBUS_PIN_SSC		1
#define GMBUS_PIN_VGADDC	2
#define GMBUS_PIN_PANEL		3
#define GMBUS_PIN_DPD_CHV	3 /* HDMID_CHV */
#define GMBUS_PIN_DPC		4 /* HDMIC */
#define GMBUS_PIN_DPB		5 /* SDVO, HDMIB */
#define GMBUS_PIN_DPD		6 /* HDMID */
#define GMBUS_PIN_RESERVED	7 /* 7 reserved */
#define GMBUS_PIN_1_BXT		1 /* BXT+ (atom) and CNP+ (big core) */
#define GMBUS_PIN_2_BXT		2
#define GMBUS_PIN_3_BXT		3
#define GMBUS_PIN_4_CNP		4
#define GMBUS_PIN_5_MTP		5
#define GMBUS_PIN_9_TC1_ICP	9
#define GMBUS_PIN_10_TC2_ICP	10
#define GMBUS_PIN_11_TC3_ICP	11
#define GMBUS_PIN_12_TC4_ICP	12
#define GMBUS_PIN_13_TC5_TGP	13
#define GMBUS_PIN_14_TC6_TGP	14

#define GMBUS_NUM_PINS	15 /* including 0 */

int intel_gmbus_setup(struct intel_display *display);
void intel_gmbus_teardown(struct intel_display *display);
bool intel_gmbus_is_valid_pin(struct intel_display *display, unsigned int pin);
int intel_gmbus_output_aksv(struct i2c_adapter *adapter);

struct i2c_adapter *
intel_gmbus_get_adapter(struct intel_display *display, unsigned int pin);
void intel_gmbus_force_bit(struct i2c_adapter *adapter, bool force_bit);
bool intel_gmbus_is_forced_bit(struct i2c_adapter *adapter);
void intel_gmbus_reset(struct intel_display *display);

void intel_gmbus_irq_handler(struct intel_display *display);

#endif /* __INTEL_GMBUS_H__ */
