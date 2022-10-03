# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Marvell's RVU Admin Function driver
#

ccflags-y += -I$(src)
obj-$(CONFIG_OCTEONTX2_MBOX) += rvu_mbox.o
obj-$(CONFIG_OCTEONTX2_AF) += rvu_af.o

rvu_mbox-y := mbox.o rvu_trace.o
rvu_af-y := cgx.o rvu.o rvu_cgx.o rvu_npa.o rvu_nix.o \
		  rvu_reg.o rvu_npc.o rvu_debugfs.o ptp.o rvu_npc_fs.o \
		  rvu_cpt.o rvu_devlink.o rpm.o rvu_cn10k.o rvu_switch.o \
		  rvu_sdp.o rvu_npc_hash.o
