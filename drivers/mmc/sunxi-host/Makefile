
#
# Makefile for MMC/SD host controller drivers
#

ifeq ($(CONFIG_MMC_DEBUG),y)
	EXTRA_CFLAGS		+= -DDEBUG
endif

	
obj-$(CONFIG_MMC_SUNXI)    += mmc_host.o

mmc_host-y := sdxc.o host_op.o host_fs.o
