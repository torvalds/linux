#
# Makefile for MMC/SD card drivers
#
ifeq ($(CONFIG_MMC_CPRM),y)
EXTRA_CFLAGS += -I$(src)/cprm/softcprm
EXTRA_CFLAGS += -I$(src)/cprm/include
endif

obj-$(CONFIG_MMC_BLOCK)		+= mmc_block.o
mmc_block-objs			:= block.o queue.o
ifeq ($(CONFIG_MMC_CPRM),y)
mmc_block-objs			+= cprmdrv_samsung.o
endif
obj-$(CONFIG_MMC_TEST)		+= mmc_test.o

obj-$(CONFIG_SDIO_UART)		+= sdio_uart.o
