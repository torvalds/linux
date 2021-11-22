# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the ipmi drivers.
#

ipmi_si-y := ipmi_si_intf.o ipmi_kcs_sm.o ipmi_smic_sm.o ipmi_bt_sm.o \
	ipmi_si_hotmod.o ipmi_si_hardcode.o ipmi_si_platform.o \
	ipmi_si_port_io.o ipmi_si_mem_io.o
ifdef CONFIG_PCI
ipmi_si-y += ipmi_si_pci.o
endif
ifdef CONFIG_PARISC
ipmi_si-y += ipmi_si_parisc.o
endif

obj-$(CONFIG_IPMI_HANDLER) += ipmi_msghandler.o
obj-$(CONFIG_IPMI_DEVICE_INTERFACE) += ipmi_devintf.o
obj-$(CONFIG_IPMI_SI) += ipmi_si.o
obj-$(CONFIG_IPMI_DMI_DECODE) += ipmi_dmi.o
obj-$(CONFIG_IPMI_PLAT_DATA) += ipmi_plat_data.o
obj-$(CONFIG_IPMI_SSIF) += ipmi_ssif.o
obj-$(CONFIG_IPMI_IPMB) += ipmi_ipmb.o
obj-$(CONFIG_IPMI_POWERNV) += ipmi_powernv.o
obj-$(CONFIG_IPMI_WATCHDOG) += ipmi_watchdog.o
obj-$(CONFIG_IPMI_POWEROFF) += ipmi_poweroff.o
obj-$(CONFIG_IPMI_KCS_BMC) += kcs_bmc.o
obj-$(CONFIG_IPMI_KCS_BMC_SERIO) += kcs_bmc_serio.o
obj-$(CONFIG_IPMI_KCS_BMC_CDEV_IPMI) += kcs_bmc_cdev_ipmi.o
obj-$(CONFIG_ASPEED_BT_IPMI_BMC) += bt-bmc.o
obj-$(CONFIG_ASPEED_KCS_IPMI_BMC) += kcs_bmc_aspeed.o
obj-$(CONFIG_NPCM7XX_KCS_IPMI_BMC) += kcs_bmc_npcm7xx.o
obj-$(CONFIG_IPMB_DEVICE_INTERFACE) += ipmb_dev_int.o
