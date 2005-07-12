#
# Makefile for the PCI bus specific drivers.
#

obj-y		+= access.o bus.o probe.o remove.o pci.o quirks.o \
			names.o pci-driver.o search.o pci-sysfs.o \
			rom.o
obj-$(CONFIG_PROC_FS) += proc.o

ifndef CONFIG_SPARC64
obj-y += setup-res.o
endif

obj-$(CONFIG_HOTPLUG) += hotplug.o

# Build the PCI Hotplug drivers if we were asked to
obj-$(CONFIG_HOTPLUG_PCI) += hotplug/

#
# Some architectures use the generic PCI setup functions
#
obj-$(CONFIG_X86) += setup-bus.o
obj-$(CONFIG_ALPHA) += setup-bus.o setup-irq.o
obj-$(CONFIG_ARM) += setup-bus.o setup-irq.o
obj-$(CONFIG_PARISC) += setup-bus.o
obj-$(CONFIG_SUPERH) += setup-bus.o setup-irq.o
obj-$(CONFIG_PPC32) += setup-irq.o
obj-$(CONFIG_PPC64) += setup-bus.o
obj-$(CONFIG_MIPS) += setup-bus.o setup-irq.o
obj-$(CONFIG_X86_VISWS) += setup-irq.o
obj-$(CONFIG_PCI_MSI) += msi.o

#
# ACPI Related PCI FW Functions
#
obj-$(CONFIG_ACPI)    += pci-acpi.o

# Cardbus & CompactPCI use setup-bus
obj-$(CONFIG_HOTPLUG) += setup-bus.o

ifndef CONFIG_X86
obj-y += syscall.o
endif

ifeq ($(CONFIG_PCI_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

hostprogs-y := gen-devlist

# Dependencies on generated files need to be listed explicitly
$(obj)/names.o: $(obj)/devlist.h $(obj)/classlist.h
$(obj)/classlist.h: $(obj)/devlist.h

# And that's how to generate them
quiet_cmd_devlist = DEVLIST $@
      cmd_devlist = ( cd $(obj); ./gen-devlist ) < $<
$(obj)/devlist.h: $(src)/pci.ids $(obj)/gen-devlist
	$(call cmd,devlist)

# Files generated that shall be removed upon make clean
clean-files := devlist.h classlist.h

# Build PCI Express stuff if needed
obj-$(CONFIG_PCIEPORTBUS) += pcie/

