# TODO: all of these should be removed
ccflags-y := -DLINUX -D__KERNEL__ -DEXPORT_SYMTAB -D__NO_VERSION__
ccflags-y += -DHOSTAP

vt6656_stage-y +=	main_usb.o \
			card.o  \
			mac.o   \
			baseband.o \
			80211mgr.o \
			wcmd.o\
			rxtx.o \
			dpc.o \
			power.o \
			datarate.o \
			key.o \
			rf.o \
			usbpipe.o \
			channel.o \
			firmware.o \
			int.o

obj-$(CONFIG_VT6656) +=	vt6656_stage.o
