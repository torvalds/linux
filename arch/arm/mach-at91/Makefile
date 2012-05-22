#
# Makefile for the linux kernel.
#

obj-y		:= irq.o gpio.o setup.o
obj-m		:=
obj-n		:=
obj-		:=

obj-$(CONFIG_AT91_PMC_UNIT)	+= clock.o
obj-$(CONFIG_AT91_SAM9_ALT_RESET) += at91sam9_alt_reset.o
obj-$(CONFIG_AT91_SAM9G45_RESET) += at91sam9g45_reset.o
obj-$(CONFIG_SOC_AT91SAM9)	+= at91sam926x_time.o sam9_smc.o

# CPU-specific support
obj-$(CONFIG_SOC_AT91RM9200)	+= at91rm9200.o at91rm9200_time.o
obj-$(CONFIG_SOC_AT91SAM9260)	+= at91sam9260.o
obj-$(CONFIG_SOC_AT91SAM9261)	+= at91sam9261.o
obj-$(CONFIG_SOC_AT91SAM9263)	+= at91sam9263.o
obj-$(CONFIG_SOC_AT91SAM9G45)	+= at91sam9g45.o
obj-$(CONFIG_SOC_AT91SAM9X5)	+= at91sam9x5.o
obj-$(CONFIG_SOC_AT91SAM9RL)	+= at91sam9rl.o

obj-$(CONFIG_ARCH_AT91RM9200)	+= at91rm9200_devices.o
obj-$(CONFIG_ARCH_AT91SAM9260)	+= at91sam9260_devices.o
obj-$(CONFIG_ARCH_AT91SAM9261)	+= at91sam9261_devices.o
obj-$(CONFIG_ARCH_AT91SAM9G10)	+= at91sam9261_devices.o
obj-$(CONFIG_ARCH_AT91SAM9263)	+= at91sam9263_devices.o
obj-$(CONFIG_ARCH_AT91SAM9RL)	+= at91sam9rl_devices.o
obj-$(CONFIG_ARCH_AT91SAM9G20)	+= at91sam9260_devices.o
obj-$(CONFIG_ARCH_AT91SAM9G45)	+= at91sam9g45_devices.o
obj-$(CONFIG_ARCH_AT91X40)	+= at91x40.o at91x40_time.o

# AT91RM9200 board-specific support
obj-$(CONFIG_MACH_ONEARM)	+= board-1arm.o
obj-$(CONFIG_ARCH_AT91RM9200DK)	+= board-rm9200dk.o
obj-$(CONFIG_MACH_AT91RM9200EK)	+= board-rm9200ek.o
obj-$(CONFIG_MACH_CSB337)	+= board-csb337.o
obj-$(CONFIG_MACH_CSB637)	+= board-csb637.o
obj-$(CONFIG_MACH_CARMEVA)	+= board-carmeva.o
obj-$(CONFIG_MACH_KB9200)	+= board-kb9202.o
obj-$(CONFIG_MACH_ATEB9200)	+= board-eb9200.o
obj-$(CONFIG_MACH_KAFA)		+= board-kafa.o
obj-$(CONFIG_MACH_PICOTUX2XX)	+= board-picotux200.o
obj-$(CONFIG_MACH_ECBAT91)	+= board-ecbat91.o
obj-$(CONFIG_MACH_YL9200)	+= board-yl-9200.o
obj-$(CONFIG_MACH_CPUAT91)	+= board-cpuat91.o
obj-$(CONFIG_MACH_ECO920)	+= board-eco920.o
obj-$(CONFIG_MACH_RSI_EWS)	+= board-rsi-ews.o

# AT91SAM9260 board-specific support
obj-$(CONFIG_MACH_AT91SAM9260EK) += board-sam9260ek.o
obj-$(CONFIG_MACH_CAM60)	+= board-cam60.o
obj-$(CONFIG_MACH_SAM9_L9260)	+= board-sam9-l9260.o
obj-$(CONFIG_MACH_USB_A9260)	+= board-usb-a926x.o
obj-$(CONFIG_MACH_QIL_A9260)	+= board-qil-a9260.o
obj-$(CONFIG_MACH_AFEB9260)	+= board-afeb-9260v1.o
obj-$(CONFIG_MACH_CPU9260)	+= board-cpu9krea.o
obj-$(CONFIG_MACH_FLEXIBITY)	+= board-flexibity.o

# AT91SAM9261 board-specific support
obj-$(CONFIG_MACH_AT91SAM9261EK) += board-sam9261ek.o
obj-$(CONFIG_MACH_AT91SAM9G10EK) += board-sam9261ek.o

# AT91SAM9263 board-specific support
obj-$(CONFIG_MACH_AT91SAM9263EK) += board-sam9263ek.o
obj-$(CONFIG_MACH_USB_A9263)	+= board-usb-a926x.o
obj-$(CONFIG_MACH_NEOCORE926)	+= board-neocore926.o

# AT91SAM9RL board-specific support
obj-$(CONFIG_MACH_AT91SAM9RLEK)	+= board-sam9rlek.o

# AT91SAM9G20 board-specific support
obj-$(CONFIG_MACH_AT91SAM9G20EK) += board-sam9g20ek.o
obj-$(CONFIG_MACH_CPU9G20)	+= board-cpu9krea.o
obj-$(CONFIG_MACH_ACMENETUSFOXG20) += board-foxg20.o
obj-$(CONFIG_MACH_STAMP9G20)	+= board-stamp9g20.o
obj-$(CONFIG_MACH_PORTUXG20)	+= board-stamp9g20.o
obj-$(CONFIG_MACH_PCONTROL_G20)	+= board-pcontrol-g20.o board-stamp9g20.o
obj-$(CONFIG_MACH_GSIA18S)	+= board-gsia18s.o board-stamp9g20.o
obj-$(CONFIG_MACH_USB_A9G20)	+= board-usb-a926x.o

# AT91SAM9260/AT91SAM9G20 board-specific support
obj-$(CONFIG_MACH_SNAPPER_9260)	+= board-snapper9260.o

# AT91SAM9G45 board-specific support
obj-$(CONFIG_MACH_AT91SAM9M10G45EK) += board-sam9m10g45ek.o

# AT91SAM board with device-tree
obj-$(CONFIG_MACH_AT91SAM_DT) += board-dt.o

# AT91X40 board-specific support
obj-$(CONFIG_MACH_AT91EB01)	+= board-eb01.o

# Drivers
obj-y				+= leds.o

# Power Management
obj-$(CONFIG_PM)		+= pm.o
obj-$(CONFIG_AT91_SLOW_CLOCK)	+= pm_slowclock.o
obj-$(CONFIG_CPU_IDLE)	+= cpuidle.o

ifeq ($(CONFIG_PM_DEBUG),y)
CFLAGS_pm.o += -DDEBUG
endif
