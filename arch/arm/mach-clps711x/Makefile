#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y			:= irq.o mm.o time.o
obj-m			:=
obj-n			:=
obj-			:=

obj-$(CONFIG_ARCH_CEIVA) += ceiva.o
obj-$(CONFIG_ARCH_AUTCPU12) += autcpu12.o
obj-$(CONFIG_ARCH_CDB89712) += cdb89712.o
obj-$(CONFIG_ARCH_CLEP7312) += clep7312.o
obj-$(CONFIG_ARCH_EDB7211)  += edb7211-arch.o edb7211-mm.o
obj-$(CONFIG_ARCH_FORTUNET) += fortunet.o
obj-$(CONFIG_MACH_MP1000)   += mp1000-mach.o mp1000-mm.o mp1000-seprom.o
obj-$(CONFIG_ARCH_P720T)    += p720t.o
leds-$(CONFIG_ARCH_P720T)   += p720t-leds.o
obj-$(CONFIG_LEDS)          += $(leds-y)
