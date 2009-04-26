obj-$(CONFIG_RT2870)	+= rt2870sta.o

# TODO: all of these should be removed
EXTRA_CFLAGS += -DLINUX -DAGGREGATION_SUPPORT -DPIGGYBACK_SUPPORT -DWMM_SUPPORT
EXTRA_CFLAGS += -DRT2870
EXTRA_CFLAGS += -DDBG
EXTRA_CFLAGS += -DDOT11_N_SUPPORT
EXTRA_CFLAGS += -DWPA_SUPPLICANT_SUPPORT

rt2870sta-objs :=		\
	common/md5.o		\
	common/mlme.o		\
	common/rtmp_wep.o	\
	common/action.o		\
	common/cmm_data.o	\
	common/rtmp_init.o	\
	common/rtmp_tkip.o	\
	common/cmm_sync.o	\
	common/eeprom.o		\
	common/cmm_sanity.o	\
	common/cmm_info.o	\
	common/cmm_wpa.o	\
	common/dfs.o		\
	common/spectrum.o	\
	sta/assoc.o		\
	sta/aironet.o		\
	sta/auth.o		\
	sta/auth_rsp.o		\
	sta/sync.o		\
	sta/sanity.o		\
	sta/rtmp_data.o		\
	sta/connect.o		\
	sta/wpa.o		\
	rt_linux.o		\
	rt_profile.o		\
	rt_main_dev.o		\
	sta_ioctl.o		\
	common/ba_action.o	\
	2870_main_dev.o		\
	common/2870_rtmp_init.o	\
	common/rtusb_io.o	\
	common/rtusb_bulk.o	\
	common/rtusb_data.o	\
	common/cmm_data_2870.o

