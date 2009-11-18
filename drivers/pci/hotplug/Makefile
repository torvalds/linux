#
# Makefile for the Linux kernel pci hotplug controller drivers.
#

obj-$(CONFIG_HOTPLUG_PCI)		+= pci_hotplug.o
obj-$(CONFIG_HOTPLUG_PCI_COMPAQ)	+= cpqphp.o
obj-$(CONFIG_HOTPLUG_PCI_IBM)		+= ibmphp.o

# pciehp should be linked before acpiphp in order to allow the native driver
# to attempt to bind first. We can then fall back to generic support.

obj-$(CONFIG_HOTPLUG_PCI_PCIE)		+= pciehp.o
obj-$(CONFIG_HOTPLUG_PCI_ACPI)		+= acpiphp.o
obj-$(CONFIG_HOTPLUG_PCI_ACPI_IBM)	+= acpiphp_ibm.o
obj-$(CONFIG_HOTPLUG_PCI_CPCI_ZT5550)	+= cpcihp_zt5550.o
obj-$(CONFIG_HOTPLUG_PCI_CPCI_GENERIC)	+= cpcihp_generic.o
obj-$(CONFIG_HOTPLUG_PCI_SHPC)		+= shpchp.o
obj-$(CONFIG_HOTPLUG_PCI_RPA)		+= rpaphp.o
obj-$(CONFIG_HOTPLUG_PCI_RPA_DLPAR)	+= rpadlpar_io.o
obj-$(CONFIG_HOTPLUG_PCI_SGI)		+= sgi_hotplug.o

# Link this last so it doesn't claim devices that have a real hotplug driver
obj-$(CONFIG_HOTPLUG_PCI_FAKE)		+= fakephp.o

pci_hotplug-objs	:=	pci_hotplug_core.o pcihp_slot.o

ifdef CONFIG_HOTPLUG_PCI_CPCI
pci_hotplug-objs	+=	cpci_hotplug_core.o	\
				cpci_hotplug_pci.o
endif
ifdef CONFIG_ACPI
pci_hotplug-objs 	+= 	acpi_pcihp.o
endif

cpqphp-objs		:=	cpqphp_core.o	\
				cpqphp_ctrl.o	\
				cpqphp_sysfs.o	\
				cpqphp_pci.o
cpqphp-$(CONFIG_HOTPLUG_PCI_COMPAQ_NVRAM) += cpqphp_nvram.o
cpqphp-objs += $(cpqphp-y)

ibmphp-objs		:=	ibmphp_core.o	\
				ibmphp_ebda.o	\
				ibmphp_pci.o	\
				ibmphp_res.o	\
				ibmphp_hpc.o

acpiphp-objs		:=	acpiphp_core.o	\
				acpiphp_glue.o

rpaphp-objs		:=	rpaphp_core.o	\
				rpaphp_pci.o	\
				rpaphp_slot.o

rpadlpar_io-objs	:=	rpadlpar_core.o \
				rpadlpar_sysfs.o

pciehp-objs		:=	pciehp_core.o	\
				pciehp_ctrl.o	\
				pciehp_pci.o	\
				pciehp_hpc.o
ifdef CONFIG_ACPI
pciehp-objs		+=	pciehp_acpi.o
endif

shpchp-objs		:=	shpchp_core.o	\
				shpchp_ctrl.o	\
				shpchp_pci.o	\
				shpchp_sysfs.o	\
				shpchp_hpc.o
