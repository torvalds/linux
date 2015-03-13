tm6000-y := tm6000-cards.o \
		   tm6000-core.o  \
		   tm6000-i2c.o   \
		   tm6000-video.o \
		   tm6000-stds.o \
		   tm6000-input.o

obj-$(CONFIG_BACKPORT_VIDEO_TM6000) += tm6000.o
obj-$(CONFIG_BACKPORT_VIDEO_TM6000_ALSA) += tm6000-alsa.o
obj-$(CONFIG_BACKPORT_VIDEO_TM6000_DVB) += tm6000-dvb.o

ccflags-y += -I$(backport_srctree)/drivers/media/i2c
ccflags-y += -I$(backport_srctree)/drivers/media/tuners
ccflags-y += -I$(backport_srctree)/drivers/media/dvb-core
ccflags-y += -I$(backport_srctree)/drivers/media/dvb-frontends
