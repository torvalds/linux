cx23885-objs	:= cx23885-cards.o cx23885-video.o cx23885-vbi.o cx23885-core.o cx23885-i2c.o cx23885-dvb.o cx23885-417.o

obj-$(CONFIG_VIDEO_CX23885) += cx23885.o

EXTRA_CFLAGS += -Idrivers/media/video
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
EXTRA_CFLAGS += -Idrivers/media/dvb/frontends

EXTRA_CFLAGS += $(extra-cflags-y) $(extra-cflags-m)
