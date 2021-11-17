# SPDX-License-Identifier: GPL-2.0
ivtv-objs	:= ivtv-routing.o ivtv-cards.o ivtv-controls.o \
		   ivtv-driver.o ivtv-fileops.o ivtv-firmware.o \
		   ivtv-gpio.o ivtv-i2c.o ivtv-ioctl.o ivtv-irq.o \
		   ivtv-mailbox.o ivtv-queue.o ivtv-streams.o ivtv-udma.o \
		   ivtv-vbi.o ivtv-yuv.o
ivtv-alsa-objs := ivtv-alsa-main.o ivtv-alsa-pcm.o

obj-$(CONFIG_VIDEO_IVTV) += ivtv.o
obj-$(CONFIG_VIDEO_IVTV_ALSA) += ivtv-alsa.o
obj-$(CONFIG_VIDEO_FB_IVTV) += ivtvfb.o

ccflags-y += -I$(srctree)/drivers/media/tuners
ccflags-y += -I$(srctree)/drivers/media/dvb-frontends

