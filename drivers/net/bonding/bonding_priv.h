/* SPDX-License-Identifier: GPL-1.0+ */
/*
 * Bond several ethernet interfaces into a Cisco, running 'Etherchannel'.
 *
 * Portions are (c) Copyright 1995 Simon "Guru Aleph-Null" Janes
 * NCM: Network and Communications Management, Inc.
 *
 * BUT, I'm the one who modified it for ethernet, so:
 * (c) Copyright 1999, Thomas Davis, tadavis@lbl.gov
 *
 */

#ifndef _BONDING_PRIV_H
#define _BONDING_PRIV_H
#include <generated/utsrelease.h>

#define DRV_NAME	"bonding"
#define DRV_DESCRIPTION	"Ethernet Channel Bonding Driver"

#define bond_version DRV_DESCRIPTION ": v" UTS_RELEASE "\n"

#endif
