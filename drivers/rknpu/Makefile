# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_ROCKCHIP_RKNPU) += rknpu.o

ccflags-y += -I$(src)/include
ccflags-y += -Werror

rknpu-y += rknpu_drv.o
rknpu-y += rknpu_reset.o
rknpu-y += rknpu_job.o
rknpu-y += rknpu_gem.o
rknpu-y += rknpu_fence.o
