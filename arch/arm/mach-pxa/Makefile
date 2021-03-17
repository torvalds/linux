# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the linux kernel.
#

# Common support (must be linked before board specific support)
obj-y				+= devices.o generic.o irq.o reset.o
obj-$(CONFIG_PM)		+= pm.o sleep.o standby.o

# Generic drivers that other drivers may depend upon

# SoC-specific code
obj-$(CONFIG_PXA25x)		+= mfp-pxa2xx.o pxa2xx.o pxa25x.o
obj-$(CONFIG_PXA27x)		+= mfp-pxa2xx.o pxa2xx.o pxa27x.o
obj-$(CONFIG_PXA3xx)		+= mfp-pxa3xx.o pxa3xx.o smemc.o pxa3xx-ulpi.o
obj-$(CONFIG_CPU_PXA300)	+= pxa300.o
obj-$(CONFIG_CPU_PXA320)	+= pxa320.o
obj-$(CONFIG_CPU_PXA930)	+= pxa930.o

# NOTE: keep the order of boards in accordance to their order in Kconfig

# Device Tree support
obj-$(CONFIG_MACH_PXA25X_DT)	+= pxa-dt.o
obj-$(CONFIG_MACH_PXA27X_DT)	+= pxa-dt.o
obj-$(CONFIG_MACH_PXA3XX_DT)	+= pxa-dt.o

# Intel/Marvell Dev Platforms
obj-$(CONFIG_ARCH_LUBBOCK)	+= lubbock.o
obj-$(CONFIG_MACH_MAINSTONE)	+= mainstone.o
obj-$(CONFIG_MACH_ZYLONITE300)	+= zylonite.o zylonite_pxa300.o
obj-$(CONFIG_MACH_ZYLONITE320)	+= zylonite.o zylonite_pxa320.o
obj-$(CONFIG_MACH_LITTLETON)	+= littleton.o
obj-$(CONFIG_MACH_TAVOREVB)	+= tavorevb.o
obj-$(CONFIG_MACH_SAAR)		+= saar.o

# 3rd Party Dev Platforms
obj-$(CONFIG_ARCH_PXA_IDP)	+= idp.o
obj-$(CONFIG_ARCH_VIPER)	+= viper.o
obj-$(CONFIG_MACH_ARCOM_ZEUS)	+= zeus.o
obj-$(CONFIG_MACH_BALLOON3)	+= balloon3.o
obj-$(CONFIG_MACH_CSB726)	+= csb726.o
obj-$(CONFIG_CSB726_CSB701)	+= csb701.o
obj-$(CONFIG_MACH_CM_X300)      += cm-x300.o
obj-$(CONFIG_MACH_CAPC7117)     += capc7117.o mxm8x10.o
obj-$(CONFIG_ARCH_GUMSTIX)	+= gumstix.o
obj-$(CONFIG_GUMSTIX_AM200EPD)	+= am200epd.o
obj-$(CONFIG_GUMSTIX_AM300EPD)	+= am300epd.o
obj-$(CONFIG_MACH_INTELMOTE2)	+= stargate2.o
obj-$(CONFIG_MACH_STARGATE2)	+= stargate2.o
obj-$(CONFIG_MACH_XCEP)         += xcep.o
obj-$(CONFIG_MACH_TRIZEPS4)	+= trizeps4.o
obj-$(CONFIG_MACH_LOGICPD_PXA270)	+= lpd270.o
obj-$(CONFIG_MACH_PCM027)		+= pcm027.o
obj-$(CONFIG_MACH_PCM990_BASEBOARD)	+= pcm990-baseboard.o
obj-$(CONFIG_MACH_COLIBRI)			+= colibri-pxa270.o
obj-$(CONFIG_MACH_COLIBRI_EVALBOARD)	+= colibri-evalboard.o
obj-$(CONFIG_MACH_COLIBRI_PXA270_INCOME)	+= colibri-pxa270-income.o
obj-$(CONFIG_MACH_COLIBRI300)	+= colibri-pxa3xx.o colibri-pxa300.o
obj-$(CONFIG_MACH_COLIBRI320)	+= colibri-pxa3xx.o colibri-pxa320.o
obj-$(CONFIG_MACH_VPAC270)	+= vpac270.o

# End-user Products
obj-$(CONFIG_MACH_H4700)	+= hx4700.o
obj-$(CONFIG_MACH_H5000)	+= h5000.o
obj-$(CONFIG_MACH_HIMALAYA)	+= himalaya.o
obj-$(CONFIG_MACH_MAGICIAN)	+= magician.o
obj-$(CONFIG_MACH_MIOA701)	+= mioa701.o mioa701_bootresume.o
obj-$(CONFIG_PXA_EZX)           += ezx.o
obj-$(CONFIG_MACH_MP900C)	+= mp900.o
obj-$(CONFIG_MACH_PALMTE2)	+= palmte2.o
obj-$(CONFIG_MACH_PALMTC)	+= palmtc.o
obj-$(CONFIG_MACH_PALM27X)	+= palm27x.o
obj-$(CONFIG_MACH_PALMT5)	+= palmt5.o
obj-$(CONFIG_MACH_PALMTX)	+= palmtx.o
obj-$(CONFIG_MACH_PALMZ72)	+= palmz72.o
obj-$(CONFIG_MACH_PALMLD)	+= palmld.o
obj-$(CONFIG_PALM_TREO)		+= palmtreo.o
obj-$(CONFIG_PXA_SHARP_C7xx)	+= corgi.o sharpsl_pm.o corgi_pm.o
obj-$(CONFIG_PXA_SHARP_Cxx00)	+= spitz.o sharpsl_pm.o spitz_pm.o
obj-$(CONFIG_MACH_POODLE)	+= poodle.o
obj-$(CONFIG_MACH_TOSA)		+= tosa.o
obj-$(CONFIG_MACH_ICONTROL)     += icontrol.o mxm8x10.o
obj-$(CONFIG_ARCH_PXA_ESERIES)	+= eseries.o
obj-$(CONFIG_MACH_ZIPIT2)	+= z2.o

obj-$(CONFIG_PXA_SYSTEMS_CPLDS)	+= pxa_cplds_irqs.o
obj-$(CONFIG_TOSA_BT)		+= tosa-bt.o
