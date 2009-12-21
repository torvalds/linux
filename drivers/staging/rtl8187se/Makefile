
#EXTRA_CFLAGS += -DCONFIG_IEEE80211_NOWEP=y
#EXTRA_CFLAGS += -std=gnu89
#EXTRA_CFLAGS += -O2
#CC            = gcc

EXTRA_CFLAGS += -DSW_ANTE
EXTRA_CFLAGS += -DTX_TRACK
EXTRA_CFLAGS += -DHIGH_POWER
EXTRA_CFLAGS += -DSW_DIG
EXTRA_CFLAGS += -DRATE_ADAPT

#enable it for legacy power save, disable it for leisure  power save
EXTRA_CFLAGS += -DENABLE_LPS


#EXTRA_CFLAGS += -mhard-float -DCONFIG_FORCE_HARD_FLOAT=y

r8187se-objs :=			\
		r8180_core.o		\
		r8180_93cx6.o		\
		r8180_wx.o		\
		r8180_rtl8225z2.o	\
		r8185b_init.o		\
		r8180_dm.o		\
		ieee80211/dot11d.o			\
		ieee80211/ieee80211_softmac.o		\
		ieee80211/ieee80211_rx.o		\
		ieee80211/ieee80211_tx.o		\
		ieee80211/ieee80211_wx.o		\
		ieee80211/ieee80211_module.o		\
		ieee80211/ieee80211_softmac_wx.o	\
		ieee80211/ieee80211_crypt.o		\
		ieee80211/ieee80211_crypt_tkip.o	\
		ieee80211/ieee80211_crypt_ccmp.o	\
		ieee80211/ieee80211_crypt_wep.o

obj-$(CONFIG_R8187SE)	+= r8187se.o

