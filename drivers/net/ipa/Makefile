# Un-comment the next line if you want to validate configuration data
#ccflags-y		+=	-DIPA_VALIDATE

obj-$(CONFIG_QCOM_IPA)	+=	ipa.o

ipa-y			:=	ipa_main.o ipa_clock.o ipa_reg.o ipa_mem.o \
				ipa_table.o ipa_interrupt.o gsi.o gsi_trans.o \
				ipa_gsi.o ipa_smp2p.o ipa_uc.o \
				ipa_endpoint.o ipa_cmd.o ipa_modem.o \
				ipa_resource.o ipa_qmi.o ipa_qmi_msg.o \
				ipa_sysfs.o

ipa-y			+=	ipa_data-v3.1.o ipa_data-v3.5.1.o \
				ipa_data-v4.2.o ipa_data-v4.5.o \
				ipa_data-v4.9.o ipa_data-v4.11.o
