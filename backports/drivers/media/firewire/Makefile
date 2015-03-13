obj-$(CONFIG_BACKPORT_DVB_FIREDTV) += firedtv.o

firedtv-y += firedtv-avc.o firedtv-ci.o firedtv-dvb.o firedtv-fe.o firedtv-fw.o
firedtv-$(CONFIG_BACKPORT_DVB_FIREDTV_INPUT)    += firedtv-rc.o

ccflags-y += -I$(backport_srctree)/drivers/media/dvb-core
