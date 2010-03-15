#
# Makefile for PCI-Express Root Port PME signaling driver
#

obj-$(CONFIG_PCIE_PME) += pmedriver.o

pmedriver-objs := pcie_pme.o
pmedriver-$(CONFIG_ACPI) += pcie_pme_acpi.o
