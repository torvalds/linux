cx88xx-objs	:= cx88-cards.o cx88-core.o cx88-i2c.o cx88-tvaudio.o \
		   cx88-input.o
cx8800-objs	:= cx88-video.o cx88-vbi.o
cx8802-objs	:= cx88-mpeg.o

obj-$(CONFIG_VIDEO_CX88) += cx88xx.o cx8800.o cx8802.o cx88-blackbird.o
obj-$(CONFIG_VIDEO_CX88_DVB) += cx88-dvb.o

EXTRA_CFLAGS += -I$(src)/..
EXTRA_CFLAGS += -I$(srctree)/drivers/media/dvb/dvb-core
EXTRA_CFLAGS += -I$(srctree)/drivers/media/dvb/frontends
