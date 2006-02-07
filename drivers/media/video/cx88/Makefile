cx88xx-objs	:= cx88-cards.o cx88-core.o cx88-i2c.o cx88-tvaudio.o \
		   cx88-input.o
cx8800-objs	:= cx88-video.o cx88-vbi.o
cx8802-objs	:= cx88-mpeg.o

obj-$(CONFIG_VIDEO_CX88) += cx88xx.o cx8800.o cx8802.o cx88-blackbird.o
obj-$(CONFIG_VIDEO_CX88_DVB) += cx88-dvb.o
obj-$(CONFIG_VIDEO_CX88_ALSA) += cx88-alsa.o
obj-$(CONFIG_VIDEO_CX88_VP3054) += cx88-vp3054-i2c.o

EXTRA_CFLAGS += -I$(src)/..
EXTRA_CFLAGS += -I$(srctree)/drivers/media/dvb/dvb-core
EXTRA_CFLAGS += -I$(srctree)/drivers/media/dvb/frontends

extra-cflags-$(CONFIG_VIDEO_BUF_DVB) += -DHAVE_VIDEO_BUF_DVB=1
extra-cflags-$(CONFIG_DVB_CX22702)   += -DHAVE_CX22702=1
extra-cflags-$(CONFIG_DVB_OR51132)   += -DHAVE_OR51132=1
extra-cflags-$(CONFIG_DVB_LGDT330X)  += -DHAVE_LGDT330X=1
extra-cflags-$(CONFIG_DVB_MT352)     += -DHAVE_MT352=1
extra-cflags-$(CONFIG_DVB_NXT200X)   += -DHAVE_NXT200X=1
extra-cflags-$(CONFIG_DVB_CX24123)   += -DHAVE_CX24123=1
extra-cflags-$(CONFIG_VIDEO_CX88_VP3054)+= -DHAVE_VP3054_I2C=1

EXTRA_CFLAGS += $(extra-cflags-y) $(extra-cflags-m)
