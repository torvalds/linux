# SPDX-License-Identifier: GPL-2.0

ccflags-y			+= -I$(src)

obj-$(CONFIG_NVME_COMMON)	+= nvme-common.o

nvme-common-$(CONFIG_NVME_AUTH)	+= auth.o
nvme-common-$(CONFIG_NVME_KEYRING) += keyring.o
