# SPDX-License-Identifier: GPL-2.0
# TODO: all of these should be removed
ccflags-y := -DLINUX -D__KERNEL__ -DEXPORT_SYMTAB -D__NO_VERSION__
ccflags-y += -DHOSTAP

vt6656_stage-y +=	main_usb.o \
			card.o  \
			mac.o   \
			baseband.o \
			wcmd.o\
			rxtx.o \
			dpc.o \
			power.o \
			key.o \
			rf.o \
			usbpipe.o \
			channel.o \
			firmware.o \
			int.o

obj-$(CONFIG_VT6656) +=	vt6656_stage.o
