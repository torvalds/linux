#
# Makefile for local APIC drivers and for the IO-APIC code
#

obj-$(CONFIG_X86_LOCAL_APIC)	+= apic.o probe_$(BITS).o ipi.o nmi.o
obj-$(CONFIG_X86_IO_APIC)	+= io_apic.o
obj-$(CONFIG_SMP)		+= ipi.o

ifeq ($(CONFIG_X86_64),y)
obj-y				+= apic_flat_64.o
obj-$(CONFIG_X86_X2APIC)	+= x2apic_cluster.o
obj-$(CONFIG_X86_X2APIC)	+= x2apic_phys.o
obj-$(CONFIG_X86_UV)		+= x2apic_uv_x.o
endif

obj-$(CONFIG_X86_BIGSMP)	+= bigsmp_32.o
obj-$(CONFIG_X86_NUMAQ)		+= numaq_32.o
obj-$(CONFIG_X86_ES7000)	+= es7000_32.o
obj-$(CONFIG_X86_SUMMIT)	+= summit_32.o
