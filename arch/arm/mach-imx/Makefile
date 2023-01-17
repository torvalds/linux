# SPDX-License-Identifier: GPL-2.0
obj-y := cpu.o system.o irq-common.o

obj-$(CONFIG_SOC_IMX25) += cpu-imx25.o mach-imx25.o pm-imx25.o

obj-$(CONFIG_SOC_IMX27) += cpu-imx27.o pm-imx27.o mach-imx27.o

obj-$(CONFIG_SOC_IMX31) += mm-imx3.o cpu-imx31.o mach-imx31.o
obj-$(CONFIG_SOC_IMX35) += mm-imx3.o cpu-imx35.o mach-imx35.o

imx5-pm-$(CONFIG_PM) += pm-imx5.o
obj-$(CONFIG_SOC_IMX5) += cpu-imx5.o $(imx5-pm-y)

obj-$(CONFIG_MXC_TZIC) += tzic.o
obj-$(CONFIG_MXC_AVIC) += avic.o

ifeq ($(CONFIG_CPU_IDLE),y)
obj-$(CONFIG_SOC_IMX5) += cpuidle-imx5.o
obj-$(CONFIG_SOC_IMX6Q) += cpuidle-imx6q.o
obj-$(CONFIG_SOC_IMX6SL) += cpuidle-imx6sl.o
obj-$(CONFIG_SOC_IMX6SLL) += cpuidle-imx6sx.o
obj-$(CONFIG_SOC_IMX6SX) += cpuidle-imx6sx.o
obj-$(CONFIG_SOC_IMX6UL) += cpuidle-imx6sx.o
obj-$(CONFIG_SOC_IMX7ULP) += cpuidle-imx7ulp.o
endif

ifdef CONFIG_SND_SOC_IMX_PCM_FIQ
obj-y += ssi-fiq.o
obj-y += ssi-fiq-ksym.o
endif

obj-$(CONFIG_HAVE_IMX_ANATOP) += anatop.o
obj-$(CONFIG_HAVE_IMX_GPC) += gpc.o
obj-$(CONFIG_HAVE_IMX_MMDC) += mmdc.o
obj-$(CONFIG_HAVE_IMX_SRC) += src.o
ifneq ($(CONFIG_SOC_IMX6)$(CONFIG_SOC_IMX7D_CA7)$(CONFIG_SOC_LS1021A),)
obj-$(CONFIG_SMP) += headsmp.o platsmp.o
obj-$(CONFIG_HOTPLUG_CPU) += hotplug.o
endif
obj-$(CONFIG_SOC_IMX6Q) += mach-imx6q.o
obj-$(CONFIG_SOC_IMX6SL) += mach-imx6sl.o
obj-$(CONFIG_SOC_IMX6SLL) += mach-imx6sl.o
obj-$(CONFIG_SOC_IMX6SX) += mach-imx6sx.o
obj-$(CONFIG_SOC_IMX6UL) += mach-imx6ul.o
obj-$(CONFIG_SOC_IMX7D_CA7) += mach-imx7d.o
obj-$(CONFIG_SOC_IMX7D_CM4) += mach-imx7d-cm4.o
obj-$(CONFIG_SOC_IMX7ULP) += mach-imx7ulp.o pm-imx7ulp.o

ifeq ($(CONFIG_SUSPEND),y)
obj-$(CONFIG_SOC_IMX6) += suspend-imx6.o
obj-$(CONFIG_SOC_IMX53) += suspend-imx53.o
endif
ifeq ($(CONFIG_ARM_CPU_SUSPEND),y)
obj-$(CONFIG_SOC_IMX6) += resume-imx6.o
endif
obj-$(CONFIG_SOC_IMX6) += pm-imx6.o

obj-$(CONFIG_SOC_IMX1) += mach-imx1.o
obj-$(CONFIG_SOC_IMX50) += mach-imx50.o
obj-$(CONFIG_SOC_IMX51) += mach-imx51.o
obj-$(CONFIG_SOC_IMX53) += mach-imx53.o

obj-$(CONFIG_SOC_IMXRT) += mach-imxrt.o

obj-$(CONFIG_SOC_VF610) += mach-vf610.o

obj-$(CONFIG_SOC_LS1021A) += mach-ls1021a.o
