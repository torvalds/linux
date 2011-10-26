
#ccflags-y += -DCONFIG_IEEE80211_NOWEP=y
#ccflags-y += -std=gnu89
#ccflags-y += -O2
#CC            = gcc

ccflags-y := -DSW_ANTE
ccflags-y += -DTX_TRACK
ccflags-y += -DHIGH_POWER
ccflags-y += -DSW_DIG
ccflags-y += -DRATE_ADAPT

#enable it for legacy power save, disable it for leisure  power save
ccflags-y += -DENABLE_LPS


#ccflags-y := -mhard-float -DCONFIG_FORCE_HARD_FLOAT=y

r8187se-y :=			\
		r8180_core.o		\
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

