obj-m := cedusb.o
cedusb-objs := usb1401.o ced_ioc.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
KBUILD_EXTRA_SYMBOLS := $(PWD)
EXTRA_CFLAGS = -I$(HOME)/src/ced1401 
all:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean

