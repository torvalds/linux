#
# Makefile for Unisys timskmod
#

obj-$(CONFIG_UNISYS_VISORUTIL)	+= visorutil.o

visorutil-y := charqueue.o  periodic_work.o  memregion_direct.o visorkmodutils.o

ccflags-y += -Idrivers/staging/unisys/include
