/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File hw_atl_a0.h: Declaration of abstract interface for Atlantic hardware
 * specific functions.
 */

#ifndef HW_ATL_A0_H
#define HW_ATL_A0_H

#include "../aq_common.h"

extern const struct aq_hw_caps_s hw_atl_a0_caps_aqc100;
extern const struct aq_hw_caps_s hw_atl_a0_caps_aqc107;
extern const struct aq_hw_caps_s hw_atl_a0_caps_aqc108;
extern const struct aq_hw_caps_s hw_atl_a0_caps_aqc109;

extern const struct aq_hw_ops hw_atl_ops_a0;

#endif /* HW_ATL_A0_H */
