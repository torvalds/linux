cx88xx-objs	:= cx88-cards.o cx88-core.o cx88-i2c.o cx88-tvaudio.o \
		   cx88-input.o
cx8800-objs	:= cx88-video.o cx88-vbi.o
cx8802-objs	:= cx88-mpeg.o

obj-$(CONFIG_VIDEO_CX88) += cx88xx.o cx8800.o
obj-$(CONFIG_VIDEO_CX88_MPEG) += cx8802.o
obj-$(CONFIG_VIDEO_CX88_ALSA) += cx88-alsa.o
obj-$(CONFIG_VIDEO_CX88_BLACKBIRD) += cx88-blackbird.o
obj-$(CONFIG_VIDEO_CX88_DVB) += cx88-dvb.o
obj-$(CONFIG_VIDEO_CX88_VP3054) += cx88-vp3054-i2c.o

EXTRA_CFLAGS += -Idrivers/media/video
EXTRA_CFLAGS += -Idrivers/media/common/tuners
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
EXTRA_CFLAGS += -Idrivers/media/dvb/frontends
