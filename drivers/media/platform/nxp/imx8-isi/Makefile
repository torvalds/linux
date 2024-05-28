# SPDX-License-Identifier: GPL-2.0-only

imx8-isi-y := imx8-isi-core.o imx8-isi-crossbar.o imx8-isi-gasket.o \
	imx8-isi-hw.o imx8-isi-pipe.o imx8-isi-video.o
imx8-isi-$(CONFIG_DEBUG_FS) += imx8-isi-debug.o
imx8-isi-$(CONFIG_VIDEO_IMX8_ISI_M2M) += imx8-isi-m2m.o

obj-$(CONFIG_VIDEO_IMX8_ISI) += imx8-isi.o
