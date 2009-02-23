obj-$(CONFIG_DVB_FIREDTV) += firedtv.o

firedtv-y := firedtv-avc.o firedtv-ci.o firedtv-dvb.o firedtv-fe.o
firedtv-$(CONFIG_DVB_FIREDTV_IEEE1394) += firedtv-1394.o
firedtv-$(CONFIG_DVB_FIREDTV_INPUT)    += firedtv-rc.o

ccflags-y += -Idrivers/media/dvb/dvb-core
ccflags-$(CONFIG_DVB_FIREDTV_IEEE1394) += -Idrivers/ieee1394
