#
#  Makefile for the SJA1000 CAN controller drivers.
#

obj-$(CONFIG_CAN_SJA1000) += sja1000.o
obj-$(CONFIG_CAN_SJA1000_PLATFORM) += sja1000_platform.o
obj-$(CONFIG_CAN_EMS_PCI) += ems_pci.o

ccflags-$(CONFIG_CAN_DEBUG_DEVICES) := -DDEBUG
