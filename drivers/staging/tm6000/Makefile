tm6000-objs := tm6000-cards.o \
		   tm6000-core.o  \
		   tm6000-i2c.o   \
		   tm6000-video.o

ifeq ($(CONFIG_VIDEO_TM6000_DVB),y)
tm6000-objs := tm6000-dvb.o \
		   hack.o
endif

obj-$(CONFIG_VIDEO_TM6000) += tm6000.o

EXTRA_CFLAGS = -Idrivers/media/video
