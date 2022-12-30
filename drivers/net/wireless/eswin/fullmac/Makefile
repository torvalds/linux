$(ECRNX_MODULE_NAME)-y := ecrnx_msg_tx.o          \
               ecrnx_msg_rx.o          \
               ecrnx_utils.o           \
               ecrnx_cmds.o            \
               ecrnx_cfgfile.o         \
               ecrnx_strs.o            \
               ecrnx_txq.o             \
               ecrnx_mod_params.o      \
               ecrnx_platform.o	       \
               ipc_host.o	       \
               hal_desc.o	       \
               ecrnx_iwpriv.o	       \
	       fw_head_check.o         \
               slave_log_buf.o         \
	       $(MAC_SRC)/ecrnx_tdls.o \
	       $(MAC_SRC)/ecrnx_mesh.o \
	       $(MAC_SRC)/ecrnx_main.o \
	       $(MAC_SRC)/ecrnx_rx.o   \
               $(MAC_SRC)/ecrnx_tx.o \
               $(MAC_SRC)/ecrnx_calibration_data.o

ifeq ($(CONFIG_ECRNX_ESWIN_SDIO), y)
$(ECRNX_MODULE_NAME)-y += sdio/sdio.o         \
               sdio/ecrnx_sdio.o    \
               sdio/core.o         \
               sdio/fw.o           \
	       eswin_port/eswin_utils.o 
ifeq ($(CONFIG_ECRNX_ESWIN_SDIO), y)
#$(ECRNX_MODULE_NAME)-y += sdio/debug.o         
endif
endif

ifeq ($(CONFIG_ECRNX_ESWIN_USB), y)
$(ECRNX_MODULE_NAME)-y += usb/usb.o         \
               usb/ecrnx_usb.o    \
               usb/core.o         \
               usb/fw.o           \
	       eswin_port/eswin_utils.o

endif

ifeq ($(CONFIG_ECRNX_WIFO_CAIL), y)
$(ECRNX_MODULE_NAME)-y += $(MAC_SRC)/ecrnx_amt.o
endif

$(ECRNX_MODULE_NAME)-$(CONFIG_ECRNX_RADAR)       += ecrnx_radar.o
$(ECRNX_MODULE_NAME)-$(CONFIG_DEBUG_FS)         += ecrnx_debugfs.o
$(ECRNX_MODULE_NAME)-$(CONFIG_DEBUG_FS)         += ecrnx_fw_dump.o
$(ECRNX_MODULE_NAME)-$(CONFIG_DEBUG_FS)         += ecrnx_fw_trace.o
$(ECRNX_MODULE_NAME)-$(CONFIG_ECRNX_NL80211_TESTMODE) += ecrnx_testmode.o
$(ECRNX_MODULE_NAME)-$(CONFIG_ECRNX_BFMER)       += ecrnx_bfmer.o
$(ECRNX_MODULE_NAME)-$(CONFIG_ECRNX_MUMIMO_TX)   += ecrnx_mu_group.o
$(ECRNX_MODULE_NAME)-$(CONFIG_ECRNX_DBG)         += ecrnx_debug.o
$(ECRNX_MODULE_NAME)-$(CONFIG_ECRNX_P2P)         += $(MAC_SRC)/ecrnx_p2p.o
$(ECRNX_MODULE_NAME)-$(CONFIG_ECRNX_DEBUGFS_CUSTOM) += $(MAC_SRC)/ecrnx_debugfs_func.o
$(ECRNX_MODULE_NAME)-$(CONFIG_ECRNX_DEBUGFS_CUSTOM) += $(MAC_SRC)/ecrnx_debugfs_custom.o


EXTRA_CFLAGS+=-Wno-date-time
EXTRA_CFLAGS+=-Wno-declaration-after-statement
EXTRA_CFLAGS+=-Wno-format
EXTRA_CFLAGS+=-Wno-unused-variable
EXTRA_CFLAGS+=-Wno-misleading-indentation
EXTRA_CFLAGS+=-Wno-maybe-uninitialized
#EXTRA_CFLAGS+=-Wno-int-conversion
#EXTRA_CFLAGS+=-Wno-unused-function
#EXTRA_CFLAGS+=-Wno-undef
#EXTRA_CFLAGS+=-Wno-discarded-qualifiers
#EXTRA_CFLAGS+=-Wno-unused-label


ccflags-y := -DCONFIG_ECRNX_FULLMAC

ccflags-y += -I$(DRIVER_PATH)
ccflags-y += -I$(DRIVER_PATH)/$(MAC_SRC)
ccflags-y += -I$(DRIVER_PATH)/eswin_port/

ifeq ($(CONFIG_ECRNX_ESWIN_SDIO), y)
ccflags-y += -I$(DRIVER_PATH)/sdio/
else
ccflags-y += -I$(DRIVER_PATH)/usb/
endif

ccflags-$(CONFIG_ECRNX_RADAR) += -DCONFIG_ECRNX_RADAR
ccflags-$(CONFIG_ECRNX_MON_DATA) += -DCONFIG_ECRNX_MON_DATA
ccflags-$(CONFIG_ECRNX_BFMER) += -DCONFIG_ECRNX_BFMER
ccflags-$(CONFIG_ECRNX_SPLIT_TX_BUF) += -DCONFIG_ECRNX_SPLIT_TX_BUF
ifeq ($(CONFIG_ECRNX_SPLIT_TX_BUF), y)
ccflags-$(CONFIG_ECRNX_AMSDUS_TX) += -DCONFIG_ECRNX_AMSDUS_TX
endif
ccflags-$(CONFIG_ECRNX_SW_PROFILING) += -DCONFIG_ECRNX_SW_PROFILING
ccflags-$(CONFIG_ECRNX_MUMIMO_TX) += -DCONFIG_ECRNX_MUMIMO_TX

ifeq ($(CONFIG_ECRNX_MUMIMO_TX), y)
ccflags-y += -DCONFIG_USER_MAX=2
else
ccflags-y += -DCONFIG_USER_MAX=1
endif

ifeq ($(CONFIG_ECRNX_BCMC), y)
ccflags-y += -DNX_TXQ_CNT=5
else
ccflags-y += -DNX_TXQ_CNT=4
endif

# FW PLATFORM
ifeq ($(CONFIG_ECRNX_DEBUGFS_CUSTOM), y)
ccflags-$(CONFIG_ECRNX_DEBUGFS_CUSTOM) += -DCONFIG_ECRNX_DEBUGFS_CUSTOM
endif

ifeq ($(CONFIG_ECRNX_ESWIN), y)
ccflags-$(CONFIG_ECRNX_ESWIN) += -DCONFIG_ECRNX_ESWIN
endif

ifeq ($(CONFIG_ECRNX_ESWIN_SDIO), y)
ccflags-$(CONFIG_ECRNX_ESWIN_SDIO) += -DCONFIG_ECRNX_ESWIN_SDIO
endif

# SDIO TEST
ifeq ($(CONFIG_TEST_ESWIN_SDIO), y)
ccflags-$(CONFIG_TEST_ESWIN_SDIO) += -DCONFIG_TEST_ESWIN_SDIO
endif

ifeq ($(CONFIG_ECRNX_ESWIN_USB), y)
ccflags-$(CONFIG_ECRNX_ESWIN_USB) += -DCONFIG_ECRNX_ESWIN_USB
endif


# standalone wifi
ifeq ($(CONFIG_STANDALONE_WIFI), y)
ccflags-$(CONFIG_STANDALONE_WIFI) += -DCONFIG_STANDALONE_WIFI
endif

# standalone wifi+ble
ifeq ($(CONFIG_STANDALONE_WIFI_BLE), y)
ccflags-$(CONFIG_STANDALONE_WIFI_BLE) += -DCONFIG_STANDALONE_WIFI_BLE
endif

# HE config
ifeq ($(CONFIG_ECRNX_HE), y)
ccflags-$(CONFIG_ECRNX_HE) += -DCONFIG_ECRNX_HE
endif

# P2P config
ifeq ($(CONFIG_ECRNX_P2P), y)
ccflags-$(CONFIG_ECRNX_P2P) += -DCONFIG_ECRNX_P2P
endif

# 5G config
ifeq ($(CONFIG_ECRNX_5G), y)
ccflags-$(CONFIG_ECRNX_5G) += -DCONFIG_ECRNX_5G
endif

# Android platform config
ifeq ($(CONFIG_ECRNX_ANDRIOD), y)
ccflags-$(CONFIG_ECRNX_ANDRIOD) += -DCONFIG_ECRNX_ANDRIOD
endif

# For old kernel (<=3.19)
ifeq ($(shell test $(VERSION) -lt 4 -a "$(CONFIG_VENDOR_ECRNX)" = y ; echo $$?),0)
ccflags-y += -DCONFIG_VENDOR_ECRNX_VHT_NO80
endif

#CONFIG SDIO WIFI CALIBRATION
ifeq ($(CONFIG_ECRNX_WIFO_CAIL), y)
ccflags-$(CONFIG_ECRNX_WIFO_CAIL) += -DCONFIG_ECRNX_WIFO_CAIL
endif
