#
# Makefile for all Linux ACPI interpreter subdirectories
#

obj-y := hwacpi.o  hwgpe.o  hwregs.o  hwsleep.o

obj-$(ACPI_FUTURE_USAGE) += hwtimer.o

EXTRA_CFLAGS += $(ACPI_CFLAGS)
