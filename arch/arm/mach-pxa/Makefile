#
# Makefile for the linux kernel.
#

# Common support (must be linked before board specific support)
obj-y				+= clock.o devices.o generic.o irq.o \
				   time.o reset.o
obj-$(CONFIG_PM)		+= pm.o sleep.o standby.o

ifeq ($(CONFIG_CPU_FREQ),y)
obj-$(CONFIG_PXA25x)		+= cpufreq-pxa2xx.o
obj-$(CONFIG_PXA27x)		+= cpufreq-pxa2xx.o
obj-$(CONFIG_PXA3xx)		+= cpufreq-pxa3xx.o
endif

# Generic drivers that other drivers may depend upon
obj-$(CONFIG_PXA_SSP)		+= ssp.o
obj-$(CONFIG_PXA_PWM)		+= pwm.o

# SoC-specific code
obj-$(CONFIG_PXA25x)		+= mfp-pxa2xx.o pxa2xx.o pxa25x.o
obj-$(CONFIG_PXA27x)		+= mfp-pxa2xx.o pxa2xx.o pxa27x.o
obj-$(CONFIG_PXA3xx)		+= mfp-pxa3xx.o pxa3xx.o smemc.o
obj-$(CONFIG_CPU_PXA300)	+= pxa300.o
obj-$(CONFIG_CPU_PXA320)	+= pxa320.o
obj-$(CONFIG_CPU_PXA930)	+= pxa930.o

# Specific board support
obj-$(CONFIG_ARCH_GUMSTIX)	+= gumstix.o
obj-$(CONFIG_GUMSTIX_AM200EPD)	+= am200epd.o
obj-$(CONFIG_GUMSTIX_AM300EPD)	+= am300epd.o
obj-$(CONFIG_ARCH_LUBBOCK)	+= lubbock.o
obj-$(CONFIG_MACH_LOGICPD_PXA270) += lpd270.o
obj-$(CONFIG_MACH_MAINSTONE)	+= mainstone.o
obj-$(CONFIG_MACH_MP900C)	+= mp900.o
obj-$(CONFIG_ARCH_PXA_IDP)	+= idp.o
obj-$(CONFIG_MACH_TRIZEPS4)	+= trizeps4.o
obj-$(CONFIG_MACH_COLIBRI)	+= colibri-pxa270.o
obj-$(CONFIG_MACH_COLIBRI300)	+= colibri-pxa3xx.o colibri-pxa300.o
obj-$(CONFIG_MACH_COLIBRI320)	+= colibri-pxa3xx.o colibri-pxa320.o
obj-$(CONFIG_MACH_H5000)	+= h5000.o
obj-$(CONFIG_PXA_SHARP_C7xx)	+= corgi.o sharpsl_pm.o corgi_pm.o
obj-$(CONFIG_PXA_SHARP_Cxx00)	+= spitz.o sharpsl_pm.o spitz_pm.o
obj-$(CONFIG_CORGI_SSP_DEPRECATED)	+= corgi_ssp.o corgi_lcd.o
obj-$(CONFIG_MACH_POODLE)	+= poodle.o
obj-$(CONFIG_MACH_PCM027)	+= pcm027.o
obj-$(CONFIG_MACH_PCM990_BASEBOARD)	+= pcm990-baseboard.o
obj-$(CONFIG_MACH_TOSA)		+= tosa.o
obj-$(CONFIG_MACH_EM_X270)	+= em-x270.o
obj-$(CONFIG_MACH_MAGICIAN)	+= magician.o
obj-$(CONFIG_MACH_HIMALAYA)	+= himalaya.o
obj-$(CONFIG_MACH_MIOA701)	+= mioa701.o mioa701_bootresume.o
obj-$(CONFIG_ARCH_PXA_ESERIES)	+= eseries.o
obj-$(CONFIG_MACH_E330)		+= e330.o
obj-$(CONFIG_MACH_E350)		+= e350.o
obj-$(CONFIG_MACH_E740)		+= e740.o
obj-$(CONFIG_MACH_E750)		+= e750.o
obj-$(CONFIG_MACH_E400)		+= e400.o
obj-$(CONFIG_MACH_E800)		+= e800.o
obj-$(CONFIG_MACH_PALMTE2)	+= palmte2.o
obj-$(CONFIG_MACH_PALMT5)	+= palmt5.o
obj-$(CONFIG_MACH_PALMTX)	+= palmtx.o
obj-$(CONFIG_MACH_PALMLD)	+= palmld.o
obj-$(CONFIG_MACH_PALMZ72)	+= palmz72.o
obj-$(CONFIG_ARCH_VIPER)	+= viper.o

ifeq ($(CONFIG_MACH_ZYLONITE),y)
  obj-y				+= zylonite.o
  obj-$(CONFIG_CPU_PXA300)	+= zylonite_pxa300.o
  obj-$(CONFIG_CPU_PXA320)	+= zylonite_pxa320.o
endif
obj-$(CONFIG_MACH_LITTLETON)	+= littleton.o
obj-$(CONFIG_MACH_TAVOREVB)	+= tavorevb.o
obj-$(CONFIG_MACH_SAAR)		+= saar.o

obj-$(CONFIG_MACH_ARMCORE)      += cm-x2xx.o cm-x255.o cm-x270.o
obj-$(CONFIG_MACH_CM_X300)      += cm-x300.o
obj-$(CONFIG_PXA_EZX)           += ezx.o

obj-$(CONFIG_MACH_INTELMOTE2)   += imote2.o
obj-$(CONFIG_MACH_CSB726)	+= csb726.o
obj-$(CONFIG_CSB726_CSB701)	+= csb701.o

# Support for blinky lights
led-y := leds.o
led-$(CONFIG_ARCH_LUBBOCK)	+= leds-lubbock.o
led-$(CONFIG_MACH_MAINSTONE)	+= leds-mainstone.o
led-$(CONFIG_ARCH_PXA_IDP)	+= leds-idp.o

obj-$(CONFIG_LEDS)		+= $(led-y)

ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_MACH_ARMCORE) += cm-x2xx-pci.o
endif

obj-$(CONFIG_TOSA_BT)		+= tosa-bt.o
