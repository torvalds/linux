obj-$(CONFIG_DVB_FIREDTV) += firedtv.o

firedtv-y += firedtv-avc.o firedtv-ci.o firedtv-dvb.o firedtv-fe.o firedtv-fw.o
firedtv-$(CONFIG_DVB_FIREDTV_INPUT)    += firedtv-rc.o

ccflags-y += -Idrivers/media/dvb-core
