# WIFI
obj-$(CONFIG_IWLWIFI)	+= iwlwifi.o
iwlwifi-objs		:= iwl-agn.o iwl-agn-rs.o iwl-mac80211.o
iwlwifi-objs		+= iwl-ucode.o iwl-agn-tx.o iwl-debug.o
iwlwifi-objs		+= iwl-agn-lib.o iwl-agn-calib.o iwl-io.o
iwlwifi-objs		+= iwl-agn-tt.o iwl-agn-sta.o iwl-agn-rx.o

iwlwifi-objs		+= iwl-eeprom.o iwl-power.o
iwlwifi-objs		+= iwl-scan.o iwl-led.o
iwlwifi-objs		+= iwl-agn-rxon.o iwl-agn-devices.o
iwlwifi-objs		+= iwl-5000.o
iwlwifi-objs		+= iwl-6000.o
iwlwifi-objs		+= iwl-1000.o
iwlwifi-objs		+= iwl-2000.o
iwlwifi-objs		+= iwl-pci.o
iwlwifi-objs		+= iwl-drv.o
iwlwifi-objs		+= iwl-notif-wait.o
iwlwifi-objs		+= iwl-trans-pcie.o iwl-trans-pcie-rx.o iwl-trans-pcie-tx.o


iwlwifi-$(CONFIG_IWLWIFI_DEBUGFS) += iwl-debugfs.o
iwlwifi-$(CONFIG_IWLWIFI_DEVICE_TRACING) += iwl-devtrace.o
iwlwifi-$(CONFIG_IWLWIFI_DEVICE_TESTMODE) += iwl-testmode.o

CFLAGS_iwl-devtrace.o := -I$(src)

ccflags-y += -D__CHECK_ENDIAN__
