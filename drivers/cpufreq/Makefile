# CPUfreq core
obj-$(CONFIG_CPU_FREQ)			+= cpufreq.o
# CPUfreq stats
obj-$(CONFIG_CPU_FREQ_STAT)             += cpufreq_stats.o

# CPUfreq governors 
obj-$(CONFIG_CPU_FREQ_GOV_PERFORMANCE)	+= cpufreq_performance.o
obj-$(CONFIG_CPU_FREQ_GOV_POWERSAVE)	+= cpufreq_powersave.o
obj-$(CONFIG_CPU_FREQ_GOV_USERSPACE)	+= cpufreq_userspace.o
obj-$(CONFIG_CPU_FREQ_GOV_ONDEMAND)	+= cpufreq_ondemand.o
obj-$(CONFIG_CPU_FREQ_GOV_CONSERVATIVE)	+= cpufreq_conservative.o
obj-$(CONFIG_CPU_FREQ_GOV_INTERACTIVE)	+= cpufreq_interactive.o

# CPUfreq cross-arch helpers
obj-$(CONFIG_CPU_FREQ_TABLE)		+= freq_table.o

##################################################################################d
# x86 drivers.
# Link order matters. K8 is preferred to ACPI because of firmware bugs in early
# K8 systems. ACPI is preferred to all other hardware-specific drivers.
# speedstep-* is preferred over p4-clockmod.

obj-$(CONFIG_X86_POWERNOW_K8)		+= powernow-k8.o mperf.o
obj-$(CONFIG_X86_ACPI_CPUFREQ)		+= acpi-cpufreq.o mperf.o
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

##################################################################################d

# ARM SoC drivers
obj-$(CONFIG_UX500_SOC_DB8500)		+= db8500-cpufreq.o
