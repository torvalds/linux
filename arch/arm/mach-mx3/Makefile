#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y				:= mm.o devices.o cpu.o
CFLAGS_mm.o = -DIMX_NEEDS_DEPRECATED_SYMBOLS
CFLAGS_devices.o = -DIMX_NEEDS_DEPRECATED_SYMBOLS
obj-$(CONFIG_ARCH_MX31)		+= clock-imx31.o iomux-imx31.o
obj-$(CONFIG_ARCH_MX35)		+= clock-imx35.o
obj-$(CONFIG_MACH_MX31ADS)	+= mach-mx31ads.o
obj-$(CONFIG_MACH_MX31LILLY)	+= mach-mx31lilly.o mx31lilly-db.o
obj-$(CONFIG_MACH_MX31LITE)	+= mach-mx31lite.o mx31lite-db.o
obj-$(CONFIG_MACH_PCM037)	+= mach-pcm037.o
obj-$(CONFIG_MACH_PCM037_EET)	+= mach-pcm037_eet.o
obj-$(CONFIG_MACH_MX31_3DS)	+= mach-mx31_3ds.o
CFLAGS_mach-mx31_3ds.o = -DIMX_NEEDS_DEPRECATED_SYMBOLS
obj-$(CONFIG_MACH_MX31MOBOARD)	+= mach-mx31moboard.o mx31moboard-devboard.o \
				   mx31moboard-marxbot.o mx31moboard-smartbot.o
obj-$(CONFIG_MACH_QONG)		+= mach-qong.o
obj-$(CONFIG_MACH_PCM043)	+= mach-pcm043.o
obj-$(CONFIG_MACH_ARMADILLO5X0) += mach-armadillo5x0.o
obj-$(CONFIG_MACH_MX35_3DS)	+= mach-mx35_3ds.o
obj-$(CONFIG_MACH_KZM_ARM11_01)	+= mach-kzm_arm11_01.o
obj-$(CONFIG_MACH_EUKREA_CPUIMX35)	+= mach-cpuimx35.o
obj-$(CONFIG_MACH_EUKREA_MBIMXSD35_BASEBOARD)	+= eukrea_mbimxsd-baseboard.o
