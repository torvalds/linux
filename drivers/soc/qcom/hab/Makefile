# SPDX-License-Identifier: GPL-2.0-only
CFLAGS_hab.o := -I$(src)

# Common code
msm_hab-objs = \
	hab.o \
	hab_msg.o \
	hab_vchan.o \
	hab_pchan.o \
	hab_open.o \
	hab_mimex.o \
	hab_pipe.o \
	hab_parser.o \
	hab_stat.o

# Common code in Linux
msm_hab-objs +=	\
	khab.o \
	hab_linux.o \
	hab_mem_linux.o \
	khab_test.o

# Hypervisor specific
ifdef CONFIG_GHS_VMM
msm_hab-objs += \
	ghs_comm.o \
	ghs_comm_linux.o \
	hab_ghs.o \
	hab_ghs_linux.o
else
ifdef CONFIG_MSM_VIRTIO_HAB
msm_hab-objs += hab_virtio.o
else
ifdef CONFIG_MSM_VHOST_HAB
ccflags-y += -I$(srctree)/drivers/vhost
msm_hab-objs += hab_vhost.o
else
ifdef CONFIG_QTI_QUIN_GVM
msm_hab-objs += \
	qvm_comm.o \
	qvm_comm_linux.o \
	hab_qvm.o \
	hab_qvm_linux.o
else
msm_hab-objs += \
	hab_comm.o \
	hyp_stub.o
endif
endif
endif
endif

obj-$(CONFIG_MSM_HAB) += msm_hab.o
