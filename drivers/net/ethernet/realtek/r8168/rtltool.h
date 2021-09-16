/* SPDX-License-Identifier: GPL-2.0-only */
/*
################################################################################
#
# r8168 is the Linux device driver released for Realtek Gigabit Ethernet
# controllers with PCI-Express interface.
#
# Copyright(c) 2021 Realtek Semiconductor Corp. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, see <http://www.gnu.org/licenses/>.
#
# Author:
# Realtek NIC software team <nicfae@realtek.com>
# No. 2, Innovation Road II, Hsinchu Science Park, Hsinchu 300, Taiwan
#
################################################################################
*/

/************************************************************************************
 *  This product is covered by one or more of the following patents:
 *  US6,570,884, US6,115,776, and US6,327,625.
 ***********************************************************************************/

#ifndef _LINUX_RTLTOOL_H
#define _LINUX_RTLTOOL_H

#define SIOCRTLTOOL		SIOCDEVPRIVATE+1

enum rtl_cmd {
        RTLTOOL_READ_MAC=0,
        RTLTOOL_WRITE_MAC,
        RTLTOOL_READ_PHY,
        RTLTOOL_WRITE_PHY,
        RTLTOOL_READ_EPHY,
        RTLTOOL_WRITE_EPHY,
        RTLTOOL_READ_ERI,
        RTLTOOL_WRITE_ERI,
        RTLTOOL_READ_PCI,
        RTLTOOL_WRITE_PCI,
        RTLTOOL_READ_EEPROM,
        RTLTOOL_WRITE_EEPROM,

        RTL_READ_OOB_MAC,
        RTL_WRITE_OOB_MAC,

        RTL_ENABLE_PCI_DIAG,
        RTL_DISABLE_PCI_DIAG,

        RTL_READ_MAC_OCP,
        RTL_WRITE_MAC_OCP,

        RTL_DIRECT_READ_PHY_OCP,
        RTL_DIRECT_WRITE_PHY_OCP,

        RTLTOOL_INVALID
};

struct rtltool_cmd {
        __u32	cmd;
        __u32	offset;
        __u32	len;
        __u32	data;
};

enum mode_access {
        MODE_NONE=0,
        MODE_READ,
        MODE_WRITE
};

#ifdef __KERNEL__
int rtl8168_tool_ioctl(struct rtl8168_private *tp, struct ifreq *ifr);
#endif

#endif /* _LINUX_RTLTOOL_H */
