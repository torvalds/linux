# SPDX-License-Identifier: GPL-2.0
#
# S/390 crypto devices
#

ap-objs := ap_bus.o ap_card.o ap_queue.o
obj-$(subst m,y,$(CONFIG_ZCRYPT)) += ap.o
# zcrypt_api.o and zcrypt_msgtype*.o depend on ap.o
zcrypt-objs := zcrypt_api.o zcrypt_card.o zcrypt_queue.o
zcrypt-objs += zcrypt_msgtype6.o zcrypt_msgtype50.o
obj-$(CONFIG_ZCRYPT) += zcrypt.o
# adapter drivers depend on ap.o and zcrypt.o
obj-$(CONFIG_ZCRYPT) += zcrypt_cex2c.o zcrypt_cex2a.o zcrypt_cex4.o

# pkey kernel module
pkey-objs := pkey_api.o
obj-$(CONFIG_PKEY) += pkey.o

# adjunct processor matrix
vfio_ap-objs := vfio_ap_drv.o vfio_ap_ops.o
obj-$(CONFIG_VFIO_AP) += vfio_ap.o
