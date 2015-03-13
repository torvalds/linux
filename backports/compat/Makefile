ccflags-y += -I$(src)
ifeq ($(CONFIG_BACKPORT_INTEGRATE),)
obj-m += compat.o
else
obj-y += compat.o
endif
compat-y += main.o

# Kernel backport compatibility code
compat-$(CONFIG_BACKPORT_KERNEL_3_0) += compat-3.0.o
compat-$(CONFIG_BACKPORT_KERNEL_3_1) += compat-3.1.o
compat-$(CONFIG_BACKPORT_KERNEL_3_2) += backport-3.2.o
compat-$(CONFIG_BACKPORT_KERNEL_3_3) += compat-3.3.o
compat-$(CONFIG_BACKPORT_KERNEL_3_4) += compat-3.4.o
compat-$(CONFIG_BACKPORT_KERNEL_3_5) += compat-3.5.o user_namespace.o
compat-$(CONFIG_BACKPORT_KERNEL_3_6) += compat-3.6.o
compat-$(CONFIG_BACKPORT_KERNEL_3_7) += compat-3.7.o
compat-$(CONFIG_BACKPORT_KERNEL_3_8) += compat-3.8.o
compat-$(CONFIG_BACKPORT_KERNEL_3_9) += compat-3.9.o
compat-$(CONFIG_BACKPORT_KERNEL_3_10) += backport-3.10.o
compat-$(CONFIG_BACKPORT_KERNEL_3_12) += backport-3.12.o
compat-$(CONFIG_BACKPORT_KERNEL_3_13) += backport-3.13.o
compat-$(CONFIG_BACKPORT_KERNEL_3_14) += backport-3.14.o
compat-$(CONFIG_BACKPORT_KERNEL_3_15) += backport-3.15.o
compat-$(CONFIG_BACKPORT_KERNEL_3_17) += backport-3.17.o
compat-$(CONFIG_BACKPORT_KERNEL_3_18) += backport-3.18.o
compat-$(CONFIG_BACKPORT_KERNEL_3_19) += backport-3.19.o

compat-$(CONFIG_BACKPORT_BPAUTO_BUILD_CRYPTO_CCM) += crypto-ccm.o
compat-$(CONFIG_BACKPORT_BPAUTO_BUILD_DMA_SHARED_HELPERS) += dma-shared-helpers.o
compat-$(CONFIG_BACKPORT_BPAUTO_BUILD_WANT_DEV_COREDUMP) += drivers-base-devcoredump.o
compat-$(CONFIG_BACKPORT_BPAUTO_BUILD_AVERAGE) += lib-average.o
cordic-objs += lib-cordic.o
obj-$(CONFIG_BACKPORT_BPAUTO_BUILD_CORDIC) += cordic.o
compat-$(CONFIG_BACKPORT_BPAUTO_BUILD_CRYPTO_CCM) += crypto-ccm.o
