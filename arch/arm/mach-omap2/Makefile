#
# Makefile for the linux kernel.
#

# Common support
obj-y := irq.o id.o io.o memory.o control.o prcm.o clock.o mux.o \
		devices.o serial.o gpmc.o timer-gp.o powerdomain.o \
		clockdomain.o

obj-$(CONFIG_OMAP_MCBSP) += mcbsp.o

# Functions loaded to SRAM
obj-$(CONFIG_ARCH_OMAP2420)		+= sram242x.o
obj-$(CONFIG_ARCH_OMAP2430)		+= sram243x.o
obj-$(CONFIG_ARCH_OMAP3)		+= sram34xx.o

# Power Management
ifeq ($(CONFIG_PM),y)
obj-y					+= pm.o
obj-$(CONFIG_ARCH_OMAP24XX)		+= sleep24xx.o
endif

# Clock framework
obj-$(CONFIG_ARCH_OMAP2)		+= clock24xx.o
obj-$(CONFIG_ARCH_OMAP3)		+= clock34xx.o

# Specific board support
obj-$(CONFIG_MACH_OMAP_GENERIC)		+= board-generic.o
obj-$(CONFIG_MACH_OMAP_H4)		+= board-h4.o
obj-$(CONFIG_MACH_OMAP_2430SDP)		+= board-2430sdp.o
obj-$(CONFIG_MACH_OMAP_APOLLON)		+= board-apollon.o
obj-$(CONFIG_MACH_OMAP3_BEAGLE)		+= board-omap3beagle.o

