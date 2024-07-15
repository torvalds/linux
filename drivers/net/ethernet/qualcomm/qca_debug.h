/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/*
 *   Copyright (c) 2011, 2012, Qualcomm Atheros Communications Inc.
 *   Copyright (c) 2014, I2SE GmbH
 */

/*   This file contains debugging routines for use in the QCA7K driver.
 */

#ifndef _QCA_DEBUG_H
#define _QCA_DEBUG_H

#include "qca_spi.h"

void qcaspi_init_device_debugfs(struct qcaspi *qca);

void qcaspi_remove_device_debugfs(struct qcaspi *qca);

void qcaspi_set_ethtool_ops(struct net_device *dev);

#endif /* _QCA_DEBUG_H */
