#
# Makefile for the Linux/MIPS kernel.
#

extra-y		:= head.o init_task.o vmlinux.lds

obj-y		+= cpu-probe.o branch.o entry.o genex.o irq.o process.o \
		   ptrace.o reset.o setup.o signal.o syscall.o \
		   time.o topology.o traps.o unaligned.o watch.o vdso.o

ifdef CONFIG_FUNCTION_TRACER
CFLAGS_REMOVE_ftrace.o = -pg
CFLAGS_REMOVE_early_printk.o = -pg
CFLAGS_REMOVE_perf_event.o = -pg
CFLAGS_REMOVE_perf_event_mipsxx.o = -pg
endif

obj-$(CONFIG_CEVT_BCM1480)	+= cevt-bcm1480.o
obj-$(CONFIG_CEVT_R4K_LIB)	+= cevt-r4k.o
obj-$(CONFIG_MIPS_MT_SMTC)	+= cevt-smtc.o
obj-$(CONFIG_CEVT_DS1287)	+= cevt-ds1287.o
obj-$(CONFIG_CEVT_GT641XX)	+= cevt-gt641xx.o
obj-$(CONFIG_CEVT_SB1250)	+= cevt-sb1250.o
obj-$(CONFIG_CEVT_TXX9)		+= cevt-txx9.o
obj-$(CONFIG_CSRC_BCM1480)	+= csrc-bcm1480.o
obj-$(CONFIG_CSRC_IOASIC)	+= csrc-ioasic.o
obj-$(CONFIG_CSRC_POWERTV)	+= csrc-powertv.o
obj-$(CONFIG_CSRC_R4K_LIB)	+= csrc-r4k.o
obj-$(CONFIG_CSRC_SB1250)	+= csrc-sb1250.o
obj-$(CONFIG_SYNC_R4K)		+= sync-r4k.o

obj-$(CONFIG_STACKTRACE)	+= stacktrace.o
obj-$(CONFIG_MODULES)		+= mips_ksyms.o module.o

obj-$(CONFIG_FUNCTION_TRACER)	+= mcount.o ftrace.o

obj-$(CONFIG_CPU_LOONGSON2)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_MIPS32)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_MIPS64)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R3000)		+= r2300_fpu.o r2300_switch.o
obj-$(CONFIG_CPU_R4300)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R4X00)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R5000)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R6000)		+= r6000_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R5432)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R5500)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R8000)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_RM7000)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_RM9000)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_NEVADA)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_R10000)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_SB1)		+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_TX39XX)	+= r2300_fpu.o r2300_switch.o
obj-$(CONFIG_CPU_TX49XX)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_VR41XX)	+= r4k_fpu.o r4k_switch.o
obj-$(CONFIG_CPU_CAVIUM_OCTEON)	+= octeon_switch.o
obj-$(CONFIG_CPU_XLR)		+= r4k_fpu.o r4k_switch.o

obj-$(CONFIG_SMP)		+= smp.o
obj-$(CONFIG_SMP_UP)		+= smp-up.o

obj-$(CONFIG_MIPS_MT)		+= mips-mt.o
obj-$(CONFIG_MIPS_MT_FPAFF)	+= mips-mt-fpaff.o
obj-$(CONFIG_MIPS_MT_SMTC)	+= smtc.o smtc-asm.o smtc-proc.o
obj-$(CONFIG_MIPS_MT_SMP)	+= smp-mt.o
obj-$(CONFIG_MIPS_CMP)		+= smp-cmp.o
obj-$(CONFIG_CPU_MIPSR2)	+= spram.o

obj-$(CONFIG_MIPS_VPE_LOADER)	+= vpe.o
obj-$(CONFIG_MIPS_VPE_APSP_API)	+= rtlx.o
obj-$(CONFIG_MIPS_APSP_KSPD)	+= kspd.o

obj-$(CONFIG_I8259)		+= i8259.o
obj-$(CONFIG_IRQ_CPU)		+= irq_cpu.o
obj-$(CONFIG_IRQ_CPU_RM7K)	+= irq-rm7000.o
obj-$(CONFIG_IRQ_CPU_RM9K)	+= irq-rm9000.o
obj-$(CONFIG_MIPS_MSC)		+= irq-msc01.o
obj-$(CONFIG_IRQ_TXX9)		+= irq_txx9.o
obj-$(CONFIG_IRQ_GT641XX)	+= irq-gt641xx.o
obj-$(CONFIG_IRQ_GIC)		+= irq-gic.o

obj-$(CONFIG_KPROBES)		+= kprobes.o
obj-$(CONFIG_32BIT)		+= scall32-o32.o
obj-$(CONFIG_64BIT)		+= scall64-64.o
obj-$(CONFIG_MIPS32_COMPAT)	+= linux32.o ptrace32.o signal32.o
obj-$(CONFIG_MIPS32_N32)	+= binfmt_elfn32.o scall64-n32.o signal_n32.o
obj-$(CONFIG_MIPS32_O32)	+= binfmt_elfo32.o scall64-o32.o

obj-$(CONFIG_KGDB)		+= kgdb.o
obj-$(CONFIG_PROC_FS)		+= proc.o

obj-$(CONFIG_64BIT)		+= cpu-bugs64.o

obj-$(CONFIG_I8253)		+= i8253.o

obj-$(CONFIG_GPIO_TXX9)		+= gpio_txx9.o

obj-$(CONFIG_KEXEC)		+= machine_kexec.o relocate_kernel.o
obj-$(CONFIG_EARLY_PRINTK)	+= early_printk.o
obj-$(CONFIG_SPINLOCK_TEST)	+= spinlock_test.o
obj-$(CONFIG_MIPS_MACHINE)	+= mips_machine.o

obj-$(CONFIG_OF)		+= prom.o

CFLAGS_cpu-bugs64.o	= $(shell if $(CC) $(KBUILD_CFLAGS) -Wa,-mdaddi -c -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-DHAVE_AS_SET_DADDI"; fi)

obj-$(CONFIG_HAVE_STD_PC_SERIAL_PORT)	+= 8250-platform.o

obj-$(CONFIG_MIPS_CPUFREQ)	+= cpufreq/

obj-$(CONFIG_PERF_EVENTS)	+= perf_event.o
obj-$(CONFIG_HW_PERF_EVENTS)	+= perf_event_mipsxx.o

obj-$(CONFIG_JUMP_LABEL)	+= jump_label.o

CPPFLAGS_vmlinux.lds		:= $(KBUILD_CFLAGS)
