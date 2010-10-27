#
#  Makefile for the SJA1000 CAN controller drivers.
#

obj-$(CONFIG_CAN_SJA1000) += sja1000.o
obj-$(CONFIG_CAN_SJA1000_ISA) += sja1000_isa.o
obj-$(CONFIG_CAN_SJA1000_PLATFORM) += sja1000_platform.o
obj-$(CONFIG_CAN_SJA1000_OF_PLATFORM) += sja1000_of_platform.o
obj-$(CONFIG_CAN_EMS_PCI) += ems_pci.o
obj-$(CONFIG_CAN_KVASER_PCI) += kvaser_pci.o
obj-$(CONFIG_CAN_PLX_PCI) += plx_pci.o

ccflags-$(CONFIG_CAN_DEBUG_DEVICES) := -DDEBUG
