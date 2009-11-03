#
# Makefile for the PCI bus specific drivers.
#

obj-y		+= access.o bus.o probe.o remove.o pci.o quirks.o \
			pci-driver.o search.o pci-sysfs.o rom.o setup-res.o \
			irq.o
obj-$(CONFIG_PROC_FS) += proc.o
obj-$(CONFIG_SYSFS) += slot.o

obj-$(CONFIG_PCI_LEGACY) += legacy.o
CFLAGS_legacy.o += -Wno-deprecated-declarations

# Build PCI Express stuff if needed
obj-$(CONFIG_PCIEPORTBUS) += pcie/

obj-$(CONFIG_HOTPLUG) += hotplug.o

# Build the PCI Hotplug drivers if we were asked to
obj-$(CONFIG_HOTPLUG_PCI) += hotplug/
ifdef CONFIG_HOTPLUG_PCI
obj-y += hotplug-pci.o
endif

# Build the PCI MSI interrupt support
obj-$(CONFIG_PCI_MSI) += msi.o

# Build the Hypertransport interrupt support
obj-$(CONFIG_HT_IRQ) += htirq.o

# Build Intel IOMMU support
obj-$(CONFIG_DMAR) += dmar.o iova.o intel-iommu.o

obj-$(CONFIG_INTR_REMAP) += dmar.o intr_remapping.o

obj-$(CONFIG_PCI_IOV) += iov.o

#
# Some architectures use the generic PCI setup functions
#
obj-$(CONFIG_X86) += setup-bus.o
obj-$(CONFIG_ALPHA) += setup-bus.o setup-irq.o
obj-$(CONFIG_ARM) += setup-bus.o setup-irq.o
obj-$(CONFIG_PARISC) += setup-bus.o
obj-$(CONFIG_SUPERH) += setup-bus.o setup-irq.o
obj-$(CONFIG_PPC) += setup-bus.o
obj-$(CONFIG_MIPS) += setup-bus.o setup-irq.o
obj-$(CONFIG_X86_VISWS) += setup-irq.o
obj-$(CONFIG_MN10300) += setup-bus.o

#
# ACPI Related PCI FW Functions
#
obj-$(CONFIG_ACPI)    += pci-acpi.o

# Cardbus & CompactPCI use setup-bus
obj-$(CONFIG_HOTPLUG) += setup-bus.o

obj-$(CONFIG_PCI_SYSCALL) += syscall.o

obj-$(CONFIG_PCI_STUB) += pci-stub.o

ifeq ($(CONFIG_PCI_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
