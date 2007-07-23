#
# Makefile for PCI-Express Root Port Advanced Error Reporting Driver
#

obj-$(CONFIG_PCIEAER) += aerdriver.o

aerdriver-objs := aerdrv_errprint.o aerdrv_core.o aerdrv.o
aerdriver-$(CONFIG_ACPI) += aerdrv_acpi.o

