bcma-y					+= main.o scan.o core.o sprom.o
bcma-y					+= driver_chipcommon.o driver_chipcommon_pmu.o
bcma-$(CONFIG_BCMA_SFLASH)		+= driver_chipcommon_sflash.o
bcma-$(CONFIG_BCMA_NFLASH)		+= driver_chipcommon_nflash.o
bcma-y					+= driver_pci.o
bcma-y					+= driver_pcie2.o
bcma-$(CONFIG_BCMA_DRIVER_PCI_HOSTMODE)	+= driver_pci_host.o
bcma-$(CONFIG_BCMA_DRIVER_MIPS)		+= driver_mips.o
bcma-$(CONFIG_BCMA_DRIVER_GMAC_CMN)	+= driver_gmac_cmn.o
bcma-$(CONFIG_BCMA_DRIVER_GPIO)		+= driver_gpio.o
bcma-$(CONFIG_BCMA_HOST_PCI)		+= host_pci.o
bcma-$(CONFIG_BCMA_HOST_SOC)		+= host_soc.o
obj-$(CONFIG_BCMA)			+= bcma.o

ccflags-$(CONFIG_BCMA_DEBUG)		:= -DDEBUG
