# EXTRA_CFLAGS += -Wpacked

usb8xxx-objs := main.o fw.o wext.o \
		rx.o tx.o cmd.o 	  \
		cmdresp.o scan.o	  \
		join.o 11d.o 		  \
		ioctl.o debugfs.o	  \
		ethtool.o assoc.o

ifeq ($(CONFIG_LIBERTAS_USB_DEBUG), y)
EXTRA_CFLAGS += -DDEBUG -DPROC_DEBUG
endif


# This is needed to support the newer boot2 bootloader (v >= 3104)
EXTRA_CFLAGS += -DSUPPORT_BOOT_COMMAND
usb8xxx-objs += if_bootcmd.o
usb8xxx-objs += if_usb.o

obj-$(CONFIG_LIBERTAS_USB) += usb8xxx.o

