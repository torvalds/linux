# TODO: all of these should be removed
ccflags-y := -DLINUX -D__KERNEL__ -D__NO_VERSION__
ccflags-y += -DHOSTAP

vt6655_stage-y +=	device_main.o \
	card.o \
	channel.o \
	mac.o \
	baseband.o \
	wctl.o \
	80211mgr.o \
	rxtx.o \
	dpc.o \
	power.o \
	srom.o \
	mib.o \
	rc4.o \
	tether.o \
	tcrc.o \
	wpa.o \
	key.o \
	tkip.o \
	michael.o \
	wroute.o \
	rf.o \
	wpa2.o \
	aes_ccmp.o \
	vntwifi.o

obj-$(CONFIG_VT6655) +=	vt6655_stage.o
