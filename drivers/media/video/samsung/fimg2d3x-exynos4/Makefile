# drivers/media/video/samsung/fimg2d3x/Makefile
#
# Copyright (c) 2010 Samsung Electronics Co., Ltd.
#	http://www.samsung.com/
#
# Licensed under GPLv2

obj-y				:=
obj-m				:=
obj-n				:=
obj-				:=

obj-$(CONFIG_VIDEO_FIMG2D3X) += fimg2d_dev.o fimg2d_cache.o fimg2d3x_regs.o fimg2d_core.o

ifeq ($(CONFIG_VIDEO_FIMG2D3X_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
