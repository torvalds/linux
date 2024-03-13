# SPDX-License-Identifier: GPL-2.0

obj-$(CONFIG_ACPI)		+= boot.o
obj-$(CONFIG_ACPI_SLEEP)	+= sleep.o wakeup_$(BITS).o
obj-$(CONFIG_ACPI_APEI)		+= apei.o
obj-$(CONFIG_ACPI_CPPC_LIB)	+= cppc.o

ifneq ($(CONFIG_ACPI_PROCESSOR),)
obj-y				+= cstate.o
endif

