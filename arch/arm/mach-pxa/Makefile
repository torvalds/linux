#
# Makefile for the linux kernel.
#

# Common support (must be linked before board specific support)
obj-y				+= clock.o devices.o generic.o irq.o dma.o \
				   time.o gpio.o
obj-$(CONFIG_PXA25x)		+= pxa25x.o mfp-pxa2xx.o
obj-$(CONFIG_PXA27x)		+= pxa27x.o mfp-pxa2xx.o
obj-$(CONFIG_PXA3xx)		+= pxa3xx.o mfp-pxa3xx.o smemc.o
obj-$(CONFIG_CPU_PXA300)	+= pxa300.o
obj-$(CONFIG_CPU_PXA320)	+= pxa320.o

# Specific board support
obj-$(CONFIG_ARCH_GUMSTIX)	+= gumstix.o
obj-$(CONFIG_ARCH_LUBBOCK)	+= lubbock.o
obj-$(CONFIG_MACH_LOGICPD_PXA270) += lpd270.o
obj-$(CONFIG_MACH_MAINSTONE)	+= mainstone.o
obj-$(CONFIG_ARCH_PXA_IDP)	+= idp.o
obj-$(CONFIG_MACH_TRIZEPS4)	+= trizeps4.o
obj-$(CONFIG_MACH_COLIBRI)	+= colibri.o
obj-$(CONFIG_PXA_SHARP_C7xx)	+= corgi.o corgi_ssp.o corgi_lcd.o sharpsl_pm.o corgi_pm.o
obj-$(CONFIG_PXA_SHARP_Cxx00)	+= spitz.o corgi_ssp.o corgi_lcd.o sharpsl_pm.o spitz_pm.o
obj-$(CONFIG_MACH_AKITA)	+= akita-ioexp.o
obj-$(CONFIG_MACH_POODLE)	+= poodle.o corgi_ssp.o
obj-$(CONFIG_MACH_PCM027)	+= pcm027.o
obj-$(CONFIG_MACH_PCM990_BASEBOARD)	+= pcm990-baseboard.o
obj-$(CONFIG_MACH_TOSA)		+= tosa.o
obj-$(CONFIG_MACH_EM_X270)	+= em-x270.o
obj-$(CONFIG_MACH_MAGICIAN)	+= magician.o
obj-$(CONFIG_ARCH_PXA_ESERIES)	+= eseries.o

ifeq ($(CONFIG_MACH_ZYLONITE),y)
  obj-y				+= zylonite.o
  obj-$(CONFIG_CPU_PXA300)	+= zylonite_pxa300.o
  obj-$(CONFIG_CPU_PXA320)	+= zylonite_pxa320.o
endif
obj-$(CONFIG_MACH_LITTLETON)	+= littleton.o

obj-$(CONFIG_MACH_ARMCORE)      += cm-x270.o

# Support for blinky lights
led-y := leds.o
led-$(CONFIG_ARCH_LUBBOCK)	+= leds-lubbock.o
led-$(CONFIG_MACH_MAINSTONE)	+= leds-mainstone.o
led-$(CONFIG_ARCH_PXA_IDP)	+= leds-idp.o
led-$(CONFIG_MACH_TRIZEPS4)	+= leds-trizeps4.o

obj-$(CONFIG_LEDS)		+= $(led-y)

# Misc features
obj-$(CONFIG_PM)		+= pm.o sleep.o standby.o
obj-$(CONFIG_CPU_FREQ)		+= cpu-pxa.o
obj-$(CONFIG_PXA_SSP)		+= ssp.o

ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_MACH_ARMCORE) += cm-x270-pci.o
endif
