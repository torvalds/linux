# TODO: all of these should be removed
EXTRA_CFLAGS += -DLINUX -D__KERNEL__ -DMODULE  -DEXPORT_SYMTAB -D__NO_VERSION__
EXTRA_CFLAGS += -DHOSTAP

vt6655-y +=	device_main.o \
	card.o \
	mac.o \
	baseband.o \
	wctl.o \
	80211mgr.o \
	wcmd.o \
	wmgr.o \
	bssdb.o \
	rxtx.o \
	dpc.o \
	power.o \
	datarate.o \
	srom.o \
	mib.o \
	rc4.o \
	tether.o \
	tcrc.o \
	ioctl.o \
	hostap.o \
	wpa.o \
	key.o \
	tkip.o \
	michael.o \
	wroute.o \
	rf.o \
	iwctl.o \
	wpactl.o \
	wpa2.o \
	aes_ccmp.o \
	vntwifi.o \
	IEEE11h.o

obj-$(CONFIG_VT6655) +=	vt6655.o
