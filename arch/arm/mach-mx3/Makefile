#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y				:= mm.o devices.o
obj-$(CONFIG_ARCH_MX31)		+= clock.o iomux.o
obj-$(CONFIG_ARCH_MX35)		+= clock-imx35.o
obj-$(CONFIG_MACH_MX31ADS)	+= mx31ads.o
obj-$(CONFIG_MACH_MX31LILLY)	+= mx31lilly.o mx31lilly-db.o
obj-$(CONFIG_MACH_MX31LITE)	+= mx31lite.o
obj-$(CONFIG_MACH_PCM037)	+= pcm037.o
obj-$(CONFIG_MACH_MX31_3DS)	+= mx31pdk.o
obj-$(CONFIG_MACH_MX31MOBOARD)	+= mx31moboard.o mx31moboard-devboard.o \
				   mx31moboard-marxbot.o
obj-$(CONFIG_MACH_QONG)		+= qong.o
obj-$(CONFIG_MACH_PCM043)	+= pcm043.o
obj-$(CONFIG_MACH_ARMADILLO5X0) += armadillo5x0.o
obj-$(CONFIG_MACH_MX35_3DS)	+= mx35pdk.o
