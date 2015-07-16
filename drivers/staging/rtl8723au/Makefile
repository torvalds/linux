r8723au-y :=				\
		core/rtw_cmd.o		\
		core/rtw_efuse.o	\
		core/rtw_ieee80211.o	\
		core/rtw_mlme.o		\
		core/rtw_mlme_ext.o	\
		core/rtw_pwrctrl.o	\
		core/rtw_recv.o		\
		core/rtw_security.o	\
		core/rtw_sreset.o	\
		core/rtw_sta_mgt.o	\
		core/rtw_xmit.o		\
		core/rtw_wlan_util.o	\
		hal/hal_com.o		\
		hal/hal_intf.o		\
		hal/Hal8723PwrSeq.o	\
		hal/Hal8723UHWImg_CE.o	\
		hal/HalDMOutSrc8723A_CE.o \
		hal/HalHWImg8723A_BB.o	\
		hal/HalHWImg8723A_MAC.o	\
		hal/HalHWImg8723A_RF.o	\
		hal/HalPwrSeqCmd.o	\
		hal/odm_RegConfig8723A.o \
		hal/odm_debug.o		\
		hal/odm_interface.o	\
		hal/odm_HWConfig.o	\
		hal/odm.o		\
		hal/rtl8723a_cmd.o	\
		hal/rtl8723a_dm.o	\
		hal/rtl8723a_hal_init.o	\
		hal/rtl8723a_phycfg.o	\
		hal/rtl8723a_rf6052.o	\
		hal/rtl8723a_rxdesc.o	\
		hal/rtl8723a_sreset.o	\
		hal/rtl8723au_recv.o	\
		hal/rtl8723au_xmit.o	\
		hal/usb_halinit.o	\
		hal/usb_ops_linux.o	\
		os_dep/ioctl_cfg80211.o	\
		os_dep/mlme_linux.o	\
		os_dep/os_intfs.o	\
		os_dep/recv_linux.o	\
		os_dep/usb_intf.o	\
		os_dep/usb_ops_linux.o	\
		os_dep/xmit_linux.o

r8723au-$(CONFIG_8723AU_BT_COEXIST) += hal/rtl8723a_bt-coexist.o
r8723au-$(CONFIG_8723AU_AP_MODE) += core/rtw_ap.o

obj-$(CONFIG_R8723AU)	:= r8723au.o

ccflags-y += $(call cc-option,-Wtype-limits,)
ccflags-y += -D__CHECK_ENDIAN__ -I$(src)/include
