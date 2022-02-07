# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT) += rvu_cptpf.o rvu_cptvf.o

rvu_cptpf-objs := otx2_cptpf_main.o otx2_cptpf_mbox.o \
		  otx2_cpt_mbox_common.o otx2_cptpf_ucode.o otx2_cptlf.o \
		  cn10k_cpt.o otx2_cpt_devlink.o
rvu_cptvf-objs := otx2_cptvf_main.o otx2_cptvf_mbox.o otx2_cptlf.o \
		  otx2_cpt_mbox_common.o otx2_cptvf_reqmgr.o \
		  otx2_cptvf_algs.o cn10k_cpt.o

ccflags-y += -I$(srctree)/drivers/net/ethernet/marvell/octeontx2/af
