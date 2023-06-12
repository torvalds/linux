ccflags-y += -DDEFAULT_SYMBOL_NAMESPACE=IDXD

obj-$(CONFIG_INTEL_IDXD) += idxd.o
idxd-y := init.o irq.o device.o sysfs.o submit.o dma.o cdev.o debugfs.o

idxd-$(CONFIG_INTEL_IDXD_PERFMON) += perfmon.o

obj-$(CONFIG_INTEL_IDXD_BUS) += idxd_bus.o
idxd_bus-y := bus.o

obj-$(CONFIG_INTEL_IDXD_COMPAT) += idxd_compat.o
idxd_compat-y := compat.o
