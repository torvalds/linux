NIC_SELECT = RTL8192U

ccflags-y := -std=gnu89
ccflags-y += -O2

ccflags-y += -DCONFIG_FORCE_HARD_FLOAT=y
ccflags-y += -DJACKSON_NEW_8187 -DJACKSON_NEW_RX
ccflags-y += -DTHOMAS_BEACON -DTHOMAS_TASKLET -DTHOMAS_SKB -DTHOMAS_TURBO
#ccflags-y += -DUSB_TX_DRIVER_AGGREGATION_ENABLE
#ccflags-y += -DUSB_RX_AGGREGATION_SUPPORT
ccflags-y += -DUSE_ONE_PIPE
ccflags-y += -DENABLE_DOT11D
ccflags-y += -Idrivers/staging/rtl8192u/ieee80211

r8192u_usb-objs := r8192U_core.o r8180_93cx6.o r8192U_wx.o		\
		  r8190_rtl8256.o r819xU_phy.o r819xU_firmware.o	\
		  r819xU_cmdpkt.o r8192U_dm.o r819xU_firmware_img.o	\
		  ieee80211/ieee80211_crypt.o				\
		  ieee80211/ieee80211_crypt_tkip.o			\
		  ieee80211/ieee80211_crypt_ccmp.o			\
		  ieee80211/ieee80211_crypt_wep.o			\
		  ieee80211/ieee80211_rx.o				\
		  ieee80211/ieee80211_softmac.o				\
		  ieee80211/ieee80211_tx.o				\
		  ieee80211/ieee80211_wx.o				\
		  ieee80211/ieee80211_module.o				\
		  ieee80211/ieee80211_softmac_wx.o			\
		  ieee80211/rtl819x_HTProc.o				\
		  ieee80211/rtl819x_TSProc.o				\
		  ieee80211/rtl819x_BAProc.o				\
		  ieee80211/dot11d.o

obj-$(CONFIG_RTL8192U) += r8192u_usb.o
