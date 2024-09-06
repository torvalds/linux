# SPDX-License-Identifier: GPL-2.0
#
# linux/drivers/maps/Makefile
#

ifeq ($(CONFIG_MTD_COMPLEX_MAPPINGS),y)
obj-$(CONFIG_MTD)		+= map_funcs.o
endif

# Chip mappings
obj-$(CONFIG_MTD_CFI_FLAGADM)	+= cfi_flagadm.o
obj-$(CONFIG_MTD_DC21285)	+= dc21285.o
obj-$(CONFIG_MTD_L440GX)	+= l440gx.o
obj-$(CONFIG_MTD_AMD76XROM)	+= amd76xrom.o
obj-$(CONFIG_MTD_ESB2ROM)	+= esb2rom.o
obj-$(CONFIG_MTD_ICHXROM)	+= ichxrom.o
obj-$(CONFIG_MTD_CK804XROM)	+= ck804xrom.o
obj-$(CONFIG_MTD_TSUNAMI)	+= tsunami_flash.o
obj-$(CONFIG_MTD_PXA2XX)	+= pxa2xx-flash.o
obj-$(CONFIG_MTD_PHYSMAP)	+= physmap.o
physmap-y			:= physmap-core.o
physmap-$(CONFIG_MTD_PHYSMAP_BT1_ROM) += physmap-bt1-rom.o
physmap-$(CONFIG_MTD_PHYSMAP_VERSATILE) += physmap-versatile.o
physmap-$(CONFIG_MTD_PHYSMAP_GEMINI) += physmap-gemini.o
physmap-$(CONFIG_MTD_PHYSMAP_IXP4XX) += physmap-ixp4xx.o
obj-$(CONFIG_MTD_PISMO)		+= pismo.o
obj-$(CONFIG_MTD_PCMCIA)	+= pcmciamtd.o
obj-$(CONFIG_MTD_SA1100)	+= sa1100-flash.o
obj-$(CONFIG_MTD_SBC_GXX)	+= sbc_gxx.o
obj-$(CONFIG_MTD_SC520CDP)	+= sc520cdp.o
obj-$(CONFIG_MTD_NETSC520)	+= netsc520.o
obj-$(CONFIG_MTD_TS5500)	+= ts5500_flash.o
obj-$(CONFIG_MTD_SUN_UFLASH)	+= sun_uflash.o
obj-$(CONFIG_MTD_SCx200_DOCFLASH)+= scx200_docflash.o
obj-$(CONFIG_MTD_SOLUTIONENGINE)+= solutionengine.o
obj-$(CONFIG_MTD_PCI)		+= pci.o
obj-$(CONFIG_MTD_IMPA7)		+= impa7.o
obj-$(CONFIG_MTD_UCLINUX)	+= uclinux.o
obj-$(CONFIG_MTD_NETtel)	+= nettel.o
obj-$(CONFIG_MTD_SCB2_FLASH)	+= scb2_flash.o
obj-$(CONFIG_MTD_PLATRAM)	+= plat-ram.o
obj-$(CONFIG_MTD_VMU)		+= vmu-flash.o
obj-$(CONFIG_MTD_LANTIQ)	+= lantiq-flash.o
