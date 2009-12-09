#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y	:=  generic.o devices.o serial.o

obj-$(CONFIG_MACH_MX21) += clock_imx21.o

obj-$(CONFIG_MACH_MX27) += cpu_imx27.o
obj-$(CONFIG_MACH_MX27) += clock_imx27.o

obj-$(CONFIG_MACH_MX21ADS) += mx21ads.o
obj-$(CONFIG_MACH_MX27ADS) += mx27ads.o
obj-$(CONFIG_MACH_PCM038) += pcm038.o
obj-$(CONFIG_MACH_PCM970_BASEBOARD) += pcm970-baseboard.o
obj-$(CONFIG_MACH_MX27_3DS) += mx27pdk.o
obj-$(CONFIG_MACH_MX27LITE) += mx27lite.o
obj-$(CONFIG_MACH_EUKREA_CPUIMX27) += eukrea_cpuimx27.o
obj-$(CONFIG_MACH_EUKREA_MBIMX27_BASEBOARD) += eukrea_mbimx27-baseboard.o
obj-$(CONFIG_MACH_PCA100) += pca100.o
obj-$(CONFIG_MACH_MXT_TD60) += mxt_td60.o

