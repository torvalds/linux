obj-y				:= i386.o

obj-$(CONFIG_PCI_BIOS)		+= pcbios.o
obj-$(CONFIG_PCI_MMCONFIG)	+= mmconfig.o
obj-$(CONFIG_PCI_DIRECT)	+= direct.o

pci-y				:= fixup.o
pci-$(CONFIG_ACPI_PCI)		+= acpi.o
pci-y				+= legacy.o irq.o

pci-$(CONFIG_X86_VISWS)		:= visws.o fixup.o
pci-$(CONFIG_X86_NUMAQ)		:= numa.o irq.o

obj-y				+= $(pci-y) common.o
