obj-$(CONFIG_IWLCORE)	:= iwlcore.o
iwlcore-objs 		:= iwl-core.o iwl-eeprom.o iwl-hcmd.o
iwlcore-$(CONFIG_IWLWIFI_DEBUGFS) += iwl-debugfs.o
iwlcore-$(CONFIG_IWLWIFI_LEDS) += iwl-led.o
iwlcore-$(CONFIG_IWLWIFI_RFKILL) += iwl-rfkill.o

obj-$(CONFIG_IWL3945)	+= iwl3945.o
iwl3945-objs		:= iwl3945-base.o iwl-3945.o iwl-3945-rs.o
iwl3945-$(CONFIG_IWL3945_LEDS) += iwl-3945-led.o

obj-$(CONFIG_IWL4965)	+= iwl4965.o
iwl4965-objs		:= iwl4965-base.o iwl-4965.o iwl-4965-rs.o iwl-sta.o

