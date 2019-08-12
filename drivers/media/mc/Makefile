# SPDX-License-Identifier: GPL-2.0

mc-objs	:= mc-device.o mc-devnode.o mc-entity.o \
	   mc-request.o

ifeq ($(CONFIG_USB),y)
	mc-objs += mc-dev-allocator.o
endif

obj-$(CONFIG_MEDIA_SUPPORT) += mc.o
