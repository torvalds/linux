# SPDX-License-Identifier: GPL-2.0

obj-y				+= callchain.o callchain_$(BITS).o perf_regs.o
obj-$(CONFIG_COMPAT)		+= callchain_32.o

obj-$(CONFIG_PPC_PERF_CTRS)	+= core-book3s.o
obj64-$(CONFIG_PPC_PERF_CTRS)	+= ppc970-pmu.o power5-pmu.o \
				   power5+-pmu.o power6-pmu.o power7-pmu.o \
				   isa207-common.o power8-pmu.o power9-pmu.o \
				   generic-compat-pmu.o power10-pmu.o bhrb.o
obj32-$(CONFIG_PPC_PERF_CTRS)	+= mpc7450-pmu.o

obj-$(CONFIG_PPC_POWERNV)	+= imc-pmu.o
obj-$(CONFIG_FSL_EMB_PERF_EVENT) += core-fsl-emb.o
obj-$(CONFIG_FSL_EMB_PERF_EVENT_E500) += e500-pmu.o e6500-pmu.o

obj-$(CONFIG_HV_PERF_CTRS) += hv-24x7.o hv-gpci.o hv-common.o

obj-$(CONFIG_PPC_8xx) += 8xx-pmu.o

obj-$(CONFIG_PPC64)		+= $(obj64-y)
obj-$(CONFIG_PPC32)		+= $(obj32-y)
