cx231xx-objs     := cx231xx-video.o cx231xx-i2c.o cx231xx-cards.o cx231xx-core.o \
		    cx231xx-avcore.o cx231xx-417.o cx231xx-pcb-cfg.o cx231xx-vbi.o

cx231xx-alsa-objs := cx231xx-audio.o

obj-$(CONFIG_VIDEO_CX231XX) += cx231xx.o
obj-$(CONFIG_VIDEO_CX231XX_ALSA) += cx231xx-alsa.o
obj-$(CONFIG_VIDEO_CX231XX_DVB) += cx231xx-dvb.o

EXTRA_CFLAGS += -Idrivers/media/video
EXTRA_CFLAGS += -Idrivers/media/common/tuners
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
EXTRA_CFLAGS += -Idrivers/media/dvb/frontends
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-usb

