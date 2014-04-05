ccflags-$(CONFIG_ARCH_MULTIPLATFORM) := -I$(srctree)/$(src)/include \
	-I$(srctree)/arch/arm/plat-orion/include

AFLAGS_coherency_ll.o		:= -Wa,-march=armv7-a

obj-y				 += system-controller.o mvebu-soc-id.o
obj-$(CONFIG_MACH_MVEBU_V7)      += board-v7.o
obj-$(CONFIG_MACH_DOVE)		 += dove.o
obj-$(CONFIG_ARCH_MVEBU)	 += coherency.o coherency_ll.o pmsu.o
obj-$(CONFIG_SMP)                += platsmp.o headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)        += hotplug.o
obj-$(CONFIG_MACH_KIRKWOOD)	 += kirkwood.o kirkwood-pm.o
obj-$(CONFIG_MACH_T5325)	 += board-t5325.o
