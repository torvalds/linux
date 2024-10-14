# SPDX-License-Identifier: GPL-2.0
#
# S/390 crypto devices
#

ap-objs := ap_bus.o ap_card.o ap_queue.o
obj-$(CONFIG_AP) += ap.o
# zcrypt_api.o and zcrypt_msgtype*.o depend on ap.o
zcrypt-objs := zcrypt_api.o zcrypt_card.o zcrypt_queue.o
zcrypt-objs += zcrypt_msgtype6.o zcrypt_msgtype50.o
zcrypt-objs += zcrypt_ccamisc.o zcrypt_ep11misc.o
obj-$(CONFIG_ZCRYPT) += zcrypt.o
# adapter drivers depend on ap.o and zcrypt.o
obj-$(CONFIG_ZCRYPT) += zcrypt_cex4.o

# pkey base and api module
pkey-objs := pkey_base.o pkey_api.o pkey_sysfs.o
obj-$(CONFIG_PKEY) += pkey.o

# pkey cca handler module
pkey-cca-objs := pkey_cca.o
obj-$(CONFIG_PKEY_CCA) += pkey-cca.o

# pkey ep11 handler module
pkey-ep11-objs := pkey_ep11.o
obj-$(CONFIG_PKEY_EP11) += pkey-ep11.o

# pkey pckmo handler module
pkey-pckmo-objs := pkey_pckmo.o
obj-$(CONFIG_PKEY_PCKMO) += pkey-pckmo.o

# adjunct processor matrix
vfio_ap-objs := vfio_ap_drv.o vfio_ap_ops.o
obj-$(CONFIG_VFIO_AP) += vfio_ap.o
