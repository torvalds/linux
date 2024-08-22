# SPDX-License-Identifier: GPL-2.0-only

ifneq ($(CONFIG_VIRTIO_NPU),)
obj-$(CONFIG_VIRTIO_NPU) := virtio_npu.o
else
msm_npu-objs := npu_dbg.o \
                npu_dev.o \
                npu_debugfs.o \
                npu_host_ipc.o \
                npu_hw_access.o \
                npu_mgr.o

obj-$(CONFIG_MSM_NPU) := msm_npu.o
endif
