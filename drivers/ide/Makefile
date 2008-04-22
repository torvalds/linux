#
# Makefile for the kernel ata, atapi, and ide block device drivers.
#
# 12 September 2000, Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
# Rewritten to use lists instead of if-statements.
#
# Note : at this point, these files are compiled on all systems.
# In the future, some of these should be built conditionally.
#
# link order is important here

EXTRA_CFLAGS				+= -Idrivers/ide

ide-core-y += ide.o ide-io.o ide-iops.o ide-lib.o ide-probe.o ide-taskfile.o

# core IDE code
ide-core-$(CONFIG_BLK_DEV_IDEPCI)	+= setup-pci.o
ide-core-$(CONFIG_BLK_DEV_IDEDMA)	+= ide-dma.o
ide-core-$(CONFIG_IDE_PROC_FS)		+= ide-proc.o
ide-core-$(CONFIG_BLK_DEV_IDEACPI)	+= ide-acpi.o

obj-$(CONFIG_BLK_DEV_IDE)		+= ide-core.o

ifeq ($(CONFIG_IDE_ARM), y)
	ide-arm-core-y += arm/ide_arm.o
	obj-y += ide-arm-core.o
endif

obj-$(CONFIG_BLK_DEV_IDE)		+= legacy/ pci/

obj-$(CONFIG_IDEPCI_PCIBUS_ORDER)	+= ide-scan-pci.o

ifeq ($(CONFIG_BLK_DEV_CMD640), y)
	cmd640-core-y += pci/cmd640.o
	obj-y += cmd640-core.o
endif

obj-$(CONFIG_BLK_DEV_IDE)		+= cris/ ppc/
obj-$(CONFIG_IDE_H8300)			+= h8300/
obj-$(CONFIG_IDE_GENERIC)		+= ide-generic.o
obj-$(CONFIG_BLK_DEV_IDEPNP)		+= ide-pnp.o

ide-cd_mod-y += ide-cd.o ide-cd_ioctl.o ide-cd_verbose.o

obj-$(CONFIG_BLK_DEV_IDEDISK)		+= ide-disk.o
obj-$(CONFIG_BLK_DEV_IDECD)		+= ide-cd_mod.o
obj-$(CONFIG_BLK_DEV_IDETAPE)		+= ide-tape.o
obj-$(CONFIG_BLK_DEV_IDEFLOPPY)		+= ide-floppy.o

ifeq ($(CONFIG_BLK_DEV_IDECS), y)
	ide-cs-core-y += legacy/ide-cs.o
	obj-y += ide-cs-core.o
endif

ifeq ($(CONFIG_BLK_DEV_PLATFORM), y)
	ide-platform-core-y += legacy/ide_platform.o
	obj-y += ide-platform-core.o
endif

obj-$(CONFIG_BLK_DEV_IDE)		+= arm/ mips/

# old hd driver must be last
ifeq ($(CONFIG_BLK_DEV_HD), y)
	hd-core-y += legacy/hd.o
	obj-y += hd-core.o
endif
