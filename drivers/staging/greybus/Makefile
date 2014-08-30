greybus-y := core.o gbuf.o i2c-gb.o

obj-m += greybus.o
obj-m += sdio-gb.o
obj-m += gpio-gb.o
obj-m += uart-gb.o

KERNELVER		?= $(shell uname -r)
KERNELDIR 		?= /lib/modules/$(KERNELVER)/build
PWD			:= $(shell pwd)

all: module

module:
	$(MAKE) -C $(KERNELDIR) M=$(PWD)

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers

coccicheck:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) coccicheck

