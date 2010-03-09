NIC_SELECT = RTL8192E

EXTRA_CFLAGS += -DRTL8192E
EXTRA_CFLAGS += -std=gnu89
EXTRA_CFLAGS += -O2
EXTRA_CFLAGS += -DTHOMAS_TURBO
EXTRA_CFLAGS += -DENABLE_DOT11D

EXTRA_CFLAGS += -DENABLE_IPS
EXTRA_CFLAGS += -DENABLE_LPS

r8192e_pci-objs :=		\
	r8192E_core.o		\
	r8180_93cx6.o		\
	r8192E_wx.o		\
	r8190_rtl8256.o		\
	r819xE_phy.o		\
	r819xE_firmware.o	\
	r819xE_cmdpkt.o		\
	r8192E_dm.o		\
	ieee80211/ieee80211_rx.o		\
	ieee80211/ieee80211_softmac.o		\
	ieee80211/ieee80211_tx.o		\
	ieee80211/ieee80211_wx.o		\
	ieee80211/ieee80211_module.o		\
	ieee80211/ieee80211_softmac_wx.o	\
	ieee80211/rtl819x_HTProc.o		\
	ieee80211/rtl819x_TSProc.o		\
	ieee80211/rtl819x_BAProc.o		\
	ieee80211/dot11d.o			\
	ieee80211/ieee80211_crypt.o		\
	ieee80211/ieee80211_crypt_tkip.o	\
	ieee80211/ieee80211_crypt_ccmp.o	\
	ieee80211/ieee80211_crypt_wep.o

obj-$(CONFIG_RTL8192E) += r8192e_pci.o

