#
# Makefile for memory devices
#

ifeq ($(CONFIG_DDR),y)
obj-$(CONFIG_OF)		+= of_memory.o
endif
obj-$(CONFIG_ATMEL_SDRAMC)	+= atmel-sdramc.o
obj-$(CONFIG_TI_AEMIF)		+= ti-aemif.o
obj-$(CONFIG_TI_EMIF)		+= emif.o
obj-$(CONFIG_OMAP_GPMC)		+= omap-gpmc.o
obj-$(CONFIG_FSL_CORENET_CF)	+= fsl-corenet-cf.o
obj-$(CONFIG_FSL_IFC)		+= fsl_ifc.o
obj-$(CONFIG_MVEBU_DEVBUS)	+= mvebu-devbus.o
obj-$(CONFIG_TEGRA20_MC)	+= tegra20-mc.o

obj-$(CONFIG_TEGRA_MC)		+= tegra/
