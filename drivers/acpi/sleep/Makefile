obj-y					:= poweroff.o wakeup.o
obj-$(CONFIG_PM_SLEEP)			+= main.o
obj-$(CONFIG_X86)			+= proc.o

EXTRA_CFLAGS += $(ACPI_CFLAGS)
