# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel tpm device drivers.
#
obj-$(CONFIG_TCG_TPM) += tpm.o
tpm-y := tpm-interface.o tpm-dev.o tpm-sysfs.o tpm-chip.o tpm2-cmd.o \
	 tpm-dev-common.o tpmrm-dev.o tpm1_eventlog.o tpm2_eventlog.o \
         tpm2-space.o
tpm-$(CONFIG_ACPI) += tpm_ppi.o tpm_acpi.o
tpm-$(CONFIG_OF) += tpm_of.o
obj-$(CONFIG_TCG_TIS_CORE) += tpm_tis_core.o
obj-$(CONFIG_TCG_TIS) += tpm_tis.o
obj-$(CONFIG_TCG_TIS_SPI) += tpm_tis_spi.o
obj-$(CONFIG_TCG_TIS_I2C_ATMEL) += tpm_i2c_atmel.o
obj-$(CONFIG_TCG_TIS_I2C_INFINEON) += tpm_i2c_infineon.o
obj-$(CONFIG_TCG_TIS_I2C_NUVOTON) += tpm_i2c_nuvoton.o
obj-$(CONFIG_TCG_NSC) += tpm_nsc.o
obj-$(CONFIG_TCG_ATMEL) += tpm_atmel.o
obj-$(CONFIG_TCG_INFINEON) += tpm_infineon.o
obj-$(CONFIG_TCG_IBMVTPM) += tpm_ibmvtpm.o
obj-$(CONFIG_TCG_TIS_ST33ZP24) += st33zp24/
obj-$(CONFIG_TCG_XEN) += xen-tpmfront.o
obj-$(CONFIG_TCG_CRB) += tpm_crb.o
obj-$(CONFIG_TCG_VTPM_PROXY) += tpm_vtpm_proxy.o
