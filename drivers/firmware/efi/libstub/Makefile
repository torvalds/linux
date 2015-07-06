#
# The stub may be linked into the kernel proper or into a separate boot binary,
# but in either case, it executes before the kernel does (with MMU disabled) so
# things like ftrace and stack-protector are likely to cause trouble if left
# enabled, even if doing so doesn't break the build.
#
cflags-$(CONFIG_X86_32)		:= -march=i386
cflags-$(CONFIG_X86_64)		:= -mcmodel=small
cflags-$(CONFIG_X86)		+= -m$(BITS) -D__KERNEL__ $(LINUX_INCLUDE) -O2 \
				   -fPIC -fno-strict-aliasing -mno-red-zone \
				   -mno-mmx -mno-sse -DDISABLE_BRANCH_PROFILING

cflags-$(CONFIG_ARM64)		:= $(subst -pg,,$(KBUILD_CFLAGS))
cflags-$(CONFIG_ARM)		:= $(subst -pg,,$(KBUILD_CFLAGS)) \
				   -fno-builtin -fpic -mno-single-pic-base

KBUILD_CFLAGS			:= $(cflags-y) \
				   $(call cc-option,-ffreestanding) \
				   $(call cc-option,-fno-stack-protector)

GCOV_PROFILE			:= n
KASAN_SANITIZE			:= n

lib-y				:= efi-stub-helper.o
lib-$(CONFIG_EFI_ARMSTUB)	+= arm-stub.o fdt.o

#
# arm64 puts the stub in the kernel proper, which will unnecessarily retain all
# code indefinitely unless it is annotated as __init/__initdata/__initconst etc.
# So let's apply the __init annotations at the section level, by prefixing
# the section names directly. This will ensure that even all the inline string
# literals are covered.
#
extra-$(CONFIG_ARM64)		:= $(lib-y)
lib-$(CONFIG_ARM64)		:= $(patsubst %.o,%.init.o,$(lib-y))

OBJCOPYFLAGS := --prefix-alloc-sections=.init
$(obj)/%.init.o: $(obj)/%.o FORCE
	$(call if_changed,objcopy)
