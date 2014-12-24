greybus-y :=	core.o		\
		debugfs.o	\
		ap.o		\
		manifest.o	\
		module.o	\
		interface.o	\
		bundle.o	\
		connection.o	\
		protocol.o	\
		operation.o

gpbridge-y :=	gpb.o		\
		sdio-gb.o	\
		uart-gb.o	\
		pwm-gb.o	\
		gpio-gb.o	\
		usb-gb.o

obj-m += greybus.o
obj-m += gpbridge.o
obj-m += i2c-gb.o
obj-m += vibrator-gb.o
obj-m += battery-gb.o
obj-m += es1-ap-usb.o

KERNELVER		?= $(shell uname -r)
KERNELDIR 		?= /lib/modules/$(KERNELVER)/build
PWD			:= $(shell pwd)

# add -Wall to try to catch everything we can.
ccFlags-y := -Wall

all: module

module:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)

check:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) C=2 CF="-D__CHECK_ENDIAN__"

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers

coccicheck:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) coccicheck

