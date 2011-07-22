bcma-y					+= main.o scan.o core.o sprom.o
bcma-y					+= driver_chipcommon.o driver_chipcommon_pmu.o
bcma-y					+= driver_pci.o
bcma-$(CONFIG_BCMA_DRIVER_PCI_HOSTMODE)	+= driver_pci_host.o
bcma-$(CONFIG_BCMA_HOST_PCI)		+= host_pci.o
obj-$(CONFIG_BCMA)			+= bcma.o

ccflags-$(CONFIG_BCMA_DEBUG)		:= -DDEBUG
