# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the PCI bus specific drivers.

obj-$(CONFIG_PCI)		+= access.o bus.o probe.o host-bridge.o \
				   remove.o pci.o pci-driver.o search.o \
				   rom.o setup-res.o irq.o vpd.o \
				   setup-bus.o vc.o mmap.o devres.o

obj-$(CONFIG_PCI)		+= msi/
obj-$(CONFIG_PCI)		+= pcie/
obj-$(CONFIG_PCI)		+= pwrctrl/

ifdef CONFIG_PCI
obj-$(CONFIG_PROC_FS)		+= proc.o
obj-$(CONFIG_SYSFS)		+= pci-sysfs.o slot.o
obj-$(CONFIG_ACPI)		+= pci-acpi.o
obj-$(CONFIG_GENERIC_PCI_IOMAP) += iomap.o
endif

obj-$(CONFIG_OF)		+= of.o
obj-$(CONFIG_PCI_QUIRKS)	+= quirks.o
obj-$(CONFIG_HOTPLUG_PCI)	+= hotplug/
obj-$(CONFIG_PCI_ATS)		+= ats.o
obj-$(CONFIG_PCI_IOV)		+= iov.o
obj-$(CONFIG_PCI_BRIDGE_EMUL)	+= pci-bridge-emul.o
obj-$(CONFIG_PCI_LABEL)		+= pci-label.o
obj-$(CONFIG_X86_INTEL_MID)	+= pci-mid.o
obj-$(CONFIG_PCI_SYSCALL)	+= syscall.o
obj-$(CONFIG_PCI_STUB)		+= pci-stub.o
obj-$(CONFIG_PCI_PF_STUB)	+= pci-pf-stub.o
obj-$(CONFIG_PCI_ECAM)		+= ecam.o
obj-$(CONFIG_PCI_P2PDMA)	+= p2pdma.o
obj-$(CONFIG_XEN_PCIDEV_FRONTEND) += xen-pcifront.o
obj-$(CONFIG_VGA_ARB)		+= vgaarb.o
obj-$(CONFIG_PCI_DOE)		+= doe.o
obj-$(CONFIG_PCI_DYNAMIC_OF_NODES) += of_property.o
obj-$(CONFIG_PCI_NPEM)		+= npem.o
obj-$(CONFIG_PCIE_TPH)		+= tph.o

# Endpoint library must be initialized before its users
obj-$(CONFIG_PCI_ENDPOINT)	+= endpoint/

obj-y				+= controller/
obj-y				+= switch/

subdir-ccflags-$(CONFIG_PCI_DEBUG) := -DDEBUG
