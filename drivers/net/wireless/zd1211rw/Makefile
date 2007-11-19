obj-$(CONFIG_ZD1211RW) += zd1211rw.o

zd1211rw-objs := zd_chip.o zd_ieee80211.o zd_mac.o \
		zd_rf_al2230.o zd_rf_rf2959.o \
		zd_rf_al7230b.o zd_rf_uw2453.o \
		zd_rf.o zd_usb.o

ifeq ($(CONFIG_ZD1211RW_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif

