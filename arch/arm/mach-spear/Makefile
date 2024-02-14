# SPDX-License-Identifier: GPL-2.0
#
# SPEAr Platform specific Makefile
#

# Common support
obj-y	:= restart.o time.o

smp-$(CONFIG_SMP)		+= headsmp.o platsmp.o
smp-$(CONFIG_HOTPLUG_CPU)	+= hotplug.o

obj-$(CONFIG_ARCH_SPEAR13XX)	+= spear13xx.o $(smp-y)
obj-$(CONFIG_MACH_SPEAR1310)	+= spear1310.o
obj-$(CONFIG_MACH_SPEAR1340)	+= spear1340.o

obj-$(CONFIG_ARCH_SPEAR3XX)	+= spear3xx.o
obj-$(CONFIG_ARCH_SPEAR3XX)	+= pl080.o
obj-$(CONFIG_MACH_SPEAR300)	+= spear300.o
obj-$(CONFIG_MACH_SPEAR310)	+= spear310.o
obj-$(CONFIG_MACH_SPEAR320)	+= spear320.o

obj-$(CONFIG_ARCH_SPEAR6XX)	+= spear6xx.o
obj-$(CONFIG_ARCH_SPEAR6XX)	+= pl080.o

CFLAGS_hotplug.o		+= -march=armv7-a
