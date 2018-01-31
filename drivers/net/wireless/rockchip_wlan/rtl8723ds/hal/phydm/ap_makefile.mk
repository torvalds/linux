# SPDX-License-Identifier: GPL-2.0

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
	phydm/phydm_acs.o\
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
	phydm/txbf/phydm_hal_txbf_api.o\
	EdcaTurboCheck.o\
	phydm/halrf/halrf.o\
	phydm/halrf/halphyrf_ap.o\
	phydm/halrf/halrf_powertracking_ap.o\
	phydm/halrf/halrf_powertracking.o\
	phydm/halrf/halrf_kfree.o

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
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		rtl8192cd-objs += \
		phydm/rtl8814a/halhwimg8814a_bb.o\
		phydm/rtl8814a/halhwimg8814a_mac.o\
		phydm/rtl8814a/halhwimg8814a_rf.o\
		phydm/rtl8814a/phydm_regconfig8814a.o\
		phydm/rtl8814a/phydm_rtl8814a.o			
	endif
endif
	
ifeq ($(CONFIG_WLAN_HAL_8822BE),y)
	_PHYDM_FILES += phydm/halrf/rtl8822b/halrf_8822b.o
	_PHYDM_FILES += phydm/halrf/rtl8822b/halrf_iqk_8822b.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8822b/halhwimg8822b_bb.o\
		phydm/rtl8822b/halhwimg8822b_mac.o\
		phydm/rtl8822b/halhwimg8822b_rf.o\
		phydm/rtl8822b/phydm_regconfig8822b.o\
		phydm/rtl8822b/phydm_hal_api8822b.o\
		phydm/rtl8822b/phydm_rtl8822b.o
	endif
endif

ifeq ($(CONFIG_WLAN_HAL_8821CE),y)
	_PHYDM_FILES += phydm/halrf/rtl8821c/halrf_8821c.o
	_PHYDM_FILES += phydm/halrf/rtl8821c/halrf_iqk_8821c.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8821c/halhwimg8821c_bb.o\
		phydm/rtl8821c/halhwimg8821c_mac.o\
		phydm/rtl8821c/halhwimg8821c_rf.o\
		phydm/rtl8821c/phydm_regconfig8821c.o\
		phydm/rtl8821c/phydm_hal_api8821c.o
	endif
endif
	
ifeq ($(CONFIG_WLAN_HAL_8197F),y)
		_PHYDM_FILES += phydm/halrf/rtl8197f/halrf_8197f.o
		_PHYDM_FILES += phydm/halrf/rtl8197f/halrf_iqk_8197f.o
		_PHYDM_FILES += efuse_97f/efuse.o
	ifeq ($(CONFIG_RTL_ODM_WLAN_DRIVER),y)
		_PHYDM_FILES += \
		phydm/rtl8197f/halhwimg8197f_bb.o\
		phydm/rtl8197f/halhwimg8197f_mac.o\
		phydm/rtl8197f/halhwimg8197f_rf.o\
		phydm/rtl8197f/phydm_hal_api8197f.o\
		phydm/rtl8197f/phydm_regconfig8197f.o\
		phydm/rtl8197f/phydm_rtl8197f.o
	endif
endif
