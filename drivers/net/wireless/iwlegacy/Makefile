obj-$(CONFIG_IWLEGACY)	+= iwl-legacy.o
iwl-legacy-objs 		:= iwl-core.o iwl-eeprom.o iwl-hcmd.o iwl-power.o
iwl-legacy-objs 		+= iwl-rx.o iwl-tx.o iwl-sta.o
iwl-legacy-objs 		+= iwl-scan.o iwl-led.o
iwl-legacy-$(CONFIG_IWLEGACY_DEBUGFS) += iwl-debugfs.o

iwl-legacy-objs += $(iwl-legacy-m)

# 4965
obj-$(CONFIG_IWL4965)	+= iwl4965.o
iwl4965-objs		:= 4965.o 4965-mac.o 4965-rs.o 4965-calib.o
iwl4965-$(CONFIG_IWLEGACY_DEBUGFS) += 4965-debug.o

# 3945
obj-$(CONFIG_IWL3945)	+= iwl3945.o
iwl3945-objs		:= 3945-mac.o 3945.o iwl-3945-rs.o
iwl3945-$(CONFIG_IWLEGACY_DEBUGFS) += iwl-3945-debugfs.o

ccflags-y += -D__CHECK_ENDIAN__
