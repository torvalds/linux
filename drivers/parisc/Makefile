# SPDX-License-Identifier: GPL-2.0
#
# Makefile for most of the non-PCI devices in PA-RISC machines
#

# I/O SAPIC is also on IA64 platforms.
# The two could be merged into a common source some day.
obj-$(CONFIG_IOSAPIC)		+= iosapic.o
obj-$(CONFIG_IOMMU_SBA)		+= sba_iommu.o
obj-$(CONFIG_PCI_LBA)		+= lba_pci.o

# Only use one of them: ccio-rm-dma is for PCX-W systems *only*
# obj-$(CONFIG_IOMMU_CCIO)	+= ccio-rm-dma.o
obj-$(CONFIG_IOMMU_CCIO)	+= ccio-dma.o

obj-$(CONFIG_GSC)		+= gsc.o

obj-$(CONFIG_HPPB)		+= hppb.o
obj-$(CONFIG_GSC_DINO)		+= dino.o
obj-$(CONFIG_GSC_LASI)		+= lasi.o asp.o
obj-$(CONFIG_GSC_WAX)		+= wax.o
obj-$(CONFIG_EISA)		+= eisa.o eisa_enumerator.o eisa_eeprom.o

obj-$(CONFIG_SUPERIO)		+= superio.o
obj-$(CONFIG_CHASSIS_LCD_LED)	+= led.o
obj-$(CONFIG_PDC_STABLE)	+= pdc_stable.o
obj-y				+= power.o

