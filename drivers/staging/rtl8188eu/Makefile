r8188eu-y :=				\
		core/rtw_ap.o		\
		core/rtw_cmd.o		\
		core/rtw_debug.o	\
		core/rtw_efuse.o	\
		core/rtw_ieee80211.o	\
		core/rtw_ioctl_set.o	\
		core/rtw_iol.o		\
		core/rtw_led.o		\
		core/rtw_mlme.o		\
		core/rtw_mlme_ext.o	\
		core/rtw_mp.o		\
		core/rtw_pwrctrl.o	\
		core/rtw_p2p.o		\
		core/rtw_recv.o		\
		core/rtw_rf.o		\
		core/rtw_security.o	\
		core/rtw_sreset.o	\
		core/rtw_sta_mgt.o	\
		core/rtw_wlan_util.o	\
		core/rtw_xmit.o		\
		hal/HalHWImg8188E_MAC.o	\
		hal/HalHWImg8188E_BB.o	\
		hal/HalHWImg8188E_RF.o	\
		hal/HalPhyRf_8188e.o	\
		hal/HalPwrSeqCmd.o	\
		hal/Hal8188EPwrSeq.o	\
		hal/Hal8188ERateAdaptive.o\
		hal/hal_intf.o		\
		hal/hal_com.o		\
		hal/odm.o		\
		hal/odm_debug.o		\
		hal/odm_HWConfig.o	\
		hal/odm_RegConfig8188E.o\
		hal/odm_RTL8188E.o	\
		hal/rtl8188e_cmd.o	\
		hal/rtl8188e_dm.o	\
		hal/rtl8188e_hal_init.o	\
		hal/rtl8188e_mp.o	\
		hal/rtl8188e_phycfg.o	\
		hal/rtl8188e_rf6052.o	\
		hal/rtl8188e_rxdesc.o	\
		hal/rtl8188e_xmit.o	\
		hal/rtl8188eu_led.o	\
		hal/rtl8188eu_recv.o	\
		hal/rtl8188eu_xmit.o	\
		hal/usb_halinit.o	\
		os_dep/ioctl_linux.o	\
		os_dep/mlme_linux.o	\
		os_dep/os_intfs.o	\
		os_dep/osdep_service.o	\
		os_dep/recv_linux.o	\
		os_dep/rtw_android.o	\
		os_dep/usb_intf.o	\
		os_dep/usb_ops_linux.o	\
		os_dep/xmit_linux.o

obj-$(CONFIG_R8188EU)	:= r8188eu.o

ccflags-y += -D__CHECK_ENDIAN__ -I$(src)/include
