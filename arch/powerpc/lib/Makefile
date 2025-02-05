# SPDX-License-Identifier: GPL-2.0
#
# Makefile for ppc-specific library files..
#

CFLAGS_code-patching.o += -fno-stack-protector
CFLAGS_feature-fixups.o += -fno-stack-protector

CFLAGS_REMOVE_code-patching.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_feature-fixups.o = $(CC_FLAGS_FTRACE)

KASAN_SANITIZE_code-patching.o := n
KASAN_SANITIZE_feature-fixups.o := n
# restart_table.o contains functions called in the NMI interrupt path
# which can be in real mode. Disable KASAN.
KASAN_SANITIZE_restart_table.o := n
KCSAN_SANITIZE_code-patching.o := n
KCSAN_SANITIZE_feature-fixups.o := n

ifdef CONFIG_KASAN
CFLAGS_code-patching.o += -DDISABLE_BRANCH_PROFILING
CFLAGS_feature-fixups.o += -DDISABLE_BRANCH_PROFILING
endif

CFLAGS_code-patching.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)
CFLAGS_feature-fixups.o += $(DISABLE_LATENT_ENTROPY_PLUGIN)

obj-y += code-patching.o feature-fixups.o pmem.o

obj-$(CONFIG_CODE_PATCHING_SELFTEST) += test-code-patching.o

ifndef CONFIG_KASAN
obj-y	+=	string.o memcmp_$(BITS).o
obj-$(CONFIG_PPC32)	+= strlen_32.o
endif

obj-$(CONFIG_PPC32)	+= div64.o copy_32.o crtsavres.o

obj-$(CONFIG_FUNCTION_ERROR_INJECTION)	+= error-inject.o

# See corresponding test in arch/powerpc/Makefile
# 64-bit linker creates .sfpr on demand for final link (vmlinux),
# so it is only needed for modules, and only for older linkers which
# do not support --save-restore-funcs
ifndef CONFIG_LD_IS_BFD
always-$(CONFIG_PPC64)	+= crtsavres.o
endif

obj-$(CONFIG_PPC_BOOK3S_64) += copyuser_power7.o copypage_power7.o \
			       memcpy_power7.o restart_table.o

obj64-y	+= copypage_64.o copyuser_64.o mem_64.o hweight_64.o \
	   memcpy_64.o copy_mc_64.o

ifdef CONFIG_PPC_QUEUED_SPINLOCKS
obj-$(CONFIG_SMP)	+= qspinlock.o
else
obj64-$(CONFIG_SMP)	+= locks.o
endif

obj64-$(CONFIG_ALTIVEC)	+= vmx-helper.o
obj64-$(CONFIG_KPROBES_SANITY_TEST)	+= test_emulate_step.o \
					   test_emulate_step_exec_instr.o

obj-y			+= checksum_$(BITS).o checksum_wrappers.o \
			   string_$(BITS).o

obj-y			+= sstep.o
obj-$(CONFIG_PPC_FPU)	+= ldstfp.o
obj64-y			+= quad.o

obj-$(CONFIG_PPC_LIB_RHEAP) += rheap.o

obj-$(CONFIG_FTR_FIXUP_SELFTEST) += feature-fixups-test.o

obj-$(CONFIG_ALTIVEC)	+= xor_vmx.o xor_vmx_glue.o
CFLAGS_xor_vmx.o += -mhard-float -maltivec $(call cc-option,-mabi=altivec)
# Enable <altivec.h>
CFLAGS_xor_vmx.o += -isystem $(shell $(CC) -print-file-name=include)

obj-$(CONFIG_CRC32_ARCH) += crc32-powerpc.o
crc32-powerpc-y := crc32-glue.o crc32c-vpmsum_asm.o

obj-$(CONFIG_CRC_T10DIF_ARCH) += crc-t10dif-powerpc.o
crc-t10dif-powerpc-y := crc-t10dif-glue.o crct10dif-vpmsum_asm.o

obj-$(CONFIG_PPC64) += $(obj64-y)
