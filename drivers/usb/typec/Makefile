# SPDX-License-Identifier: GPL-2.0
# define_trace.h needs to know how to find our header
CFLAGS_tps6598x.o		:= -I$(src)

obj-$(CONFIG_TYPEC)		+= typec.o
typec-y				:= class.o mux.o bus.o
obj-$(CONFIG_TYPEC)		+= altmodes/
obj-$(CONFIG_TYPEC_TCPM)	+= tcpm/
obj-$(CONFIG_TYPEC_UCSI)	+= ucsi/
obj-$(CONFIG_TYPEC_HD3SS3220)	+= hd3ss3220.o
obj-$(CONFIG_TYPEC_TPS6598X)	+= tps6598x.o
obj-$(CONFIG_TYPEC_QCOM_PMIC)	+= qcom-pmic-typec.o
obj-$(CONFIG_TYPEC_STUSB160X) 	+= stusb160x.o
obj-$(CONFIG_TYPEC)		+= mux/
