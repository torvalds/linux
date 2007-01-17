#
# Reuse the i386 cpufreq drivers
#

SRCDIR := ../../../i386/kernel/cpu/cpufreq

obj-$(CONFIG_X86_POWERNOW_K8) += powernow-k8.o
obj-$(CONFIG_X86_ACPI_CPUFREQ) += acpi-cpufreq.o
obj-$(CONFIG_X86_SPEEDSTEP_CENTRINO) += speedstep-centrino.o
obj-$(CONFIG_X86_P4_CLOCKMOD) += p4-clockmod.o
obj-$(CONFIG_X86_SPEEDSTEP_LIB) += speedstep-lib.o

powernow-k8-objs := ${SRCDIR}/powernow-k8.o
speedstep-centrino-objs := ${SRCDIR}/speedstep-centrino.o
acpi-cpufreq-objs := ${SRCDIR}/acpi-cpufreq.o
p4-clockmod-objs := ${SRCDIR}/p4-clockmod.o
speedstep-lib-objs := ${SRCDIR}/speedstep-lib.o
