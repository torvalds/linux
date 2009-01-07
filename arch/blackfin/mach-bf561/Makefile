#
# arch/blackfin/mach-bf561/Makefile
#

extra-y := head.o

obj-y := ints-priority.o dma.o

obj-$(CONFIG_BF561_COREB) += coreb.o
obj-$(CONFIG_SMP)  += smp.o secondary.o atomic.o
