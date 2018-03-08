# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the PCI bus specific drivers.
#

obj-$(CONFIG_PCI)	+= access.o bus.o probe.o host-bridge.o remove.o pci.o \
			pci-driver.o search.o pci-sysfs.o rom.o setup-res.o \
			irq.o vpd.o setup-bus.o vc.o mmap.o setup-irq.o

ifdef CONFIG_PCI
obj-$(CONFIG_PROC_FS) += proc.o
obj-$(CONFIG_SYSFS) += slot.o
obj-$(CONFIG_OF) += of.o
endif

obj-$(CONFIG_PCI_QUIRKS) += quirks.o

# Build PCI Express stuff if needed
obj-$(CONFIG_PCIEPORTBUS) += pcie/

# Build the PCI Hotplug drivers if we were asked to
obj-$(CONFIG_HOTPLUG_PCI) += hotplug/

# Build the PCI MSI interrupt support
obj-$(CONFIG_PCI_MSI) += msi.o

obj-$(CONFIG_PCI_ATS) += ats.o
obj-$(CONFIG_PCI_IOV) += iov.o

#
# ACPI Related PCI FW Functions
# ACPI _DSM provided firmware instance and string name
#
obj-$(CONFIG_ACPI)    += pci-acpi.o

# SMBIOS provided firmware instance and labels
obj-$(CONFIG_PCI_LABEL) += pci-label.o

# Intel MID platform PM support
obj-$(CONFIG_X86_INTEL_MID) += pci-mid.o

obj-$(CONFIG_PCI_SYSCALL) += syscall.o

obj-$(CONFIG_PCI_STUB) += pci-stub.o

obj-$(CONFIG_PCI_ECAM) += ecam.o

obj-$(CONFIG_XEN_PCIDEV_FRONTEND) += xen-pcifront.o

ccflags-$(CONFIG_PCI_DEBUG) := -DDEBUG

# PCI host controller drivers
obj-y += host/
obj-y += switch/

obj-$(CONFIG_PCI_ENDPOINT)	+= endpoint/

# Endpoint library must be initialized before its users
obj-$(CONFIG_PCIE_CADENCE)	+= cadence/
# pcie-hisi.o quirks are needed even without CONFIG_PCIE_DW
obj-y				+= dwc/
