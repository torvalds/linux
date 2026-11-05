# SPDX-License-Identifier: GPL-2.0-only

intel-ipu6-y			:= ipu6.o \
				ipu6-bus.o \
				ipu6-dma.o \
				ipu6-mmu.o \
				ipu6-mmu-hw.o \
				ipu7-mmu-hw.o \
				ipu6-buttress.o \
				ipu6-cpd.o \
				ipu6-fw-com.o \
				ipu7-fw-com.o \
				ipu7-boot.o

obj-$(CONFIG_VIDEO_INTEL_IPU6)	+= intel-ipu6.o

intel-ipu6-isys-y		:= ipu6-isys.o \
				ipu6-isys-csi2.o \
				ipu6-fw-isys.o \
				ipu7-fw-isys.o \
				ipu6-isys-video.o \
				ipu6-isys-queue.o \
				ipu6-isys-subdev.o \
				ipu6-isys-mcd-phy.o \
				ipu6-isys-jsl-phy.o \
				ipu6-isys-dwc-phy.o \
				ipu7-isys-csi-phy.o

obj-$(CONFIG_VIDEO_INTEL_IPU6)	+= intel-ipu6-isys.o
