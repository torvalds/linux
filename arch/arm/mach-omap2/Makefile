#
# Makefile for the linux kernel.
#

# Common support
obj-y := irq.o id.o io.o sdrc.o control.o prcm.o clock.o mux.o \
		devices.o serial.o gpmc.o timer-gp.o powerdomain.o \
		clockdomain.o

obj-$(CONFIG_OMAP_MCBSP) += mcbsp.o

# Functions loaded to SRAM
obj-$(CONFIG_ARCH_OMAP2420)		+= sram242x.o
obj-$(CONFIG_ARCH_OMAP2430)		+= sram243x.o
obj-$(CONFIG_ARCH_OMAP3)		+= sram34xx.o

# SMS/SDRC
obj-$(CONFIG_ARCH_OMAP2)		+= sdrc2xxx.o
# obj-$(CONFIG_ARCH_OMAP3)		+= sdrc3xxx.o

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
obj-$(CONFIG_MACH_OMAP_2430SDP)		+= board-2430sdp.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OMAP_APOLLON)		+= board-apollon.o
obj-$(CONFIG_MACH_OMAP3_BEAGLE)		+= board-omap3beagle.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OMAP_LDP)		+= board-ldp.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OVERO)		+= board-overo.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OMAP3_PANDORA)	+= board-omap3pandora.o \
					   mmc-twl4030.o
obj-$(CONFIG_MACH_OMAP_3430SDP)		+= board-3430sdp.o \
					   mmc-twl4030.o

obj-$(CONFIG_MACH_NOKIA_RX51)		+= board-rx51.o \
					   board-rx51-peripherals.o \
					   mmc-twl4030.o

# Platform specific device init code
ifeq ($(CONFIG_USB_MUSB_SOC),y)
obj-y					+= usb-musb.o
endif
