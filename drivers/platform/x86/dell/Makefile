# SPDX-License-Identifier: GPL-2.0
#
# Makefile for linux/drivers/platform/x86/dell
# Dell x86 Platform-Specific Drivers
#

obj-$(CONFIG_ALIENWARE_WMI)		+= alienware-wmi.o
obj-$(CONFIG_DCDBAS)			+= dcdbas.o
obj-$(CONFIG_DELL_LAPTOP)		+= dell-laptop.o
obj-$(CONFIG_DELL_RBTN)			+= dell-rbtn.o
obj-$(CONFIG_DELL_RBU)			+= dell_rbu.o
obj-$(CONFIG_DELL_SMBIOS)		+= dell-smbios.o
dell-smbios-objs			:= dell-smbios-base.o
dell-smbios-$(CONFIG_DELL_SMBIOS_WMI)	+= dell-smbios-wmi.o
dell-smbios-$(CONFIG_DELL_SMBIOS_SMM)	+= dell-smbios-smm.o
obj-$(CONFIG_DELL_SMO8800)		+= dell-smo8800.o
obj-$(CONFIG_DELL_WMI)			+= dell-wmi.o
obj-$(CONFIG_DELL_WMI_AIO)		+= dell-wmi-aio.o
obj-$(CONFIG_DELL_WMI_DESCRIPTOR)	+= dell-wmi-descriptor.o
obj-$(CONFIG_DELL_WMI_LED)		+= dell-wmi-led.o
obj-$(CONFIG_DELL_WMI_SYSMAN)		+= dell-wmi-sysman/
