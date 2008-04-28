#
# Makefile for the kernel ISAPNP driver.
#

isapnp-proc-$(CONFIG_PROC_FS) = proc.o

obj-y := core.o compat.o $(isapnp-proc-y)

ifeq ($(CONFIG_PNP_DEBUG),y)
EXTRA_CFLAGS += -DDEBUG
endif
