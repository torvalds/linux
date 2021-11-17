# SPDX-License-Identifier: GPL-2.0
subdir-ccflags-y +=  -I$(srctree)/drivers/net/ethernet/freescale/fman

obj-$(CONFIG_FSL_FMAN) += fsl_dpaa_fman.o
obj-$(CONFIG_FSL_FMAN) += fsl_dpaa_fman_port.o
obj-$(CONFIG_FSL_FMAN) += fsl_dpaa_mac.o

fsl_dpaa_fman-objs	:= fman_muram.o fman.o fman_sp.o fman_keygen.o
fsl_dpaa_fman_port-objs := fman_port.o
fsl_dpaa_mac-objs:= mac.o fman_dtsec.o fman_memac.o fman_tgec.o
