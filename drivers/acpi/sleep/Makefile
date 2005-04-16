obj-y					:= poweroff.o wakeup.o
obj-$(CONFIG_ACPI_SLEEP)		+= main.o
obj-$(CONFIG_ACPI_SLEEP_PROC_FS)	+= proc.o

EXTRA_CFLAGS += $(ACPI_CFLAGS)
