# SPDX-License-Identifier: GPL-2.0-only
#
#  Makefile for the Bosch M_CAN controller driver.
#

obj-$(CONFIG_CAN_M_CAN) += m_can.o
obj-$(CONFIG_CAN_M_CAN_PCI) += m_can_pci.o
obj-$(CONFIG_CAN_M_CAN_PLATFORM) += m_can_platform.o
obj-$(CONFIG_CAN_M_CAN_TCAN4X5X) += tcan4x5x.o

tcan4x5x-objs :=
tcan4x5x-objs += tcan4x5x-core.o
tcan4x5x-objs += tcan4x5x-regmap.o
