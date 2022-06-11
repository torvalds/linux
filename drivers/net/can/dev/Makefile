# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_CAN_DEV) += can-dev.o

can-dev-y += skb.o

can-dev-$(CONFIG_CAN_CALC_BITTIMING) += calc_bittiming.o
can-dev-$(CONFIG_CAN_NETLINK) += bittiming.o
can-dev-$(CONFIG_CAN_NETLINK) += dev.o
can-dev-$(CONFIG_CAN_NETLINK) += length.o
can-dev-$(CONFIG_CAN_NETLINK) += netlink.o
can-dev-$(CONFIG_CAN_RX_OFFLOAD) += rx-offload.o
