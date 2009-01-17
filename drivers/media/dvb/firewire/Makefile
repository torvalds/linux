firedtv-objs := firedtv-1394.o	\
		firedtv-dvb.o	\
		firedtv-fe.o	\
		firedtv-iso.o	\
		avc.o		\
		cmp.o		\
		firedtv-rc.o	\
		firedtv-ci.o

obj-$(CONFIG_DVB_FIREDTV) += firedtv.o

EXTRA_CFLAGS := -Idrivers/ieee1394
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
