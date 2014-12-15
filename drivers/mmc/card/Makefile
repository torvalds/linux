#
# Makefile for MMC/SD card drivers
#

obj-$(CONFIG_MMC_BLOCK)			+= mmc_block.o
mmc_block-objs				:= block.o queue.o emmc_partitions.o
obj-$(CONFIG_SECURITYKEY)		+= emmc_key.o

obj-$(CONFIG_EMMC_SECURE_STORAGE)	+= mmc_storage.o

obj-$(CONFIG_MMC_TEST)			+= mmc_test.o

obj-$(CONFIG_SDIO_UART)			+= sdio_uart.o

