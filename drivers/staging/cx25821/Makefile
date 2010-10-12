cx25821-objs   := cx25821-core.o cx25821-cards.o cx25821-i2c.o \
		       cx25821-gpio.o cx25821-medusa-video.o \
		       cx25821-video.o cx25821-video-upstream.o \
		       cx25821-video-upstream-ch2.o \
		       cx25821-audio-upstream.o

obj-$(CONFIG_VIDEO_CX25821) += cx25821.o
obj-$(CONFIG_VIDEO_CX25821_ALSA) += cx25821-alsa.o

EXTRA_CFLAGS += -Idrivers/media/video
EXTRA_CFLAGS += -Idrivers/media/common/tuners
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
EXTRA_CFLAGS += -Idrivers/media/dvb/frontends
