# CPUfreq core
obj-$(CONFIG_CPU_FREQ)			+= cpufreq.o freq_table.o

# CPUfreq stats
obj-$(CONFIG_CPU_FREQ_STAT)             += cpufreq_stats.o

# CPUfreq governors 
obj-$(CONFIG_CPU_FREQ_GOV_PERFORMANCE)	+= cpufreq_performance.o
obj-$(CONFIG_CPU_FREQ_GOV_POWERSAVE)	+= cpufreq_powersave.o
obj-$(CONFIG_CPU_FREQ_GOV_USERSPACE)	+= cpufreq_userspace.o
obj-$(CONFIG_CPU_FREQ_GOV_ONDEMAND)	+= cpufreq_ondemand.o
obj-$(CONFIG_CPU_FREQ_GOV_CONSERVATIVE)	+= cpufreq_conservative.o
obj-$(CONFIG_CPU_FREQ_GOV_COMMON)		+= cpufreq_governor.o
obj-$(CONFIG_CPU_FREQ_GOV_ATTR_SET)	+= cpufreq_governor_attr_set.o

obj-$(CONFIG_CPUFREQ_DT)		+= cpufreq-dt.o
obj-$(CONFIG_CPUFREQ_DT_PLATDEV)	+= cpufreq-dt-platdev.o

##################################################################################
# x86 drivers.
# Link order matters. K8 is preferred to ACPI because of firmware bugs in early
# K8 systems. This is still the case but acpi-cpufreq errors out so that
# powernow-k8 can load then. ACPI is preferred to all other hardware-specific drivers.
# speedstep-* is preferred over p4-clockmod.

obj-$(CONFIG_X86_ACPI_CPUFREQ)		+= acpi-cpufreq.o
obj-$(CONFIG_X86_POWERNOW_K8)		+= powernow-k8.o
obj-$(CONFIG_X86_PCC_CPUFREQ)		+= pcc-cpufreq.o
obj-$(CONFIG_X86_POWERNOW_K6)		+= powernow-k6.o
obj-$(CONFIG_X86_POWERNOW_K7)		+= powernow-k7.o
obj-$(CONFIG_X86_LONGHAUL)		+= longhaul.o
obj-$(CONFIG_X86_E_POWERSAVER)		+= e_powersaver.o
obj-$(CONFIG_ELAN_CPUFREQ)		+= elanfreq.o
obj-$(CONFIG_SC520_CPUFREQ)		+= sc520_freq.o
obj-$(CONFIG_X86_LONGRUN)		+= longrun.o
obj-$(CONFIG_X86_GX_SUSPMOD)		+= gx-suspmod.o
obj-$(CONFIG_X86_SPEEDSTEP_ICH)		+= speedstep-ich.o
obj-$(CONFIG_X86_SPEEDSTEP_LIB)		+= speedstep-lib.o
obj-$(CONFIG_X86_SPEEDSTEP_SMI)		+= speedstep-smi.o
obj-$(CONFIG_X86_SPEEDSTEP_CENTRINO)	+= speedstep-centrino.o
obj-$(CONFIG_X86_P4_CLOCKMOD)		+= p4-clockmod.o
obj-$(CONFIG_X86_CPUFREQ_NFORCE2)	+= cpufreq-nforce2.o
obj-$(CONFIG_X86_INTEL_PSTATE)		+= intel_pstate.o
obj-$(CONFIG_X86_AMD_FREQ_SENSITIVITY)	+= amd_freq_sensitivity.o
obj-$(CONFIG_X86_SFI_CPUFREQ)		+= sfi-cpufreq.o

##################################################################################
# ARM SoC drivers
obj-$(CONFIG_ARM_BIG_LITTLE_CPUFREQ)	+= arm_big_little.o
# big LITTLE per platform glues. Keep DT_BL_CPUFREQ as the last entry in all big
# LITTLE drivers, so that it is probed last.
obj-$(CONFIG_ARM_DT_BL_CPUFREQ)		+= arm_big_little_dt.o

obj-$(CONFIG_ARCH_DAVINCI)		+= davinci-cpufreq.o
obj-$(CONFIG_UX500_SOC_DB8500)		+= dbx500-cpufreq.o
obj-$(CONFIG_ARM_EXYNOS5440_CPUFREQ)	+= exynos5440-cpufreq.o
obj-$(CONFIG_ARM_HIGHBANK_CPUFREQ)	+= highbank-cpufreq.o
obj-$(CONFIG_ARM_IMX6Q_CPUFREQ)		+= imx6q-cpufreq.o
obj-$(CONFIG_ARM_INTEGRATOR)		+= integrator-cpufreq.o
obj-$(CONFIG_ARM_KIRKWOOD_CPUFREQ)	+= kirkwood-cpufreq.o
obj-$(CONFIG_ARM_MT8173_CPUFREQ)	+= mt8173-cpufreq.o
obj-$(CONFIG_ARM_OMAP2PLUS_CPUFREQ)	+= omap-cpufreq.o
obj-$(CONFIG_ARM_PXA2xx_CPUFREQ)	+= pxa2xx-cpufreq.o
obj-$(CONFIG_PXA3xx)			+= pxa3xx-cpufreq.o
obj-$(CONFIG_ARM_S3C24XX_CPUFREQ)	+= s3c24xx-cpufreq.o
obj-$(CONFIG_ARM_S3C24XX_CPUFREQ_DEBUGFS) += s3c24xx-cpufreq-debugfs.o
obj-$(CONFIG_ARM_S3C2410_CPUFREQ)	+= s3c2410-cpufreq.o
obj-$(CONFIG_ARM_S3C2412_CPUFREQ)	+= s3c2412-cpufreq.o
obj-$(CONFIG_ARM_S3C2416_CPUFREQ)	+= s3c2416-cpufreq.o
obj-$(CONFIG_ARM_S3C2440_CPUFREQ)	+= s3c2440-cpufreq.o
obj-$(CONFIG_ARM_S3C64XX_CPUFREQ)	+= s3c64xx-cpufreq.o
obj-$(CONFIG_ARM_S5PV210_CPUFREQ)	+= s5pv210-cpufreq.o
obj-$(CONFIG_ARM_SA1100_CPUFREQ)	+= sa1100-cpufreq.o
obj-$(CONFIG_ARM_SA1110_CPUFREQ)	+= sa1110-cpufreq.o
obj-$(CONFIG_ARM_SCPI_CPUFREQ)		+= scpi-cpufreq.o
obj-$(CONFIG_ARM_SPEAR_CPUFREQ)		+= spear-cpufreq.o
obj-$(CONFIG_ARM_STI_CPUFREQ)		+= sti-cpufreq.o
obj-$(CONFIG_ARM_TEGRA20_CPUFREQ)	+= tegra20-cpufreq.o
obj-$(CONFIG_ARM_TEGRA124_CPUFREQ)	+= tegra124-cpufreq.o
obj-$(CONFIG_ARM_VEXPRESS_SPC_CPUFREQ)	+= vexpress-spc-cpufreq.o
obj-$(CONFIG_ACPI_CPPC_CPUFREQ) += cppc_cpufreq.o
obj-$(CONFIG_MACH_MVEBU_V7)		+= mvebu-cpufreq.o


##################################################################################
# PowerPC platform drivers
obj-$(CONFIG_CPU_FREQ_CBE)		+= ppc-cbe-cpufreq.o
ppc-cbe-cpufreq-y			+= ppc_cbe_cpufreq_pervasive.o ppc_cbe_cpufreq.o
obj-$(CONFIG_CPU_FREQ_CBE_PMI)		+= ppc_cbe_cpufreq_pmi.o
obj-$(CONFIG_CPU_FREQ_MAPLE)		+= maple-cpufreq.o
obj-$(CONFIG_QORIQ_CPUFREQ)   		+= qoriq-cpufreq.o
obj-$(CONFIG_CPU_FREQ_PMAC)		+= pmac32-cpufreq.o
obj-$(CONFIG_CPU_FREQ_PMAC64)		+= pmac64-cpufreq.o
obj-$(CONFIG_PPC_PASEMI_CPUFREQ)	+= pasemi-cpufreq.o
obj-$(CONFIG_POWERNV_CPUFREQ)		+= powernv-cpufreq.o

##################################################################################
# Other platform drivers
obj-$(CONFIG_AVR32_AT32AP_CPUFREQ)	+= at32ap-cpufreq.o
obj-$(CONFIG_BFIN_CPU_FREQ)		+= blackfin-cpufreq.o
obj-$(CONFIG_CRIS_MACH_ARTPEC3)		+= cris-artpec3-cpufreq.o
obj-$(CONFIG_ETRAXFS)			+= cris-etraxfs-cpufreq.o
obj-$(CONFIG_IA64_ACPI_CPUFREQ)		+= ia64-acpi-cpufreq.o
obj-$(CONFIG_LOONGSON2_CPUFREQ)		+= loongson2_cpufreq.o
obj-$(CONFIG_LOONGSON1_CPUFREQ)		+= loongson1-cpufreq.o
obj-$(CONFIG_SH_CPU_FREQ)		+= sh-cpufreq.o
obj-$(CONFIG_SPARC_US2E_CPUFREQ)	+= sparc-us2e-cpufreq.o
obj-$(CONFIG_SPARC_US3_CPUFREQ)		+= sparc-us3-cpufreq.o
obj-$(CONFIG_UNICORE32)			+= unicore2-cpufreq.o
