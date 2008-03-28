obj-$(CONFIG_IWLCORE)	+= iwlcore.o
iwlcore-objs 		= iwl-core.o iwl-eeprom.o iwl-hcmd.o

ifeq ($(CONFIG_IWLWIFI_DEBUGFS),y)
	iwlcore-objs += iwl-debugfs.o
endif

ifeq ($(CONFIG_IWLWIFI_LEDS),y)
	iwlcore-objs += iwl-led.o
endif

ifeq ($(CONFIG_IWLCORE_RFKILL),y)
	iwlcore-objs += iwl-rfkill.o
endif

obj-$(CONFIG_IWL3945)	+= iwl3945.o
iwl3945-objs		= iwl3945-base.o iwl-3945.o iwl-3945-rs.o

ifeq ($(CONFIG_IWL3945_LEDS),y)
	iwl3945-objs += iwl-3945-led.o
endif


obj-$(CONFIG_IWL4965)	+= iwl4965.o
iwl4965-objs		= iwl4965-base.o iwl-4965.o iwl-4965-rs.o

