/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 */

/* File aq_ethtool.h: Declaration of ethertool related functions. */

#ifndef AQ_ETHTOOL_H
#define AQ_ETHTOOL_H

#include "aq_common.h"

extern const struct ethtool_ops aq_ethtool_ops;
#define AQ_PRIV_FLAGS_MASK   (AQ_HW_LOOPBACK_MASK)

#endif /* AQ_ETHTOOL_H */
