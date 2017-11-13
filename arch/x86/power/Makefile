# SPDX-License-Identifier: GPL-2.0
OBJECT_FILES_NON_STANDARD_hibernate_asm_$(BITS).o := y

# __restore_processor_state() restores %gs after S3 resume and so should not
# itself be stack-protected
nostackp := $(call cc-option, -fno-stack-protector)
CFLAGS_cpu.o	:= $(nostackp)

obj-$(CONFIG_PM_SLEEP)		+= cpu.o
obj-$(CONFIG_HIBERNATION)	+= hibernate_$(BITS).o hibernate_asm_$(BITS).o
