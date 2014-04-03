
imxdrm-objs := imx-drm-core.o imx-fb.o

obj-$(CONFIG_DRM_IMX) += imxdrm.o

obj-$(CONFIG_DRM_IMX_PARALLEL_DISPLAY) += parallel-display.o
obj-$(CONFIG_DRM_IMX_TVE) += imx-tve.o
obj-$(CONFIG_DRM_IMX_LDB) += imx-ldb.o
obj-$(CONFIG_DRM_IMX_FB_HELPER) += imx-fbdev.o
obj-$(CONFIG_DRM_IMX_IPUV3_CORE) += ipu-v3/

imx-ipuv3-crtc-objs  := ipuv3-crtc.o ipuv3-plane.o
obj-$(CONFIG_DRM_IMX_IPUV3)	+= imx-ipuv3-crtc.o
obj-$(CONFIG_DRM_IMX_HDMI) += imx-hdmi.o
