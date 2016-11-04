
#
# Makefile for the linux kernel.
#

asflags-y := -ansi
ccflags-y := -Werror

extra-y     := head_$(BITS).o

# Undefine sparc when processing vmlinux.lds - it is used
# And teach CPP we are doing $(BITS) builds (for this case)
CPPFLAGS_vmlinux.lds := -Usparc -m$(BITS)
extra-y              += vmlinux.lds

ifdef CONFIG_FUNCTION_TRACER
# Do not profile debug and lowlevel utilities
CFLAGS_REMOVE_ftrace.o := -pg
CFLAGS_REMOVE_time_$(BITS).o := -pg
CFLAGS_REMOVE_perf_event.o := -pg
CFLAGS_REMOVE_pcr.o := -pg
endif

obj-$(CONFIG_SPARC64)   += urtt_fill.o
obj-$(CONFIG_SPARC32)   += entry.o wof.o wuf.o
obj-$(CONFIG_SPARC32)   += etrap_32.o
obj-$(CONFIG_SPARC32)   += rtrap_32.o
obj-y                   += traps_$(BITS).o

# IRQ
obj-y                   += irq_$(BITS).o
obj-$(CONFIG_SPARC32)   += sun4m_irq.o sun4d_irq.o

obj-y                   += process_$(BITS).o
obj-y                   += signal_$(BITS).o
obj-y                   += sigutil_$(BITS).o
obj-$(CONFIG_SPARC32)   += ioport.o
obj-y                   += setup_$(BITS).o
obj-y                   += idprom.o
obj-y                   += sys_sparc_$(BITS).o
obj-$(CONFIG_SPARC32)   += systbls_32.o
obj-y                   += time_$(BITS).o
obj-$(CONFIG_SPARC32)   += windows.o
obj-y                   += cpu.o
obj-$(CONFIG_SPARC32)   += devices.o
obj-y                   += ptrace_$(BITS).o
obj-y                   += unaligned_$(BITS).o
obj-y                   += una_asm_$(BITS).o
obj-y                   += prom_common.o
obj-y                   += prom_$(BITS).o
obj-y                   += of_device_common.o
obj-y                   += of_device_$(BITS).o
obj-$(CONFIG_SPARC64)   += prom_irqtrans.o

obj-$(CONFIG_SPARC32)   += leon_kernel.o
obj-$(CONFIG_SPARC32)   += leon_pmc.o

obj-$(CONFIG_SPARC64)   += reboot.o
obj-$(CONFIG_SPARC64)   += sysfs.o
obj-$(CONFIG_SPARC64)   += iommu.o
obj-$(CONFIG_SPARC64)   += central.o
obj-$(CONFIG_SPARC64)   += starfire.o
obj-$(CONFIG_SPARC64)   += power.o
obj-$(CONFIG_SPARC64)   += sbus.o
obj-$(CONFIG_SPARC64)   += ebus.o
obj-$(CONFIG_SPARC64)   += visemul.o
obj-$(CONFIG_SPARC64)   += hvapi.o
obj-$(CONFIG_SPARC64)   += sstate.o
obj-$(CONFIG_SPARC64)   += mdesc.o
obj-$(CONFIG_SPARC64)	+= pcr.o
obj-$(CONFIG_SPARC64)	+= nmi.o
obj-$(CONFIG_SPARC64_SMP) += cpumap.o

obj-y                     += dma.o

obj-$(CONFIG_PCIC_PCI)    += pcic.o
obj-$(CONFIG_LEON_PCI)    += leon_pci.o
obj-$(CONFIG_SPARC_GRPCI2)+= leon_pci_grpci2.o
obj-$(CONFIG_SPARC_GRPCI1)+= leon_pci_grpci1.o

obj-$(CONFIG_SMP)         += trampoline_$(BITS).o smp_$(BITS).o
obj-$(CONFIG_SPARC32_SMP) += sun4m_smp.o sun4d_smp.o leon_smp.o
obj-$(CONFIG_SPARC64_SMP) += hvtramp.o

obj-y                     += auxio_$(BITS).o
obj-$(CONFIG_SUN_PM)      += apc.o pmc.o

obj-$(CONFIG_MODULES)     += module.o
obj-$(CONFIG_MODULES)     += sparc_ksyms.o
obj-$(CONFIG_SPARC_LED)   += led.o
obj-$(CONFIG_KGDB)        += kgdb_$(BITS).o


obj-$(CONFIG_DYNAMIC_FTRACE) += ftrace.o
obj-$(CONFIG_FUNCTION_GRAPH_TRACER) += ftrace.o

obj-$(CONFIG_EARLYFB) += btext.o
obj-$(CONFIG_STACKTRACE)     += stacktrace.o
# sparc64 PCI
obj-$(CONFIG_SPARC64_PCI)    += pci.o pci_common.o psycho_common.o
obj-$(CONFIG_SPARC64_PCI)    += pci_psycho.o pci_sabre.o pci_schizo.o
obj-$(CONFIG_SPARC64_PCI)    += pci_sun4v.o pci_sun4v_asm.o pci_fire.o
obj-$(CONFIG_SPARC64_PCI_MSI) += pci_msi.o

obj-$(CONFIG_COMPAT)         += sys32.o sys_sparc32.o signal32.o

obj-$(CONFIG_US3_MC)    += chmc.o

obj-$(CONFIG_KPROBES)   += kprobes.o
obj-$(CONFIG_SUN_LDOMS) += ldc.o vio.o viohs.o ds.o

obj-$(CONFIG_AUDIT)     += audit.o
audit--$(CONFIG_AUDIT)  := compat_audit.o
obj-$(CONFIG_COMPAT)    += $(audit--y)

pc--$(CONFIG_PERF_EVENTS) := perf_event.o
obj-$(CONFIG_SPARC64)	+= $(pc--y)

obj-$(CONFIG_SPARC64)	+= jump_label.o
