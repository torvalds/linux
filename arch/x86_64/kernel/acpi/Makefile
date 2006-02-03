obj-y			:= boot.o
boot-y			:= ../../../i386/kernel/acpi/boot.o
obj-$(CONFIG_ACPI_SLEEP)	+= sleep.o wakeup.o

ifneq ($(CONFIG_ACPI_PROCESSOR),)
obj-y			+= processor.o
endif

