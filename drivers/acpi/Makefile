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
obj-y				+= acpi.o \
					acpica/

# All the builtin files are in the "acpi." module_param namespace.
acpi-y				+= osl.o utils.o reboot.o
acpi-y				+= nvs.o

# Power management related files
acpi-y				+= wakeup.o
acpi-y				+= sleep.o
acpi-y				+= device_pm.o
acpi-$(CONFIG_ACPI_SLEEP)	+= proc.o


#
# ACPI Bus and Device Drivers
#
acpi-y				+= bus.o glue.o
acpi-y				+= scan.o
acpi-y				+= resource.o
acpi-y				+= acpi_processor.o
acpi-y				+= processor_core.o
acpi-y				+= ec.o
acpi-$(CONFIG_ACPI_DOCK)	+= dock.o
acpi-y				+= pci_root.o pci_link.o pci_irq.o
acpi-$(CONFIG_X86_INTEL_LPSS)	+= acpi_lpss.o
acpi-y				+= acpi_platform.o
acpi-y				+= power.o
acpi-y				+= event.o
acpi-y				+= sysfs.o
acpi-$(CONFIG_DEBUG_FS)		+= debugfs.o
acpi-$(CONFIG_ACPI_NUMA)	+= numa.o
acpi-$(CONFIG_ACPI_PROCFS_POWER) += cm_sbs.o
ifdef CONFIG_ACPI_VIDEO
acpi-y				+= video_detect.o
endif

# These are (potentially) separate modules

# IPMI may be used by other drivers, so it has to initialise before them
obj-$(CONFIG_ACPI_IPMI)		+= acpi_ipmi.o

obj-$(CONFIG_ACPI_AC) 		+= ac.o
obj-$(CONFIG_ACPI_BUTTON)	+= button.o
obj-$(CONFIG_ACPI_FAN)		+= fan.o
obj-$(CONFIG_ACPI_VIDEO)	+= video.o
obj-$(CONFIG_ACPI_PCI_SLOT)	+= pci_slot.o
obj-$(CONFIG_ACPI_PROCESSOR)	+= processor.o
obj-$(CONFIG_ACPI_CONTAINER)	+= container.o
obj-$(CONFIG_ACPI_THERMAL)	+= thermal.o
obj-$(CONFIG_ACPI_HOTPLUG_MEMORY) += acpi_memhotplug.o
obj-$(CONFIG_ACPI_BATTERY)	+= battery.o
obj-$(CONFIG_ACPI_SBS)		+= sbshc.o
obj-$(CONFIG_ACPI_SBS)		+= sbs.o
obj-$(CONFIG_ACPI_HED)		+= hed.o
obj-$(CONFIG_ACPI_EC_DEBUGFS)	+= ec_sys.o
obj-$(CONFIG_ACPI_CUSTOM_METHOD)+= custom_method.o
obj-$(CONFIG_ACPI_BGRT)		+= bgrt.o
obj-$(CONFIG_ACPI_I2C)		+= acpi_i2c.o

# processor has its own "processor." module_param namespace
processor-y			:= processor_driver.o processor_throttling.o
processor-y			+= processor_idle.o processor_thermal.o
processor-$(CONFIG_CPU_FREQ)	+= processor_perflib.o

obj-$(CONFIG_ACPI_PROCESSOR_AGGREGATOR) += acpi_pad.o

obj-$(CONFIG_ACPI_APEI)		+= apei/
