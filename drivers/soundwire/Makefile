# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for soundwire core
#

#Bus Objs
soundwire-bus-y := bus_type.o bus.o master.o slave.o mipi_disco.o stream.o  \
			sysfs_slave.o sysfs_slave_dpn.o
obj-$(CONFIG_SOUNDWIRE) += soundwire-bus.o

soundwire-generic-allocation-objs := generic_bandwidth_allocation.o
obj-$(CONFIG_SOUNDWIRE_GENERIC_ALLOCATION) += soundwire-generic-allocation.o

ifdef CONFIG_DEBUG_FS
soundwire-bus-y += debugfs.o
endif

#Cadence Objs
soundwire-cadence-y := cadence_master.o
obj-$(CONFIG_SOUNDWIRE_CADENCE) += soundwire-cadence.o

#Intel driver
soundwire-intel-y :=	intel.o intel_auxdevice.o intel_init.o dmi-quirks.o
obj-$(CONFIG_SOUNDWIRE_INTEL) += soundwire-intel.o

#Qualcomm driver
soundwire-qcom-y :=	qcom.o
obj-$(CONFIG_SOUNDWIRE_QCOM) += soundwire-qcom.o
