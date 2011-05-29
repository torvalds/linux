obj-$(CONFIG_IWLWIFI_LEGACY)	+= iwl-legacy.o
iwl-legacy-objs 		:= iwl-core.o iwl-eeprom.o iwl-hcmd.o iwl-power.o
iwl-legacy-objs 		+= iwl-rx.o iwl-tx.o iwl-sta.o
iwl-legacy-objs 		+= iwl-scan.o iwl-led.o
iwl-legacy-$(CONFIG_IWLWIFI_LEGACY_DEBUGFS) += iwl-debugfs.o
iwl-legacy-$(CONFIG_IWLWIFI_LEGACY_DEVICE_TRACING) += iwl-devtrace.o

iwl-legacy-objs += $(iwl-legacy-m)

CFLAGS_iwl-devtrace.o := -I$(src)

# 4965
obj-$(CONFIG_IWL4965)	+= iwl4965.o
iwl4965-objs		:= iwl-4965.o iwl4965-base.o iwl-4965-rs.o iwl-4965-led.o
iwl4965-objs		+= iwl-4965-ucode.o iwl-4965-tx.o
iwl4965-objs		+= iwl-4965-lib.o iwl-4965-rx.o iwl-4965-calib.o
iwl4965-objs		+= iwl-4965-sta.o iwl-4965-eeprom.o
iwl4965-$(CONFIG_IWLWIFI_LEGACY_DEBUGFS) += iwl-4965-debugfs.o

# 3945
obj-$(CONFIG_IWL3945)	+= iwl3945.o
iwl3945-objs		:= iwl3945-base.o iwl-3945.o iwl-3945-rs.o iwl-3945-led.o
iwl3945-$(CONFIG_IWLWIFI_LEGACY_DEBUGFS) += iwl-3945-debugfs.o

ccflags-y += -D__CHECK_ENDIAN__
