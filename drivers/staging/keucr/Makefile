ccflags-y := -Idrivers/scsi

obj-$(CONFIG_USB_ENESTORAGE)	+= keucr.o

keucr-y :=			\
		usb.o		\
		scsiglue.o	\
		transport.o	\
		init.o		\
		smscsi.o	\
		smilmain.o	\
		smilsub.o	\
		smilecc.o
