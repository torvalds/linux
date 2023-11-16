# SPDX-License-Identifier: GPL-2.0

ccflags-y			+= -I$(src)

obj-$(CONFIG_NVME_AUTH)		+= nvme-auth.o
obj-$(CONFIG_NVME_KEYRING)	+= nvme-keyring.o

nvme-auth-y			+= auth.o
nvme-keyring-y			+= keyring.o
