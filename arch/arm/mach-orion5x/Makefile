# SPDX-License-Identifier: GPL-2.0
ccflags-y := -I$(srctree)/arch/arm/plat-orion/include

obj-y				+= common.o pci.o irq.o mpp.o
obj-$(CONFIG_MACH_KUROBOX_PRO)	+= kurobox_pro-setup.o
obj-$(CONFIG_MACH_TERASTATION_PRO2)	+= terastation_pro2-setup.o
obj-$(CONFIG_MACH_LINKSTATION_PRO) += kurobox_pro-setup.o
obj-$(CONFIG_MACH_DNS323)	+= dns323-setup.o
obj-$(CONFIG_MACH_TS209)	+= ts209-setup.o tsx09-common.o
obj-$(CONFIG_MACH_TS409)	+= ts409-setup.o tsx09-common.o
obj-$(CONFIG_MACH_TS78XX)	+= ts78xx-setup.o
obj-$(CONFIG_MACH_MV2120)	+= mv2120-setup.o
obj-$(CONFIG_MACH_NET2BIG)	+= net2big-setup.o

obj-$(CONFIG_ARCH_ORION5X_DT)		+= board-dt.o
obj-$(CONFIG_MACH_D2NET_DT)	+= board-d2net.o
obj-$(CONFIG_MACH_MSS2_DT)	+= board-mss2.o
obj-$(CONFIG_MACH_RD88F5182_DT)	+= board-rd88f5182.o
