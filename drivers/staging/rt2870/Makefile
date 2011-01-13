#
obj-$(CONFIG_RT2870)	+= rt2870sta.o

# TODO: all of these should be removed
ccflags-y := -DLINUX -DAGGREGATION_SUPPORT -DPIGGYBACK_SUPPORT -DWMM_SUPPORT
ccflags-y += -DRTMP_MAC_USB -DRTMP_USB_SUPPORT -DRT2870 -DRTMP_TIMER_TASK_SUPPORT
ccflags-y += -DRTMP_RF_RW_SUPPORT -DRTMP_EFUSE_SUPPORT -DRT30xx -DRT3070
ccflags-y += -DDBG

rt2870sta-y :=		\
	common/crypt_md5.o	\
	common/crypt_sha2.o	\
	common/crypt_hmac.o	\
	common/mlme.o		\
	common/cmm_wep.o	\
	common/action.o		\
	common/cmm_data.o	\
	common/rtmp_init.o	\
	common/cmm_tkip.o	\
	common/cmm_aes.o	\
	common/cmm_sync.o	\
	common/eeprom.o		\
	common/cmm_sanity.o	\
	common/cmm_info.o	\
	common/cmm_cfg.o	\
	common/cmm_wpa.o	\
	common/dfs.o		\
	common/spectrum.o	\
	common/rtmp_timer.o	\
	common/rt_channel.o	\
	common/cmm_asic.o	\
	sta/assoc.o		\
	sta/auth.o		\
	sta/auth_rsp.o		\
	sta/sync.o		\
	sta/sanity.o		\
	sta/rtmp_data.o		\
	sta/connect.o		\
	sta/wpa.o		\
	rt_linux.o		\
	rt_main_dev.o		\
	sta_ioctl.o		\
	common/ba_action.o	\
	usb_main_dev.o		\
	rt_usb.o		\
	common/cmm_mac_usb.o	\
	common/rtusb_io.o	\
	common/rtusb_bulk.o	\
	common/rtusb_data.o	\
	common/cmm_data_usb.o	\
	common/rtmp_mcu.o	\
	common/ee_efuse.o	\
	chips/rt30xx.o		\
	common/rt_rf.o		\
	chips/rt3070.o
