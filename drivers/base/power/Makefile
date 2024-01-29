# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_PM)	+= sysfs.o generic_ops.o common.o qos.o runtime.o wakeirq.o
obj-$(CONFIG_PM_SLEEP)	+= main.o wakeup.o wakeup_stats.o
obj-$(CONFIG_PM_TRACE_RTC)	+= trace.o
obj-$(CONFIG_HAVE_CLK)	+= clock_ops.o
obj-$(CONFIG_PM_QOS_KUNIT_TEST) += qos-test.o

ccflags-$(CONFIG_DEBUG_DRIVER) := -DDEBUG
