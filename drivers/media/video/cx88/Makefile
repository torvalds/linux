cx88xx-objs	:= cx88-cards.o cx88-core.o cx88-i2c.o cx88-tvaudio.o \
		   cx88-input.o
cx8800-objs	:= cx88-video.o cx88-vbi.o
cx8802-objs	:= cx88-mpeg.o

obj-$(CONFIG_VIDEO_CX88) += cx88xx.o cx8800.o cx8802.o cx88-blackbird.o
obj-$(CONFIG_VIDEO_CX88_DVB) += cx88-dvb.o

EXTRA_CFLAGS += -I$(src)/..
EXTRA_CFLAGS += -I$(srctree)/drivers/media/dvb/dvb-core
EXTRA_CFLAGS += -I$(srctree)/drivers/media/dvb/frontends
ifneq ($(CONFIG_VIDEO_BUF_DVB),n)
 EXTRA_CFLAGS += -DHAVE_VIDEO_BUF_DVB=1
endif
ifneq ($(CONFIG_DVB_CX22702),n)
 EXTRA_CFLAGS += -DHAVE_CX22702=1
endif
ifneq ($(CONFIG_DVB_OR51132),n)
 EXTRA_CFLAGS += -DHAVE_OR51132=1
endif
ifneq ($(CONFIG_DVB_LGDT330X),n)
 EXTRA_CFLAGS += -DHAVE_LGDT330X=1
endif
ifneq ($(CONFIG_DVB_MT352),n)
 EXTRA_CFLAGS += -DHAVE_MT352=1
endif
ifneq ($(CONFIG_DVB_NXT200X),n)
 EXTRA_CFLAGS += -DHAVE_NXT200X=1
endif
