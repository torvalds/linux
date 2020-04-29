# SPDX-License-Identifier: GPL-2.0
#
# Makefile for Marvell's OcteonTX2 RVU Admin Function driver
#

obj-$(CONFIG_OCTEONTX2_MBOX) += octeontx2_mbox.o
obj-$(CONFIG_OCTEONTX2_AF) += octeontx2_af.o

octeontx2_mbox-y := mbox.o
octeontx2_af-y := cgx.o rvu.o rvu_cgx.o rvu_npa.o rvu_nix.o \
		  rvu_reg.o rvu_npc.o rvu_debugfs.o
