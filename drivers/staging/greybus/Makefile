greybus-y :=	core.o		\
		gbuf.o		\
		sysfs.o		\
		debugfs.o	\
		ap.o		\
		module.o	\
		interface.o	\
		function.o	\
		connection.o	\
		operation.o	\
		i2c-gb.o	\
		gpio-gb.o	\
		sdio-gb.o	\
		uart-gb.o	\
		battery-gb.o

obj-m += greybus.o
obj-m += es1-ap-usb.o
obj-m += test_sink.o

KERNELVER		?= $(shell uname -r)
KERNELDIR 		?= /lib/modules/$(KERNELVER)/build
PWD			:= $(shell pwd)

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

