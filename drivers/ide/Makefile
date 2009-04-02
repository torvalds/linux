#
# link order is important here
#

EXTRA_CFLAGS				+= -Idrivers/ide

ide-core-y += ide.o ide-ioctls.o ide-io.o ide-iops.o ide-lib.o ide-probe.o \
	      ide-taskfile.o ide-pm.o ide-park.o ide-sysfs.o ide-devsets.o \
	      ide-io-std.o ide-eh.o

# core IDE code
ide-core-$(CONFIG_IDE_XFER_MODE)	+= ide-pio-blacklist.o ide-xfer-mode.o
ide-core-$(CONFIG_IDE_TIMINGS)		+= ide-timings.o
ide-core-$(CONFIG_IDE_ATAPI)		+= ide-atapi.o
ide-core-$(CONFIG_BLK_DEV_IDEPCI)	+= setup-pci.o
ide-core-$(CONFIG_BLK_DEV_IDEDMA)	+= ide-dma.o
ide-core-$(CONFIG_BLK_DEV_IDEDMA_SFF)	+= ide-dma-sff.o
ide-core-$(CONFIG_IDE_PROC_FS)		+= ide-proc.o
ide-core-$(CONFIG_BLK_DEV_IDEACPI)	+= ide-acpi.o
ide-core-$(CONFIG_IDE_LEGACY)		+= ide-legacy.o

obj-$(CONFIG_IDE)			+= ide-core.o

obj-$(CONFIG_BLK_DEV_ALI14XX)		+= ali14xx.o
obj-$(CONFIG_BLK_DEV_UMC8672)		+= umc8672.o
obj-$(CONFIG_BLK_DEV_DTC2278)		+= dtc2278.o
obj-$(CONFIG_BLK_DEV_HT6560B)		+= ht6560b.o
obj-$(CONFIG_BLK_DEV_QD65XX)		+= qd65xx.o
obj-$(CONFIG_BLK_DEV_4DRIVES)		+= ide-4drives.o

obj-$(CONFIG_BLK_DEV_GAYLE)		+= gayle.o
obj-$(CONFIG_BLK_DEV_FALCON_IDE)	+= falconide.o
obj-$(CONFIG_BLK_DEV_MAC_IDE)		+= macide.o
obj-$(CONFIG_BLK_DEV_Q40IDE)		+= q40ide.o
obj-$(CONFIG_BLK_DEV_BUDDHA)		+= buddha.o

obj-$(CONFIG_BLK_DEV_AEC62XX)		+= aec62xx.o
obj-$(CONFIG_BLK_DEV_ALI15X3)		+= alim15x3.o
obj-$(CONFIG_BLK_DEV_AMD74XX)		+= amd74xx.o
obj-$(CONFIG_BLK_DEV_ATIIXP)		+= atiixp.o
obj-$(CONFIG_BLK_DEV_CELLEB)		+= scc_pata.o
obj-$(CONFIG_BLK_DEV_CMD64X)		+= cmd64x.o
obj-$(CONFIG_BLK_DEV_CS5520)		+= cs5520.o
obj-$(CONFIG_BLK_DEV_CS5530)		+= cs5530.o
obj-$(CONFIG_BLK_DEV_CS5535)		+= cs5535.o
obj-$(CONFIG_BLK_DEV_CS5536)		+= cs5536.o
obj-$(CONFIG_BLK_DEV_SC1200)		+= sc1200.o
obj-$(CONFIG_BLK_DEV_CY82C693)		+= cy82c693.o
obj-$(CONFIG_BLK_DEV_DELKIN)		+= delkin_cb.o
obj-$(CONFIG_BLK_DEV_HPT366)		+= hpt366.o
obj-$(CONFIG_BLK_DEV_IT8172)		+= it8172.o
obj-$(CONFIG_BLK_DEV_IT8213)		+= it8213.o
obj-$(CONFIG_BLK_DEV_IT821X)		+= it821x.o
obj-$(CONFIG_BLK_DEV_JMICRON)		+= jmicron.o
obj-$(CONFIG_BLK_DEV_NS87415)		+= ns87415.o
obj-$(CONFIG_BLK_DEV_OPTI621)		+= opti621.o
obj-$(CONFIG_BLK_DEV_PDC202XX_OLD)	+= pdc202xx_old.o
obj-$(CONFIG_BLK_DEV_PDC202XX_NEW)	+= pdc202xx_new.o
obj-$(CONFIG_BLK_DEV_PIIX)		+= piix.o
obj-$(CONFIG_BLK_DEV_RZ1000)		+= rz1000.o
obj-$(CONFIG_BLK_DEV_SVWKS)		+= serverworks.o
obj-$(CONFIG_BLK_DEV_SGIIOC4)		+= sgiioc4.o
obj-$(CONFIG_BLK_DEV_SIIMAGE)		+= siimage.o
obj-$(CONFIG_BLK_DEV_SIS5513)		+= sis5513.o
obj-$(CONFIG_BLK_DEV_SL82C105)		+= sl82c105.o
obj-$(CONFIG_BLK_DEV_SLC90E66)		+= slc90e66.o
obj-$(CONFIG_BLK_DEV_TC86C001)		+= tc86c001.o
obj-$(CONFIG_BLK_DEV_TRIFLEX)		+= triflex.o
obj-$(CONFIG_BLK_DEV_TRM290)		+= trm290.o
obj-$(CONFIG_BLK_DEV_VIA82CXXX)		+= via82cxxx.o

# Must appear at the end of the block
obj-$(CONFIG_BLK_DEV_GENERIC)		+= ide-pci-generic.o

obj-$(CONFIG_IDEPCI_PCIBUS_ORDER)	+= ide-scan-pci.o

obj-$(CONFIG_BLK_DEV_CMD640)		+= cmd640.o

obj-$(CONFIG_BLK_DEV_IDE_PMAC)		+= pmac.o

obj-$(CONFIG_IDE_H8300)			+= ide-h8300.o

obj-$(CONFIG_IDE_GENERIC)		+= ide-generic.o
obj-$(CONFIG_BLK_DEV_IDEPNP)		+= ide-pnp.o

ide-gd_mod-y += ide-gd.o
ide-cd_mod-y += ide-cd.o ide-cd_ioctl.o ide-cd_verbose.o

ifeq ($(CONFIG_IDE_GD_ATA), y)
	ide-gd_mod-y += ide-disk.o ide-disk_ioctl.o
ifeq ($(CONFIG_IDE_PROC_FS), y)
	ide-gd_mod-y += ide-disk_proc.o
endif
endif

ifeq ($(CONFIG_IDE_GD_ATAPI), y)
	ide-gd_mod-y += ide-floppy.o ide-floppy_ioctl.o
ifeq ($(CONFIG_IDE_PROC_FS), y)
	ide-gd_mod-y += ide-floppy_proc.o
endif
endif

obj-$(CONFIG_IDE_GD)			+= ide-gd_mod.o
obj-$(CONFIG_BLK_DEV_IDECD)		+= ide-cd_mod.o
obj-$(CONFIG_BLK_DEV_IDETAPE)		+= ide-tape.o

obj-$(CONFIG_BLK_DEV_IDECS)		+= ide-cs.o

obj-$(CONFIG_BLK_DEV_PLATFORM)		+= ide_platform.o

obj-$(CONFIG_BLK_DEV_IDE_ICSIDE)	+= icside.o
obj-$(CONFIG_BLK_DEV_IDE_RAPIDE)	+= rapide.o
obj-$(CONFIG_BLK_DEV_PALMCHIP_BK3710)	+= palm_bk3710.o

obj-$(CONFIG_BLK_DEV_IDE_AU1XXX)	+= au1xxx-ide.o

obj-$(CONFIG_BLK_DEV_IDE_TX4938)	+= tx4938ide.o
obj-$(CONFIG_BLK_DEV_IDE_TX4939)	+= tx4939ide.o
obj-$(CONFIG_BLK_DEV_IDE_AT91)		+= at91_ide.o
