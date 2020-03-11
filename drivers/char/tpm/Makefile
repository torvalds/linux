# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the kernel tpm device drivers.
#
obj-$(CONFIG_TCG_TPM) += tpm.o
tpm-y := tpm-chip.o
tpm-y += tpm-dev-common.o
tpm-y += tpm-dev.o
tpm-y += tpm-interface.o
tpm-y += tpm1-cmd.o
tpm-y += tpm2-cmd.o
tpm-y += tpmrm-dev.o
tpm-y += tpm2-space.o
tpm-y += tpm-sysfs.o
tpm-y += eventlog/common.o
tpm-y += eventlog/tpm1.o
tpm-y += eventlog/tpm2.o

tpm-$(CONFIG_ACPI) += tpm_ppi.o eventlog/acpi.o
tpm-$(CONFIG_EFI) += eventlog/efi.o
tpm-$(CONFIG_OF) += eventlog/of.o
obj-$(CONFIG_TCG_TIS_CORE) += tpm_tis_core.o
obj-$(CONFIG_TCG_TIS) += tpm_tis.o

obj-$(CONFIG_TCG_TIS_SPI) += tpm_tis_spi.o
tpm_tis_spi-y := tpm_tis_spi_main.o
tpm_tis_spi-$(CONFIG_TCG_TIS_SPI_CR50) += tpm_tis_spi_cr50.o

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
obj-$(CONFIG_TCG_FTPM_TEE) += tpm_ftpm_tee.o
