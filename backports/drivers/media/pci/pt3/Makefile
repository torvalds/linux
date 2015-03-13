
earth-pt3-objs += pt3.o pt3_i2c.o pt3_dma.o

obj-$(CONFIG_BACKPORT_DVB_PT3) += earth-pt3.o

ccflags-y += -I$(backport_srctree)/drivers/media/dvb-core
ccflags-y += -I$(backport_srctree)/drivers/media/dvb-frontends
ccflags-y += -I$(backport_srctree)/drivers/media/tuners
