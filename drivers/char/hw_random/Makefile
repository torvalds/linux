#
# Makefile for HW Random Number Generator (RNG) device drivers.
#

obj-$(CONFIG_HW_RANDOM) += rng-core.o
rng-core-y := core.o
obj-$(CONFIG_HW_RANDOM_TIMERIOMEM) += timeriomem-rng.o
obj-$(CONFIG_HW_RANDOM_INTEL) += intel-rng.o
obj-$(CONFIG_HW_RANDOM_AMD) += amd-rng.o
obj-$(CONFIG_HW_RANDOM_GEODE) += geode-rng.o
obj-$(CONFIG_HW_RANDOM_N2RNG) += n2-rng.o
n2-rng-y := n2-drv.o n2-asm.o
obj-$(CONFIG_HW_RANDOM_VIA) += via-rng.o
obj-$(CONFIG_HW_RANDOM_IXP4XX) += ixp4xx-rng.o
obj-$(CONFIG_HW_RANDOM_OMAP) += omap-rng.o
obj-$(CONFIG_HW_RANDOM_PASEMI) += pasemi-rng.o
obj-$(CONFIG_HW_RANDOM_VIRTIO) += virtio-rng.o
obj-$(CONFIG_HW_RANDOM_TX4939) += tx4939-rng.o
obj-$(CONFIG_HW_RANDOM_MXC_RNGA) += mxc-rnga.o
obj-$(CONFIG_HW_RANDOM_OCTEON) += octeon-rng.o
obj-$(CONFIG_HW_RANDOM_NOMADIK) += nomadik-rng.o
obj-$(CONFIG_HW_RANDOM_PICOXCELL) += picoxcell-rng.o
