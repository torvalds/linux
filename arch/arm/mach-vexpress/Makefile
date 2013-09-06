#
# Makefile for the linux kernel.
#
ccflags-$(CONFIG_ARCH_MULTIPLATFORM) := -I$(srctree)/$(src)/include \
	-I$(srctree)/arch/arm/plat-versatile/include

obj-y					:= v2m.o
obj-$(CONFIG_ARCH_VEXPRESS_CA9X4)	+= ct-ca9x4.o
obj-$(CONFIG_ARCH_VEXPRESS_DCSCB)	+= dcscb.o	dcscb_setup.o
obj-$(CONFIG_ARCH_VEXPRESS_TC2_PM)	+= tc2_pm.o spc.o
obj-$(CONFIG_SMP)			+= platsmp.o
obj-$(CONFIG_HOTPLUG_CPU)		+= hotplug.o
