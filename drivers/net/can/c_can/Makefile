# SPDX-License-Identifier: GPL-2.0-only
#
#  Makefile for the Bosch C_CAN controller drivers.
#

obj-$(CONFIG_CAN_C_CAN) += c_can.o

c_can-objs :=
c_can-objs += c_can_ethtool.o
c_can-objs += c_can_main.o

obj-$(CONFIG_CAN_C_CAN_PLATFORM) += c_can_platform.o
obj-$(CONFIG_CAN_C_CAN_PCI) += c_can_pci.o
