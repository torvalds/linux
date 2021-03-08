# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Broadcom VK driver
#

obj-$(CONFIG_BCM_VK) += bcm_vk.o
bcm_vk-objs := \
	bcm_vk_dev.o \
	bcm_vk_msg.o \
	bcm_vk_sg.o

bcm_vk-$(CONFIG_BCM_VK_TTY) += bcm_vk_tty.o
