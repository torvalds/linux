# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Marvell PPv2 driver.
#
obj-$(CONFIG_MVPP2) := mvpp2.o

mvpp2-y := mvpp2_main.o mvpp2_prs.o mvpp2_cls.o mvpp2_debugfs.o
mvpp2-$(CONFIG_MVPP2_PTP) += mvpp2_tai.o
