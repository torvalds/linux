obj-$(CONFIG_IMX_IPUV3_CORE) += imx-ipu-v3.o

imx-ipu-v3-objs := ipu-common.o ipu-cpmem.o ipu-csi.o ipu-dc.o ipu-di.o \
		ipu-dp.o ipu-dmfc.o ipu-ic.o ipu-image-convert.o \
		ipu-smfc.o ipu-vdi.o

ifdef CONFIG_DRM
	imx-ipu-v3-objs += ipu-pre.o ipu-prg.o
endif
