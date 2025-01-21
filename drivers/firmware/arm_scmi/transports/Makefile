# SPDX-License-Identifier: GPL-2.0-only
# Keep before scmi_transport_mailbox.o to allow precedence
# while matching the compatible.
scmi_transport_smc-objs := smc.o
obj-$(CONFIG_ARM_SCMI_TRANSPORT_SMC) += scmi_transport_smc.o
scmi_transport_mailbox-objs := mailbox.o
obj-$(CONFIG_ARM_SCMI_TRANSPORT_MAILBOX) += scmi_transport_mailbox.o
scmi_transport_optee-objs := optee.o
obj-$(CONFIG_ARM_SCMI_TRANSPORT_OPTEE) += scmi_transport_optee.o
scmi_transport_virtio-objs := virtio.o
obj-$(CONFIG_ARM_SCMI_TRANSPORT_VIRTIO) += scmi_transport_virtio.o

ifeq ($(CONFIG_THUMB2_KERNEL)$(CONFIG_CC_IS_CLANG),yy)
# The use of R7 in the SMCCC conflicts with the compiler's use of R7 as a frame
# pointer in Thumb2 mode, which is forcibly enabled by Clang when profiling
# hooks are inserted via the -pg switch.
CFLAGS_REMOVE_smc.o += $(CC_FLAGS_FTRACE)
endif
