
saa7134-objs :=	saa7134-cards.o saa7134-core.o saa7134-i2c.o	\
		saa7134-ts.o saa7134-tvaudio.o saa7134-vbi.o    \
		saa7134-video.o saa7134-input.o

obj-$(CONFIG_VIDEO_SAA7134) +=  saa7134.o saa7134-empress.o \
				saa6752hs.o

obj-$(CONFIG_VIDEO_SAA7134_ALSA) += saa7134-alsa.o
obj-$(CONFIG_VIDEO_SAA7134_OSS) += saa7134-oss.o

obj-$(CONFIG_VIDEO_SAA7134_DVB) += saa7134-dvb.o

EXTRA_CFLAGS += -I$(src)/..
EXTRA_CFLAGS += -I$(srctree)/drivers/media/dvb/dvb-core
EXTRA_CFLAGS += -I$(srctree)/drivers/media/dvb/frontends

extra-cflags-$(CONFIG_VIDEO_BUF_DVB) += -DHAVE_VIDEO_BUF_DVB=1
extra-cflags-$(CONFIG_DVB_MT352)     += -DHAVE_MT352=1
extra-cflags-$(CONFIG_DVB_TDA1004X)  += -DHAVE_TDA1004X=1
extra-cflags-$(CONFIG_DVB_NXT200X)   += -DHAVE_NXT200X=1

EXTRA_CFLAGS += $(extra-cflags-y) $(extra-cflags-m)
