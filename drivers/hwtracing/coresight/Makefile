# SPDX-License-Identifier: GPL-2.0
#
# Makefile for CoreSight drivers.
#
CFLAGS_coresight-stm.o := -D__DISABLE_TRACE_MMIO__

obj-$(CONFIG_CORESIGHT) += coresight.o
coresight-y := coresight-core.o  coresight-etm-perf.o coresight-platform.o \
		coresight-sysfs.o coresight-syscfg.o coresight-config.o \
		coresight-cfg-preload.o coresight-cfg-afdo.o \
		coresight-syscfg-configfs.o
obj-$(CONFIG_CORESIGHT_LINK_AND_SINK_TMC) += coresight-tmc.o
coresight-tmc-y := coresight-tmc-core.o coresight-tmc-etf.o \
		      coresight-tmc-etr.o coresight-byte-cntr.o \
		      coresight-tmc-usb.o
obj-$(CONFIG_CORESIGHT_SINK_TPIU) += coresight-tpiu.o
obj-$(CONFIG_CORESIGHT_SINK_ETBV10) += coresight-etb10.o
obj-$(CONFIG_CORESIGHT_LINKS_AND_SINKS) += coresight-funnel.o \
					   coresight-replicator.o
obj-$(CONFIG_CORESIGHT_SOURCE_ETM3X) += coresight-etm3x.o
coresight-etm3x-y := coresight-etm3x-core.o coresight-etm-cp14.o \
		     coresight-etm3x-sysfs.o
obj-$(CONFIG_CORESIGHT_SOURCE_ETM4X) += coresight-etm4x.o
coresight-etm4x-y := coresight-etm4x-core.o coresight-etm4x-sysfs.o \
			coresight-etm4x-cfg.o
obj-$(CONFIG_CORESIGHT_STM) += coresight-stm.o
obj-$(CONFIG_CORESIGHT_CPU_DEBUG) += coresight-cpu-debug.o
obj-$(CONFIG_CORESIGHT_CATU) += coresight-catu.o
obj-$(CONFIG_CORESIGHT_CTI) += coresight-cti.o
obj-$(CONFIG_CORESIGHT_TRBE) += coresight-trbe.o
coresight-cti-y := coresight-cti-core.o	coresight-cti-platform.o \
		   coresight-cti-sysfs.o
obj-$(CONFIG_CORESIGHT_DUMMY) += coresight-dummy.o
obj-$(CONFIG_CORESIGHT_REMOTE_ETM) += coresight-remote-etm.o coresight-tmc-sec.o
obj-$(CONFIG_CORESIGHT_TRACE_NOC) += coresight-trace-noc.o
obj-$(CONFIG_CORESIGHT_TGU) += coresight-tgu.o
obj-$(CONFIG_CORESIGHT_HWEVENT) += coresight-hwevent.o
obj-$(CONFIG_CORESIGHT_DBGUI) += coresight-dbgui.o
obj-$(CONFIG_CORESIGHT_TPDA) += coresight-tpda.o
obj-$(CONFIG_CORESIGHT_TPDM) += coresight-tpdm.o
obj-$(CONFIG_CORESIGHT_CSR) += coresight-csr.o
