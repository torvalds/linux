#
# Makefile for the linux kernel.
#

ccflags-$(CONFIG_ARCH_MULTIPLATFORM) := -I$(srctree)/$(src)/include \
	-I$(srctree)/arch/arm/plat-omap/include

# Common support
obj-y := id.o io.o control.o mux.o devices.o fb.o serial.o timer.o pm.o \
	 common.o gpio.o dma.o wd_timer.o display.o i2c.o hdq1w.o omap_hwmod.o \
	 omap_device.o sram.o drm.o

hwmod-common				= omap_hwmod.o omap_hwmod_reset.o \
					  omap_hwmod_common_data.o
clock-common				= clock.o clock_common_data.o \
					  clkt_dpll.o clkt_clksel.o
secure-common				= omap-smc.o omap-secure.o

obj-$(CONFIG_ARCH_OMAP2) += $(omap-2-3-common) $(hwmod-common)
obj-$(CONFIG_ARCH_OMAP3) += $(omap-2-3-common) $(hwmod-common) $(secure-common)
obj-$(CONFIG_ARCH_OMAP4) += $(hwmod-common) $(secure-common)
obj-$(CONFIG_SOC_AM33XX) += $(hwmod-common)
obj-$(CONFIG_SOC_OMAP5)	 += $(hwmod-common) $(secure-common)
obj-$(CONFIG_SOC_AM43XX) += $(hwmod-common) $(secure-common)
obj-$(CONFIG_SOC_DRA7XX) += $(hwmod-common) $(secure-common)

ifneq ($(CONFIG_SND_OMAP_SOC_MCBSP),)
obj-y += mcbsp.o
endif

obj-$(CONFIG_TWL4030_CORE) += omap_twl.o
obj-$(CONFIG_SOC_HAS_OMAP2_SDRC)	+= sdrc.o

# SMP support ONLY available for OMAP4

smp-$(CONFIG_SMP)			+= omap-smp.o omap-headsmp.o
smp-$(CONFIG_HOTPLUG_CPU)		+= omap-hotplug.o
omap-4-5-common				=  omap4-common.o omap-wakeupgen.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(omap-4-5-common) $(smp-y) sleep44xx.o
obj-$(CONFIG_SOC_OMAP5)			+= $(omap-4-5-common) $(smp-y) sleep44xx.o
obj-$(CONFIG_SOC_AM43XX)		+= $(omap-4-5-common)
obj-$(CONFIG_SOC_DRA7XX)		+= $(omap-4-5-common) $(smp-y) sleep44xx.o

plus_sec := $(call as-instr,.arch_extension sec,+sec)
AFLAGS_omap-headsmp.o			:=-Wa,-march=armv7-a$(plus_sec)
AFLAGS_omap-smc.o			:=-Wa,-march=armv7-a$(plus_sec)
AFLAGS_sleep44xx.o			:=-Wa,-march=armv7-a$(plus_sec)

# Functions loaded to SRAM
obj-$(CONFIG_SOC_OMAP2420)		+= sram242x.o
obj-$(CONFIG_SOC_OMAP2430)		+= sram243x.o
obj-$(CONFIG_ARCH_OMAP3)		+= sram34xx.o

AFLAGS_sram242x.o			:=-Wa,-march=armv6
AFLAGS_sram243x.o			:=-Wa,-march=armv6
AFLAGS_sram34xx.o			:=-Wa,-march=armv7-a

# Restart code (OMAP4/5 currently in omap4-common.c)
obj-$(CONFIG_SOC_OMAP2420)		+= omap2-restart.o
obj-$(CONFIG_SOC_OMAP2430)		+= omap2-restart.o
obj-$(CONFIG_SOC_TI81XX)		+= ti81xx-restart.o
obj-$(CONFIG_SOC_AM33XX)		+= am33xx-restart.o
obj-$(CONFIG_SOC_AM43XX)		+= omap4-restart.o
obj-$(CONFIG_ARCH_OMAP3)		+= omap3-restart.o
obj-$(CONFIG_ARCH_OMAP4)		+= omap4-restart.o
obj-$(CONFIG_SOC_OMAP5)			+= omap4-restart.o
obj-$(CONFIG_SOC_DRA7XX)		+= omap4-restart.o

# Pin multiplexing
obj-$(CONFIG_ARCH_OMAP3)		+= mux34xx.o

# SMS/SDRC
obj-$(CONFIG_ARCH_OMAP2)		+= sdrc2xxx.o
# obj-$(CONFIG_ARCH_OMAP3)		+= sdrc3xxx.o

# OPP table initialization
ifeq ($(CONFIG_PM_OPP),y)
obj-y					+= opp.o
obj-$(CONFIG_ARCH_OMAP3)		+= opp3xxx_data.o
obj-$(CONFIG_ARCH_OMAP4)		+= opp4xxx_data.o
endif

# Power Management
obj-$(CONFIG_OMAP_PM_NOOP)		+= omap-pm-noop.o

ifeq ($(CONFIG_PM),y)
obj-$(CONFIG_ARCH_OMAP2)		+= pm24xx.o
obj-$(CONFIG_ARCH_OMAP2)		+= sleep24xx.o
obj-$(CONFIG_ARCH_OMAP3)		+= pm34xx.o sleep34xx.o
omap-4-5-pm-common			=  pm44xx.o omap-mpuss-lowpower.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(omap-4-5-pm-common)
obj-$(CONFIG_SOC_OMAP5)			+= $(omap-4-5-pm-common)
obj-$(CONFIG_SOC_DRA7XX)		+= $(omap-4-5-pm-common)
obj-$(CONFIG_PM_DEBUG)			+= pm-debug.o

obj-$(CONFIG_POWER_AVS_OMAP)		+= sr_device.o
obj-$(CONFIG_POWER_AVS_OMAP_CLASS3)    += smartreflex-class3.o

AFLAGS_sleep24xx.o			:=-Wa,-march=armv6
AFLAGS_sleep34xx.o			:=-Wa,-march=armv7-a$(plus_sec)

endif

ifeq ($(CONFIG_CPU_IDLE),y)
obj-$(CONFIG_ARCH_OMAP3)                += cpuidle34xx.o
omap-4-5-idle-common			=  cpuidle44xx.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(omap-4-5-idle-common)
obj-$(CONFIG_SOC_OMAP5)			+= $(omap-4-5-idle-common)
obj-$(CONFIG_SOC_DRA7XX)		+= $(omap-4-5-idle-common)
endif

# PRCM
obj-y					+= prm_common.o cm_common.o
obj-$(CONFIG_ARCH_OMAP2)		+= prm2xxx_3xxx.o prm2xxx.o cm2xxx.o
obj-$(CONFIG_ARCH_OMAP3)		+= prm2xxx_3xxx.o prm3xxx.o cm3xxx.o
obj-$(CONFIG_ARCH_OMAP3)		+= vc3xxx_data.o vp3xxx_data.o
omap-prcm-4-5-common			=  cminst44xx.o prm44xx.o \
					   prcm_mpu44xx.o prminst44xx.o \
					   vc44xx_data.o vp44xx_data.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(omap-prcm-4-5-common)
obj-$(CONFIG_SOC_OMAP5)			+= $(omap-prcm-4-5-common)
obj-$(CONFIG_SOC_DRA7XX)		+= $(omap-prcm-4-5-common)
am33xx-43xx-prcm-common			+= prm33xx.o cm33xx.o
obj-$(CONFIG_SOC_TI81XX)		+= $(am33xx-43xx-prcm-common)
obj-$(CONFIG_SOC_AM33XX)		+= $(am33xx-43xx-prcm-common)
obj-$(CONFIG_SOC_AM43XX)		+= $(omap-prcm-4-5-common) \
					   $(am33xx-43xx-prcm-common)

# OMAP voltage domains
voltagedomain-common			:= voltage.o vc.o vp.o
obj-$(CONFIG_ARCH_OMAP2)		+= $(voltagedomain-common)
obj-$(CONFIG_ARCH_OMAP2)		+= voltagedomains2xxx_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= $(voltagedomain-common)
obj-$(CONFIG_ARCH_OMAP3)		+= voltagedomains3xxx_data.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(voltagedomain-common)
obj-$(CONFIG_ARCH_OMAP4)		+= voltagedomains44xx_data.o
obj-$(CONFIG_SOC_AM33XX)		+= $(voltagedomain-common)
obj-$(CONFIG_SOC_AM43XX)		+= $(voltagedomain-common)
obj-$(CONFIG_SOC_OMAP5)			+= $(voltagedomain-common)
obj-$(CONFIG_SOC_OMAP5)                += voltagedomains54xx_data.o
obj-$(CONFIG_SOC_DRA7XX)		+= $(voltagedomain-common)

# OMAP powerdomain framework
powerdomain-common			+= powerdomain.o powerdomain-common.o
obj-$(CONFIG_ARCH_OMAP2)		+= $(powerdomain-common)
obj-$(CONFIG_ARCH_OMAP2)		+= powerdomains2xxx_data.o
obj-$(CONFIG_ARCH_OMAP2)		+= powerdomains2xxx_3xxx_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= $(powerdomain-common)
obj-$(CONFIG_ARCH_OMAP3)		+= powerdomains3xxx_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= powerdomains2xxx_3xxx_data.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(powerdomain-common)
obj-$(CONFIG_ARCH_OMAP4)		+= powerdomains44xx_data.o
obj-$(CONFIG_SOC_AM33XX)		+= $(powerdomain-common)
obj-$(CONFIG_SOC_AM33XX)		+= powerdomains33xx_data.o
obj-$(CONFIG_SOC_AM43XX)		+= $(powerdomain-common)
obj-$(CONFIG_SOC_AM43XX)		+= powerdomains43xx_data.o
obj-$(CONFIG_SOC_OMAP5)			+= $(powerdomain-common)
obj-$(CONFIG_SOC_OMAP5)			+= powerdomains54xx_data.o
obj-$(CONFIG_SOC_DRA7XX)		+= $(powerdomain-common)
obj-$(CONFIG_SOC_DRA7XX)		+= powerdomains7xx_data.o

# PRCM clockdomain control
clockdomain-common			+= clockdomain.o
obj-$(CONFIG_ARCH_OMAP2)		+= $(clockdomain-common)
obj-$(CONFIG_ARCH_OMAP2)		+= clockdomains2xxx_3xxx_data.o
obj-$(CONFIG_SOC_OMAP2420)		+= clockdomains2420_data.o
obj-$(CONFIG_SOC_OMAP2430)		+= clockdomains2430_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= $(clockdomain-common)
obj-$(CONFIG_ARCH_OMAP3)		+= clockdomains2xxx_3xxx_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= clockdomains3xxx_data.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(clockdomain-common)
obj-$(CONFIG_ARCH_OMAP4)		+= clockdomains44xx_data.o
obj-$(CONFIG_SOC_AM33XX)		+= $(clockdomain-common)
obj-$(CONFIG_SOC_AM33XX)		+= clockdomains33xx_data.o
obj-$(CONFIG_SOC_TI81XX)		+= $(clockdomain-common)
obj-$(CONFIG_SOC_TI81XX)		+= clockdomains81xx_data.o
obj-$(CONFIG_SOC_AM43XX)		+= $(clockdomain-common)
obj-$(CONFIG_SOC_AM43XX)		+= clockdomains43xx_data.o
obj-$(CONFIG_SOC_OMAP5)			+= $(clockdomain-common)
obj-$(CONFIG_SOC_OMAP5)			+= clockdomains54xx_data.o
obj-$(CONFIG_SOC_DRA7XX)		+= $(clockdomain-common)
obj-$(CONFIG_SOC_DRA7XX)		+= clockdomains7xx_data.o

# Clock framework
obj-$(CONFIG_ARCH_OMAP2)		+= $(clock-common) clock2xxx.o
obj-$(CONFIG_ARCH_OMAP2)		+= clkt2xxx_dpllcore.o
obj-$(CONFIG_ARCH_OMAP2)		+= clkt2xxx_virt_prcm_set.o
obj-$(CONFIG_ARCH_OMAP2)		+= clkt2xxx_dpll.o clkt_iclk.o
obj-$(CONFIG_SOC_OMAP2430)		+= clock2430.o
obj-$(CONFIG_ARCH_OMAP3)		+= $(clock-common) clock3xxx.o
obj-$(CONFIG_ARCH_OMAP3)		+= clock34xx.o clkt34xx_dpll3m2.o
obj-$(CONFIG_ARCH_OMAP3)		+= clock3517.o clock36xx.o
obj-$(CONFIG_ARCH_OMAP3)		+= dpll3xxx.o cclock3xxx_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= clkt_iclk.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(clock-common)
obj-$(CONFIG_ARCH_OMAP4)		+= dpll3xxx.o dpll44xx.o
obj-$(CONFIG_SOC_AM33XX)		+= $(clock-common) dpll3xxx.o
obj-$(CONFIG_SOC_OMAP5)			+= $(clock-common)
obj-$(CONFIG_SOC_OMAP5)			+= dpll3xxx.o dpll44xx.o
obj-$(CONFIG_SOC_DRA7XX)		+= $(clock-common)
obj-$(CONFIG_SOC_DRA7XX)		+= dpll3xxx.o dpll44xx.o
obj-$(CONFIG_SOC_AM43XX)		+= $(clock-common) dpll3xxx.o

# OMAP2 clock rate set data (old "OPP" data)
obj-$(CONFIG_SOC_OMAP2420)		+= opp2420_data.o
obj-$(CONFIG_SOC_OMAP2430)		+= opp2430_data.o

# hwmod data
obj-y					+= omap_hwmod_common_ipblock_data.o
obj-$(CONFIG_SOC_OMAP2420)		+= omap_hwmod_2xxx_ipblock_data.o
obj-$(CONFIG_SOC_OMAP2420)		+= omap_hwmod_2xxx_3xxx_ipblock_data.o
obj-$(CONFIG_SOC_OMAP2420)		+= omap_hwmod_2xxx_interconnect_data.o
obj-$(CONFIG_SOC_OMAP2420)		+= omap_hwmod_2xxx_3xxx_interconnect_data.o
obj-$(CONFIG_SOC_OMAP2420)		+= omap_hwmod_2420_data.o
obj-$(CONFIG_SOC_OMAP2430)		+= omap_hwmod_2xxx_ipblock_data.o
obj-$(CONFIG_SOC_OMAP2430)		+= omap_hwmod_2xxx_3xxx_ipblock_data.o
obj-$(CONFIG_SOC_OMAP2430)		+= omap_hwmod_2xxx_interconnect_data.o
obj-$(CONFIG_SOC_OMAP2430)		+= omap_hwmod_2xxx_3xxx_interconnect_data.o
obj-$(CONFIG_SOC_OMAP2430)		+= omap_hwmod_2430_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= omap_hwmod_2xxx_3xxx_ipblock_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= omap_hwmod_2xxx_3xxx_interconnect_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= omap_hwmod_3xxx_data.o
obj-$(CONFIG_SOC_AM33XX)		+= omap_hwmod_33xx_data.o
obj-$(CONFIG_SOC_AM33XX)		+= omap_hwmod_33xx_43xx_interconnect_data.o
obj-$(CONFIG_SOC_AM33XX)		+= omap_hwmod_33xx_43xx_ipblock_data.o
obj-$(CONFIG_SOC_AM43XX)		+= omap_hwmod_43xx_data.o
obj-$(CONFIG_SOC_AM43XX)		+= omap_hwmod_33xx_43xx_interconnect_data.o
obj-$(CONFIG_SOC_AM43XX)		+= omap_hwmod_33xx_43xx_ipblock_data.o
obj-$(CONFIG_SOC_TI81XX)		+= omap_hwmod_81xx_data.o
obj-$(CONFIG_ARCH_OMAP4)		+= omap_hwmod_44xx_data.o
obj-$(CONFIG_SOC_OMAP5)			+= omap_hwmod_54xx_data.o
obj-$(CONFIG_SOC_DRA7XX)		+= omap_hwmod_7xx_data.o

# EMU peripherals
obj-$(CONFIG_HW_PERF_EVENTS)		+= pmu.o

iommu-$(CONFIG_OMAP_IOMMU)		:= omap-iommu.o
obj-y					+= $(iommu-m) $(iommu-y)

# OMAP2420 MSDI controller integration support ("MMC")
obj-$(CONFIG_SOC_OMAP2420)		+= msdi.o

# Specific board support
obj-$(CONFIG_MACH_OMAP_GENERIC)		+= board-generic.o pdata-quirks.o
obj-$(CONFIG_MACH_OMAP3_BEAGLE)		+= board-omap3beagle.o
obj-$(CONFIG_MACH_DEVKIT8000)     	+= board-devkit8000.o
obj-$(CONFIG_MACH_OMAP_LDP)		+= board-ldp.o
obj-$(CONFIG_MACH_OMAP3530_LV_SOM)      += board-omap3logic.o
obj-$(CONFIG_MACH_OMAP3_TORPEDO)        += board-omap3logic.o
obj-$(CONFIG_MACH_OVERO)		+= board-overo.o
obj-$(CONFIG_MACH_OMAP3_PANDORA)	+= board-omap3pandora.o
obj-$(CONFIG_MACH_NOKIA_N8X0)		+= board-n8x0.o
obj-$(CONFIG_MACH_NOKIA_RX51)		+= board-rx51.o sdram-nokia.o
obj-$(CONFIG_MACH_NOKIA_RX51)		+= board-rx51-peripherals.o
obj-$(CONFIG_MACH_NOKIA_RX51)		+= board-rx51-video.o
obj-$(CONFIG_MACH_CM_T35)		+= board-cm-t35.o
obj-$(CONFIG_MACH_TOUCHBOOK)		+= board-omap3touchbook.o

obj-$(CONFIG_MACH_SBC3530)		+= board-omap3stalker.o

# Platform specific device init code

omap-flash-$(CONFIG_MTD_NAND_OMAP2)	:= board-flash.o
omap-flash-$(CONFIG_MTD_ONENAND_OMAP2)	:= board-flash.o
obj-y					+= $(omap-flash-y) $(omap-flash-m)

omap-hsmmc-$(CONFIG_MMC_OMAP_HS)	:= hsmmc.o
obj-y					+= $(omap-hsmmc-m) $(omap-hsmmc-y)

obj-y					+= usb-musb.o
obj-y					+= omap_phy_internal.o

obj-$(CONFIG_MACH_OMAP2_TUSB6010)	+= usb-tusb6010.o
obj-y					+= usb-host.o

onenand-$(CONFIG_MTD_ONENAND_OMAP2)	:= gpmc-onenand.o
obj-y					+= $(onenand-m) $(onenand-y)

nand-$(CONFIG_MTD_NAND_OMAP2)		:= gpmc-nand.o
obj-y					+= $(nand-m) $(nand-y)

smsc911x-$(CONFIG_SMSC911X)		:= gpmc-smsc911x.o
obj-y					+= $(smsc911x-m) $(smsc911x-y)
ifneq ($(CONFIG_HWSPINLOCK_OMAP),)
obj-y					+= hwspinlock.o
endif

obj-y					+= common-board-devices.o twl-common.o dss-common.o
