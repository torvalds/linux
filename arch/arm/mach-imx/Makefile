#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y	:=  devices.o

obj-$(CONFIG_IMX_HAVE_DMA_V1) += dma-v1.o

obj-$(CONFIG_ARCH_MX1) += clock-imx1.o mm-imx1.o
obj-$(CONFIG_MACH_MX21) += clock-imx21.o mm-imx21.o

obj-$(CONFIG_MACH_MX27) += cpu-imx27.o pm-imx27.o
obj-$(CONFIG_MACH_MX27) += clock-imx27.o mm-imx27.o

# Support for CMOS sensor interface
obj-$(CONFIG_MX1_VIDEO)	+= mx1-camera-fiq.o mx1-camera-fiq-ksym.o

obj-$(CONFIG_ARCH_MX1ADS) += mach-mx1ads.o
obj-$(CONFIG_MACH_SCB9328) += mach-scb9328.o

obj-$(CONFIG_MACH_MX21ADS) += mach-mx21ads.o

obj-$(CONFIG_MACH_MX27ADS) += mach-mx27ads.o
obj-$(CONFIG_MACH_PCM038) += mach-pcm038.o
obj-$(CONFIG_MACH_PCM970_BASEBOARD) += pcm970-baseboard.o
obj-$(CONFIG_MACH_MX27_3DS) += mach-mx27_3ds.o
obj-$(CONFIG_MACH_IMX27LITE) += mach-imx27lite.o
obj-$(CONFIG_MACH_IMX27_VISSTRIM_M10) += mach-imx27_visstrim_m10.o
obj-$(CONFIG_MACH_CPUIMX27) += mach-cpuimx27.o
obj-$(CONFIG_MACH_EUKREA_MBIMX27_BASEBOARD) += eukrea_mbimx27-baseboard.o
obj-$(CONFIG_MACH_PCA100) += mach-pca100.o
obj-$(CONFIG_MACH_MXT_TD60) += mach-mxt_td60.o
