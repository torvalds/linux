#
# Makefile for Unisys visorbus
#

obj-$(CONFIG_UNISYS_VISORBUS)	+= visorbus.o

visorbus-y := visorbus_main.o devmajorminor_attr.o channel_attr.o
visorbus-y	+= visorchannel_funcs.o

ccflags-y += -Idrivers/staging/unisys/include
ccflags-y += -Idrivers/staging/unisys/visorchipset
ccflags-y += -Idrivers/staging/unisys/common-spar/include
ccflags-y += -Idrivers/staging/unisys/common-spar/include/channels
ccflags-y += -Idrivers/staging/unisys/visorutil
