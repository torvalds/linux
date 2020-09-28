# SPDX-License-Identifier: GPL-2.0
#
# Makefile for CoreSight drivers.
#
obj-$(CONFIG_CORESIGHT) += coresight.o coresight-etm-perf.o \
			   coresight-platform.o coresight-sysfs.o
obj-$(CONFIG_CORESIGHT_LINK_AND_SINK_TMC) += coresight-tmc.o \
					     coresight-tmc-etf.o \
					     coresight-tmc-etr.o
obj-$(CONFIG_CORESIGHT_SINK_TPIU) += coresight-tpiu.o
obj-$(CONFIG_CORESIGHT_SINK_ETBV10) += coresight-etb10.o
obj-$(CONFIG_CORESIGHT_LINKS_AND_SINKS) += coresight-funnel.o \
					   coresight-replicator.o
obj-$(CONFIG_CORESIGHT_SOURCE_ETM3X) += coresight-etm3x.o
coresight-etm3x-y := coresight-etm3x-core.o coresight-etm-cp14.o \
		     coresight-etm3x-sysfs.o
obj-$(CONFIG_CORESIGHT_SOURCE_ETM4X) += coresight-etm4x.o \
					coresight-etm4x-sysfs.o
obj-$(CONFIG_CORESIGHT_STM) += coresight-stm.o
obj-$(CONFIG_CORESIGHT_CPU_DEBUG) += coresight-cpu-debug.o
obj-$(CONFIG_CORESIGHT_CATU) += coresight-catu.o
obj-$(CONFIG_CORESIGHT_CTI) += coresight-cti.o \
				coresight-cti-platform.o \
				coresight-cti-sysfs.o
