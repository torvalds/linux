# SPDX-License-Identifier: GPL-2.0
# For include/trace/define_trace.h to include trace.h
CFLAGS_regmap.o := -I$(src)

obj-$(CONFIG_REGMAP) += regmap.o regcache.o
obj-$(CONFIG_REGMAP) += regcache-rbtree.o regcache-flat.o
obj-$(CONFIG_REGCACHE_COMPRESSED) += regcache-lzo.o
obj-$(CONFIG_DEBUG_FS) += regmap-debugfs.o
obj-$(CONFIG_REGMAP_AC97) += regmap-ac97.o
obj-$(CONFIG_REGMAP_I2C) += regmap-i2c.o
obj-$(CONFIG_REGMAP_SLIMBUS) += regmap-slimbus.o
obj-$(CONFIG_REGMAP_SPI) += regmap-spi.o
obj-$(CONFIG_REGMAP_SPMI) += regmap-spmi.o
obj-$(CONFIG_REGMAP_MMIO) += regmap-mmio.o
obj-$(CONFIG_REGMAP_IRQ) += regmap-irq.o
obj-$(CONFIG_REGMAP_W1) += regmap-w1.o
obj-$(CONFIG_REGMAP_SOUNDWIRE) += regmap-sdw.o
obj-$(CONFIG_REGMAP_SCCB) += regmap-sccb.o
obj-$(CONFIG_REGMAP_I3C) += regmap-i3c.o
obj-$(CONFIG_REGMAP_SPI_AVMM) += regmap-spi-avmm.o
