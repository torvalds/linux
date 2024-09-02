# SPDX-License-Identifier: GPL-2.0-only

zram-y	:=	zcomp.o zram_drv.o

zram-$(CONFIG_ZRAM_BACKEND_LZO)	+= backend_lzorle.o backend_lzo.o
zram-$(CONFIG_ZRAM_BACKEND_LZ4)	+= backend_lz4.o

obj-$(CONFIG_ZRAM)	+=	zram.o
