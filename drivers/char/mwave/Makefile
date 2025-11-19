# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for ACP Modem (Mwave).
#
# See the README file in this directory for more info. <paulsch@us.ibm.com>
#

obj-$(CONFIG_MWAVE) += mwave.o

mwave-y := mwavedd.o smapi.o tp3780i.o 3780i.o

# To compile in lots (~20 KiB) of run-time enablable printk()s for debugging:
ccflags-y += -DMW_TRACE
