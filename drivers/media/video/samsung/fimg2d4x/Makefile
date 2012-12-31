# drivers/media/video/samsung/fimg2d4x/Makefile
#
# Copyright (c) 2011 Samsung Electronics Co., Ltd.
#	http://www.samsung.com/
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

obj-$(CONFIG_VIDEO_FIMG2D) += fimg2d_drv.o fimg2d_ctx.o fimg2d_cache.o fimg2d_clk.o fimg2d_helper.o
obj-$(CONFIG_VIDEO_FIMG2D4X) += fimg2d4x_blt.o fimg2d4x_hw.o

ifeq ($(CONFIG_VIDEO_FIMG2D_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
