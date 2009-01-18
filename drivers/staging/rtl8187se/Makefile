
#EXTRA_CFLAGS += -DCONFIG_IEEE80211_NOWEP=y
#EXTRA_CFLAGS += -DCONFIG_RTL8180_IOMAP
#EXTRA_CFLAGS += -std=gnu89
#EXTRA_CFLAGS += -O2
#CC            = gcc
EXTRA_CFLAGS += -DTHOMAS_TURBO
#CFLAGS += -DCONFIG_RTL8185B
#CFLAGS += -DCONFIG_RTL818x_S

#added for EeePC testing
EXTRA_CFLAGS += -DENABLE_IPS
EXTRA_CFLAGS += -DSW_ANTE
EXTRA_CFLAGS += -DTX_TRACK
EXTRA_CFLAGS += -DHIGH_POWER
EXTRA_CFLAGS += -DSW_DIG
EXTRA_CFLAGS += -DRATE_ADAPT
EXTRA_CFLAGS += -DCONFIG_RTL8180_PM

#+YJ,080626
EXTRA_CFLAGS += -DENABLE_DOT11D

#enable it for legacy power save, disable it for leisure  power save
EXTRA_CFLAGS += -DENABLE_LPS


#EXTRA_CFLAGS += -mhard-float -DCONFIG_FORCE_HARD_FLOAT=y

rtl8187se-objs :=			\
		r8180_core.o		\
		r8180_sa2400.o		\
		r8180_93cx6.o		\
		r8180_wx.o		\
		r8180_max2820.o		\
		r8180_gct.o		\
		r8180_rtl8225.o		\
		r8180_rtl8255.o		\
		r8180_rtl8225z2.o	\
		r8185b_init.o		\
		r8180_dm.o		\
		r8180_pm.o		\
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

obj-$(CONFIG_RTL8187SE)	+= rtl8187se.o

