# SPDX-License-Identifier: GPL-2.0-only

# Branch profiling isn't noinstr-safe
ccflags-$(CONFIG_TRACE_BRANCH_PROFILING) += -DDISABLE_BRANCH_PROFILING

obj-$(CONFIG_INTEL_IDLE)			+= intel_idle.o
