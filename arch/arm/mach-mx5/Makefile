#
# Makefile for the linux kernel.
#

# Object file lists.
obj-y   := cpu.o mm.o clock-mx51-mx53.o ehci.o system.o

obj-$(CONFIG_PM) += pm-imx5.o
obj-$(CONFIG_CPU_FREQ_IMX)    += cpu_op-mx51.o
obj-$(CONFIG_MACH_MX51_BABBAGE) += board-mx51_babbage.o
obj-$(CONFIG_MACH_MX51_3DS) += board-mx51_3ds.o
obj-$(CONFIG_MACH_MX53_EVK) += board-mx53_evk.o
obj-$(CONFIG_MACH_MX53_SMD) += board-mx53_smd.o
obj-$(CONFIG_MACH_MX53_LOCO) += board-mx53_loco.o
obj-$(CONFIG_MACH_MX53_ARD) += board-mx53_ard.o
obj-$(CONFIG_MACH_EUKREA_CPUIMX51) += board-cpuimx51.o
obj-$(CONFIG_MACH_EUKREA_MBIMX51_BASEBOARD) += eukrea_mbimx51-baseboard.o
obj-$(CONFIG_MACH_EUKREA_CPUIMX51SD) += board-cpuimx51sd.o
obj-$(CONFIG_MACH_EUKREA_MBIMXSD51_BASEBOARD) += eukrea_mbimxsd-baseboard.o
obj-$(CONFIG_MX51_EFIKA_COMMON) += mx51_efika.o
obj-$(CONFIG_MACH_MX51_EFIKAMX) += board-mx51_efikamx.o
obj-$(CONFIG_MACH_MX51_EFIKASB) += board-mx51_efikasb.o
obj-$(CONFIG_MACH_MX50_RDP) += board-mx50_rdp.o

obj-$(CONFIG_MACH_IMX51_DT) += imx51-dt.o
obj-$(CONFIG_MACH_IMX53_DT) += imx53-dt.o
