obj-$(CONFIG_USB_WUSB)		+= wusbcore.o wusb-cbaf.o
obj-$(CONFIG_USB_HWA_HCD)	+= wusb-wa.o

wusbcore-objs := 	\
	crypto.o	\
	devconnect.o	\
	dev-sysfs.o	\
	mmc.o		\
	pal.o		\
	rh.o		\
	reservation.o	\
	security.o	\
	wusbhc.o

wusb-cbaf-objs := cbaf.o

wusb-wa-objs :=	wa-hc.o		\
		wa-nep.o	\
		wa-rpipe.o	\
		wa-xfer.o
