#
# Makefile for the ia32 kernel emulation subsystem.
#

obj-$(CONFIG_IA32_EMULATION) := sys_ia32.o ia32_signal.o

obj-$(CONFIG_IA32_AOUT) += ia32_aout.o

audit-class-$(CONFIG_AUDIT) := audit.o
obj-$(CONFIG_IA32_EMULATION) += $(audit-class-y)
