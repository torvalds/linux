#
# Makefile for the kernel ata, atapi, and ide block device drivers.
#
# 12 September 2000, Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
# Rewritten to use lists instead of if-statements.
#
# Note : at this point, these files are compiled on all systems.
# In the future, some of these should be built conditionally.
#
# First come modules that register themselves with the core

EXTRA_CFLAGS				+= -Idrivers/ide

obj-$(CONFIG_BLK_DEV_IDE)		+= pci/

ide-core-y += ide.o ide-io.o ide-iops.o ide-lib.o ide-probe.o ide-taskfile.o

ide-core-$(CONFIG_BLK_DEV_CMD640)	+= pci/cmd640.o

# Core IDE code - must come before legacy
ide-core-$(CONFIG_BLK_DEV_IDEPCI)	+= setup-pci.o
ide-core-$(CONFIG_BLK_DEV_IDEDMA)	+= ide-dma.o
ide-core-$(CONFIG_PROC_FS)		+= ide-proc.o
ide-core-$(CONFIG_BLK_DEV_IDEPNP)	+= ide-pnp.o

# built-in only drivers from arm/
ide-core-$(CONFIG_IDE_ARM)		+= arm/ide_arm.o

# built-in only drivers from legacy/
ide-core-$(CONFIG_BLK_DEV_BUDDHA)	+= legacy/buddha.o
ide-core-$(CONFIG_BLK_DEV_FALCON_IDE)	+= legacy/falconide.o
ide-core-$(CONFIG_BLK_DEV_GAYLE)	+= legacy/gayle.o
ide-core-$(CONFIG_BLK_DEV_MAC_IDE)	+= legacy/macide.o
ide-core-$(CONFIG_BLK_DEV_Q40IDE)	+= legacy/q40ide.o

# built-in only drivers from ppc/
ide-core-$(CONFIG_BLK_DEV_MPC8xx_IDE)	+= ppc/mpc8xx.o
ide-core-$(CONFIG_BLK_DEV_IDE_PMAC)	+= ppc/pmac.o

# built-in only drivers from h8300/
ide-core-$(CONFIG_H8300)		+= h8300/ide-h8300.o

obj-$(CONFIG_BLK_DEV_IDE)		+= ide-core.o
obj-$(CONFIG_IDE_GENERIC)		+= ide-generic.o

obj-$(CONFIG_BLK_DEV_IDEDISK)		+= ide-disk.o
obj-$(CONFIG_BLK_DEV_IDECD)		+= ide-cd.o
obj-$(CONFIG_BLK_DEV_IDETAPE)		+= ide-tape.o
obj-$(CONFIG_BLK_DEV_IDEFLOPPY)		+= ide-floppy.o

obj-$(CONFIG_BLK_DEV_IDE)		+= legacy/ arm/ mips/
obj-$(CONFIG_BLK_DEV_HD)		+= legacy/
obj-$(CONFIG_ETRAX_IDE)		+= cris/
