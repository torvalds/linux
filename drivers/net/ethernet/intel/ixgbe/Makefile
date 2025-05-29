# SPDX-License-Identifier: GPL-2.0
# Copyright(c) 1999 - 2024 Intel Corporation.
#
# Makefile for the Intel(R) 10GbE PCI Express ethernet driver
#

subdir-ccflags-y += -I$(src)
obj-$(CONFIG_IXGBE) += ixgbe.o

ixgbe-y := ixgbe_main.o ixgbe_common.o ixgbe_ethtool.o \
           ixgbe_82599.o ixgbe_82598.o ixgbe_phy.o ixgbe_sriov.o \
           ixgbe_mbx.o ixgbe_x540.o ixgbe_x550.o ixgbe_lib.o ixgbe_ptp.o \
           ixgbe_xsk.o ixgbe_e610.o devlink/devlink.o ixgbe_fw_update.o \
	   devlink/region.o

ixgbe-$(CONFIG_IXGBE_DCB) +=  ixgbe_dcb.o ixgbe_dcb_82598.o \
                              ixgbe_dcb_82599.o ixgbe_dcb_nl.o

ixgbe-$(CONFIG_IXGBE_HWMON) += ixgbe_sysfs.o
ixgbe-$(CONFIG_DEBUG_FS) += ixgbe_debugfs.o
ixgbe-$(CONFIG_FCOE:m=y) += ixgbe_fcoe.o
ixgbe-$(CONFIG_IXGBE_IPSEC) += ixgbe_ipsec.o
