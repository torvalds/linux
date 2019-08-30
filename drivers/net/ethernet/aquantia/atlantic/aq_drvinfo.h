/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (C) 2014-2017 aQuantia Corporation. */

/* File aq_drvinfo.h: Declaration of common code for firmware info in sys.*/

#ifndef AQ_DRVINFO_H
#define AQ_DRVINFO_H

#include "aq_nic.h"
#include "aq_hw.h"
#include "hw_atl/hw_atl_utils.h"

int aq_drvinfo_init(struct net_device *ndev);

#endif /* AQ_DRVINFO_H */
