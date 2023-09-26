# SPDX-License-Identifier: GPL-2.0
obj-$(CONFIG_RPMSG)		+= rpmsg_core.o
obj-$(CONFIG_RPMSG_CHAR)	+= rpmsg_char.o
obj-$(CONFIG_RPMSG_CTRL)	+= rpmsg_ctrl.o
obj-$(CONFIG_RPMSG_NS)		+= rpmsg_ns.o
obj-$(CONFIG_RPMSG_MTK_SCP)	+= mtk_rpmsg.o
qcom_glink-objs			:= qcom_glink_native.o qcom_glink_ssr.o
qcom_glink-objs			+= qcom_glink_memshare.o
obj-$(CONFIG_RPMSG_QCOM_GLINK) += qcom_glink.o
obj-$(CONFIG_RPMSG_QCOM_GLINK_RPM) += qcom_glink_rpm.o
obj-$(CONFIG_RPMSG_QCOM_GLINK_SMEM) += qcom_glink_smem.o
obj-$(CONFIG_RPMSG_QCOM_GLINK_SPSS) += qcom_glink_spss.o
qcom_glink_cma-objs		:= qcom_glink_cma_core.o virtio_glink_cma.o
obj-$(CONFIG_RPMSG_QCOM_GLINK_CMA) += qcom_glink_cma.o
obj-$(CONFIG_RPMSG_QCOM_SMD)	+= qcom_smd.o
obj-$(CONFIG_QCOM_GLINK_PKT) += glink_pkt.o
obj-$(CONFIG_RPMSG_VIRTIO)	+= virtio_rpmsg_bus.o
obj-$(CONFIG_MSM_RPM_SMD)   +=  rpm-smd.o
