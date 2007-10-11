obj-$(CONFIG_ACPI)		+= boot.o
ifneq ($(CONFIG_PCI),)
obj-$(CONFIG_X86_IO_APIC)	+= earlyquirk.o
endif
obj-$(CONFIG_ACPI_SLEEP)	+= sleep_32.o wakeup_32.o

ifneq ($(CONFIG_ACPI_PROCESSOR),)
obj-y				+= cstate.o processor.o
endif

