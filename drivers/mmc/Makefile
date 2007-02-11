#
# Makefile for the kernel mmc device drivers.
#

ifeq ($(CONFIG_MMC_DEBUG),y)
	EXTRA_CFLAGS		+= -DDEBUG
endif

#
# Core
#
obj-$(CONFIG_MMC)		+= mmc_core.o
mmc_core-y			:= mmc.o mmc_sysfs.o

obj-$(CONFIG_MMC)		+= card/
obj-$(CONFIG_MMC)		+= host/

