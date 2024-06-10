# SPDX-License-Identifier: GPL-2.0
# link order matters; don't link the more generic brcmstb_nand.o before the
# more specific iproc_nand.o, for instance
obj-$(CONFIG_MTD_NAND_BRCMNAND_IPROC)	+= iproc_nand.o
obj-$(CONFIG_MTD_NAND_BRCMNAND_BCMBCA)	+= bcmbca_nand.o
obj-$(CONFIG_MTD_NAND_BRCMNAND_BCM63XX)	+= bcm6368_nand.o
obj-$(CONFIG_MTD_NAND_BRCMNAND_BRCMSTB)	+= brcmstb_nand.o
obj-$(CONFIG_MTD_NAND_BRCMNAND)		+= brcmnand.o

obj-$(CONFIG_MTD_NAND_BRCMNAND_BCMA)	+= bcma_nand.o
