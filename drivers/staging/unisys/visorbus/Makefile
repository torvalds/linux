#
# Makefile for Unisys visorbus
#

obj-$(CONFIG_UNISYS_VISORBUS)	+= visorbus.o

visorbus-y := visorbus_main.o
visorbus-y += visorchannel.o
visorbus-y += visorchipset.o
visorbus-y += periodic_work.o

ccflags-y += -Idrivers/staging/unisys/include
ccflags-y += -Idrivers/staging/unisys/common-spar/include
ccflags-y += -Idrivers/staging/unisys/common-spar/include/channels
ccflags-y += -Idrivers/staging/unisys/visorutil
ccflags-y += -Idrivers/staging/unisys/visorbus
