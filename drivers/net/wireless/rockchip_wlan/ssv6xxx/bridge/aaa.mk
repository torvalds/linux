#aaa.mk - what is this used for?

obj-m     += ssvsdiobridge.o

ssvsdiobridge-y +=	sdiobridge.o
ssvsdiobridge-y +=	debug.o



#Define CONFIG_CABRIO_DEBUG to show debug messages
ccflags-y += -DCONFIG_CABRIO_DEBUG

ifndef ($(KBUILD_EXTMOD),)
KDIR=/lib/modules/`uname -r`/build

_all:
	$(MAKE) -C $(KDIR) M=$(PWD) KBUILD_EXTRA_SYMBOLS=$(PWD)/../ssvdevice/Module.symvers modules 2>&1 | tee make.log
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f make.log
	
install:
	@-rmmod ssvsdiobridge
	$(MAKE) INSTALL_MOD_DIR=kernel/drivers/net/wireless/ssv6200 -C $(KDIR) M=$(PWD) modules_install
	modprobe ssvsdiobridge

endif
