#
# Makefile for the kernel ISAPNP driver.
#

isapnp-proc-$(CONFIG_PROC_FS) = proc.o

obj-y := core.o compat.o $(isapnp-proc-y)
