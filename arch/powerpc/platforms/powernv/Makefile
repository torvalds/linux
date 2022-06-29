# SPDX-License-Identifier: GPL-2.0

# nothing that deals with real mode is safe to KASAN
# in particular, idle code runs a bunch of things in real mode
KASAN_SANITIZE_idle.o := n
KASAN_SANITIZE_pci-ioda.o := n
# pnv_machine_check_early
KASAN_SANITIZE_setup.o := n

obj-y			+= setup.o opal-call.o opal-wrappers.o opal.o opal-async.o
obj-y			+= idle.o opal-rtc.o opal-nvram.o opal-lpc.o opal-flash.o
obj-y			+= rng.o opal-elog.o opal-dump.o opal-sysparam.o opal-sensor.o
obj-y			+= opal-msglog.o opal-hmi.o opal-power.o opal-irqchip.o
obj-y			+= opal-kmsg.o opal-powercap.o opal-psr.o opal-sensor-groups.o
obj-y			+= ultravisor.o

obj-$(CONFIG_SMP)	+= smp.o subcore.o subcore-asm.o
obj-$(CONFIG_FA_DUMP)	+= opal-fadump.o
obj-$(CONFIG_PRESERVE_FA_DUMP)	+= opal-fadump.o
obj-$(CONFIG_OPAL_CORE)	+= opal-core.o
obj-$(CONFIG_PCI)	+= pci.o pci-ioda.o pci-ioda-tce.o
obj-$(CONFIG_PCI_IOV)   += pci-sriov.o
obj-$(CONFIG_CXL_BASE)	+= pci-cxl.o
obj-$(CONFIG_EEH)	+= eeh-powernv.o
obj-$(CONFIG_MEMORY_FAILURE)	+= opal-memory-errors.o
obj-$(CONFIG_OPAL_PRD)	+= opal-prd.o
obj-$(CONFIG_PERF_EVENTS) += opal-imc.o
obj-$(CONFIG_PPC_MEMTRACE)	+= memtrace.o
obj-$(CONFIG_PPC_VAS)	+= vas.o vas-window.o vas-debug.o vas-fault.o
obj-$(CONFIG_OCXL_BASE)	+= ocxl.o
obj-$(CONFIG_SCOM_DEBUGFS) += opal-xscom.o
obj-$(CONFIG_PPC_SECURE_BOOT) += opal-secvar.o
