ifeq ($(CONFIG_X86_32),y)

obj-y				:= i386.o init.o

obj-$(CONFIG_PCI_BIOS)		+= pcbios.o
obj-$(CONFIG_PCI_MMCONFIG)	+= mmconfig_32.o direct.o mmconfig-shared.o
obj-$(CONFIG_PCI_DIRECT)	+= direct.o
obj-$(CONFIG_PCI_OLPC)		+= olpc.o

pci-y				:= fixup.o

# Do not change the ordering here. There is a nasty init function
# ordering dependency which breaks when you move acpi.o below
# legacy/irq.o
pci-$(CONFIG_ACPI)		+= acpi.o
pci-y				+= legacy.o irq.o
pci-$(CONFIG_NUMA)		+= mp_bus_to_node.o

# Careful: VISWS overrule the pci-y above. The colons are
# therefor correct. This needs a proper fix by distangling the code.
pci-$(CONFIG_X86_VISWS)		:= visws.o fixup.o

pci-$(CONFIG_X86_NUMAQ)		+= numa.o

obj-y				+= $(pci-y) common.o early.o
obj-y				+= amd_bus.o

else

#
# Makefile for X86_64 specific PCI routines
#

obj-y				:= i386.o
obj-$(CONFIG_PCI_DIRECT)	+= direct.o
obj-y				+= fixup.o init.o
obj-$(CONFIG_ACPI)		+= acpi.o
obj-y				+= legacy.o irq.o common.o early.o
# mmconfig has a 64bit special
obj-$(CONFIG_PCI_MMCONFIG)	+= mmconfig_64.o direct.o mmconfig-shared.o

obj-y				+= amd_bus.o

endif
