# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Rockchip RK628 display bridge driver.
#
video-rk628-objs	:= rk628.o rk628_cru.o rk628_hdmirx.o rk628_dsi.o rk628_combrxphy.o rk628_combtxphy.o
obj-$(CONFIG_VIDEO_RK628)	+= video-rk628.o

rk628-csi-objs	:= rk628_csi_v4l2.o
obj-$(CONFIG_VIDEO_RK628_CSI)	+= rk628-csi.o

rk628-bt1120-objs	:= rk628_bt1120_v4l2.o
obj-$(CONFIG_VIDEO_RK628_BT1120)	+= rk628-bt1120.o