#
# Makefile for the kernel mmc core.
#

ifeq ($(CONFIG_MMC_DEBUG),y)
	EXTRA_CFLAGS		+= -DDEBUG
endif

obj-$(CONFIG_MMC)		+= mmc_core.o
mmc_core-y			:= core.o sysfs.o bus.o host.o \
				   mmc.o mmc_ops.o sd.o sd_ops.o

