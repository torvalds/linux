ccflags-$(CONFIG_USB_WUSB_CBAF_DEBUG) := -DDEBUG

obj-$(CONFIG_USB_WUSB)		+= wusbcore.o
obj-$(CONFIG_USB_HWA_HCD)	+= wusb-wa.o
obj-$(CONFIG_USB_WUSB_CBAF)	+= wusb-cbaf.o


wusbcore-y :=		\
	crypto.o	\
	devconnect.o	\
	dev-sysfs.o	\
	mmc.o		\
	pal.o		\
	rh.o		\
	reservation.o	\
	security.o	\
	wusbhc.o

wusb-cbaf-y := cbaf.o

wusb-wa-y :=		\
	wa-hc.o		\
	wa-nep.o	\
	wa-rpipe.o	\
	wa-xfer.o
