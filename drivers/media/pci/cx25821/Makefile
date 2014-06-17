cx25821-y   := cx25821-core.o cx25821-cards.o cx25821-i2c.o \
		       cx25821-gpio.o cx25821-medusa-video.o \
		       cx25821-video.o cx25821-video-upstream.o

obj-$(CONFIG_VIDEO_CX25821) += cx25821.o
obj-$(CONFIG_VIDEO_CX25821_ALSA) += cx25821-alsa.o

ccflags-y += -Idrivers/media/i2c
ccflags-y += -Idrivers/media/common
