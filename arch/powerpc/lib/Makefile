#
# Makefile for ppc-specific library files..
#

subdir-ccflags-$(CONFIG_PPC_WERROR) := -Werror

ccflags-$(CONFIG_PPC64)	:= $(NO_MINIMAL_TOC)

CFLAGS_REMOVE_code-patching.o = $(CC_FLAGS_FTRACE)
CFLAGS_REMOVE_feature-fixups.o = $(CC_FLAGS_FTRACE)

obj-y += string.o alloc.o code-patching.o feature-fixups.o

obj-$(CONFIG_PPC32)	+= div64.o copy_32.o crtsavres.o

# See corresponding test in arch/powerpc/Makefile
# 64-bit linker creates .sfpr on demand for final link (vmlinux),
# so it is only needed for modules, and only for older linkers which
# do not support --save-restore-funcs
ifeq ($(call ld-ifversion, -lt, 225000000, y),y)
extra-$(CONFIG_PPC64)	+= crtsavres.o
endif

obj64-y	+= copypage_64.o copyuser_64.o mem_64.o hweight_64.o \
	   copyuser_power7.o string_64.o copypage_power7.o memcpy_power7.o \
	   memcpy_64.o memcmp_64.o

obj64-$(CONFIG_SMP)	+= locks.o
obj64-$(CONFIG_ALTIVEC)	+= vmx-helper.o
obj64-$(CONFIG_KPROBES_SANITY_TEST) += test_emulate_step.o

obj-y			+= checksum_$(BITS).o checksum_wrappers.o

obj-y			+= sstep.o ldstfp.o quad.o
obj64-y			+= quad.o

obj-$(CONFIG_PPC_LIB_RHEAP) += rheap.o

obj-$(CONFIG_FTR_FIXUP_SELFTEST) += feature-fixups-test.o

obj-$(CONFIG_ALTIVEC)	+= xor_vmx.o xor_vmx_glue.o
CFLAGS_xor_vmx.o += -maltivec $(call cc-option,-mabi=altivec)

obj-$(CONFIG_PPC64) += $(obj64-y)
