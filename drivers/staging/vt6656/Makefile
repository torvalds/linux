# TODO: all of these should be removed
ccflags-y := -DLINUX -D__KERNEL__ -DEXPORT_SYMTAB -D__NO_VERSION__
ccflags-y += -DHOSTAP

vt6656_stage-y +=	main_usb.o \
			card.o  \
			mac.o   \
			baseband.o \
			wctl.o  \
			80211mgr.o \
			wcmd.o\
			wmgr.o \
			bssdb.o \
			wpa2.o \
			rxtx.o \
			dpc.o \
			power.o \
			datarate.o \
			rc4.o \
			tether.o \
			tcrc.o \
			hostap.o \
			wpa.o \
			key.o \
			tkip.o \
			michael.o \
			rf.o \
			iwctl.o \
			wpactl.o \
			aes_ccmp.o \
			usbpipe.o \
			channel.o \
			control.o \
			firmware.o \
			int.o

obj-$(CONFIG_VT6656) +=	vt6656_stage.o
