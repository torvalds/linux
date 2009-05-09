#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y				:= mm.o devices.o
obj-$(CONFIG_ARCH_MX31)		+= clock.o iomux.o
obj-$(CONFIG_ARCH_MX35)		+= clock-imx35.o
obj-$(CONFIG_MACH_MX31ADS)	+= mx31ads.o
obj-$(CONFIG_MACH_MX31LITE)	+= mx31lite.o
obj-$(CONFIG_MACH_PCM037)	+= pcm037.o
obj-$(CONFIG_MACH_MX31_3DS)	+= mx31pdk.o
obj-$(CONFIG_MACH_MX31MOBOARD)	+= mx31moboard.o mx31moboard-devboard.o \
				   mx31moboard-marxbot.o
obj-$(CONFIG_MACH_QONG)		+= qong.o
