obj-$(CONFIG_IWLWIFI)	+= iwlcore.o
iwlcore-objs 		:= iwl-core.o iwl-eeprom.o iwl-hcmd.o iwl-power.o
iwlcore-objs 		+= iwl-rx.o iwl-tx.o iwl-sta.o iwl-calib.o
iwlcore-objs 		+= iwl-scan.o
iwlcore-$(CONFIG_IWLWIFI_DEBUGFS) += iwl-debugfs.o
iwlcore-$(CONFIG_IWLWIFI_LEDS) += iwl-led.o
iwlcore-$(CONFIG_IWLWIFI_RFKILL) += iwl-rfkill.o
iwlcore-$(CONFIG_IWLWIFI_SPECTRUM_MEASUREMENT) += iwl-spectrum.o

obj-$(CONFIG_IWLAGN)	+= iwlagn.o
iwlagn-objs		:= iwl-agn.o iwl-agn-rs.o

iwlagn-$(CONFIG_IWL4965) += iwl-4965.o
iwlagn-$(CONFIG_IWL5000) += iwl-5000.o
iwlagn-$(CONFIG_IWL5000) += iwl-6000.o
iwlagn-$(CONFIG_IWL5000) += iwl-1000.o

obj-$(CONFIG_IWL3945)	+= iwl3945.o
iwl3945-objs		:= iwl3945-base.o iwl-3945.o iwl-3945-rs.o iwl-3945-led.o


