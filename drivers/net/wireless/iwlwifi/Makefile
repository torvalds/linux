obj-$(CONFIG_IWLWIFI)	+= iwlcore.o
iwlcore-objs 		:= iwl-core.o iwl-eeprom.o iwl-hcmd.o iwl-power.o
iwlcore-objs 		+= iwl-rx.o iwl-tx.o iwl-sta.o
iwlcore-objs 		+= iwl-scan.o iwl-led.o
iwlcore-$(CONFIG_IWL3945) += iwl-legacy.o
iwlcore-$(CONFIG_IWL4965) += iwl-legacy.o
iwlcore-$(CONFIG_IWLWIFI_DEBUGFS) += iwl-debugfs.o
iwlcore-$(CONFIG_IWLWIFI_DEVICE_TRACING) += iwl-devtrace.o

# If 3945 is selected only, iwl-legacy.o will be added
# to iwlcore-m above, but it needs to be built in.
iwlcore-objs += $(iwlcore-m)

CFLAGS_iwl-devtrace.o := -I$(src)

# AGN
obj-$(CONFIG_IWLAGN)	+= iwlagn.o
iwlagn-objs		:= iwl-agn.o iwl-agn-rs.o iwl-agn-led.o
iwlagn-objs		+= iwl-agn-ucode.o iwl-agn-tx.o
iwlagn-objs		+= iwl-agn-lib.o iwl-agn-rx.o iwl-agn-calib.o
iwlagn-objs		+= iwl-agn-tt.o iwl-agn-sta.o iwl-agn-eeprom.o
iwlagn-$(CONFIG_IWLWIFI_DEBUGFS) += iwl-agn-debugfs.o

iwlagn-$(CONFIG_IWL4965) += iwl-4965.o
iwlagn-$(CONFIG_IWL5000) += iwl-agn-rxon.o iwl-agn-hcmd.o iwl-agn-ict.o
iwlagn-$(CONFIG_IWL5000) += iwl-5000.o
iwlagn-$(CONFIG_IWL5000) += iwl-6000.o
iwlagn-$(CONFIG_IWL5000) += iwl-1000.o

# 3945
obj-$(CONFIG_IWL3945)	+= iwl3945.o
iwl3945-objs		:= iwl3945-base.o iwl-3945.o iwl-3945-rs.o iwl-3945-led.o
iwl3945-$(CONFIG_IWLWIFI_DEBUGFS) += iwl-3945-debugfs.o

ccflags-y += -D__CHECK_ENDIAN__
