# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel mmc core.
#

obj-$(CONFIG_MMC)		+= mmc_core.o
mmc_core-y			:= core.o bus.o host.o \
				   mmc.o mmc_ops.o sd.o sd_ops.o \
				   sdio.o sdio_ops.o sdio_bus.o \
				   sdio_cis.o sdio_io.o sdio_irq.o \
				   slot-gpio.o regulator.o
mmc_core-$(CONFIG_OF)		+= pwrseq.o
obj-$(CONFIG_PWRSEQ_SIMPLE)	+= pwrseq_simple.o
obj-$(CONFIG_PWRSEQ_SD8787)	+= pwrseq_sd8787.o
obj-$(CONFIG_PWRSEQ_EMMC)	+= pwrseq_emmc.o
mmc_core-$(CONFIG_DEBUG_FS)	+= debugfs.o
obj-$(CONFIG_MMC_BLOCK)		+= mmc_block.o
mmc_block-objs			:= block.o queue.o
obj-$(CONFIG_MMC_TEST)		+= mmc_test.o
obj-$(CONFIG_SDIO_UART)		+= sdio_uart.o
