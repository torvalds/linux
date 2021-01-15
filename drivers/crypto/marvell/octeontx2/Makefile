# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT) += octeontx2-cpt.o

octeontx2-cpt-objs := otx2_cptpf_main.o otx2_cptpf_mbox.o \
		      otx2_cpt_mbox_common.o

ccflags-y += -I$(srctree)/drivers/net/ethernet/marvell/octeontx2/af
