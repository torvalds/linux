NIC_SELECT = RTL8192SU

EXTRA_CFLAGS += -std=gnu89
EXTRA_CFLAGS += -O2

EXTRA_CFLAGS += -DJACKSON_NEW_RX
EXTRA_CFLAGS += -DTHOMAS_BEACON

#EXTRA_CFLAGS += -DMUTIPLE_BULK_OUT

r8192s_usb-objs :=		\
	r8180_93cx6.o		\
	r8192U_wx.o		\
	r8192S_phy.o		\
	r8192S_rtl6052.o	\
	r8192S_rtl8225.o	\
	r819xU_cmdpkt.o		\
	r8192U_dm.o		\
	r8192SU_HWImg.o		\
	r8192S_firmware.o	\
	r8192S_Efuse.o		\
	r8192U_core.o		\
	r8192U_pm.o		\
	ieee80211/ieee80211_crypt.o		\
	ieee80211/ieee80211_crypt_tkip.o	\
	ieee80211/ieee80211_crypt_ccmp.o	\
	ieee80211/ieee80211_crypt_wep.o		\
	ieee80211/ieee80211_rx.o	\
	ieee80211/ieee80211_softmac.o	\
	ieee80211/ieee80211_tx.o	\
	ieee80211/ieee80211_wx.o	\
	ieee80211/ieee80211_module.o	\
	ieee80211/ieee80211_softmac_wx.o\
	ieee80211/rtl819x_HTProc.o	\
	ieee80211/rtl819x_TSProc.o	\
	ieee80211/rtl819x_BAProc.o	\
	ieee80211/dot11d.o

obj-$(CONFIG_RTL8192SU) += r8192s_usb.o
