
saa7134-y +=	saa7134-cards.o saa7134-core.o saa7134-i2c.o
saa7134-y +=	saa7134-ts.o saa7134-tvaudio.o saa7134-vbi.o
saa7134-y +=	saa7134-video.o
saa7134-$(CONFIG_VIDEO_SAA7134_RC) += saa7134-input.o

obj-$(CONFIG_VIDEO_SAA7134) +=  saa6752hs.o saa7134.o saa7134-empress.o

obj-$(CONFIG_VIDEO_SAA7134_ALSA) += saa7134-alsa.o

obj-$(CONFIG_VIDEO_SAA7134_DVB) += saa7134-dvb.o

ccflags-y += -I$(srctree)/drivers/media/i2c
ccflags-y += -I$(srctree)/drivers/media/tuners
ccflags-y += -I$(srctree)/drivers/media/dvb-core
ccflags-y += -I$(srctree)/drivers/media/dvb-frontends
