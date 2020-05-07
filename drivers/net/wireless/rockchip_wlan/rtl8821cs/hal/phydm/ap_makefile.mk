
_PHYDM_FILES :=\
	phydm/phydm.o \
	phydm/phydm_dig.o\
	phydm/phydm_antdiv.o\
	phydm/phydm_soml.o\
	phydm/phydm_smt_ant.o\
	phydm/phydm_pathdiv.o\
	phydm/phydm_rainfo.o\
	phydm/phydm_dynamictxpower.o\
	phydm/phydm_adaptivity.o\
	phydm/phydm_debug.o\
	phydm/phydm_interface.o\
	phydm/phydm_phystatus.o\
	phydm/phydm_hwconfig.o\
	phydm/phydm_dfs.o\
	phydm/phydm_cfotracking.o\
	phydm/phydm_adc_sampling.o\
	phydm/phydm_ccx.o\
	phydm/phydm_primary_cca.o\
	phydm/phydm_cck_pd.o\
	phydm/phydm_rssi_monitor.o\
	phydm/phydm_auto_dbg.o\
	phydm/phydm_math_lib.o\
	phydm/phydm_noisemonitor.o\
	phydm/phydm_api.o\
	phydm/phydm_pow_train.o\
	phydm/phydm_lna_sat.o\
	phydm/phydm_pmac_tx_setting.o\
	phydm/phydm_mp.o\
	phydm/phydm_cck_rx_pathdiv.o\
	phydm/phydm_direct_bf.o\
	phydm/txbf/phydm_hal_txbf_api.o\
	EdcaTurboCheck.o\
	phydm/halrf/halrf.o\
	phydm/halrf/halrf_debug.o\
	phydm/halrf/halphyrf_ap.o\
	phydm/halrf/halrf_powertracking_ap.o\
	phydm/halrf/halrf_powertracking.o\
	phydm/halrf/halrf_kfree.o\
	phydm/halrf/halrf_psd.o

ifeq ($(CONFIG_RTL_88E_SUPPORT),y)
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8188e/halhwimg8188e_bb.o\
		phydm/rtl8188e/halhwimg8188e_mac.o\
		phydm/rtl8188e/halhwimg8188e_rf.o\
		phydm/rtl8188e/phydm_regconfig8188e.o\
		phydm/rtl8188e/hal8188erateadaptive.o\
		phydm/rtl8188e/phydm_rtl8188e.o\
		phydm/halrf/rtl8188e/halrf_8188e_ap.o
	endif
endif

ifeq ($(CONFIG_RTL_8812_SUPPORT),y)
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += ./phydm/halrf/rtl8812a/halrf_8812a_ap.o
	endif
	_PHYDM_FILES += phydm/rtl8812a/phydm_rtl8812a.o
endif

ifeq ($(CONFIG_WLAN_HAL_8881A),y)
	_PHYDM_FILES += phydm/halrf/rtl8821a/halrf_iqk_8821a_ap.o
endif

ifeq ($(CONFIG_WLAN_HAL_8192EE),y)
	_PHYDM_FILES += \
	phydm/halrf/rtl8192e/halrf_8192e_ap.o\
	phydm/rtl8192e/phydm_rtl8192e.o
endif

ifeq ($(CONFIG_WLAN_HAL_8814AE),y)
	rtl8192cd-objs += phydm/halrf/rtl8814a/halrf_8814a_ap.o
	rtl8192cd-objs += phydm/halrf/rtl8814a/halrf_iqk_8814a.o
	rtl8192cd-objs += phydm/halrf/rtl8814a/halhwimg8814a_rf.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		rtl8192cd-objs += \
		phydm/rtl8814a/halhwimg8814a_bb.o\
		phydm/rtl8814a/halhwimg8814a_mac.o\
		phydm/rtl8814a/phydm_regconfig8814a.o\
		phydm/rtl8814a/phydm_rtl8814a.o
	endif
endif

ifeq ($(CONFIG_WLAN_HAL_8822BE),y)
	_PHYDM_FILES += phydm/halrf/rtl8822b/halrf_8822b.o
	_PHYDM_FILES += phydm/halrf/rtl8822b/halrf_iqk_8822b.o
	_PHYDM_FILES += phydm/halrf/rtl8822b/halhwimg8822b_rf.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8822b/halhwimg8822b_bb.o\
		phydm/rtl8822b/halhwimg8822b_mac.o\
		phydm/rtl8822b/phydm_regconfig8822b.o\
		phydm/rtl8822b/phydm_hal_api8822b.o\
		phydm/rtl8822b/phydm_rtl8822b.o
	endif
endif

ifeq ($(CONFIG_WLAN_HAL_8822CE),y)
	_PHYDM_FILES += phydm/halrf/rtl8822c/halrf_8822c.o
	_PHYDM_FILES += phydm/halrf/rtl8822c/halrf_iqk_8822c.o
	_PHYDM_FILES += phydm/halrf/rtl8822c/halrf_dpk_8822c.o
	_PHYDM_FILES += phydm/halrf/rtl8822c/halrf_rfk_init_8822c.o
	_PHYDM_FILES += phydm/halrf/rtl8822c/halhwimg8822c_rf.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8822c/halhwimg8822c_bb.o\
		phydm/rtl8822c/phydm_regconfig8822c.o\
		phydm/rtl8822c/phydm_hal_api8822c.o
	endif
endif

ifeq ($(CONFIG_WLAN_HAL_8812FE),y)
	_PHYDM_FILES += phydm/halrf/rtl8812f/halrf_8812f.o
	_PHYDM_FILES += phydm/halrf/rtl8812f/halrf_iqk_8812f.o
	_PHYDM_FILES += phydm/halrf/rtl8812f/halrf_dpk_8812f.o
	_PHYDM_FILES += phydm/halrf/rtl8812f/halrf_tssi_8812f.o
	_PHYDM_FILES += phydm/halrf/rtl8812f/halrf_rfk_init_8812f.o
	_PHYDM_FILES += phydm/halrf/rtl8812f/halhwimg8812f_rf.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8812f/halhwimg8812f_bb.o\
		phydm/rtl8812f/phydm_regconfig8812f.o\
		phydm/rtl8812f/phydm_hal_api8812f.o
	endif
endif

ifeq ($(CONFIG_WLAN_HAL_8821CE),y)
	_PHYDM_FILES += phydm/halrf/rtl8821c/halrf_8821c.o
	_PHYDM_FILES += phydm/halrf/rtl8821c/halrf_iqk_8821c.o
	_PHYDM_FILES += phydm/halrf/rtl8821c/halhwimg8821c_rf.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8821c/halhwimg8821c_bb.o\
		phydm/rtl8821c/halhwimg8821c_mac.o\
		phydm/rtl8821c/phydm_regconfig8821c.o\
		phydm/rtl8821c/phydm_hal_api8821c.o
	endif
endif

ifeq ($(CONFIG_WLAN_HAL_8197F),y)
		_PHYDM_FILES += phydm/halrf/rtl8197f/halrf_8197f.o
		_PHYDM_FILES += phydm/halrf/rtl8197f/halrf_iqk_8197f.o
		_PHYDM_FILES += phydm/halrf/rtl8197f/halrf_dpk_8197f.o
		_PHYDM_FILES += phydm/halrf/rtl8197f/halhwimg8197f_rf.o
		_PHYDM_FILES += efuse_97f/efuse.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8197f/halhwimg8197f_bb.o\
		phydm/rtl8197f/halhwimg8197f_mac.o\
		phydm/rtl8197f/phydm_hal_api8197f.o\
		phydm/rtl8197f/phydm_regconfig8197f.o\
		phydm/rtl8197f/phydm_rtl8197f.o
	endif
endif


ifeq ($(CONFIG_WLAN_HAL_8192FE),y)
		_PHYDM_FILES += phydm/halrf/rtl8192f/halrf_8192f.o
		_PHYDM_FILES += phydm/halrf/rtl8192f/halrf_dpk_8192f.o
		_PHYDM_FILES += phydm/halrf/rtl8192f/halhwimg8192f_rf.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8192f/halhwimg8192f_bb.o\
		phydm/rtl8192f/halhwimg8192f_mac.o\
		phydm/rtl8192f/phydm_hal_api8192f.o\
		phydm/rtl8192f/phydm_regconfig8192f.o\
		phydm/rtl8192f/phydm_rtl8192f.o
	endif
endif

ifeq ($(CONFIG_WLAN_HAL_8198F),y)
		_PHYDM_FILES += phydm/halrf/rtl8198f/halrf_8198f.o
		_PHYDM_FILES += phydm/halrf/rtl8198f/halrf_iqk_8198f.o
		_PHYDM_FILES += phydm/halrf/rtl8198f/halrf_dpk_8198f.o
		_PHYDM_FILES += phydm/halrf/rtl8198f/halrf_rfk_init_8198f.o
		_PHYDM_FILES += phydm/halrf/rtl8198f/halhwimg8198f_rf.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8198f/phydm_hal_api8198f.o\
		phydm/rtl8198f/halhwimg8198f_bb.o\
		phydm/rtl8198f/halhwimg8198f_mac.o\
		phydm/rtl8198f/phydm_regconfig8198f.o \
		phydm/halrf/rtl8198f/halrf_8198f.o
	endif
endif

ifeq ($(CONFIG_WLAN_HAL_8814BE),y)
		_PHYDM_FILES += phydm/halrf/rtl8814b/halrf_8814b.o
		_PHYDM_FILES += phydm/halrf/rtl8814b/halrf_iqk_8814b.o
		_PHYDM_FILES += phydm/halrf/rtl8814b/halrf_dpk_8814b.o
		_PHYDM_FILES += phydm/halrf/rtl8814b/halrf_rfk_init_8814b.o
		_PHYDM_FILES += phydm/halrf/rtl8814b/halhwimg8814b_rf.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8814b/phydm_hal_api8814b.o\
		phydm/rtl8814b/halhwimg8814b_bb.o\
		phydm/rtl8814b/phydm_regconfig8814b.o \
		phydm/halrf/rtl8814b/halrf_8814b.o
	endif
endif

ifeq ($(CONFIG_WLAN_HAL_8197G),y)
		_PHYDM_FILES += phydm/halrf/rtl8197g/halrf_8197g.o
		_PHYDM_FILES += phydm/halrf/rtl8197g/halrf_iqk_8197g.o
		_PHYDM_FILES += phydm/halrf/rtl8197g/halrf_dpk_8197g.o
		_PHYDM_FILES += phydm/halrf/rtl8197g/halrf_tssi_8197g.o
		_PHYDM_FILES += phydm/halrf/rtl8197g/halrf_rfk_init_8197g.o
		_PHYDM_FILES += phydm/halrf/rtl8197g/halhwimg8197g_rf.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8197g/phydm_hal_api8197g.o\
		phydm/rtl8197g/halhwimg8197g_bb.o\
		phydm/rtl8197g/halhwimg8197g_mac.o\
		phydm/rtl8197g/phydm_regconfig8197g.o \
		phydm/halrf/rtl8197g/halrf_8197g.o
	endif
endif

