NIC_SELECT = RTL8192U

KVER  := $(shell uname -r)
MODDESTDIR := /lib/modules/$(KVER)/kernel/drivers/net/wireless/$(NIC_SELECT)

CC = gcc
ifneq ($(shell uname -r|cut -d. -f1,2), 2.4)
ccflags-y := -I$(TOPDIR)/drivers/net/wireless
ccflags-y += -O2
ccflags-y += -DJACKSON_NEW_8187 -DJACKSON_NEW_RX

#it will fail to compile in suse linux enterprise 10 sp2. This flag is to solve this problem.
ifeq ($(shell uname -r | cut -d. -f1,2,3,4), 2.6.16.60-0)
ccflags-y := -DOPENSUSE_SLED
endif

ifeq ($(NIC_SELECT),RTL8192U)
#ccflags-y := -DUSB_TX_DRIVER_AGGREGATION_ENABLE
#ccflags-y := -DUSB_RX_AGGREGATION_SUPPORT
endif
#ccflags-y := -DJOHN_NOCPY
#flags to enable or disble 80211D feature
ccflags-y += -DENABLE_DOT11D
ieee80211-rsl-objs := ieee80211_rx.o \
		      ieee80211_softmac.o \
		      ieee80211_tx.o \
		      ieee80211_wx.o \
		      ieee80211_module.o \
		      ieee80211_softmac_wx.o\
		      rtl819x_HTProc.o\
		      rtl819x_TSProc.o\
		      rtl819x_BAProc.o\
		      dot11d.o

ieee80211_crypt-rsl-objs := ieee80211_crypt.o
ieee80211_crypt_tkip-rsl-objs := ieee80211_crypt_tkip.o
ieee80211_crypt_ccmp-rsl-objs := ieee80211_crypt_ccmp.o
ieee80211_crypt_wep-rsl-objs := ieee80211_crypt_wep.o

obj-m +=ieee80211-rsl.o
obj-m +=ieee80211_crypt-rsl.o
obj-m +=ieee80211_crypt_wep-rsl.o
obj-m +=ieee80211_crypt_tkip-rsl.o
obj-m +=ieee80211_crypt_ccmp-rsl.o

KSRC := /lib/modules/$(KVER)/build
INSTALL_PREFIX :=

all: modules

modules:
	$(MAKE) -C $(KSRC) M=$(PWD) CC=$(CC) modules

install: modules
	rm -fr $(MODDESTDIR)
	mkdir -p $(MODDESTDIR)
	@install -p -m 644 ieee80211_crypt-rsl.ko $(MODDESTDIR)
	@install -p -m 644 ieee80211_crypt_wep-rsl.ko $(MODDESTDIR)
	@install -p -m 644 ieee80211_crypt_tkip-rsl.ko $(MODDESTDIR)
	@install -p -m 644 ieee80211_crypt_ccmp-rsl.ko $(MODDESTDIR)
	@install -p -m 644 ieee80211-rsl.ko $(MODDESTDIR)
	depmod -a
uninstall:
	rm -fr $(MODDESTDIR)
	depmod -a

else
LD := ld
KSRC := /lib/modules/$(KVER)/build
CONFIG_FILE := $(KSRC)/include/linux/autoconf.h

CFLAGS += -DLINUX -D__KERNEL__ -DMODULE -O2 -pipe -Wall
CFLAGS += -I$(KSRC)/include -I.
#Kernel 2.4.31
CFLAGS += -DMODVERSIONS -DEXPORT_SYMTAB -include $(KSRC)/include/linux/modversions.h
#Kernel 2.4.20
#CFLAGS += -D__NO_VERSION__ -DEXPORT_SYMTAB
#CFLAGS += -DENABLE_DOT11D
SMP := $(shell $(CC) $(MODCFLAGS) -E -dM $(CONFIG_FILE) | \
   grep CONFIG_SMP | awk '{print $$3}')
ifneq ($(SMP),1)
   SMP := 0
endif
ifeq ($(SMP),1)
   CFLAGS    += -D__SMP__
endif

#CFLAGS += -DJOHN_NOCPY

OBJS := ${patsubst %.c, %.o, ${wildcard *.c}}
all:${OBJS} ieee80211_crypt-rsl.o michael_mic-rsl.o aes-rsl.o ieee80211_crypt_wep-rsl.o ieee80211_crypt_tkip-rsl.o ieee80211_crypt_ccmp-rsl.o crypto-rsl.o ieee80211-rsl.o

ieee80211_crypt-rsl.o: ieee80211_crypt.o
	mv $^ $@

michael_mic-rsl.o: michael_mic.o
	mv $^ $@

aes-rsl.o: aes.o
	mv $^ $@

ieee80211_crypt_wep-rsl.o: ieee80211_crypt_wep.o
	mv $^ $@

ieee80211_crypt_tkip-rsl.o: ieee80211_crypt_tkip.o
	mv $^ $@

ieee80211_crypt_ccmp-rsl.o: ieee80211_crypt_ccmp.o
	mv $^ $@

crypto-rsl.o: arc4.o api.o autoload.o cipher.o compress.o digest.o scatterwalk.o proc.o
	$(LD) -r $^ -o $@

ieee80211-rsl.o: ieee80211_rx.o ieee80211_tx.o ieee80211_wx.o ieee80211_module.o ieee80211_softmac_wx.o ieee80211_softmac.o rtl819x_HTProc.o rtl819x_TSProc.o rtl819x_BAProc.o dot11d.o
	$(LD) -r $^ -o $@
install:
	rm -fr $(MODDESTDIR)
	mkdir -p $(MODDESTDIR)
	@install -p -m 644 ieee80211_crypt-rsl.o $(MODDESTDIR)
	@install -p -m 644 crypto-rsl.o $(MODDESTDIR)
	@install -p -m 644 michael_mic-rsl.o $(MODDESTDIR)
	@install -p -m 644 aes-rsl.o $(MODDESTDIR)
	@install -p -m 644 ieee80211_crypt_wep-rsl.o $(MODDESTDIR)
	@install -p -m 644 ieee80211_crypt_tkip-rsl.o $(MODDESTDIR)
	@install -p -m 644 ieee80211_crypt_ccmp-rsl.o $(MODDESTDIR)
	@install -p -m 644 ieee80211-rsl.o $(MODDESTDIR)
	/sbin/depmod -a ${shell uname -r}

uninstall:
	rm -fr $(MODDESTDIR)
	/sbin/depmod -a ${shell uname -r}

endif

.PHONY: clean
clean:
	rm -fr *.mod.c *.mod *.o .*.cmd *.mod.* *.ko *.o *~
	rm -rf .tmp_versions
	rm -rf Module.symvers
