#
# Build options:
#   PRIV_OBJ := 1 for object version
#

IO_MAP := 0
HOSTAP := 1
PRIV_OBJ := 0



#KSP : = 0
KSP :=  /lib/modules/$(shell uname -r)/build \
#	/usr/src/linux-$(shell uname -r) \
#	/usr/src/linux-$(shell uname -r | sed 's/-.*//') \
#	/usr/src/kernel-headers-$(shell uname -r) \
#	/usr/src/kernel-source-$(shell uname -r) \
#	/usr/src/linux-$(shell uname -r | sed 's/\([0-9]*\.[0-9]*\)\..*/\1/') \
#	/usr/src/linux   /home/plice

#test_dir = $(shell [ -e $(dir)/include/linux ] && echo $(dir))

#KSP := $(foreach dir, $(KSP), $(test_dir))


KSRC := $(firstword $(KSP))

#ifeq (,$(KSRC))
#	$(    error Linux kernel source not found)
#endif

# check kernel version
KVER := $(shell uname -r | cut -c1-3 | sed 's/2\.[56]/2\.6/')
KERVER2=$(shell uname -r | cut -d. -f2)

ifeq ($(KVER), 2.6)
# 2.6 kernel
TARGET = viawget.ko

else
TARGET = viawget.o

endif

INSTDIR	:= $(shell find /lib/modules/$(shell uname -r) -name $(TARGET) -printf "%h\n" | sort | head -1)
ifeq (,$(INSTDIR))
	ifeq (,$(KERVER2))
		ifneq (,$(wildcard /lib/modules/$(shell uname -r)/kernel))
			INSTDIR := /lib/modules/$(shell uname -r)/kernel/drivers/net
		else
			INSTDIR := /lib/modules/$(shell uname -r)/net
		endif
	else
		ifneq ($(KERVER2),2)
			INSTDIR := /lib/modules/$(shell uname -r)/kernel/drivers/net
		else
			INSTDIR := /lib/modules/$(shell uname -r)/net
		endif
	endif
endif


SRC = device_main.c card.c mac.c baseband.c wctl.c 80211mgr.c \
      wcmd.c wmgr.c bssdb.c  wpa2.c rxtx.c dpc.c power.c datarate.c \
      srom.c mib.c rc4.c tether.c tcrc.c ioctl.c hostap.c wpa.c key.c \
      tkip.c michael.c wroute.c rf.c iwctl.c wpactl.c aes_ccmp.c \
      vntwifi.c IEEE11h.c

ifeq ($(IO_MAP), 1)
  EXTRA_CFLAGS += -DIO_MAP
endif

ifeq ($(HOSTAP), 1)
  EXTRA_CFLAGS += -DHOSTAP
endif

ifeq ($(PRIV_OBJ), 1)
  EXTRA_CFLAGS += -DPRIVATE_OBJ
endif

EXTRA_CFLAGS += -I$(PWD) -I$(PWD)/../include -I$(PWD)/../solomon

EXTRA_CFLAGS +=  -I$(PWD)/include -I$(PWD)/solomon

# build rule
ifeq ($(KVER), 2.6)
# 2.6 kernel

ifndef KERNEL_CONF
KERNEL_CONF=	$(KSRC)/.config
endif

include ${KERNEL_CONF}

obj-m += viawget.o

viawget-objs :=	device_main.o card.o mac.o baseband.o wctl.o 80211mgr.o \
	wcmd.o wmgr.o bssdb.o rxtx.o dpc.o power.o datarate.o srom.o \
	mib.o rc4.o tether.o tcrc.o ioctl.o hostap.o wpa.o key.o tkip.o \
	michael.o wroute.o rf.o iwctl.o wpactl.o wpa2.o aes_ccmp.o \
	vntwifi.o IEEE11h.o

.c.o:
	$(CC) $(CFLAGS) -o $@ $<

default:
	make -C $(KSRC) SUBDIRS=$(shell pwd) modules

else

# 2.2/2.4 kernel
OBJS :=	device_main.o card.o mac.o baseband.o wctl.o 80211mgr.o \
	wcmd.o wmgr.o bssdb.o rxtx.o dpc.o power.o datarate.o srom.o \
	mib.o rc4.o tether.o tcrc.o ioctl.o hostap.o wpa.o key.o tkip.o \
	michael.o wroute.o rf.o iwctl.o wpactl.o wpa2.o aes_ccmp.o \
	vntwifi.o IEEE11h.o

VERSION_FILE := $(KSRC)/include/linux/version.h
CONFIG_FILE  := $(KSRC)/include/linux/config.h


ifeq (,$(wildcard $(VERSION_FILE)))
  $(error Linux kernel source not configured - missing version.h)
endif

ifeq (,$(wildcard $(CONFIG_FILE)))
  $(error Linux kernel source not configured - missing config.h)
endif

ifneq (,$(findstring egcs-2.91.66, $(shell cat /proc/version)))
  CC := kgcc gcc cc
else
  CC := gcc cc
endif

test_cc = $(shell which $(cc) > /dev/null 2>&1 && echo $(cc))
CC := $(foreach cc, $(CC), $(test_cc))
CC := $(firstword $(CC))

EXTRA_CFLAGS += -Wall -DLINUX -D__KERNEL__ -DMODULE  -DEXPORT_SYMTAB -D__NO_VERSION__ -O2 -pipe
EXTRA_CFLAGS += -I$(KSRC)/include -Wstrict-prototypes -fomit-frame-pointer -fno-strict-aliasing
EXTRA_CFLAGS += $(shell [ -f $(KSRC)/include/linux/modversions.h ] && \
            echo "-DMODVERSIONS -include $(KSRC)/include/linux/modversions.h")

.SILENT: $(TARGET) clean


# look for SMP in config.h
SMP := $(shell $(CC) $(CFLAGS) -E -dM $(CONFIG_FILE) | \
         grep CONFIG_SMP | awk '{ print $$3 }')

ifneq ($(SMP),1)
  SMP := 0
endif


ifeq ($(SMP), 1)
  EXTRA_CFLAGS += -D__SMP__
endif


ifeq ($(PRIV_OBJ), 1)
  EXTRA_CFLAGS += -DPRIVATE_OBJ
  TARGET = x86g_up.o

ifeq ($(SMP), 1)
  TARGET = x86g_smp.o
endif

endif


# check x86_64
SUBARCH := $(shell uname -m)
ifeq ($(SUBARCH),x86_64)
    EXTRA_CFLAGS += -mcmodel=kernel -mno-red-zone
endif


$(TARGET): $(filter-out $(TARGET), $(SRC:.c=.o))
	$(LD) -r $^ -o $@
	echo; echo
	echo "**************************************************"
	echo "Build options:"
	echo "   VERSION    $(KVER)"
	echo -n "   SMP             "
	if [ "$(SMP)" = "1" ]; \
		then echo "Enabled"; else echo "Disabled"; fi



endif # ifeq ($(KVER),2.6)


ifeq ($(KVER), 2.6)
install: default
else
install: clean $(TARGET)
endif
	mkdir -p $(MOD_ROOT)$(INSTDIR)
	install -m 644 -o root $(TARGET) $(MOD_ROOT)$(INSTDIR)

ifeq (,$(MOD_ROOT))
	/sbin/depmod -a || true
else
	/sbin/depmod -b $(MOD_ROOT) -a || true
endif


uninstall:
	rm -f $(INSTDIR)/$(TARGET)
	/sbin/depmod -a

clean:
	rm -f $(TARGET) $(SRC:.c=.o) *.o *~
	rm -f .*.o.d .*.o.cmd .*.ko.cmd *.mod.c *.mod.o

-include .depend.mak
