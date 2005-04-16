#
# Makefile for the linux kernel.
#

# Object file lists.

obj-y					:= clock.o core.o lm.o time.o
obj-$(CONFIG_ARCH_INTEGRATOR_AP)	+= integrator_ap.o
obj-$(CONFIG_ARCH_INTEGRATOR_CP)	+= integrator_cp.o

obj-$(CONFIG_LEDS)			+= leds.o
obj-$(CONFIG_PCI)			+= pci_v3.o pci.o
obj-$(CONFIG_CPU_FREQ_INTEGRATOR)	+= cpu.o
obj-$(CONFIG_INTEGRATOR_IMPD1)		+= impd1.o
