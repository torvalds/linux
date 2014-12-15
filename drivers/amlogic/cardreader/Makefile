#
# linux/arch/arc/drivers/Makefile
#
#

#O_TARGET	:= peripherals.o
#CFLAGS+= -mno-volatile-cache
obj-$(CONFIG_CARDREADER) +=  card_sysfs.o
obj-$(CONFIG_CARDREADER) +=  card_block.o
obj-$(CONFIG_CARDREADER) +=  cardreader.o
obj-$(CONFIG_CARDREADER) +=  card_io.o
obj-$(CONFIG_SDIO) += sdio_sys.o sdio_io.o sdio_ops.o sdio_cis.o
obj-$(CONFIG_SD_MMC) += sd/
obj-$(CONFIG_AM_ATA) += ata/
obj-$(CONFIG_CF) += cf/
obj-$(CONFIG_MS_MSPRO) += ms/
obj-$(CONFIG_XD) += xd/
obj-$(CONFIG_AML_CARD_KEY)	+=	card_key.o

#include $(TOPDIR)/Rules.make

