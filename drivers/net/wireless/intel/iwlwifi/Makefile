# SPDX-License-Identifier: GPL-2.0
# common
obj-$(CONFIG_IWLWIFI)	+= iwlwifi.o
iwlwifi-objs		+= iwl-io.o
iwlwifi-objs		+= iwl-drv.o
iwlwifi-objs		+= iwl-debug.o
iwlwifi-objs		+= iwl-eeprom-read.o iwl-eeprom-parse.o
iwlwifi-objs		+= iwl-phy-db.o iwl-nvm-parse.o
iwlwifi-objs		+= pcie/drv.o pcie/rx.o pcie/tx.o pcie/trans.o
iwlwifi-objs		+= pcie/ctxt-info.o pcie/ctxt-info-gen3.o
iwlwifi-objs		+= pcie/trans-gen2.o pcie/tx-gen2.o
iwlwifi-$(CONFIG_IWLDVM) += cfg/1000.o cfg/2000.o cfg/5000.o cfg/6000.o
iwlwifi-$(CONFIG_IWLMVM) += cfg/7000.o cfg/8000.o cfg/9000.o cfg/22000.o
iwlwifi-objs		+= iwl-dbg-tlv.o
iwlwifi-objs		+= iwl-trans.o
iwlwifi-objs		+= queue/tx.o

iwlwifi-objs		+= fw/img.o fw/notif-wait.o fw/rs.o
iwlwifi-objs		+= fw/dbg.o fw/pnvm.o fw/dump.o
iwlwifi-$(CONFIG_IWLMVM) += fw/paging.o fw/smem.o fw/init.o
iwlwifi-$(CONFIG_ACPI) += fw/acpi.o
iwlwifi-$(CONFIG_EFI)	+= fw/uefi.o
iwlwifi-$(CONFIG_IWLWIFI_DEBUGFS) += fw/debugfs.o

iwlwifi-objs += $(iwlwifi-m)

iwlwifi-$(CONFIG_IWLWIFI_DEVICE_TRACING) += iwl-devtrace.o

ccflags-y += -I$(src)

obj-$(CONFIG_IWLDVM)	+= dvm/
obj-$(CONFIG_IWLMVM)	+= mvm/

CFLAGS_iwl-devtrace.o := -I$(src)
