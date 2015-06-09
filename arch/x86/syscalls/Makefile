out := $(obj)/../include/generated/asm
uapi := $(obj)/../include/generated/uapi/asm

# Create output directory if not already present
_dummy := $(shell [ -d '$(out)' ] || mkdir -p '$(out)') \
	  $(shell [ -d '$(uapi)' ] || mkdir -p '$(uapi)')

syscall32 := $(srctree)/$(src)/syscall_32.tbl
syscall64 := $(srctree)/$(src)/syscall_64.tbl

syshdr := $(srctree)/$(src)/syscallhdr.sh
systbl := $(srctree)/$(src)/syscalltbl.sh

quiet_cmd_syshdr = SYSHDR  $@
      cmd_syshdr = $(CONFIG_SHELL) '$(syshdr)' '$<' '$@' \
		   '$(syshdr_abi_$(basetarget))' \
		   '$(syshdr_pfx_$(basetarget))' \
		   '$(syshdr_offset_$(basetarget))'
quiet_cmd_systbl = SYSTBL  $@
      cmd_systbl = $(CONFIG_SHELL) '$(systbl)' $< $@

quiet_cmd_hypercalls = HYPERCALLS $@
      cmd_hypercalls = $(CONFIG_SHELL) '$<' $@ $(filter-out $<,$^)

syshdr_abi_unistd_32 := i386
$(uapi)/unistd_32.h: $(syscall32) $(syshdr)
	$(call if_changed,syshdr)

syshdr_abi_unistd_32_ia32 := i386
syshdr_pfx_unistd_32_ia32 := ia32_
$(out)/unistd_32_ia32.h: $(syscall32) $(syshdr)
	$(call if_changed,syshdr)

syshdr_abi_unistd_x32 := common,x32
syshdr_offset_unistd_x32 := __X32_SYSCALL_BIT
$(uapi)/unistd_x32.h: $(syscall64) $(syshdr)
	$(call if_changed,syshdr)

syshdr_abi_unistd_64 := common,64
$(uapi)/unistd_64.h: $(syscall64) $(syshdr)
	$(call if_changed,syshdr)

syshdr_abi_unistd_64_x32 := x32
syshdr_pfx_unistd_64_x32 := x32_
$(out)/unistd_64_x32.h: $(syscall64) $(syshdr)
	$(call if_changed,syshdr)

$(out)/syscalls_32.h: $(syscall32) $(systbl)
	$(call if_changed,systbl)
$(out)/syscalls_64.h: $(syscall64) $(systbl)
	$(call if_changed,systbl)

$(out)/xen-hypercalls.h: $(srctree)/scripts/xen-hypercalls.sh
	$(call if_changed,hypercalls)

$(out)/xen-hypercalls.h: $(srctree)/include/xen/interface/xen*.h

uapisyshdr-y			+= unistd_32.h unistd_64.h unistd_x32.h
syshdr-y			+= syscalls_32.h
syshdr-$(CONFIG_X86_64)		+= unistd_32_ia32.h unistd_64_x32.h
syshdr-$(CONFIG_X86_64)		+= syscalls_64.h
syshdr-$(CONFIG_XEN)		+= xen-hypercalls.h

targets	+= $(uapisyshdr-y) $(syshdr-y)

PHONY += all
all: $(addprefix $(uapi)/,$(uapisyshdr-y))
all: $(addprefix $(out)/,$(syshdr-y))
	@:
