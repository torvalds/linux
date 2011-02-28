# AGN
obj-$(CONFIG_IWLAGN)	+= iwlagn.o
iwlagn-objs		:= iwl-agn.o iwl-agn-rs.o iwl-agn-led.o
iwlagn-objs		+= iwl-agn-ucode.o iwl-agn-tx.o
iwlagn-objs		+= iwl-agn-lib.o iwl-agn-calib.o
iwlagn-objs		+= iwl-agn-tt.o iwl-agn-sta.o iwl-agn-eeprom.o

iwlagn-objs 		+= iwl-core.o iwl-eeprom.o iwl-hcmd.o iwl-power.o
iwlagn-objs 		+= iwl-rx.o iwl-tx.o iwl-sta.o
iwlagn-objs 		+= iwl-scan.o iwl-led.o
iwlagn-objs             += iwl-agn-rxon.o iwl-agn-hcmd.o iwl-agn-ict.o
iwlagn-objs             += iwl-5000.o
iwlagn-objs             += iwl-6000.o
iwlagn-objs             += iwl-1000.o
iwlagn-objs             += iwl-2000.o

iwlagn-$(CONFIG_IWLWIFI_DEBUGFS) += iwl-agn-debugfs.o
iwlagn-$(CONFIG_IWLWIFI_DEBUGFS) += iwl-debugfs.o
iwlagn-$(CONFIG_IWLWIFI_DEVICE_TRACING) += iwl-devtrace.o

CFLAGS_iwl-devtrace.o := -I$(src)

ccflags-y += -D__CHECK_ENDIAN__
