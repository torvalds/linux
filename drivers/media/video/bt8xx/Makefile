#
# Makefile for the video capture/playback device drivers.
#

bttv-objs      :=      bttv-driver.o bttv-cards.o bttv-if.o \
		       bttv-risc.o bttv-vbi.o bttv-i2c.o bttv-gpio.o \
		       bttv-input.o

obj-$(CONFIG_VIDEO_BT848) += bttv.o

EXTRA_CFLAGS += -I$(src)/..
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
