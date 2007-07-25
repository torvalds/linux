obj-y			:= boot.o
boot-y			:= ../../../i386/kernel/acpi/boot.o
obj-y			+= sleep.o wakeup.o

ifneq ($(CONFIG_ACPI_PROCESSOR),)
obj-y			+= processor.o
processor-y		:= ../../../i386/kernel/acpi/processor.o ../../../i386/kernel/acpi/cstate.o
endif

