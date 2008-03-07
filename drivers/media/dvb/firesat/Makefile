firesat-objs := firesat_1394.o	\
		firesat_dvb.o	\
		firesat_fe.o	\
		avc_api.o	\
		cmp.o		\
		firesat-rc.o	\
		firesat-ci.o

obj-$(CONFIG_DVB_FIRESAT) += firesat.o

EXTRA_CFLAGS := -Idrivers/ieee1394
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
