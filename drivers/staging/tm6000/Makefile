tm6000-objs := tm6000-cards.o \
		   tm6000-core.o  \
		   tm6000-i2c.o   \
		   tm6000-video.o \
		   tm6000-stds.o \
		   tm6000-input.o

obj-$(CONFIG_VIDEO_TM6000) += tm6000.o
obj-$(CONFIG_VIDEO_TM6000_ALSA) += tm6000-alsa.o
obj-$(CONFIG_VIDEO_TM6000_DVB) += tm6000-dvb.o

EXTRA_CFLAGS = -Idrivers/media/video
EXTRA_CFLAGS += -Idrivers/media/common/tuners
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
EXTRA_CFLAGS += -Idrivers/media/dvb/frontends
