# SPDX-License-Identifier: GPL-2.0
#
# Makefile for RTC class/drivers.
#

#obj-$(CONFIG_VIN_SENSOR_OV5640) += ov5640_dvp.o
#obj-$(CONFIG_VIN_SENSOR_SC2235) += sc2235.o
#obj-$(CONFIG_VIN_SENSOR_OV4689) += ov4689_mipi.o
#obj-$(CONFIG_VIDEO_STF_VIN) += stf_vin.o stf_event.o stf_isp.o stf_csi.o

obj-$(CONFIG_VIN_SENSOR_OV5640) += v4l2_driver/ov5640.o
obj-$(CONFIG_VIN_SENSOR_SC2235) += v4l2_driver/sc2235.o
obj-$(CONFIG_VIN_SENSOR_OV4689) += v4l2_driver/ov4689_mipi.o
obj-$(CONFIG_VIN_SENSOR_OV13850) += v4l2_driver/ov13850_mipi.o
obj-$(CONFIG_VIN_SENSOR_IMX219) += imx219_mipi.o

obj-$(CONFIG_VIDEO_STF_VIN) += 	v4l2_driver/stfcamss.o \
	                       	v4l2_driver/stf_event.o \
				v4l2_driver/stf_dvp.o   \
				v4l2_driver/stf_csi.o   \
				v4l2_driver/stf_csiphy.o   \
				v4l2_driver/stf_isp.o   \
                                v4l2_driver/stf_video.o \
				v4l2_driver/stf_vin.o \
				v4l2_driver/stf_vin_hw_ops.o \
				v4l2_driver/stf_csi_hw_ops.o \
				v4l2_driver/stf_csiphy_hw_ops.o \
				v4l2_driver/stf_isp_hw_ops.o \
				v4l2_driver/stf_dvp_hw_ops.o \
				v4l2_driver/stf_dmabuf.o
