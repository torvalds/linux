# SPDX-License-Identifier: GPL-2.0
EXTRA_CFLAGS += -I$(src)/hal/phydm

_PHYDM_FILES := hal/phydm/phydm_debug.o	\
								hal/phydm/phydm_interface.o\
								hal/phydm/phydm_phystatus.o\
								hal/phydm/phydm_hwconfig.o\
								hal/phydm/phydm.o\
								hal/phydm/phydm_dig.o\
								hal/phydm/phydm_rainfo.o\
								hal/phydm/phydm_adaptivity.o\
								hal/phydm/phydm_cfotracking.o\
								hal/phydm/phydm_noisemonitor.o\
								hal/phydm/phydm_beamforming.o\
								hal/phydm/phydm_dfs.o\
								hal/phydm/txbf/halcomtxbf.o\
								hal/phydm/txbf/haltxbfinterface.o\
								hal/phydm/txbf/phydm_hal_txbf_api.o\
								hal/phydm/phydm_ccx.o\
								hal/phydm/phydm_cck_pd.o\
								hal/phydm/phydm_rssi_monitor.o\
								hal/phydm/phydm_math_lib.o\
								hal/phydm/phydm_api.o\
								hal/phydm/halrf/halrf.o\
								hal/phydm/halrf/halrf_debug.o\
								hal/phydm/halrf/halphyrf_ce.o\
								hal/phydm/halrf/halrf_powertracking_ce.o\
								hal/phydm/halrf/halrf_powertracking.o\
								hal/phydm/halrf/halrf_kfree.o

ifeq ($(CONFIG_RTL8188E), y)
RTL871X = rtl8188e
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8188e_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8188e_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8188e_rf.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8188e_ce.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8188e.o\
								hal/phydm/$(RTL871X)/hal8188erateadaptive.o\
								hal/phydm/$(RTL871X)/phydm_rtl8188e.o
endif

ifeq ($(CONFIG_RTL8192E), y)
RTL871X = rtl8192e
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8192e_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8192e_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8192e_rf.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8192e_ce.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8192e.o\
								hal/phydm/$(RTL871X)/phydm_rtl8192e.o
endif


ifeq ($(CONFIG_RTL8812A), y)
RTL871X = rtl8812a
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8812a_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8812a_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8812a_rf.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8812a_ce.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8812a.o\
								hal/phydm/$(RTL871X)/phydm_rtl8812a.o\
								hal/phydm/txbf/haltxbfjaguar.o
endif

ifeq ($(CONFIG_RTL8821A), y)
RTL871X = rtl8821a
_PHYDM_FILES += hal/phydm/rtl8821a/halhwimg8821a_mac.o\
								hal/phydm/rtl8821a/halhwimg8821a_bb.o\
								hal/phydm/rtl8821a/halhwimg8821a_rf.o\
								hal/phydm/halrf/rtl8812a/halrf_8812a_ce.o\
								hal/phydm/halrf/rtl8821a/halrf_8821a_ce.o\
								hal/phydm/rtl8821a/phydm_regconfig8821a.o\
								hal/phydm/rtl8821a/phydm_rtl8821a.o\
								hal/phydm/halrf/rtl8821a/halrf_iqk_8821a_ce.o\
								hal/phydm/txbf/haltxbfjaguar.o
endif


ifeq ($(CONFIG_RTL8723B), y)
RTL871X = rtl8723b
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8723b_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8723b_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8723b_rf.o\
								hal/phydm/$(RTL871X)/halhwimg8723b_mp.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8723b.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8723b_ce.o\
								hal/phydm/$(RTL871X)/phydm_rtl8723b.o
endif


ifeq ($(CONFIG_RTL8814A), y)
RTL871X = rtl8814a
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8814a_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8814a_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8814a_rf.o\
								hal/phydm/halrf/$(RTL871X)/halrf_iqk_8814a.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8814a.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8814a_ce.o\
								hal/phydm/$(RTL871X)/phydm_rtl8814a.o\
								hal/phydm/txbf/haltxbf8814a.o
endif


ifeq ($(CONFIG_RTL8723C), y)
RTL871X = rtl8703b
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8703b_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8703b_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8703b_rf.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8703b.o\
								hal/phydm/$(RTL871X)/phydm_rtl8703b.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8703b.o
endif

ifeq ($(CONFIG_RTL8723D), y)
RTL871X = rtl8723d
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8723d_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8723d_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8723d_rf.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8723d.o\
								hal/phydm/$(RTL871X)/phydm_rtl8723d.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8723d.o
endif


ifeq ($(CONFIG_RTL8710B), y)
RTL871X = rtl8710b
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8710b_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8710b_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8710b_rf.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8710b.o\
								hal/phydm/$(RTL871X)/phydm_rtl8710b.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8710b.o
endif


ifeq ($(CONFIG_RTL8188F), y)
RTL871X = rtl8188f
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8188f_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8188f_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8188f_rf.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8188f.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8188f.o \
								hal/phydm/$(RTL871X)/phydm_rtl8188f.o
endif

ifeq ($(CONFIG_RTL8822B), y)
RTL871X = rtl8822b
_PHYDM_FILES +=	hal/phydm/$(RTL871X)/halhwimg8822b_bb.o \
								hal/phydm/$(RTL871X)/halhwimg8822b_mac.o \
								hal/phydm/$(RTL871X)/halhwimg8822b_rf.o \
								hal/phydm/halrf/$(RTL871X)/halrf_8822b.o \
								hal/phydm/$(RTL871X)/phydm_hal_api8822b.o \
								hal/phydm/halrf/$(RTL871X)/halrf_iqk_8822b.o \
								hal/phydm/halrf/$(RTL871X)/halrf_rfk_init_8822b.o \
								hal/phydm/$(RTL871X)/phydm_regconfig8822b.o \
								hal/phydm/$(RTL871X)/phydm_rtl8822b.o

_PHYDM_FILES +=	hal/phydm/txbf/haltxbf8822b.o
endif


ifeq ($(CONFIG_RTL8821C), y)
RTL871X = rtl8821c
_PHYDM_FILES +=	hal/phydm/$(RTL871X)/halhwimg8821c_bb.o \
								hal/phydm/$(RTL871X)/halhwimg8821c_mac.o \
								hal/phydm/$(RTL871X)/halhwimg8821c_rf.o \
								hal/phydm/$(RTL871X)/phydm_hal_api8821c.o \
								hal/phydm/$(RTL871X)/phydm_regconfig8821c.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8821c.o\
								hal/phydm/halrf/$(RTL871X)/halrf_iqk_8821c.o
endif
ifeq ($(CONFIG_RTL8192F), y)
RTL871X = rtl8192f
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8192f_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8192f_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8192f_rf.o\
								hal/phydm/$(RTL871X)/phydm_hal_api8192f.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8192f.o\
								hal/phydm/$(RTL871X)/phydm_rtl8192f.o\
								hal/phydm/halrf/$(RTL871X)/halrf_8192f.o
endif

ifeq ($(CONFIG_RTL8198F), y)
RTL871X = rtl8198f
_PHYDM_FILES += hal/phydm/$(RTL871X)/halhwimg8198f_bb.o\
								hal/phydm/$(RTL871X)/halhwimg8198f_mac.o\
								hal/phydm/$(RTL871X)/halhwimg8198f_rf.o\
								hal/phydm/$(RTL871X)/phydm_hal_api8198f.o\
								hal/phydm/$(RTL871X)/phydm_regconfig8198f.o
endif
