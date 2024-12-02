# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for PPS clients.
#

obj-$(CONFIG_PPS_CLIENT_KTIMER)	+= pps-ktimer.o
obj-$(CONFIG_PPS_CLIENT_LDISC)	+= pps-ldisc.o
obj-$(CONFIG_PPS_CLIENT_PARPORT) += pps_parport.o
obj-$(CONFIG_PPS_CLIENT_GPIO)	+= pps-gpio.o

ccflags-$(CONFIG_PPS_DEBUG) := -DDEBUG
