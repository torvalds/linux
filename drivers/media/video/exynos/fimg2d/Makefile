# drivers/media/video/exynos/fimg2d/Makefile
#
# Copyright (c) 2011 Samsung Electronics Co., Ltd.
#	http://www.samsung.com/
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

obj-$(CONFIG_VIDEO_EXYNOS_FIMG2D) += fimg2d_drv.o fimg2d_clk.o \
				fimg2d_ctx.o fimg2d_cache.o fimg2d_helper.o \
				fimg2d4x_blt.o fimg2d4x_hw.o

ccflags-y += -DDEBUG
ccflags-y += -DBLIT_WORKQUE
ccflags-$(CONFIG_FIMG2D_CCI_SNOOP) += -DCCI_SNOOP
