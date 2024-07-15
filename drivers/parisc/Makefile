# SPDX-License-Identifier: GPL-2.0
#
# Makefile PCI and non-PCI devices in PA-RISC machines
#
# Keep the order below, e.g.
# - ccio before any potential subdevices
# - gsc is required before lasi and wax
# - asp and wax before the EISA adapters for the IRQ regions
# - EISA must come before PCI to be sure it gets IRQ region
#

obj-$(CONFIG_IOSAPIC)		+= iosapic.o
obj-$(CONFIG_IOMMU_SBA)		+= sba_iommu.o
obj-$(CONFIG_PCI_LBA)		+= lba_pci.o
obj-$(CONFIG_IOMMU_CCIO)	+= ccio-dma.o

obj-$(CONFIG_GSC)		+= gsc.o
obj-$(CONFIG_GSC_LASI)		+= lasi.o asp.o
obj-$(CONFIG_GSC_WAX)		+= wax.o
obj-$(CONFIG_EISA)		+= eisa.o eisa_enumerator.o eisa_eeprom.o
obj-$(CONFIG_HPPB)		+= hppb.o
obj-$(CONFIG_GSC_DINO)		+= dino.o

obj-$(CONFIG_SUPERIO)		+= superio.o
obj-$(CONFIG_CHASSIS_LCD_LED)	+= led.o
obj-$(CONFIG_PDC_STABLE)	+= pdc_stable.o
obj-y				+= power.o
