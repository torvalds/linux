#
# Makefile for the linux kernel.
#

# Object file lists.
obj-y   := cpu.o mm.o clock-mx51-mx53.o devices.o
obj-$(CONFIG_SOC_IMX50) += mm-mx50.o

obj-$(CONFIG_CPU_FREQ_IMX)    += cpu_op-mx51.o
obj-$(CONFIG_MACH_MX51_BABBAGE) += board-mx51_babbage.o
obj-$(CONFIG_MACH_MX51_3DS) += board-mx51_3ds.o
obj-$(CONFIG_MACH_MX53_EVK) += board-mx53_evk.o
obj-$(CONFIG_MACH_MX53_SMD) += board-mx53_smd.o
obj-$(CONFIG_MACH_MX53_LOCO) += board-mx53_loco.o
obj-$(CONFIG_MACH_EUKREA_CPUIMX51) += board-cpuimx51.o
obj-$(CONFIG_MACH_EUKREA_MBIMX51_BASEBOARD) += eukrea_mbimx51-baseboard.o
obj-$(CONFIG_MACH_EUKREA_CPUIMX51SD) += board-cpuimx51sd.o
obj-$(CONFIG_MACH_EUKREA_MBIMXSD51_BASEBOARD) += eukrea_mbimxsd-baseboard.o
obj-$(CONFIG_MACH_MX51_EFIKAMX) += board-mx51_efikamx.o
obj-$(CONFIG_MACH_MX50_RDP) += board-mx50_rdp.o
