/*
 *   Copyright (c) 2011, 2012, Qualcomm Atheros Communications Inc.
 *   Copyright (c) 2014, I2SE GmbH
 *
 *   Permission to use, copy, modify, and/or distribute this software
 *   for any purpose with or without fee is hereby granted, provided
 *   that the above copyright notice and this permission notice appear
 *   in all copies.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *   WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *   WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
 *   THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
 *   CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 *   LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
 *   NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 *   CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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
