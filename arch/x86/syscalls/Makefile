out := $(obj)/../include/generated/asm

# Create output directory if not already present
_dummy := $(shell [ -d '$(out)' ] || mkdir -p '$(out)')

syscall32 := $(srctree)/$(src)/syscall_32.tbl
syscall64 := $(srctree)/$(src)/syscall_64.tbl

syshdr := $(srctree)/$(src)/syscallhdr.sh
systbl := $(srctree)/$(src)/syscalltbl.sh

quiet_cmd_syshdr = SYSHDR  $@
      cmd_syshdr = $(CONFIG_SHELL) '$(syshdr)' $< $@ \
		   $(syshdr_abi_$(basetarget)) $(syshdr_pfx_$(basetarget))
quiet_cmd_systbl = SYSTBL  $@
      cmd_systbl = $(CONFIG_SHELL) '$(systbl)' $< $@

syshdr_abi_unistd_32 := i386
$(out)/unistd_32.h: $(syscall32) $(syshdr)
	$(call if_changed,syshdr)

syshdr_abi_unistd_32_ia32 := i386
syshdr_pfx_unistd_32_ia32 := ia32_
$(out)/unistd_32_ia32.h: $(syscall32) $(syshdr)
	$(call if_changed,syshdr)

syshdr_abi_unistd_64 := 64
$(out)/unistd_64.h: $(syscall64) $(syshdr)
	$(call if_changed,syshdr)

$(out)/syscalls_32.h: $(syscall32) $(systbl)
	$(call if_changed,systbl)
$(out)/syscalls_64.h: $(syscall64) $(systbl)
	$(call if_changed,systbl)

syshdr-y			+= unistd_32.h unistd_64.h
syshdr-y			+= syscalls_32.h
syshdr-$(CONFIG_X86_64)		+= unistd_32_ia32.h
syshdr-$(CONFIG_X86_64)		+= syscalls_64.h

targets	+= $(syshdr-y)

all: $(addprefix $(out)/,$(targets))
