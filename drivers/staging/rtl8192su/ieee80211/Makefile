NIC_SELECT = RTL8192SU

EXTRA_CFLAGS += -O2
EXTRA_CFLAGS += -DRTL8192S_DISABLE_FW_DM=0
EXTRA_CFLAGS += -DRTL8192SU
#EXTRA_CFLAGS += -DJOHN_NOCPY
EXTRA_CFLAGS += -DTHOMAS_TURBO
#flags to enable or disble 80211D feature
EXTRA_CFLAGS += -DENABLE_DOT11D
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

