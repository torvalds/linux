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

obj-$(CONFIG_RTLLIB_CRYPTO_CCMP) += rtllib_crypt_ccmp.o
obj-$(CONFIG_RTLLIB_CRYPTO_TKIP) += rtllib_crypt_tkip.o
obj-$(CONFIG_RTLLIB_CRYPTO_WEP) += rtllib_crypt_wep.o

obj-$(CONFIG_RTL8192E) += rtl8192e/
