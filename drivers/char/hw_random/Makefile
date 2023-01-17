# SPDX-License-Identifier: GPL-2.0
#
# Makefile for HW Random Number Generator (RNG) device drivers.
#

obj-$(CONFIG_HW_RANDOM) += rng-core.o
rng-core-y := core.o
obj-$(CONFIG_HW_RANDOM_TIMERIOMEM) += timeriomem-rng.o
obj-$(CONFIG_HW_RANDOM_INTEL) += intel-rng.o
obj-$(CONFIG_HW_RANDOM_AMD) += amd-rng.o
obj-$(CONFIG_HW_RANDOM_ATMEL) += atmel-rng.o
obj-$(CONFIG_HW_RANDOM_BA431) += ba431-rng.o
obj-$(CONFIG_HW_RANDOM_GEODE) += geode-rng.o
obj-$(CONFIG_HW_RANDOM_N2RNG) += n2-rng.o
n2-rng-y := n2-drv.o n2-asm.o
obj-$(CONFIG_HW_RANDOM_VIA) += via-rng.o
obj-$(CONFIG_HW_RANDOM_EXYNOS) += exynos-trng.o
obj-$(CONFIG_HW_RANDOM_IXP4XX) += ixp4xx-rng.o
obj-$(CONFIG_HW_RANDOM_OMAP) += omap-rng.o
obj-$(CONFIG_HW_RANDOM_OMAP3_ROM) += omap3-rom-rng.o
obj-$(CONFIG_HW_RANDOM_PASEMI) += pasemi-rng.o
obj-$(CONFIG_HW_RANDOM_VIRTIO) += virtio-rng.o
obj-$(CONFIG_HW_RANDOM_MXC_RNGA) += mxc-rnga.o
obj-$(CONFIG_HW_RANDOM_IMX_RNGC) += imx-rngc.o
obj-$(CONFIG_HW_RANDOM_INGENIC_RNG) += ingenic-rng.o
obj-$(CONFIG_HW_RANDOM_INGENIC_TRNG) += ingenic-trng.o
obj-$(CONFIG_HW_RANDOM_OCTEON) += octeon-rng.o
obj-$(CONFIG_HW_RANDOM_NOMADIK) += nomadik-rng.o
obj-$(CONFIG_HW_RANDOM_PSERIES) += pseries-rng.o
obj-$(CONFIG_HW_RANDOM_POWERNV) += powernv-rng.o
obj-$(CONFIG_HW_RANDOM_HISI)	+= hisi-rng.o
obj-$(CONFIG_HW_RANDOM_BCM2835) += bcm2835-rng.o
obj-$(CONFIG_HW_RANDOM_IPROC_RNG200) += iproc-rng200.o
obj-$(CONFIG_HW_RANDOM_ST) += st-rng.o
obj-$(CONFIG_HW_RANDOM_XGENE) += xgene-rng.o
obj-$(CONFIG_HW_RANDOM_STM32) += stm32-rng.o
obj-$(CONFIG_HW_RANDOM_PIC32) += pic32-rng.o
obj-$(CONFIG_HW_RANDOM_MESON) += meson-rng.o
obj-$(CONFIG_HW_RANDOM_CAVIUM) += cavium-rng.o cavium-rng-vf.o
obj-$(CONFIG_HW_RANDOM_MTK)	+= mtk-rng.o
obj-$(CONFIG_HW_RANDOM_S390) += s390-trng.o
obj-$(CONFIG_HW_RANDOM_KEYSTONE) += ks-sa-rng.o
obj-$(CONFIG_HW_RANDOM_OPTEE) += optee-rng.o
obj-$(CONFIG_HW_RANDOM_NPCM) += npcm-rng.o
obj-$(CONFIG_HW_RANDOM_CCTRNG) += cctrng.o
obj-$(CONFIG_HW_RANDOM_XIPHERA) += xiphera-trng.o
obj-$(CONFIG_HW_RANDOM_ARM_SMCCC_TRNG) += arm_smccc_trng.o
obj-$(CONFIG_HW_RANDOM_CN10K) += cn10k-rng.o
obj-$(CONFIG_HW_RANDOM_POLARFIRE_SOC) += mpfs-rng.o
obj-$(CONFIG_HW_RANDOM_JH7110) += jh7110-trng.o
