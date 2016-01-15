obj-y			+= setup.o opal-wrappers.o opal.o opal-async.o idle.o
obj-y			+= opal-rtc.o opal-nvram.o opal-lpc.o opal-flash.o
obj-y			+= rng.o opal-elog.o opal-dump.o opal-sysparam.o opal-sensor.o
obj-y			+= opal-msglog.o opal-hmi.o opal-power.o opal-irqchip.o
obj-y			+= opal-kmsg.o

obj-$(CONFIG_SMP)	+= smp.o subcore.o subcore-asm.o
obj-$(CONFIG_PCI)	+= pci.o pci-p5ioc2.o pci-ioda.o npu-dma.o
obj-$(CONFIG_EEH)	+= eeh-powernv.o
obj-$(CONFIG_PPC_SCOM)	+= opal-xscom.o
obj-$(CONFIG_MEMORY_FAILURE)	+= opal-memory-errors.o
obj-$(CONFIG_TRACEPOINTS)	+= opal-tracepoints.o
obj-$(CONFIG_OPAL_PRD)	+= opal-prd.o
