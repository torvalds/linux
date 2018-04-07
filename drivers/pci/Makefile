# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the PCI bus specific drivers.

obj-$(CONFIG_PCI)		+= access.o bus.o probe.o host-bridge.o \
				   remove.o pci.o pci-driver.o search.o \
				   pci-sysfs.o rom.o setup-res.o irq.o vpd.o \
				   setup-bus.o vc.o mmap.o setup-irq.o

ifdef CONFIG_PCI
obj-$(CONFIG_PROC_FS)		+= proc.o
obj-$(CONFIG_SYSFS)		+= slot.o
obj-$(CONFIG_OF)		+= of.o
endif

obj-$(CONFIG_PCI_QUIRKS)	+= quirks.o
obj-$(CONFIG_PCIEPORTBUS)	+= pcie/
obj-$(CONFIG_HOTPLUG_PCI)	+= hotplug/
obj-$(CONFIG_PCI_MSI)		+= msi.o
obj-$(CONFIG_PCI_ATS)		+= ats.o
obj-$(CONFIG_PCI_IOV)		+= iov.o
obj-$(CONFIG_ACPI)		+= pci-acpi.o
obj-$(CONFIG_PCI_LABEL)		+= pci-label.o
obj-$(CONFIG_X86_INTEL_MID)	+= pci-mid.o
obj-$(CONFIG_PCI_SYSCALL)	+= syscall.o
obj-$(CONFIG_PCI_STUB)		+= pci-stub.o
obj-$(CONFIG_PCI_ECAM)		+= ecam.o
obj-$(CONFIG_XEN_PCIDEV_FRONTEND) += xen-pcifront.o

obj-y				+= host/
obj-y				+= switch/

# Endpoint library must be initialized before its users
obj-$(CONFIG_PCI_ENDPOINT)	+= endpoint/

obj-$(CONFIG_PCIE_CADENCE)	+= cadence/
# pcie-hisi.o quirks are needed even without CONFIG_PCIE_DW
obj-y				+= dwc/

ccflags-$(CONFIG_PCI_DEBUG) := -DDEBUG
