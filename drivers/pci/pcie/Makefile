#
# Makefile for PCI-Express PORT Driver
#

# Build PCI Express ASPM if needed
obj-$(CONFIG_PCIEASPM)		+= aspm.o

pcieportdrv-y			:= portdrv_core.o portdrv_pci.o portdrv_bus.o
pcieportdrv-$(CONFIG_ACPI)	+= portdrv_acpi.o

obj-$(CONFIG_PCIEPORTBUS)	+= pcieportdrv.o

# Build PCI Express AER if needed
obj-$(CONFIG_PCIEAER)		+= aer/

obj-$(CONFIG_PCIE_PME) += pme.o

obj-$(CONFIG_PCIE_DPC) += pcie-dpc.o
obj-$(CONFIG_PCIE_PTM) += ptm.o
