
obj-$(CONFIG_WIMAX_I2400M) += i2400m.o
obj-$(CONFIG_WIMAX_I2400M_USB) += i2400m-usb.o
obj-$(CONFIG_WIMAX_I2400M_SDIO) += i2400m-sdio.o

i2400m-y :=		\
	control.o	\
	driver.o	\
	fw.o		\
	op-rfkill.o	\
	netdev.o	\
	tx.o		\
	rx.o

i2400m-$(CONFIG_DEBUG_FS) += debugfs.o

i2400m-usb-y :=			\
	usb-fw.o		\
	usb-notif.o		\
	usb-tx.o		\
	usb-rx.o		\
	usb.o


i2400m-sdio-y := 		\
	sdio.o      		\
	sdio-tx.o   		\
	sdio-fw.o	 	\
	sdio-rx.o
