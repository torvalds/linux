#
# Makefile for the linux kernel.
#

asflags-y := -ansi
ccflags-y := -Werror

extra-y     := head_$(BITS).o
extra-y     += init_task_$(BITS).o
extra-y     += vmlinux.lds

obj-$(CONFIG_SPARC32)   += entry.o wof.o wuf.o
obj-$(CONFIG_SPARC32)   += etrap_32.o
obj-$(CONFIG_SPARC32)   += rtrap_32.o
obj-y                   += traps_$(BITS).o

# IRQ
obj-y                   += irq_$(BITS).o
obj-$(CONFIG_SPARC32)   += sun4m_irq.o sun4c_irq.o sun4d_irq.o

obj-y                   += process_$(BITS).o
obj-y                   += signal_$(BITS).o
obj-$(CONFIG_SPARC32)   += ioport.o
obj-y                   += setup_$(BITS).o
obj-y                   += idprom_$(BITS).o
obj-y                   += sys_sparc_$(BITS).o
obj-$(CONFIG_SPARC32)   += systbls_32.o
obj-y                   += time_$(BITS).o
obj-$(CONFIG_SPARC32)   += windows.o
obj-y                   += cpu_$(BITS).o
obj-$(CONFIG_SPARC32)   += devices.o
obj-$(CONFIG_SPARC32)   += tadpole.o
obj-$(CONFIG_SPARC32)   += tick14.o
obj-y                   += ptrace_$(BITS).o
obj-y                   += unaligned_$(BITS).o
obj-y                   += una_asm_$(BITS).o
obj-$(CONFIG_SPARC32)   += muldiv.o
obj-y                   += prom_$(BITS).o
obj-y                   += of_device_$(BITS).o

# sparc32 do not use GENERIC_HARDIRQS but uses the generic devres implementation
obj-$(CONFIG_SPARC32)     += devres.o
devres-y                  := ../../../kernel/irq/devres.o

obj-$(CONFIG_SPARC32)     += dma.o

obj-$(CONFIG_SPARC32_PCI) += pcic.o

obj-$(CONFIG_SMP)         += trampoline_$(BITS).o smp_$(BITS).o
obj-$(CONFIG_SPARC32_SMP) += sun4m_smp.o sun4d_smp.o

obj-y                     += auxio_$(BITS).o
obj-$(CONFIG_SUN_PM)      += apc.o pmc.o

obj-$(CONFIG_MODULES)     += module_$(BITS).o
obj-$(CONFIG_MODULES)     += sparc_ksyms_$(BITS).o
obj-$(CONFIG_SPARC_LED)   += led.o
obj-$(CONFIG_KGDB)        += kgdb_$(BITS).o
