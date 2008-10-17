#
# link order is important here
#

EXTRA_CFLAGS				+= -Idrivers/ide

ide-core-y += ide.o ide-ioctls.o ide-io.o ide-iops.o ide-lib.o ide-probe.o \
	      ide-taskfile.o ide-park.o ide-pio-blacklist.o

# core IDE code
ide-core-$(CONFIG_IDE_TIMINGS)		+= ide-timings.o
ide-core-$(CONFIG_IDE_ATAPI)		+= ide-atapi.o
ide-core-$(CONFIG_BLK_DEV_IDEPCI)	+= setup-pci.o
ide-core-$(CONFIG_BLK_DEV_IDEDMA)	+= ide-dma.o
ide-core-$(CONFIG_BLK_DEV_IDEDMA_SFF)	+= ide-dma-sff.o
ide-core-$(CONFIG_IDE_PROC_FS)		+= ide-proc.o
ide-core-$(CONFIG_BLK_DEV_IDEACPI)	+= ide-acpi.o

obj-$(CONFIG_IDE)			+= ide-core.o

ifeq ($(CONFIG_IDE_ARM), y)
	ide-arm-core-y += arm/ide_arm.o
	obj-y += ide-arm-core.o
endif

obj-$(CONFIG_IDE)			+= legacy/ pci/

obj-$(CONFIG_IDEPCI_PCIBUS_ORDER)	+= ide-scan-pci.o

ifeq ($(CONFIG_BLK_DEV_CMD640), y)
	cmd640-core-y += pci/cmd640.o
	obj-y += cmd640-core.o
endif

obj-$(CONFIG_IDE)			+= ppc/
obj-$(CONFIG_IDE_H8300)			+= h8300/
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

ifeq ($(CONFIG_BLK_DEV_IDECS), y)
	ide-cs-core-y += legacy/ide-cs.o
	obj-y += ide-cs-core.o
endif

ifeq ($(CONFIG_BLK_DEV_PLATFORM), y)
	ide-platform-core-y += legacy/ide_platform.o
	obj-y += ide-platform-core.o
endif

obj-$(CONFIG_IDE)			+= arm/ mips/
