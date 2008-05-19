#
# Makefile for the linux kernel.
#

extra-y		:= head.o init_task.o vmlinux.lds

EXTRA_AFLAGS	:= -ansi

IRQ_OBJS := irq.o sun4m_irq.o sun4c_irq.o sun4d_irq.o
obj-y    := entry.o wof.o wuf.o etrap.o rtrap.o traps.o $(IRQ_OBJS) \
	    process.o signal.o ioport.o setup.o idprom.o \
	    sys_sparc.o systbls.o \
	    time.o windows.o cpu.o devices.o \
	    tadpole.o tick14.o ptrace.o \
	    unaligned.o una_asm.o muldiv.o \
	    prom.o of_device.o devres.o

devres-y = ../../../kernel/irq/devres.o

obj-$(CONFIG_PCI) += pcic.o
obj-$(CONFIG_SUN4) += sun4setup.o
obj-$(CONFIG_SMP) += trampoline.o smp.o sun4m_smp.o sun4d_smp.o
obj-$(CONFIG_SUN_AUXIO) += auxio.o
obj-$(CONFIG_PCI) += ebus.o
obj-$(CONFIG_SUN_PM) += apc.o pmc.o
obj-$(CONFIG_MODULES) += module.o sparc_ksyms.o
obj-$(CONFIG_SPARC_LED) += led.o
obj-$(CONFIG_KGDB) += kgdb.o
