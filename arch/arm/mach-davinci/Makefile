#
# Makefile for the linux kernel.
#
#

# Common objects
obj-y 			:= time.o irq.o clock.o serial.o io.o id.o psc.o \
			   gpio.o devices.o dma.o usb.o

obj-$(CONFIG_DAVINCI_MUX)		+= mux.o

# Chip specific
obj-$(CONFIG_ARCH_DAVINCI_DM644x)       += dm644x.o

# Board specific
obj-$(CONFIG_MACH_DAVINCI_EVM)  	+= board-dm644x-evm.o
