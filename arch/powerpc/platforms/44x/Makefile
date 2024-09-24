# SPDX-License-Identifier: GPL-2.0
obj-y	+= misc_44x.o machine_check.o uic.o soc.o
ifneq ($(CONFIG_PPC4xx_CPM),y)
obj-y	+= idle.o
endif
obj-$(CONFIG_PPC44x_SIMPLE) += ppc44x_simple.o
obj-$(CONFIG_EBONY)	+= ebony.o
obj-$(CONFIG_SAM440EP) 	+= sam440ep.o
obj-$(CONFIG_WARP)	+= warp.o
obj-$(CONFIG_ISS4xx)	+= iss4xx.o
obj-$(CONFIG_CANYONLANDS)+= canyonlands.o
obj-$(CONFIG_CURRITUCK)	+= ppc476.o
obj-$(CONFIG_AKEBONO)	+= ppc476.o
obj-$(CONFIG_FSP2)	+= fsp2.o
obj-$(CONFIG_PCI)		+= pci.o
obj-$(CONFIG_PPC4xx_HSTA_MSI)	+= hsta_msi.o
obj-$(CONFIG_PPC4xx_CPM)	+= cpm.o
obj-$(CONFIG_PPC4xx_GPIO)	+= gpio.o
