#
# Makefile for Unisys uislib
#

obj-$(CONFIG_UNISYS_UISLIB)	+= visoruislib.o

visoruislib-y := uislib.o uisqueue.o  uisthread.o  uisutils.o

ccflags-y += -Idrivers/staging/unisys/include
ccflags-y += -Idrivers/staging/unisys/channels
ccflags-y += -Idrivers/staging/unisys/visorchipset
ccflags-y += -Idrivers/staging/unisys/sparstopdriver
ccflags-y += -Idrivers/staging/unisys/common-spar/include
ccflags-y += -Idrivers/staging/unisys/common-spar/include/channels
