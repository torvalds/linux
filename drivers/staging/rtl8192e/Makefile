ccflags-y += -DUSE_FW_SOURCE_IMG_FILE
ccflags-y += -DCONFIG_PM_RTL
ccflags-y += -DCONFIG_PM
ccflags-y += -DHAVE_NET_DEVICE_OPS
ccflags-y += -DENABLE_DOT11D

r8192e_pci-objs :=		\
	rtl_core.o		\
	rtl_eeprom.o		\
	rtl_ps.o		\
	rtl_wx.o		\
	rtl_cam.o		\
	rtl_dm.o		\
	rtl_pm.o		\
	rtl_pci.o		\
	rtl_debug.o		\
	rtl_ethtool.o		\
	r8192E_dev.o		\
	r8192E_phy.o		\
	r8192E_firmware.o	\
	r8192E_cmdpkt.o		\
	r8192E_hwimg.o		\
	r8190P_rtl8256.o	\
	rtllib_rx.o		\
	rtllib_softmac.o	\
	rtllib_tx.o		\
	rtllib_wx.o		\
	rtllib_module.o		\
	rtllib_softmac_wx.o	\
	rtl819x_HTProc.o	\
	rtl819x_TSProc.o	\
	rtl819x_BAProc.o	\
	dot11d.o		\
	rtllib_crypt.o		\
	rtllib_crypt_tkip.o	\
	rtllib_crypt_ccmp.o	\
	rtllib_crypt_wep.o

obj-$(CONFIG_RTL8192E) += r8192e_pci.o

ccflags-y += -D__CHECK_ENDIAN__
