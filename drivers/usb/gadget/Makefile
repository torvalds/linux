#
# USB peripheral controller drivers
#
subdir-ccflags-$(CONFIG_USB_GADGET_DEBUG)	:= -DDEBUG
subdir-ccflags-$(CONFIG_USB_GADGET_VERBOSE)	+= -DVERBOSE_DEBUG
ccflags-y				+= -I$(srctree)/drivers/usb/gadget/udc

obj-$(CONFIG_USB_LIBCOMPOSITE)	+= libcomposite.o
libcomposite-y			:= usbstring.o config.o epautoconf.o
libcomposite-y			+= composite.o functions.o configfs.o u_f.o

obj-$(CONFIG_USB_GADGET)	+= udc/ function/ legacy/

usb_f_mtp-y			:= f_mtp.o
obj-$(CONFIG_USB_F_MTP)		+= usb_f_mtp.o
usb_f_ptp-y			:= f_ptp.o
obj-$(CONFIG_USB_F_PTP)		+= usb_f_ptp.o
usb_f_audio_source-y		:= f_audio_source.o
obj-$(CONFIG_USB_F_AUDIO_SRC)	+= usb_f_audio_source.o
