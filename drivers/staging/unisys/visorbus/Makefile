#
# Makefile for Unisys visorbus
#

obj-$(CONFIG_UNISYS_VISORBUS)	+= visorbus.o

visorbus-y := visorbus_main.o
visorbus-y += visorchannel.o
visorbus-y += visorchipset.o
visorbus-y += periodic_work.o

ccflags-y += -Idrivers/staging/unisys/include
