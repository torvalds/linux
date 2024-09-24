extron-da-hd-4k-plus-cec-objs := extron-da-hd-4k-plus.o cec-splitter.o
obj-$(CONFIG_USB_EXTRON_DA_HD_4K_PLUS_CEC) := extron-da-hd-4k-plus-cec.o

all:
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules

install:
	$(MAKE) -C $(KDIR) M=$(shell pwd) modules_install
