obj-$(CONFIG_SH_DMAE_BASE) += shdma-base.o shdma-of.o
obj-$(CONFIG_SH_DMAE) += shdma.o
shdma-y := shdmac.o
ifeq ($(CONFIG_OF),y)
shdma-$(CONFIG_SHDMA_R8A73A4) += shdma-r8a73a4.o
endif
shdma-objs := $(shdma-y)
obj-$(CONFIG_SUDMAC) += sudmac.o
obj-$(CONFIG_RCAR_HPB_DMAE) += rcar-hpbdma.o
