# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(srctree)/$(src)/include

rve-y	:= rve_drv.o rve_job.o rve_reg.o
rve-$(CONFIG_ROCKCHIP_RVE_DEBUGGER) += rve_debugger.o
rve-$(CONFIG_SYNC_FILE) += rve_fence.o

obj-$(CONFIG_ROCKCHIP_RVE)	+= rve.o
