subdir-ccflags-$(CONFIG_PPC_WERROR) := -Werror

obj-$(CONFIG_PERF_EVENTS)	+= callchain.o

obj-$(CONFIG_PPC_PERF_CTRS)	+= core-book3s.o
obj64-$(CONFIG_PPC_PERF_CTRS)	+= power4-pmu.o ppc970-pmu.o power5-pmu.o \
				   power5+-pmu.o power6-pmu.o power7-pmu.o
obj32-$(CONFIG_PPC_PERF_CTRS)	+= mpc7450-pmu.o

obj-$(CONFIG_FSL_EMB_PERF_EVENT) += core-fsl-emb.o
obj-$(CONFIG_FSL_EMB_PERF_EVENT_E500) += e500-pmu.o

obj-$(CONFIG_PPC64)		+= $(obj64-y)
obj-$(CONFIG_PPC32)		+= $(obj32-y)
