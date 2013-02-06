# linux/drivers/media/video/samsung/tvout/hw_if/Makefile
#
# Copyright (c) 2009 Samsung Electronics
#		http://www.samsung.com/
#
# Makefile for Samsung TVOUT driver

ifeq ($(CONFIG_VIDEO_TVOUT_DEBUG), y)
EXTRA_CFLAGS += -DDEBUG
endif
obj-$(CONFIG_VIDEO_TVOUT) +=	mixer.o		\
				vp.o		\
				hdmi.o		\
				hdcp.o

obj-$(CONFIG_ANALOG_TVENC) +=	sdo.o


obj-$(CONFIG_HDMI_CEC) +=	cec.o
