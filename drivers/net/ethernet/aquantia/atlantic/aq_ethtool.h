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

#define SFF_8472_ID_ADDR 0x50
#define SFF_8472_DIAGNOSTICS_ADDR 0x51

#define SFF_8472_COMP_ADDR	0x5e
#define SFF_8472_DOM_TYPE_ADDR	0x5c

#define SFF_8472_ADDRESS_CHANGE_REQ_MASK 0x4

#endif /* AQ_ETHTOOL_H */
