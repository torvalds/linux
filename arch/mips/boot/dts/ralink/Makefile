# SPDX-License-Identifier: GPL-2.0
dtb-$(CONFIG_DTB_RT2880_EVAL)	+= rt2880_eval.dtb
dtb-$(CONFIG_DTB_RT305X_EVAL)	+= rt3052_eval.dtb
dtb-$(CONFIG_DTB_RT3883_EVAL)	+= rt3883_eval.dtb
dtb-$(CONFIG_DTB_MT7620A_EVAL)	+= mt7620a_eval.dtb
dtb-$(CONFIG_DTB_OMEGA2P)	+= omega2p.dtb
dtb-$(CONFIG_DTB_VOCORE2)	+= vocore2.dtb

obj-y				+= $(patsubst %.dtb, %.dtb.o, $(dtb-y))
