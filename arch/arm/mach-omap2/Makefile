#
# Makefile for the linux kernel.
#

# Common support
obj-y := id.o io.o control.o mux.o devices.o serial.o gpmc.o timer-gp.o

omap-2-3-common				= irq.o sdrc.o omap_hwmod.o
prcm-common				= prcm.o powerdomain.o
clock-common				= clock.o clockdomain.o

obj-$(CONFIG_ARCH_OMAP2) += $(omap-2-3-common) $(prcm-common) $(clock-common)
obj-$(CONFIG_ARCH_OMAP3) += $(omap-2-3-common) $(prcm-common) $(clock-common)

obj-$(CONFIG_OMAP_MCBSP) += mcbsp.o

# SMP support ONLY available for OMAP4
obj-$(CONFIG_SMP)			+= omap-smp.o omap-headsmp.o
obj-$(CONFIG_LOCAL_TIMERS)		+= timer-mpu.o

# Functions loaded to SRAM
obj-$(CONFIG_ARCH_OMAP2420)		+= sram242x.o
obj-$(CONFIG_ARCH_OMAP2430)		+= sram243x.o
obj-$(CONFIG_ARCH_OMAP3)		+= sram34xx.o

# SMS/SDRC
obj-$(CONFIG_ARCH_OMAP2)		+= sdrc2xxx.o
# obj-$(CONFIG_ARCH_OMAP3)		+= sdrc3xxx.o

# Power Management
ifeq ($(CONFIG_PM),y)
obj-$(CONFIG_ARCH_OMAP2)		+= pm24xx.o
obj-$(CONFIG_ARCH_OMAP24XX)		+= sleep24xx.o
obj-$(CONFIG_ARCH_OMAP3)		+= pm34xx.o sleep34xx.o
obj-$(CONFIG_PM_DEBUG)			+= pm-debug.o
endif

# PRCM
obj-$(CONFIG_ARCH_OMAP2)		+= cm.o
obj-$(CONFIG_ARCH_OMAP3)		+= cm.o
obj-$(CONFIG_ARCH_OMAP4)		+= cm4xxx.o

# Clock framework
obj-$(CONFIG_ARCH_OMAP2)		+= clock24xx.o
obj-$(CONFIG_ARCH_OMAP3)		+= clock34xx.o

iommu-y					+= iommu2.o
iommu-$(CONFIG_ARCH_OMAP3)		+= omap3-iommu.o

obj-$(CONFIG_OMAP_IOMMU)		+= $(iommu-y)

# Specific board support
obj-$(CONFIG_MACH_OMAP_GENERIC)		+= board-generic.o
obj-$(CONFIG_MACH_OMAP_H4)		+= board-h4.o
obj-$(CONFIG_MACH_OMAP_2430SDP)		+= board-2430sdp.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OMAP_APOLLON)		+= board-apollon.o
obj-$(CONFIG_MACH_OMAP3_BEAGLE)		+= board-omap3beagle.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OMAP_LDP)		+= board-ldp.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OVERO)		+= board-overo.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OMAP3EVM)		+= board-omap3evm.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OMAP3_PANDORA)	+= board-omap3pandora.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OMAP_3430SDP)		+= board-3430sdp.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_NOKIA_N8X0)		+= board-n8x0.o
obj-$(CONFIG_MACH_NOKIA_RX51)		+= board-rx51.o \
					   board-rx51-peripherals.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OMAP_ZOOM2)		+= board-zoom2.o \
					   mmc-twl4030.o \
					   board-zoom-debugboard.o

obj-$(CONFIG_MACH_OMAP_4430SDP)		+= board-4430sdp.o

# Platform specific device init code
obj-y					+= usb-musb.o
obj-$(CONFIG_MACH_OMAP2_TUSB6010)	+= usb-tusb6010.o

onenand-$(CONFIG_MTD_ONENAND_OMAP2)	:= gpmc-onenand.o
obj-y					+= $(onenand-m) $(onenand-y)

smc91x-$(CONFIG_SMC91X)			:= gpmc-smc91x.o
obj-y					+= $(smc91x-m) $(smc91x-y)
