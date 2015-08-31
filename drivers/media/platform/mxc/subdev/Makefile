#Makefile for mxc csi driver

obj-$(CONFIG_VIDEO_MXC_CSI_CAMERA)	+= mx6s_capture.o
obj-$(CONFIG_MXC_VADC) += mxc_vadc.o
obj-$(CONFIG_MXC_MIPI_CSI) += mxc_mipi_csi.o

ov5640_camera-objs := ov5640.o
obj-$(CONFIG_MXC_CAMERA_OV5640) += ov5640_camera.o

ov5640_camera_mipi-objs := ov5640_mipi.o
obj-$(CONFIG_MXC_CAMERA_OV5640_MIPI) += ov5640_camera_mipi.o

ov5647_camera_mipi-objs := ov5647_mipi.o
obj-$(CONFIG_MXC_CAMERA_OV5647_MIPI) += ov5647_camera_mipi.o
