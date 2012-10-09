################################################################################
#
# Intel 10 Gigabit PCI Express Linux driver
# Copyright(c) 1999 - 2012 Intel Corporation.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
#
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
# Contact Information:
# Linux NICS <linux.nics@intel.com>
# e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
# Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
#
################################################################################

#
# Makefile for the Intel(R) 10GbE PCI Express ethernet driver
#

obj-$(CONFIG_IXGBE) += ixgbe.o

ixgbe-objs := ixgbe_main.o ixgbe_common.o ixgbe_ethtool.o \
              ixgbe_82599.o ixgbe_82598.o ixgbe_phy.o ixgbe_sriov.o \
              ixgbe_mbx.o ixgbe_x540.o ixgbe_lib.o

ixgbe-$(CONFIG_IXGBE_DCB) +=  ixgbe_dcb.o ixgbe_dcb_82598.o \
                              ixgbe_dcb_82599.o ixgbe_dcb_nl.o

ixgbe-$(CONFIG_IXGBE_PTP) += ixgbe_ptp.o
ixgbe-$(CONFIG_IXGBE_HWMON) += ixgbe_sysfs.o
ixgbe-$(CONFIG_FCOE:m=y) += ixgbe_fcoe.o
