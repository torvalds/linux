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

ccflags-y := -I$(KERNELDIR)/include
ccflags-y += -I$(src) \
		-DBUS_PCMCIA \
		-DUSE_WEXT \
		-DSTA_ONLY \
		-DWVLAN_49 \
		-DHERMES25 \
#		-DDBG \
#		-DDBG_LVL=5 \
#		-DUSE_UIL \
#		-DUSE_PROFILE \

ifeq ($(findstring HERMES25,$(ccflags-y)),)
WLNAME := wlags49_h2_cs
$(WLNAME)-y := sta_h2.o
ifeq ($(findstring STA_ONLY,$(ccflags-y)),)
$(WLNAME)-y += ap_h2.o
endif
else
WLNAME=wlags49_h25_cs
$(WLNAME)-y := sta_h25.o
ifeq ($(findstring STA_ONLY,$(ccflags-y)),)
$(WLNAME)-y += ap_h25.o
endif
endif

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

