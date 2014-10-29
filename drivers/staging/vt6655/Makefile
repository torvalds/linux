# TODO: all of these should be removed
ccflags-y := -DLINUX -D__KERNEL__ -D__NO_VERSION__
ccflags-y += -DHOSTAP

vt6655_stage-y +=	device_main.o \
	card.o \
	channel.o \
	mac.o \
	baseband.o \
	rxtx.o \
	dpc.o \
	power.o \
	srom.o \
	mib.o \
	key.o \
	rf.o

obj-$(CONFIG_VT6655) +=	vt6655_stage.o
