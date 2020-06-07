# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for soundwire core
#

#Bus Objs
soundwire-bus-objs := bus_type.o bus.o master.o slave.o mipi_disco.o stream.o  \
			sysfs_slave.o sysfs_slave_dpn.o
obj-$(CONFIG_SOUNDWIRE) += soundwire-bus.o

ifdef CONFIG_DEBUG_FS
soundwire-bus-objs += debugfs.o
endif

#Cadence Objs
soundwire-cadence-objs := cadence_master.o
obj-$(CONFIG_SOUNDWIRE_CADENCE) += soundwire-cadence.o

#Intel driver
soundwire-intel-objs :=	intel.o intel_init.o
obj-$(CONFIG_SOUNDWIRE_INTEL) += soundwire-intel.o

#Qualcomm driver
soundwire-qcom-objs :=	qcom.o
obj-$(CONFIG_SOUNDWIRE_QCOM) += soundwire-qcom.o
