ccflags-$(CONFIG_ARCH_MULTIPLATFORM) := -I$(srctree)/$(src)/include \
	-I$(srctree)/arch/arm/plat-orion/include

obj-y += system-controller.o
obj-$(CONFIG_MACH_ARMADA_370_XP) += armada-370-xp.o irq-armada-370-xp.o addr-map.o coherency.o coherency_ll.o pmsu.o
obj-$(CONFIG_SMP)                += platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)        += hotplug.o
