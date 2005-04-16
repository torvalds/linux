ifneq ($(KERNELRELEASE),)

pwc-objs	:= pwc-if.o pwc-misc.o pwc-ctrl.o pwc-uncompress.o pwc-dec1.o pwc-dec23.o pwc-kiara.o pwc-timon.o

obj-$(CONFIG_USB_PWC) += pwc.o

else

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

endif

clean:
	rm -f *.[oas] .*.flags *.ko .*.cmd .*.d .*.tmp *.mod.c 
	rm -rf .tmp_versions

