# common
obj-$(CONFIG_IWLWIFI)	+= iwlwifi.o
iwlwifi-objs		+= iwl-io.o
iwlwifi-objs		+= iwl-drv.o
iwlwifi-objs		+= iwl-debug.o
iwlwifi-objs		+= iwl-notif-wait.o
iwlwifi-objs		+= iwl-eeprom-read.o iwl-eeprom-parse.o
iwlwifi-objs		+= pcie/drv.o pcie/rx.o pcie/tx.o pcie/trans.o
iwlwifi-objs		+= pcie/1000.o pcie/2000.o pcie/5000.o pcie/6000.o

iwlwifi-$(CONFIG_IWLWIFI_DEVICE_TRACING) += iwl-devtrace.o
iwlwifi-$(CONFIG_IWLWIFI_DEVICE_TESTMODE) += iwl-test.o

ccflags-y += -D__CHECK_ENDIAN__ -I$(src)


obj-$(CONFIG_IWLDVM)	+= dvm/

CFLAGS_iwl-devtrace.o := -I$(src)
