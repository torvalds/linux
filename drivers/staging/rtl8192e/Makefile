rtllib-objs :=			\
	dot11d.o		\
	rtllib_module.o		\
	rtllib_rx.o		\
	rtllib_tx.o		\
	rtllib_wx.o		\
	rtllib_softmac.o	\
	rtllib_softmac_wx.o	\
	rtl819x_BAProc.o	\
	rtl819x_HTProc.o	\
	rtl819x_TSProc.o

obj-$(CONFIG_RTLLIB) += rtllib.o

obj-$(CONFIG_RTLLIB_CRYPTO) += rtllib_crypt.o
obj-$(CONFIG_RTLLIB_CRYPTO_CCMP) += rtllib_crypt_ccmp.o
obj-$(CONFIG_RTLLIB_CRYPTO_TKIP) += rtllib_crypt_tkip.o
obj-$(CONFIG_RTLLIB_CRYPTO_WEP) += rtllib_crypt_wep.o

r8192e_pci-objs :=		\
	r8192E_dev.o		\
	r8192E_phy.o		\
	r8192E_firmware.o	\
	r8192E_cmdpkt.o		\
	r8192E_hwimg.o		\
	r8190P_rtl8256.o	\
	rtl_cam.o		\
	rtl_core.o		\
	rtl_debug.o		\
	rtl_dm.o		\
	rtl_eeprom.o		\
	rtl_ethtool.o		\
	rtl_pci.o		\
	rtl_pm.o		\
	rtl_ps.o		\
	rtl_wx.o		\

obj-$(CONFIG_RTL8192E) += r8192e_pci.o

ccflags-y += -D__CHECK_ENDIAN__
