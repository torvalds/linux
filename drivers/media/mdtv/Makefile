smscore-objs := smscoreapi.o smsusb.o smsdvb.o

obj-$(CONFIG_MDTV_SIANO_STELLAR_USB) += smscore.o

EXTRA_CFLAGS += -Idrivers/media/video
EXTRA_CFLAGS += -Idrivers/media/dvb/dvb-core
EXTRA_CFLAGS += -Idrivers/media/dvb/frontends

EXTRA_CFLAGS += $(extra-cflags-y) $(extra-cflags-m)

