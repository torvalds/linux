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

gb-phy-y :=	gpb.o		\
		sdio.o	\
		uart.o	\
		pwm.o	\
		gpio.o	\
		i2c.o	\
		usb.o

obj-m += greybus.o
obj-m += gb-phy.o
obj-m += gb-vibrator.o
obj-m += gb-battery.o
obj-m += gb-es1.o
obj-m += gb-es2.o

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

install: module
	mkdir -p /lib/modules/$(KERNELVER)/kernel/drivers/greybus/
	cp -f *.ko /lib/modules/$(KERNELVER)/kernel/drivers/greybus/
	depmod -a $(KERNELVER)
