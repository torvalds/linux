# SPDX-License-Identifier: GPL-2.0
#
# Makefile for CoreSight drivers.
#

# Current W=1 warnings
subdir-ccflags-y += -Wextra -Wunused -Wno-unused-parameter
subdir-ccflags-y += -Wmissing-declarations
subdir-ccflags-y += -Wmissing-format-attribute
subdir-ccflags-y += -Wmissing-prototypes
subdir-ccflags-y += -Wold-style-definition
subdir-ccflags-y += -Wmissing-include-dirs
subdir-ccflags-y += -Wno-sign-compare
condflags := \
	$(call cc-option, -Wrestrict)				\
	$(call cc-option, -Wunused-but-set-variable)		\
	$(call cc-option, -Wunused-const-variable)		\
	$(call cc-option, -Wpacked-not-aligned)			\
	$(call cc-option, -Wformat-overflow)			\
	$(call cc-option, -Wformat-truncation)			\
	$(call cc-option, -Wstringop-overflow)			\
	$(call cc-option, -Wstringop-truncation)
subdir-ccflags-y += $(condflags)

obj-$(CONFIG_CORESIGHT) += coresight.o
coresight-y := coresight-core.o  coresight-etm-perf.o coresight-platform.o \
		coresight-sysfs.o coresight-syscfg.o coresight-config.o \
		coresight-cfg-preload.o coresight-cfg-afdo.o \
		coresight-syscfg-configfs.o coresight-trace-id.o
obj-$(CONFIG_CORESIGHT_LINK_AND_SINK_TMC) += coresight-tmc.o
coresight-tmc-y := coresight-tmc-core.o coresight-tmc-etf.o \
		      coresight-tmc-etr.o
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
obj-$(CONFIG_CORESIGHT_TPDM) += coresight-tpdm.o
obj-$(CONFIG_CORESIGHT_TPDA) += coresight-tpda.o
coresight-cti-y := coresight-cti-core.o	coresight-cti-platform.o \
		   coresight-cti-sysfs.o
obj-$(CONFIG_ULTRASOC_SMB) += ultrasoc-smb.o
obj-$(CONFIG_CORESIGHT_DUMMY) += coresight-dummy.o
