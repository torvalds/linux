# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#
obj-$(CONFIG_ARM_PSCI_FW)	+= psci.o
obj-$(CONFIG_ARM_PSCI_CHECKER)	+= psci_checker.o
obj-$(CONFIG_ARM_SCPI_PROTOCOL)	+= arm_scpi.o
obj-$(CONFIG_ARM_SCPI_POWER_DOMAIN) += scpi_pm_domain.o
obj-$(CONFIG_ARM_SDE_INTERFACE)	+= arm_sdei.o
obj-$(CONFIG_DMI)		+= dmi_scan.o
obj-$(CONFIG_DMI_SYSFS)		+= dmi-sysfs.o
obj-$(CONFIG_EDD)		+= edd.o
obj-$(CONFIG_EFI_PCDP)		+= pcdp.o
obj-$(CONFIG_DELL_RBU)          += dell_rbu.o
obj-$(CONFIG_DCDBAS)		+= dcdbas.o
obj-$(CONFIG_DMIID)		+= dmi-id.o
obj-$(CONFIG_ISCSI_IBFT_FIND)	+= iscsi_ibft_find.o
obj-$(CONFIG_ISCSI_IBFT)	+= iscsi_ibft.o
obj-$(CONFIG_FIRMWARE_MEMMAP)	+= memmap.o
obj-$(CONFIG_RASPBERRYPI_FIRMWARE) += raspberrypi.o
obj-$(CONFIG_ROCKCHIP_SIP)	+= rockchip_sip.o
obj-$(CONFIG_FW_CFG_SYSFS)	+= qemu_fw_cfg.o
obj-$(CONFIG_QCOM_SCM)		+= qcom_scm.o
obj-$(CONFIG_QCOM_SCM_64)	+= qcom_scm-64.o
obj-$(CONFIG_QCOM_SCM_32)	+= qcom_scm-32.o
CFLAGS_qcom_scm-32.o :=$(call as-instr,.arch armv7-a\n.arch_extension sec,-DREQUIRES_SEC=1) -march=armv7-a
obj-$(CONFIG_TI_SCI_PROTOCOL)	+= ti_sci.o

obj-$(CONFIG_ARM_SCMI_PROTOCOL)	+= arm_scmi/
obj-y				+= broadcom/
obj-y				+= meson/
obj-$(CONFIG_GOOGLE_FIRMWARE)	+= google/
obj-$(CONFIG_EFI)		+= efi/
obj-$(CONFIG_UEFI_CPER)		+= efi/
obj-y				+= tegra/
