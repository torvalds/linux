# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_DW_EDMA)		+= dw-edma.o
dw-edma-$(CONFIG_DEBUG_FS)	:= dw-edma-v0-debugfs.o	\
				   dw-hdma-v0-debugfs.o
dw-edma-objs			:= dw-edma-core.o	\
				   dw-edma-v0-core.o	\
				   dw-hdma-v0-core.o $(dw-edma-y)
obj-$(CONFIG_DW_EDMA_PCIE)	+= dw-edma-pcie.o
