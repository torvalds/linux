#
# Makefile for the PPS core.
#

pps_core-y			:= pps.o kapi.o sysfs.o
obj-$(CONFIG_PPS)		:= pps_core.o

ccflags-$(CONFIG_PPS_DEBUG) := -DDEBUG
