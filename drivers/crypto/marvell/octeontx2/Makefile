# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_CRYPTO_DEV_OCTEONTX2_CPT) += octeontx2-cpt.o octeontx2-cptvf.o

octeontx2-cpt-objs := otx2_cptpf_main.o otx2_cptpf_mbox.o \
		      otx2_cpt_mbox_common.o otx2_cptpf_ucode.o otx2_cptlf.o
octeontx2-cptvf-objs := otx2_cptvf_main.o otx2_cptvf_mbox.o otx2_cptlf.o \
			otx2_cpt_mbox_common.o otx2_cptvf_reqmgr.o \
			otx2_cptvf_algs.o

ccflags-y += -I$(srctree)/drivers/net/ethernet/marvell/octeontx2/af
