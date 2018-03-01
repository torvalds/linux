# SPDX-License-Identifier: GPL-2.0
em28xx-y +=	em28xx-core.o em28xx-i2c.o em28xx-cards.o em28xx-camera.o

em28xx-v4l-objs := em28xx-video.o em28xx-vbi.o
em28xx-alsa-objs := em28xx-audio.o
em28xx-rc-objs := em28xx-input.o

obj-$(CONFIG_VIDEO_EM28XX) += em28xx.o
obj-$(CONFIG_VIDEO_EM28XX_V4L2) += em28xx-v4l.o
obj-$(CONFIG_VIDEO_EM28XX_ALSA) += em28xx-alsa.o
obj-$(CONFIG_VIDEO_EM28XX_DVB) += em28xx-dvb.o
obj-$(CONFIG_VIDEO_EM28XX_RC) += em28xx-rc.o

ccflags-y += -Idrivers/media/tuners
ccflags-y += -Idrivers/media/dvb-frontends
