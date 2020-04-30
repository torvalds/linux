/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 */

/* File hw_atl_b0.h: Declaration of abstract interface for Atlantic hardware
 * specific functions.
 */

#ifndef HW_ATL_B0_H
#define HW_ATL_B0_H

#include "../aq_common.h"

extern const struct aq_hw_caps_s hw_atl_b0_caps_aqc100;
extern const struct aq_hw_caps_s hw_atl_b0_caps_aqc107;
extern const struct aq_hw_caps_s hw_atl_b0_caps_aqc108;
extern const struct aq_hw_caps_s hw_atl_b0_caps_aqc109;

#define hw_atl_b0_caps_aqc111 hw_atl_b0_caps_aqc108
#define hw_atl_b0_caps_aqc112 hw_atl_b0_caps_aqc109

#define hw_atl_b0_caps_aqc100s hw_atl_b0_caps_aqc100
#define hw_atl_b0_caps_aqc107s hw_atl_b0_caps_aqc107
#define hw_atl_b0_caps_aqc108s hw_atl_b0_caps_aqc108
#define hw_atl_b0_caps_aqc109s hw_atl_b0_caps_aqc109

#define hw_atl_b0_caps_aqc111s hw_atl_b0_caps_aqc108
#define hw_atl_b0_caps_aqc112s hw_atl_b0_caps_aqc109

extern const struct aq_hw_ops hw_atl_ops_b0;

#define hw_atl_ops_b1 hw_atl_ops_b0

int hw_atl_b0_hw_rss_hash_set(struct aq_hw_s *self,
			      struct aq_rss_parameters *rss_params);
int hw_atl_b0_hw_rss_set(struct aq_hw_s *self,
			 struct aq_rss_parameters *rss_params);

int hw_atl_b0_hw_start(struct aq_hw_s *self);

int hw_atl_b0_hw_irq_enable(struct aq_hw_s *self, u64 mask);
int hw_atl_b0_hw_irq_disable(struct aq_hw_s *self, u64 mask);
int hw_atl_b0_hw_irq_read(struct aq_hw_s *self, u64 *mask);

int hw_atl_b0_hw_packet_filter_set(struct aq_hw_s *self,
				   unsigned int packet_filter);

#endif /* HW_ATL_B0_H */
