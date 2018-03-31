ccflags-y := -Idrivers/net/ethernet/chelsio/cxgb4

obj-$(CONFIG_CRYPTO_DEV_CHELSIO) += chcr.o
chcr-objs :=  chcr_core.o chcr_algo.o
chcr-$(CONFIG_CHELSIO_IPSEC_INLINE) += chcr_ipsec.o
obj-$(CONFIG_CRYPTO_DEV_CHELSIO_TLS) += chtls/
