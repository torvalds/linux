#
# Makefile for the linux kernel.
#
#

# Common objects
obj-y 			:= time.o irq.o clock.o serial.o io.o id.o psc.o \
			   gpio.o devices.o dma.o usb.o

obj-$(CONFIG_DAVINCI_MUX)		+= mux.o

# Board specific
obj-$(CONFIG_MACH_DAVINCI_EVM)  += board-evm.o
