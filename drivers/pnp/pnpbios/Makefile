#
# Makefile for the kernel PNPBIOS driver.
#

pnpbios-proc-$(CONFIG_PNPBIOS_PROC_FS) = proc.o

obj-y := core.o bioscalls.o rsparser.o $(pnpbios-proc-y)
