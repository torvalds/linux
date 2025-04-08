# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the PPS core.
#

pps_core-y			:= pps.o kapi.o sysfs.o
pps_core-$(CONFIG_NTP_PPS)	+= kc.o
obj-$(CONFIG_PPS)		:= pps_core.o
obj-y				+= clients/
obj-$(CONFIG_PPS_GENERATOR)	+= generators/

ccflags-$(CONFIG_PPS_DEBUG) := -DDEBUG
