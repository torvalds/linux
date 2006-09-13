/*

  Broadcom BCM43xx wireless driver

  ethtool support

  Copyright (c) 2006 Jason Lunz <lunz@falooley.org>

  Some code in this file is derived from the 8139too.c driver
  Copyright (C) 2002 Jeff Garzik

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include "bcm43xx.h"
#include "bcm43xx_ethtool.h"

#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/utsrelease.h>


static void bcm43xx_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct bcm43xx_private *bcm = bcm43xx_priv(dev);

	strncpy(info->driver, KBUILD_MODNAME, sizeof(info->driver));
	strncpy(info->version, UTS_RELEASE, sizeof(info->version));
	strncpy(info->bus_info, pci_name(bcm->pci_dev), ETHTOOL_BUSINFO_LEN);
}

const struct ethtool_ops bcm43xx_ethtool_ops = {
	.get_drvinfo = bcm43xx_get_drvinfo,
	.get_link = ethtool_op_get_link,
};
