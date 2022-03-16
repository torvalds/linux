# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(srctree)/$(src)/include

rga3-y	:= rga_drv.o rga_common.o rga3_reg_info.o rga_dma_buf.o rga_job.o rga_hw_config.o rga2_reg_info.o rga2_mmu_info.o rga_policy.o rga_mm.o
rga3-$(CONFIG_ROCKCHIP_RGA_ASYNC) += rga_fence.o
rga3-$(CONFIG_ROCKCHIP_RGA_DEBUGGER) += rga_debugger.o

obj-$(CONFIG_ROCKCHIP_MULTI_RGA)	+= rga3.o
