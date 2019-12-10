# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux ACPI interpreter
#

ccflags-$(CONFIG_ACPI_DEBUG)	+= -DACPI_DEBUG_OUTPUT

#
# ACPI Boot-Time Table Parsing
#
obj-$(CONFIG_ACPI)		+= tables.o
obj-$(CONFIG_X86)		+= blacklist.o

#
# ACPI Core Subsystem (Interpreter)
#
obj-$(CONFIG_ACPI)		+= acpi.o \
					acpica/

# All the builtin files are in the "acpi." module_param namespace.
acpi-y				+= osi.o osl.o utils.o reboot.o
acpi-y				+= nvs.o

# Power management related files
acpi-y				+= wakeup.o
acpi-$(CONFIG_ACPI_SYSTEM_POWER_STATES_SUPPORT) += sleep.o
acpi-y				+= device_sysfs.o device_pm.o
acpi-$(CONFIG_ACPI_SLEEP)	+= proc.o


#
# ACPI Bus and Device Drivers
#
acpi-y				+= bus.o glue.o
acpi-y				+= scan.o
acpi-y				+= resource.o
acpi-y				+= acpi_processor.o
acpi-y				+= processor_core.o
acpi-$(CONFIG_ARCH_MIGHT_HAVE_ACPI_PDC) += processor_pdc.o
acpi-y				+= ec.o
acpi-$(CONFIG_ACPI_DOCK)	+= dock.o
acpi-$(CONFIG_PCI)		+= pci_root.o pci_link.o pci_irq.o
obj-$(CONFIG_ACPI_MCFG)		+= pci_mcfg.o
acpi-$(CONFIG_PCI)		+= acpi_lpss.o
acpi-y				+= acpi_apd.o
acpi-y				+= acpi_platform.o
acpi-y				+= acpi_pnp.o
acpi-$(CONFIG_ARM_AMBA)	+= acpi_amba.o
acpi-y				+= power.o
acpi-y				+= event.o
acpi-y				+= evged.o
acpi-y				+= sysfs.o
acpi-y				+= property.o
acpi-$(CONFIG_X86)		+= acpi_cmos_rtc.o
acpi-$(CONFIG_X86)		+= x86/apple.o
acpi-$(CONFIG_X86)		+= x86/utils.o
acpi-$(CONFIG_DEBUG_FS)		+= debugfs.o
acpi-$(CONFIG_ACPI_PROCFS_POWER) += cm_sbs.o
acpi-y				+= acpi_lpat.o
acpi-$(CONFIG_ACPI_LPIT)	+= acpi_lpit.o
acpi-$(CONFIG_ACPI_GENERIC_GSI) += irq.o
acpi-$(CONFIG_ACPI_WATCHDOG)	+= acpi_watchdog.o

# Address translation
acpi-$(CONFIG_ACPI_ADXL)	+= acpi_adxl.o

# These are (potentially) separate modules

# IPMI may be used by other drivers, so it has to initialise before them
obj-$(CONFIG_ACPI_IPMI)		+= acpi_ipmi.o

obj-$(CONFIG_ACPI_AC) 		+= ac.o
obj-$(CONFIG_ACPI_BUTTON)	+= button.o
obj-$(CONFIG_ACPI_FAN)		+= fan.o
obj-$(CONFIG_ACPI_VIDEO)	+= video.o
obj-$(CONFIG_ACPI_TAD)		+= acpi_tad.o
obj-$(CONFIG_ACPI_PCI_SLOT)	+= pci_slot.o
obj-$(CONFIG_ACPI_PROCESSOR)	+= processor.o
obj-$(CONFIG_ACPI)		+= container.o
obj-$(CONFIG_ACPI_THERMAL)	+= thermal.o
obj-$(CONFIG_ACPI_NFIT)		+= nfit/
obj-$(CONFIG_ACPI_NUMA)		+= numa/
obj-$(CONFIG_ACPI)		+= acpi_memhotplug.o
obj-$(CONFIG_ACPI_HOTPLUG_IOAPIC) += ioapic.o
obj-$(CONFIG_ACPI_BATTERY)	+= battery.o
obj-$(CONFIG_ACPI_SBS)		+= sbshc.o
obj-$(CONFIG_ACPI_SBS)		+= sbs.o
obj-$(CONFIG_ACPI_HED)		+= hed.o
obj-$(CONFIG_ACPI_EC_DEBUGFS)	+= ec_sys.o
obj-$(CONFIG_ACPI_CUSTOM_METHOD)+= custom_method.o
obj-$(CONFIG_ACPI_BGRT)		+= bgrt.o
obj-$(CONFIG_ACPI_CPPC_LIB)	+= cppc_acpi.o
obj-$(CONFIG_ACPI_SPCR_TABLE)	+= spcr.o
obj-$(CONFIG_ACPI_DEBUGGER_USER) += acpi_dbg.o
obj-$(CONFIG_ACPI_PPTT) 	+= pptt.o

# processor has its own "processor." module_param namespace
processor-y			:= processor_driver.o
processor-$(CONFIG_ACPI_PROCESSOR_IDLE) += processor_idle.o
processor-$(CONFIG_ACPI_CPU_FREQ_PSS)	+= processor_throttling.o	\
	processor_thermal.o
processor-$(CONFIG_CPU_FREQ)	+= processor_perflib.o

obj-$(CONFIG_ACPI_PROCESSOR_AGGREGATOR) += acpi_pad.o

obj-$(CONFIG_ACPI_APEI)		+= apei/

obj-$(CONFIG_ACPI_EXTLOG)	+= acpi_extlog.o

obj-$(CONFIG_PMIC_OPREGION)	+= pmic/intel_pmic.o
obj-$(CONFIG_BYTCRC_PMIC_OPREGION) += pmic/intel_pmic_bytcrc.o
obj-$(CONFIG_CHTCRC_PMIC_OPREGION) += pmic/intel_pmic_chtcrc.o
obj-$(CONFIG_XPOWER_PMIC_OPREGION) += pmic/intel_pmic_xpower.o
obj-$(CONFIG_BXT_WC_PMIC_OPREGION) += pmic/intel_pmic_bxtwc.o
obj-$(CONFIG_CHT_WC_PMIC_OPREGION) += pmic/intel_pmic_chtwc.o
obj-$(CONFIG_CHT_DC_TI_PMIC_OPREGION) += pmic/intel_pmic_chtdc_ti.o

obj-$(CONFIG_ACPI_CONFIGFS)	+= acpi_configfs.o

obj-$(CONFIG_TPS68470_PMIC_OPREGION)	+= pmic/tps68470_pmic.o

video-objs			+= acpi_video.o video_detect.o
obj-y				+= dptf/

obj-$(CONFIG_ARM64)		+= arm64/
