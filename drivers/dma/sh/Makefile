#
# DMA Engine Helpers
#

obj-$(CONFIG_SH_DMAE_BASE) += shdma-base.o shdma-of.o

#
# DMA Controllers
#

shdma-y := shdmac.o
shdma-$(CONFIG_SH_DMAE_R8A73A4) += shdma-r8a73a4.o
shdma-objs := $(shdma-y)
obj-$(CONFIG_SH_DMAE) += shdma.o

obj-$(CONFIG_SUDMAC) += sudmac.o
obj-$(CONFIG_RCAR_HPB_DMAE) += rcar-hpbdma.o
obj-$(CONFIG_RCAR_AUDMAC_PP) += rcar-audmapp.o
