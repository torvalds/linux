#
#  Makefile for the Linux Controller Area Network drivers.
#

obj-$(CONFIG_CAN_VCAN)		+= vcan.o

obj-$(CONFIG_CAN_DEV)		+= can-dev.o
can-dev-y			:= dev.o

obj-$(CONFIG_CAN_SJA1000)	+= sja1000/

ccflags-$(CONFIG_CAN_DEBUG_DEVICES) := -DDEBUG
