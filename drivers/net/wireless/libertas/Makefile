libertas-objs := main.o fw.o wext.o \
		rx.o tx.o cmd.o 	  \
		cmdresp.o scan.o	  \
		join.o 11d.o 		  \
		ioctl.o debugfs.o	  \
		ethtool.o assoc.o

usb8xxx-objs += if_bootcmd.o
usb8xxx-objs += if_usb.o

obj-$(CONFIG_LIBERTAS)     += libertas.o
obj-$(CONFIG_LIBERTAS_USB) += usb8xxx.o
