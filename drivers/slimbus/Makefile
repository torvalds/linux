# SPDX-License-Identifier: GPL-2.0
#
# Makefile for kernel SLIMbus framework.
#
obj-$(CONFIG_SLIMBUS)			+= slimbus.o
slimbus-y				:= core.o messaging.o sched.o

#Controllers
obj-$(CONFIG_SLIM_QCOM_CTRL)		+= slim-qcom-ctrl.o
slim-qcom-ctrl-y			:= qcom-ctrl.o
