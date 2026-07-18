# SPDX-License-Identifier: GPL-2.0
#
# Makefile for s390-specific library files..
#

# string.o implements standard library functions like memset/memcpy etc.
# Use -ffreestanding to ensure that the compiler does not try to "optimize"
# them into calls to themselves.
CFLAGS_string.o = -ffreestanding

lib-y += delay.o string.o uaccess.o find.o spinlock.o tishift.o
lib-y += csum-partial.o
lib-$(CONFIG_KPROBES) += probes.o
lib-$(CONFIG_UPROBES) += probes.o
obj-$(CONFIG_S390_KPROBES_SANITY_TEST) += test_kprobes_s390.o
test_kprobes_s390-objs += test_kprobes_asm.o test_kprobes.o

# Instrumenting memory accesses to __user data (in different address space)
# produce false positives
KASAN_SANITIZE_uaccess.o := n

obj-$(CONFIG_S390_UNWIND_SELFTEST) += test_unwind.o
CFLAGS_test_unwind.o += -fno-optimize-sibling-calls

obj-$(CONFIG_S390_MODULES_SANITY_TEST) += test_modules.o
obj-$(CONFIG_S390_MODULES_SANITY_TEST_HELPERS) += test_modules_helpers.o

lib-$(CONFIG_FUNCTION_ERROR_INJECTION) += error-inject.o

obj-$(CONFIG_EXPOLINE_EXTERN) += expoline.o
