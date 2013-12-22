#
# Makefile for the kernel tpm device drivers.
#
obj-$(CONFIG_TCG_TPM) += tpm.o
tpm-y := tpm-interface.o
tpm-$(CONFIG_ACPI) += tpm_ppi.o

ifdef CONFIG_ACPI
	tpm-y += tpm_eventlog.o tpm_acpi.o
else
ifdef CONFIG_TCG_IBMVTPM
	tpm-y += tpm_eventlog.o tpm_of.o
endif
endif
obj-$(CONFIG_TCG_TIS) += tpm_tis.o
obj-$(CONFIG_TCG_TIS_I2C_ATMEL) += tpm_i2c_atmel.o
obj-$(CONFIG_TCG_TIS_I2C_INFINEON) += tpm_i2c_infineon.o
obj-$(CONFIG_TCG_TIS_I2C_NUVOTON) += tpm_i2c_nuvoton.o
obj-$(CONFIG_TCG_NSC) += tpm_nsc.o
obj-$(CONFIG_TCG_ATMEL) += tpm_atmel.o
obj-$(CONFIG_TCG_INFINEON) += tpm_infineon.o
obj-$(CONFIG_TCG_IBMVTPM) += tpm_ibmvtpm.o
obj-$(CONFIG_TCG_ST33_I2C) += tpm_i2c_stm_st33.o
obj-$(CONFIG_TCG_XEN) += xen-tpmfront.o
