#
# Makefile for the Linux ACPI interpreter
#

ccflags-y			:= -Os
ccflags-$(CONFIG_ACPI_DEBUG)	+= -DACPI_DEBUG_OUTPUT

#
# ACPI Boot-Time Table Parsing
#
obj-y				+= tables.o
obj-$(CONFIG_X86)		+= blacklist.o

#
# ACPI Core Subsystem (Interpreter)
#
obj-y				+= osl.o utils.o reboot.o\
					acpica/

# sleep related files
obj-y				+= wakeup.o
obj-y				+= main.o
obj-$(CONFIG_ACPI_SLEEP)	+= proc.o


#
# ACPI Bus and Device Drivers
#
processor-objs	+= processor_core.o processor_throttling.o \
				processor_idle.o processor_thermal.o
ifdef CONFIG_CPU_FREQ
processor-objs	+= processor_perflib.o
endif

obj-y				+= bus.o glue.o
obj-y				+= scan.o
# Keep EC driver first. Initialization of others depend on it.
obj-y				+= ec.o
obj-$(CONFIG_ACPI_AC) 		+= ac.o
obj-$(CONFIG_ACPI_BATTERY)	+= battery.o
obj-$(CONFIG_ACPI_BUTTON)	+= button.o
obj-$(CONFIG_ACPI_FAN)		+= fan.o
obj-$(CONFIG_ACPI_DOCK)		+= dock.o
obj-$(CONFIG_ACPI_VIDEO)	+= video.o
ifdef CONFIG_ACPI_VIDEO
obj-y				+= video_detect.o
endif

obj-y				+= pci_root.o pci_link.o pci_irq.o pci_bind.o
obj-$(CONFIG_ACPI_PCI_SLOT)	+= pci_slot.o
obj-$(CONFIG_ACPI_PROCESSOR)	+= processor.o
obj-$(CONFIG_ACPI_CONTAINER)	+= container.o
obj-$(CONFIG_ACPI_THERMAL)	+= thermal.o
obj-y				+= power.o
obj-$(CONFIG_ACPI_SYSTEM)	+= system.o event.o
obj-$(CONFIG_ACPI_DEBUG)	+= debug.o
obj-$(CONFIG_ACPI_NUMA)		+= numa.o
obj-$(CONFIG_ACPI_HOTPLUG_MEMORY)	+= acpi_memhotplug.o
obj-$(CONFIG_ACPI_PROCFS_POWER)	+= cm_sbs.o
obj-$(CONFIG_ACPI_SBS)		+= sbshc.o
obj-$(CONFIG_ACPI_SBS)		+= sbs.o
