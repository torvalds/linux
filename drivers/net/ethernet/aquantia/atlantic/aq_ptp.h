/* SPDX-License-Identifier: GPL-2.0-only */
/* Aquantia Corporation Network Driver
 * Copyright (C) 2014-2019 Aquantia Corporation. All rights reserved
 */

/* File aq_ptp.h: Declaration of PTP functions.
 */
#ifndef AQ_PTP_H
#define AQ_PTP_H

#include <linux/net_tstamp.h>
#include <linux/version.h>

/* Common functions */
int aq_ptp_init(struct aq_nic_s *aq_nic, unsigned int idx_vec);

void aq_ptp_unregister(struct aq_nic_s *aq_nic);
void aq_ptp_free(struct aq_nic_s *aq_nic);

void aq_ptp_clock_init(struct aq_nic_s *aq_nic);

#endif /* AQ_PTP_H */
