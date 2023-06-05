# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_NET_DSA_AR9331)	+= ar9331.o
obj-$(CONFIG_NET_DSA_QCA8K)	+= qca8k.o
qca8k-y 			+= qca8k-common.o qca8k-8xxx.o
ifdef CONFIG_NET_DSA_QCA8K_LEDS_SUPPORT
qca8k-y				+= qca8k-leds.o
endif
