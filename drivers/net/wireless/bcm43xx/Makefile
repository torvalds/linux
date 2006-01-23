# Makefile for bcm43xx driver

VERSION		:= 0.0.1
RELEASE_NAME	:= bcm43xx-$(VERSION)

# Optional path, where the SoftMAC subsystem is located.
# You may set SOFTMAC_DIR in your bashrc, for example.
SOFTMAC_DIR	?= 

KVER		:= $(shell uname -r)
KDIR		?= /lib/modules/$(KVER)/build
PWD		:= $(shell pwd)
MODPATH		:= $(DESTDIR)/lib/modules/$(KVER)/kernel/drivers/net/bcm43xx

# Comment/uncomment to enable/disable debugging
DEBUG = y


ifeq ($(DEBUG),y)
DEBUGFS_OBJ = bcm43xx_debugfs.o
CFLAGS += -O2 -DCONFIG_BCM43XX_DEBUG
else
DEBUGFS_OBJ =
CFLAGS += -O2
endif

CFLAGS += -DBCM43xx_VERSION=$(VERSION) -I/lib/modules/$(KVER)/include
ifneq ($(SOFTMAC_DIR),)
CPPFLAGS := -I$(SOFTMAC_DIR) $(CPPFLAGS)
endif

ifneq ($(KERNELRELEASE),)
# call from kernel build system

obj-m := bcm43xx.o
bcm43xx-objs := bcm43xx_main.o bcm43xx_dma.o $(DEBUGFS_OBJ) \
		bcm43xx_radio.o bcm43xx_phy.o \
		bcm43xx_power.o bcm43xx_wx.o \
		bcm43xx_pio.o bcm43xx_ilt.o \
		bcm43xx_leds.o

else

default: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install: bcm43xx.ko
	install -d $(MODPATH)
	install -m 644 -c bcm43xx.ko $(MODPATH)
	/sbin/depmod -a

uninstall:
	rm -rf $(MODPATH)
	/sbin/depmod -a

endif

clean:
	find . \( -name '*.ko' -o -name '*.o' -o -name '.tmp_versions' -o -name '*~' -o -name '.*.cmd' \
		-o -name '*.mod.c' -o -name '*.tar.bz2' -o -name '*.rej' -o -name '*.orig' \)\
		-print | xargs rm -Rf

depend .depend dep:
	$(CC) $(CFLAGS) -M *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif

DISTFILES =  $(shell find . \( -not -name '.' \) -print | grep -v "\.tar\.bz2" | grep -v "\/\." )
DISTDIR = $(RELEASE_NAME)

release: clean
	@rm -rf $(DISTDIR)
	@mkdir $(DISTDIR)
	@chmod 777 $(DISTDIR)
	@for file in $(DISTFILES); do                   \
                if test -d $$file; then                 \
                        mkdir $(DISTDIR)/$$file;        \
                else                                    \
                        cp -p $$file $(DISTDIR)/$$file; \
                fi;                                     \
        done
	@tar -c $(DISTDIR) | bzip2 -9 > $(RELEASE_NAME).tar.bz2
	@rm -rf $(DISTDIR)
