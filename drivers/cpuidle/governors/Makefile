# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for cpuidle governors.
#

CFLAGS_qcom_lpm.o := -I$(src)
obj-$(CONFIG_CPU_IDLE_GOV_LADDER) += ladder.o
obj-$(CONFIG_CPU_IDLE_GOV_MENU) += menu.o
obj-$(CONFIG_CPU_IDLE_SIMPLE_GOV_QCOM_LPM) += qcom_simple_lpm.o
qcom_simple_lpm-y += qcom-simple-lpm.o
qcom_simple_lpm-y += qcom-simple-cluster-lpm.o
qcom_simple_lpm-y += qcom-simple-lpm-sysfs.o
obj-$(CONFIG_CPU_IDLE_GOV_QCOM_LPM) += qcom_lpm.o
qcom_lpm-y += qcom-lpm.o
qcom_lpm-y += qcom-cluster-lpm.o
qcom_lpm-y += qcom-lpm-sysfs.o
obj-$(CONFIG_CPU_IDLE_GOV_TEO) += teo.o
obj-$(CONFIG_CPU_IDLE_GOV_HALTPOLL) += haltpoll.o
