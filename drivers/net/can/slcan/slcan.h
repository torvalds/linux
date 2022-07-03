/* SPDX-License-Identifier: GPL-2.0
 * slcan.h - serial line CAN interface driver
 *
 * Copyright (C) Laurence Culhane <loz@holmes.demon.co.uk>
 * Copyright (C) Fred N. van Kempen <waltje@uwalt.nl.mugnet.org>
 * Copyright (C) Oliver Hartkopp <socketcan@hartkopp.net>
 * Copyright (C) 2022 Amarula Solutions, Dario Binacchi <dario.binacchi@amarulasolutions.com>
 *
 */

#ifndef _SLCAN_H
#define _SLCAN_H

bool slcan_err_rst_on_open(struct net_device *ndev);
int slcan_enable_err_rst_on_open(struct net_device *ndev, bool on);
void slcan_set_ethtool_ops(struct net_device *ndev);

#endif /* _SLCAN_H */
