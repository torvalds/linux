# SPDX-License-Identifier: GPL-2.0
#
# Makefile for linux/drivers/platform/x86/intel/pmt
# Intel Platform Monitoring Technology Drivers
#

obj-$(CONFIG_INTEL_PMT_CLASS)		+= pmt_class.o
pmt_class-y				:= class.o
obj-$(CONFIG_INTEL_PMT_TELEMETRY)	+= pmt_telemetry.o
pmt_telemetry-y				:= telemetry.o
obj-$(CONFIG_INTEL_PMT_CRASHLOG)	+= pmt_crashlog.o
pmt_crashlog-y				:= crashlog.o
obj-$(CONFIG_INTEL_PMT_DISCOVERY)	+= pmt_discovery.o
pmt_discovery-y				:= discovery.o features.o
obj-$(CONFIG_INTEL_PMT_KUNIT_TEST)	+= pmt-discovery-kunit.o
pmt-discovery-kunit-y			:= discovery-kunit.o
