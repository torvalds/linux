#
# Makefile for wlags49_h2_cs.ko and wlags49_h25_cs.ko
#
# Default build for Hermes-II base cards (possibly identified with
# "manfid: 0x0156, 0x0003" in "pccardctl ident" output), comment
# -DHERMES25 below
#
# If you want to build for Hermes-II.5 base cards (possibly identified with
# "manfid: 0x0156, 0x0004" in "pccardctl ident" output), uncomment
# -DHERMES25 below
#
#  If you want to build AP support (untested), comment out -DSTA_ONLY

INSTALLDIR := /lib/modules/$(shell uname -r)/kernel/drivers/net/wireless
EXTRA_CFLAGS += -I$(KERNELDIR)/include
EXTRA_CFLAGS += -I$(src) \
		-DBUS_PCMCIA \
		-DUSE_WPA \
		-DUSE_WEXT \
		-DSTA_ONLY \
		-DWVLAN_49 \
#		-DHERMES25 \
#		-DDBG \
#		-DDBG_LVL=5 \
#		-DUSE_UIL \
#		-DUSE_PROFILE \

ifeq ($(findstring HERMES25,$(EXTRA_CFLAGS)),)
WLNAME := wlags49_h2_cs
$(WLNAME)-y := sta_h2.o
ifeq ($(findstring STA_ONLY,$(EXTRA_CFLAGS)),)
$(WLNAME)-y += ap_h2.o
endif
else
WLNAME=wlags49_h25_cs
$(WLNAME)-y := sta_h25.o
ifeq ($(findstring STA_ONLY,$(EXTRA_CFLAGS)),)
$(WLNAME)-y += ap_h25.o
endif
endif

# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)

obj-m		+= $(WLNAME).o

$(WLNAME)-y +=	wl_profile.o \
		wl_wext.o \
		wl_priv.o \
		wl_main.o \
		wl_enc.o \
		wl_util.o \
		wl_netdev.o \
		wl_cs.o \
		mmd.o \
		hcf.o \
		dhf.o

$(WLNAME)-$(CONFIG_SYSFS)	+= wl_sysfs.o

# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

clean:
	rm -fr *.o *.ko *.mod.c *.mod.o .*.*.cmd Module.symvers \
		Module.markers modules.order .tmp_versions

install: default
	-rmmod $(WLNAME)
	install -d $(INSTALLDIR)
	install -m 0644 -o root -g root $(WLNAME).ko $(INSTALLDIR)
	/sbin/depmod -aq

