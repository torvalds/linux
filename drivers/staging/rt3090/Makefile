obj-$(CONFIG_RT3090)	+= rt3090sta.o

include drivers/staging/rt3090/config.mk

rt3090sta-objs := \
	common/crypt_md5.o \
	common/crypt_sha2.o \
	common/crypt_hmac.o \
	common/mlme.o \
	common/cmm_wep.o \
	common/action.o \
	common/cmm_data.o \
	common/rtmp_init.o \
	common/cmm_tkip.o \
	common/cmm_aes.o \
	common/cmm_sync.o \
	common/eeprom.o \
	common/cmm_sanity.o \
	common/cmm_info.o \
	common/cmm_cfg.o \
	common/cmm_wpa.o \
	common/dfs.o \
	common/spectrum.o \
	common/rtmp_timer.o \
	common/rt_channel.o \
	common/cmm_profile.o \
	common/cmm_asic.o \
	sta/assoc.o \
	sta/auth.o \
	sta/auth_rsp.o \
	sta/sync.o \
	sta/sanity.o \
	sta/rtmp_data.o \
	sta/connect.o \
	sta/wpa.o \
	rt_linux.o \
	rt_profile.o \
	rt_main_dev.o \
	sta_ioctl.o

#ifdef DOT11_N_SUPPORT
ifeq ($(HAS_DOT11_N_SUPPORT),y)
rt3090sta-objs += \
	common/ba_action.o
endif
#endif // DOT11_N_SUPPORT //

#ifdef ETH_CONVERT
ifeq ($(HAS_ETH_CONVERT_SUPPORT), y)
rt3090sta-objs += \
	common/cmm_mat.o \
	common/cmm_mat_iparp.o \
	common/cmm_mat_pppoe.o \
	common/cmm_mat_ipv6.o
endif
#endif // ETH_CONVERT //

ifeq ($(HAS_BLOCK_NET_IF),y)
rt3090sta-objs += common/netif_block.o
endif

ifeq ($(HAS_QOS_DLS_SUPPORT),y)
rt3090sta-objs += sta/dls.o
endif

rt3090sta-objs += \
	pci_main_dev.o \
	rt_pci_rbus.o \
	common/cmm_mac_pci.o \
	common/cmm_data_pci.o \
	common/ee_prom.o \
	common/ee_efuse.o \
	common/rtmp_mcu.o \
	chips/rt30xx.o \
	common/rt_rf.o \
	chips/rt3090.o

ifeq ($(HAS_ATE),y)
rt3090sta-objs += rt_ate.o
endif
