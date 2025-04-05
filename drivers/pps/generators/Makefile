# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for PPS generators.
#

pps_gen_core-y			:= pps_gen.o sysfs.o
obj-$(CONFIG_PPS_GENERATOR)	:= pps_gen_core.o

obj-$(CONFIG_PPS_GENERATOR_DUMMY)   += pps_gen-dummy.o
obj-$(CONFIG_PPS_GENERATOR_PARPORT) += pps_gen_parport.o

ccflags-$(CONFIG_PPS_DEBUG) := -DDEBUG
