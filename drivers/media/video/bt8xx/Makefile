#
# Makefile for the video capture/playback device drivers.
#

bttv-objs      :=      bttv-driver.o bttv-cards.o bttv-if.o \
		       bttv-risc.o bttv-vbi.o bttv-i2c.o bttv-gpio.o \
		       bttv-input.o bttv-audio-hook.o

obj-$(CONFIG_VIDEO_BT848) += bttv.o

EXTRA_CFLAGS += -Idrivers/media/video
EXTRA_CFLAGS += -Idrivers/media/common/tuners
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
