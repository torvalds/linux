/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 *
 * Copyright (C) 2014-2019 aQuantia Corporation
 * Copyright (C) 2019-2020 Marvell International Ltd.
 */

/* File aq_drvinfo.h: Declaration of common code for firmware info in sys.*/

#ifndef AQ_DRVINFO_H
#define AQ_DRVINFO_H

struct net_device;

int aq_drvinfo_init(struct net_device *ndev);

#endif /* AQ_DRVINFO_H */
