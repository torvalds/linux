# SPDX-License-Identifier: GPL-2.0-only
qcom_hgsl-objs = hgsl.o \
            hgsl_sync.o \
            hgsl_memory.o \
            hgsl_hyp.o \
            hgsl_hyp_socket.o \
            hgsl_gmugos.o

qcom_hgsl-$(CONFIG_SYSFS) += hgsl_sysfs.o
qcom_hgsl-$(CONFIG_DEBUG_FS) += hgsl_debugfs.o
qcom_hgsl-$(CONFIG_QCOM_HGSL_TCSR_SIGNAL) += hgsl_tcsr.o

obj-$(CONFIG_QCOM_HGSL) += qcom_hgsl.o
