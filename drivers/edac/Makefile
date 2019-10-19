#
# Makefile for the Linux kernel EDAC drivers.
#
# Copyright 02 Jul 2003, Linux Networx (http://lnxi.com)
# This file may be distributed under the terms of the
# GNU General Public License.
#

obj-$(CONFIG_EDAC)			:= edac_core.o

edac_core-y	:= edac_mc.o edac_device.o edac_mc_sysfs.o
edac_core-y	+= edac_module.o edac_device_sysfs.o wq.o

edac_core-$(CONFIG_EDAC_DEBUG)		+= debugfs.o

ifdef CONFIG_PCI
edac_core-y	+= edac_pci.o edac_pci_sysfs.o
endif

obj-$(CONFIG_EDAC_GHES)			+= ghes_edac.o

edac_mce_amd-y				:= mce_amd.o
obj-$(CONFIG_EDAC_DECODE_MCE)		+= edac_mce_amd.o

obj-$(CONFIG_EDAC_AMD76X)		+= amd76x_edac.o
obj-$(CONFIG_EDAC_CPC925)		+= cpc925_edac.o
obj-$(CONFIG_EDAC_I5000)		+= i5000_edac.o
obj-$(CONFIG_EDAC_I5100)		+= i5100_edac.o
obj-$(CONFIG_EDAC_I5400)		+= i5400_edac.o
obj-$(CONFIG_EDAC_I7300)		+= i7300_edac.o
obj-$(CONFIG_EDAC_I7CORE)		+= i7core_edac.o
obj-$(CONFIG_EDAC_SBRIDGE)		+= sb_edac.o
obj-$(CONFIG_EDAC_PND2)			+= pnd2_edac.o
obj-$(CONFIG_EDAC_E7XXX)		+= e7xxx_edac.o
obj-$(CONFIG_EDAC_E752X)		+= e752x_edac.o
obj-$(CONFIG_EDAC_I82443BXGX)		+= i82443bxgx_edac.o
obj-$(CONFIG_EDAC_I82875P)		+= i82875p_edac.o
obj-$(CONFIG_EDAC_I82975X)		+= i82975x_edac.o
obj-$(CONFIG_EDAC_I3000)		+= i3000_edac.o
obj-$(CONFIG_EDAC_I3200)		+= i3200_edac.o
obj-$(CONFIG_EDAC_IE31200)		+= ie31200_edac.o
obj-$(CONFIG_EDAC_X38)			+= x38_edac.o
obj-$(CONFIG_EDAC_I82860)		+= i82860_edac.o
obj-$(CONFIG_EDAC_R82600)		+= r82600_edac.o

amd64_edac_mod-y := amd64_edac.o
amd64_edac_mod-$(CONFIG_EDAC_DEBUG) += amd64_edac_dbg.o
amd64_edac_mod-$(CONFIG_EDAC_AMD64_ERROR_INJECTION) += amd64_edac_inj.o

obj-$(CONFIG_EDAC_AMD64)		+= amd64_edac_mod.o

obj-$(CONFIG_EDAC_PASEMI)		+= pasemi_edac.o

mpc85xx_edac_mod-y			:= fsl_ddr_edac.o mpc85xx_edac.o
obj-$(CONFIG_EDAC_MPC85XX)		+= mpc85xx_edac_mod.o

layerscape_edac_mod-y			:= fsl_ddr_edac.o layerscape_edac.o
obj-$(CONFIG_EDAC_LAYERSCAPE)		+= layerscape_edac_mod.o

skx_edac-y				:= skx_common.o skx_base.o
obj-$(CONFIG_EDAC_SKX)			+= skx_edac.o

i10nm_edac-y				:= skx_common.o i10nm_base.o
obj-$(CONFIG_EDAC_I10NM)		+= i10nm_edac.o

obj-$(CONFIG_EDAC_MV64X60)		+= mv64x60_edac.o
obj-$(CONFIG_EDAC_CELL)			+= cell_edac.o
obj-$(CONFIG_EDAC_PPC4XX)		+= ppc4xx_edac.o
obj-$(CONFIG_EDAC_AMD8111)		+= amd8111_edac.o
obj-$(CONFIG_EDAC_AMD8131)		+= amd8131_edac.o

obj-$(CONFIG_EDAC_HIGHBANK_MC)		+= highbank_mc_edac.o
obj-$(CONFIG_EDAC_HIGHBANK_L2)		+= highbank_l2_edac.o

obj-$(CONFIG_EDAC_OCTEON_PC)		+= octeon_edac-pc.o
obj-$(CONFIG_EDAC_OCTEON_L2C)		+= octeon_edac-l2c.o
obj-$(CONFIG_EDAC_OCTEON_LMC)		+= octeon_edac-lmc.o
obj-$(CONFIG_EDAC_OCTEON_PCI)		+= octeon_edac-pci.o
obj-$(CONFIG_EDAC_THUNDERX)		+= thunderx_edac.o

obj-$(CONFIG_EDAC_ALTERA)		+= altera_edac.o
obj-$(CONFIG_EDAC_SIFIVE)		+= sifive_edac.o
obj-$(CONFIG_EDAC_ARMADA_XP)		+= armada_xp_edac.o
obj-$(CONFIG_EDAC_SYNOPSYS)		+= synopsys_edac.o
obj-$(CONFIG_EDAC_XGENE)		+= xgene_edac.o
obj-$(CONFIG_EDAC_TI)			+= ti_edac.o
obj-$(CONFIG_EDAC_QCOM)			+= qcom_edac.o
obj-$(CONFIG_EDAC_ASPEED)		+= aspeed_edac.o
obj-$(CONFIG_EDAC_BLUEFIELD)		+= bluefield_edac.o
