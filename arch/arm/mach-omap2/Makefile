#
# Makefile for the linux kernel.
#

# Common support
obj-y := irq.o id.o io.o sram-fn.o memory.o prcm.o clock.o mux.o devices.o \
	 serial.o gpmc.o

obj-$(CONFIG_OMAP_MPU_TIMER)		+= timer-gp.o

# Power Management
obj-$(CONFIG_PM) += pm.o pm-domain.o sleep.o

# Specific board support
obj-$(CONFIG_MACH_OMAP_GENERIC)		+= board-generic.o
obj-$(CONFIG_MACH_OMAP_H4)		+= board-h4.o
obj-$(CONFIG_MACH_OMAP_2430SDP)		+= board-2430sdp.o
obj-$(CONFIG_MACH_OMAP_APOLLON)		+= board-apollon.o

