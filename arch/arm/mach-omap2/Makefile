#
# Makefile for the linux kernel.
#

# Common support
obj-y := id.o io.o control.o mux.o devices.o fb.o serial.o gpmc.o timer.o pm.o \
	 common.o gpio.o dma.o wd_timer.o display.o i2c.o hdq1w.o omap_hwmod.o \
	 omap_device.o sram.o

omap-2-3-common				= irq.o
hwmod-common				= omap_hwmod.o \
					  omap_hwmod_common_data.o
clock-common				= clock.o clock_common_data.o \
					  clkt_dpll.o clkt_clksel.o
secure-common				= omap-smc.o omap-secure.o

obj-$(CONFIG_ARCH_OMAP2) += $(omap-2-3-common) $(hwmod-common)
obj-$(CONFIG_ARCH_OMAP3) += $(omap-2-3-common) $(hwmod-common) $(secure-common)
obj-$(CONFIG_ARCH_OMAP4) += prm44xx.o $(hwmod-common) $(secure-common)
obj-$(CONFIG_SOC_AM33XX) += irq.o $(hwmod-common)
obj-$(CONFIG_SOC_OMAP5)	 += prm44xx.o $(hwmod-common) $(secure-common)

ifneq ($(CONFIG_SND_OMAP_SOC_MCBSP),)
obj-y += mcbsp.o
endif

obj-$(CONFIG_TWL4030_CORE) += omap_twl.o
obj-$(CONFIG_SOC_HAS_OMAP2_SDRC)	+= sdrc.o

# SMP support ONLY available for OMAP4

obj-$(CONFIG_SMP)			+= omap-smp.o omap-headsmp.o
obj-$(CONFIG_HOTPLUG_CPU)		+= omap-hotplug.o
omap-4-5-common				=  omap4-common.o omap-wakeupgen.o \
					   sleep44xx.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(omap-4-5-common)
obj-$(CONFIG_SOC_OMAP5)			+= $(omap-4-5-common)

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
obj-$(CONFIG_SOC_AM33XX)		+= am33xx-restart.o
obj-$(CONFIG_ARCH_OMAP3)		+= omap3-restart.o

# Pin multiplexing
obj-$(CONFIG_SOC_OMAP2420)		+= mux2420.o
obj-$(CONFIG_SOC_OMAP2430)		+= mux2430.o
obj-$(CONFIG_ARCH_OMAP3)		+= mux34xx.o
obj-$(CONFIG_ARCH_OMAP4)		+= mux44xx.o

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
obj-$(CONFIG_ARCH_OMAP4)		+= pm44xx.o omap-mpuss-lowpower.o
obj-$(CONFIG_SOC_OMAP5)			+= omap-mpuss-lowpower.o
obj-$(CONFIG_PM_DEBUG)			+= pm-debug.o

obj-$(CONFIG_POWER_AVS_OMAP)		+= sr_device.o
obj-$(CONFIG_POWER_AVS_OMAP_CLASS3)    += smartreflex-class3.o

AFLAGS_sleep24xx.o			:=-Wa,-march=armv6
AFLAGS_sleep34xx.o			:=-Wa,-march=armv7-a$(plus_sec)

ifeq ($(CONFIG_PM_VERBOSE),y)
CFLAGS_pm_bus.o				+= -DDEBUG
endif

endif

ifeq ($(CONFIG_CPU_IDLE),y)
obj-$(CONFIG_ARCH_OMAP3)                += cpuidle34xx.o
obj-$(CONFIG_ARCH_OMAP4)                += cpuidle44xx.o
endif

# PRCM
obj-y					+= prm_common.o cm_common.o
obj-$(CONFIG_ARCH_OMAP2)		+= prm2xxx_3xxx.o prm2xxx.o cm2xxx.o
obj-$(CONFIG_ARCH_OMAP3)		+= prm2xxx_3xxx.o prm3xxx.o cm3xxx.o
obj-$(CONFIG_ARCH_OMAP3)		+= vc3xxx_data.o vp3xxx_data.o
obj-$(CONFIG_SOC_AM33XX)		+= prm33xx.o cm33xx.o
omap-prcm-4-5-common			=  cminst44xx.o cm44xx.o prm44xx.o \
					   prcm_mpu44xx.o prminst44xx.o \
					   vc44xx_data.o vp44xx_data.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(omap-prcm-4-5-common)
obj-$(CONFIG_SOC_OMAP5)			+= $(omap-prcm-4-5-common)

# OMAP voltage domains
voltagedomain-common			:= voltage.o vc.o vp.o
obj-$(CONFIG_ARCH_OMAP2)		+= $(voltagedomain-common)
obj-$(CONFIG_ARCH_OMAP2)		+= voltagedomains2xxx_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= $(voltagedomain-common)
obj-$(CONFIG_ARCH_OMAP3)		+= voltagedomains3xxx_data.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(voltagedomain-common)
obj-$(CONFIG_ARCH_OMAP4)		+= voltagedomains44xx_data.o
obj-$(CONFIG_SOC_AM33XX)		+= $(voltagedomain-common)
obj-$(CONFIG_SOC_AM33XX)                += voltagedomains33xx_data.o
obj-$(CONFIG_SOC_OMAP5)			+= $(voltagedomain-common)

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
obj-$(CONFIG_SOC_OMAP5)			+= $(powerdomain-common)

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
obj-$(CONFIG_SOC_OMAP5)			+= $(clockdomain-common)

# Clock framework
obj-$(CONFIG_ARCH_OMAP2)		+= $(clock-common) clock2xxx.o
obj-$(CONFIG_ARCH_OMAP2)		+= clkt2xxx_sys.o
obj-$(CONFIG_ARCH_OMAP2)		+= clkt2xxx_dpllcore.o
obj-$(CONFIG_ARCH_OMAP2)		+= clkt2xxx_virt_prcm_set.o
obj-$(CONFIG_ARCH_OMAP2)		+= clkt2xxx_apll.o clkt2xxx_osc.o
obj-$(CONFIG_ARCH_OMAP2)		+= clkt2xxx_dpll.o clkt_iclk.o
obj-$(CONFIG_SOC_OMAP2420)		+= cclock2420_data.o
obj-$(CONFIG_SOC_OMAP2430)		+= clock2430.o cclock2430_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= $(clock-common) clock3xxx.o
obj-$(CONFIG_ARCH_OMAP3)		+= clock34xx.o clkt34xx_dpll3m2.o
obj-$(CONFIG_ARCH_OMAP3)		+= clock3517.o clock36xx.o
obj-$(CONFIG_ARCH_OMAP3)		+= dpll3xxx.o cclock3xxx_data.o
obj-$(CONFIG_ARCH_OMAP3)		+= clkt_iclk.o
obj-$(CONFIG_ARCH_OMAP4)		+= $(clock-common) cclock44xx_data.o
obj-$(CONFIG_ARCH_OMAP4)		+= dpll3xxx.o dpll44xx.o
obj-$(CONFIG_SOC_AM33XX)		+= $(clock-common) dpll3xxx.o
obj-$(CONFIG_SOC_AM33XX)		+= cclock33xx_data.o
obj-$(CONFIG_SOC_OMAP5)			+= $(clock-common)
obj-$(CONFIG_SOC_OMAP5)			+= dpll3xxx.o dpll44xx.o

# OMAP2 clock rate set data (old "OPP" data)
obj-$(CONFIG_SOC_OMAP2420)		+= opp2420_data.o
obj-$(CONFIG_SOC_OMAP2430)		+= opp2430_data.o

# hwmod data
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
obj-$(CONFIG_ARCH_OMAP4)		+= omap_hwmod_44xx_data.o

# EMU peripherals
obj-$(CONFIG_OMAP3_EMU)			+= emu.o
obj-$(CONFIG_HW_PERF_EVENTS)		+= pmu.o

obj-$(CONFIG_OMAP_MBOX_FWK)		+= mailbox_mach.o
mailbox_mach-objs			:= mailbox.o

iommu-$(CONFIG_OMAP_IOMMU)		:= omap-iommu.o
obj-y					+= $(iommu-m) $(iommu-y)

ifneq ($(CONFIG_TIDSPBRIDGE),)
obj-y					+= dsp.o
endif

# OMAP2420 MSDI controller integration support ("MMC")
obj-$(CONFIG_SOC_OMAP2420)		+= msdi.o

ifneq ($(CONFIG_DRM_OMAP),)
obj-y					+= drm.o
endif

# Specific board support
obj-$(CONFIG_MACH_OMAP_GENERIC)		+= board-generic.o
obj-$(CONFIG_MACH_OMAP_H4)		+= board-h4.o
obj-$(CONFIG_MACH_OMAP_2430SDP)		+= board-2430sdp.o
obj-$(CONFIG_MACH_OMAP_APOLLON)		+= board-apollon.o
obj-$(CONFIG_MACH_OMAP3_BEAGLE)		+= board-omap3beagle.o
obj-$(CONFIG_MACH_DEVKIT8000)     	+= board-devkit8000.o
obj-$(CONFIG_MACH_OMAP_LDP)		+= board-ldp.o
obj-$(CONFIG_MACH_OMAP3530_LV_SOM)      += board-omap3logic.o
obj-$(CONFIG_MACH_OMAP3_TORPEDO)        += board-omap3logic.o
obj-$(CONFIG_MACH_ENCORE)		+= board-omap3encore.o
obj-$(CONFIG_MACH_OVERO)		+= board-overo.o
obj-$(CONFIG_MACH_OMAP3EVM)		+= board-omap3evm.o
obj-$(CONFIG_MACH_OMAP3_PANDORA)	+= board-omap3pandora.o
obj-$(CONFIG_MACH_OMAP_3430SDP)		+= board-3430sdp.o
obj-$(CONFIG_MACH_NOKIA_N8X0)		+= board-n8x0.o
obj-$(CONFIG_MACH_NOKIA_RM680)		+= board-rm680.o sdram-nokia.o
obj-$(CONFIG_MACH_NOKIA_RX51)		+= board-rx51.o sdram-nokia.o
obj-$(CONFIG_MACH_NOKIA_RX51)		+= board-rx51-peripherals.o
obj-$(CONFIG_MACH_NOKIA_RX51)		+= board-rx51-video.o
obj-$(CONFIG_MACH_OMAP_ZOOM2)		+= board-zoom.o board-zoom-peripherals.o
obj-$(CONFIG_MACH_OMAP_ZOOM2)		+= board-zoom-display.o
obj-$(CONFIG_MACH_OMAP_ZOOM2)		+= board-zoom-debugboard.o
obj-$(CONFIG_MACH_OMAP_ZOOM3)		+= board-zoom.o board-zoom-peripherals.o
obj-$(CONFIG_MACH_OMAP_ZOOM3)		+= board-zoom-display.o
obj-$(CONFIG_MACH_OMAP_ZOOM3)		+= board-zoom-debugboard.o
obj-$(CONFIG_MACH_OMAP_3630SDP)		+= board-3630sdp.o
obj-$(CONFIG_MACH_OMAP_3630SDP)		+= board-zoom-peripherals.o
obj-$(CONFIG_MACH_OMAP_3630SDP)		+= board-zoom-display.o
obj-$(CONFIG_MACH_CM_T35)		+= board-cm-t35.o
obj-$(CONFIG_MACH_CM_T3517)		+= board-cm-t3517.o
obj-$(CONFIG_MACH_IGEP0020)		+= board-igep0020.o
obj-$(CONFIG_MACH_TOUCHBOOK)		+= board-omap3touchbook.o
obj-$(CONFIG_MACH_OMAP_4430SDP)		+= board-4430sdp.o
obj-$(CONFIG_MACH_OMAP4_PANDA)		+= board-omap4panda.o

obj-$(CONFIG_MACH_PCM049)		+= board-omap4pcm049.o

obj-$(CONFIG_MACH_OMAP3517EVM)		+= board-am3517evm.o

obj-$(CONFIG_MACH_CRANEBOARD)		+= board-am3517crane.o

obj-$(CONFIG_MACH_SBC3530)		+= board-omap3stalker.o
obj-$(CONFIG_MACH_TI8168EVM)		+= board-ti8168evm.o
obj-$(CONFIG_MACH_TI8148EVM)		+= board-ti8168evm.o

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

smc91x-$(CONFIG_SMC91X)			:= gpmc-smc91x.o
obj-y					+= $(smc91x-m) $(smc91x-y)

smsc911x-$(CONFIG_SMSC911X)		:= gpmc-smsc911x.o
obj-y					+= $(smsc911x-m) $(smsc911x-y)
ifneq ($(CONFIG_HWSPINLOCK_OMAP),)
obj-y					+= hwspinlock.o
endif

emac-$(CONFIG_TI_DAVINCI_EMAC)		:= am35xx-emac.o
obj-y					+= $(emac-m) $(emac-y)

obj-y					+= common-board-devices.o twl-common.o dss-common.o
