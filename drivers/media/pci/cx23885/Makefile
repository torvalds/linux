# SPDX-License-Identifier: GPL-2.0
cx23885-objs	:= cx23885-cards.o cx23885-video.o cx23885-vbi.o \
		    cx23885-core.o cx23885-i2c.o cx23885-dvb.o cx23885-417.o \
		    cx23885-ioctl.o cx23885-ir.o cx23885-av.o cx23885-input.o \
		    cx23888-ir.o netup-init.o cimax2.o netup-eeprom.o \
		    cx23885-f300.o cx23885-alsa.o

obj-$(CONFIG_VIDEO_CX23885) += cx23885.o
obj-$(CONFIG_MEDIA_ALTERA_CI) += altera-ci.o

ccflags-y += -I $(srctree)/drivers/media/tuners
ccflags-y += -I $(srctree)/drivers/media/dvb-frontends

ccflags-y += $(extra-cflags-y) $(extra-cflags-m)
