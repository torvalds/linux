# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_XENFS) += xenfs.o

xenfs-y			  = super.o
xenfs-$(CONFIG_XEN_DOM0) += xenstored.o
xenfs-$(CONFIG_XEN_SYMS) += xensyms.o
