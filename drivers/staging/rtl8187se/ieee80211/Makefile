
#EXTRA_CFLAGS += -I$(TOPDIR)/drivers/net/wireless
#EXTRA_CFLAGS += -O2

EXTRA_CFLAGS += -DTHOMAS_TURBO
EXTRA_CFLAGS += -DENABLE_IPS

ifeq ($(shell uname -r|cut -d. -f1,2,3,4), 2.6.16.60-0)
EXTRA_CFLAGS += -DOPENSUSE_SLED
endif

ifeq ($(shell uname -r|cut -d. -f1,2,3,4), 2.6.26.3-29)
EXTRA_CFLAGS += -DFEDORACORE_9
endif


#+YJ,080626
EXTRA_CFLAGS += -DENABLE_DOT11D

#enable it for legacy power save, disable it for leisure  power save
#CFLAGS += -DENABLE_LPS

ieee80211-rtl-objs := dot11d.o ieee80211_softmac.o ieee80211_rx.o ieee80211_tx.o ieee80211_wx.o ieee80211_module.o ieee80211_softmac_wx.o

ieee80211_crypt-rtl-objs := ieee80211_crypt.o
ieee80211_crypt_tkip-rtl-objs := ieee80211_crypt_tkip.o
ieee80211_crypt_ccmp-rtl-objs := ieee80211_crypt_ccmp.o
ieee80211_crypt_wep-rtl-objs := ieee80211_crypt_wep.o

obj-m +=ieee80211-rtl.o
obj-m +=ieee80211_crypt-rtl.o
obj-m +=ieee80211_crypt_wep-rtl.o
obj-m +=ieee80211_crypt_tkip-rtl.o
obj-m +=ieee80211_crypt_ccmp-rtl.o

