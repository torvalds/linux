#
# Makefile for the linux kernel.
#
#

# Common objects
obj-y 			:= time.o clock.o serial.o psc.o \
			   dma.o usb.o common.o sram.o aemif.o

obj-$(CONFIG_DAVINCI_MUX)		+= mux.o

# Chip specific
obj-$(CONFIG_ARCH_DAVINCI_DM644x)       += dm644x.o devices.o
obj-$(CONFIG_ARCH_DAVINCI_DM355)        += dm355.o devices.o
obj-$(CONFIG_ARCH_DAVINCI_DM646x)       += dm646x.o devices.o
obj-$(CONFIG_ARCH_DAVINCI_DM365)	+= dm365.o devices.o
obj-$(CONFIG_ARCH_DAVINCI_DA830)        += da830.o devices-da8xx.o
obj-$(CONFIG_ARCH_DAVINCI_DA850)        += da850.o devices-da8xx.o
obj-$(CONFIG_ARCH_DAVINCI_TNETV107X)    += tnetv107x.o devices-tnetv107x.o

obj-$(CONFIG_AINTC)			+= irq.o
obj-$(CONFIG_CP_INTC)			+= cp_intc.o

# Board specific
obj-$(CONFIG_MACH_DA8XX_DT)		+= da8xx-dt.o
obj-$(CONFIG_MACH_DAVINCI_EVM)  	+= board-dm644x-evm.o
obj-$(CONFIG_MACH_SFFSDR)		+= board-sffsdr.o
obj-$(CONFIG_MACH_NEUROS_OSD2)		+= board-neuros-osd2.o
obj-$(CONFIG_MACH_DAVINCI_DM355_EVM)	+= board-dm355-evm.o
obj-$(CONFIG_MACH_DM355_LEOPARD)	+= board-dm355-leopard.o
obj-$(CONFIG_MACH_DAVINCI_DM6467_EVM)	+= board-dm646x-evm.o cdce949.o
obj-$(CONFIG_MACH_DAVINCI_DM365_EVM)	+= board-dm365-evm.o
obj-$(CONFIG_MACH_DAVINCI_DA830_EVM)	+= board-da830-evm.o
obj-$(CONFIG_MACH_DAVINCI_DA850_EVM)	+= board-da850-evm.o
obj-$(CONFIG_MACH_TNETV107X)		+= board-tnetv107x-evm.o
obj-$(CONFIG_MACH_MITYOMAPL138)		+= board-mityomapl138.o
obj-$(CONFIG_MACH_OMAPL138_HAWKBOARD)	+= board-omapl138-hawk.o

# Power Management
obj-$(CONFIG_CPU_IDLE)			+= cpuidle.o
obj-$(CONFIG_SUSPEND)			+= pm.o sleep.o
obj-$(CONFIG_HAVE_CLK)			+= pm_domain.o
