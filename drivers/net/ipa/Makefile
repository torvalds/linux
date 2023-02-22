# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Qualcomm IPA driver.

IPA_VERSIONS		:=	3.1 3.5.1 4.2 4.5 4.7 4.9 4.11

# Some IPA versions can reuse another set of GSI register definitions.
GSI_IPA_VERSIONS	:=	3.1 3.5.1 4.0 4.5 4.9 4.11

obj-$(CONFIG_QCOM_IPA)	+=	ipa.o

ipa-y			:=	ipa_main.o ipa_power.o ipa_reg.o ipa_mem.o \
				ipa_table.o ipa_interrupt.o gsi.o gsi_reg.o \
				gsi_trans.o ipa_gsi.o ipa_smp2p.o ipa_uc.o \
				ipa_endpoint.o ipa_cmd.o ipa_modem.o \
				ipa_resource.o ipa_qmi.o ipa_qmi_msg.o \
				ipa_sysfs.o

ipa-y			+=	$(GSI_IPA_VERSIONS:%=reg/gsi_reg-v%.o)

ipa-y			+=	$(IPA_VERSIONS:%=reg/ipa_reg-v%.o)

ipa-y			+=	$(IPA_VERSIONS:%=data/ipa_data-v%.o)
