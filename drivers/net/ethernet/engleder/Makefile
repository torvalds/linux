# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Engleder Ethernet drivers
#

obj-$(CONFIG_TSNEP) += tsnep.o

tsnep-objs := tsnep_main.o tsnep_ethtool.o tsnep_ptp.o tsnep_tc.o \
	      $(tsnep-y)
tsnep-$(CONFIG_TSNEP_SELFTESTS) += tsnep_selftests.o
