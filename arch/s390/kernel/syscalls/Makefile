# SPDX-License-Identifier: GPL-2.0

gen	:= arch/$(ARCH)/include/generated
kapi	:= $(gen)/asm
uapi	:= $(gen)/uapi/asm

syscall	:= $(srctree)/$(src)/syscall.tbl
systbl	:= $(srctree)/$(src)/syscalltbl

gen-y := $(kapi)/syscall_table.h
kapi-hdrs-y := $(kapi)/unistd_nr.h
uapi-hdrs-y := $(uapi)/unistd_32.h
uapi-hdrs-y += $(uapi)/unistd_64.h

targets += $(addprefix ../../../,$(gen-y) $(kapi-hdrs-y) $(uapi-hdrs-y))

PHONY += kapi uapi

kapi:	$(gen-y) $(kapi-hdrs-y)
uapi:	$(uapi-hdrs-y)


# Create output directory if not already present
_dummy := $(shell [ -d '$(uapi)' ] || mkdir -p '$(uapi)') \
	  $(shell [ -d '$(kapi)' ] || mkdir -p '$(kapi)')

define filechk_syshdr
	$(CONFIG_SHELL) '$(systbl)' -H -a $(syshdr_abi_$(basetarget)) -f "$2" < $<
endef

define filechk_sysnr
	$(CONFIG_SHELL) '$(systbl)' -N -a $(sysnr_abi_$(basetarget)) < $<
endef

define filechk_syscalls
	$(CONFIG_SHELL) '$(systbl)' -S < $<
endef

syshdr_abi_unistd_32 := common,32
$(uapi)/unistd_32.h: $(syscall) FORCE
	$(call filechk,syshdr,$@)

syshdr_abi_unistd_64 := common,64
$(uapi)/unistd_64.h: $(syscall) FORCE
	$(call filechk,syshdr,$@)

$(kapi)/syscall_table.h: $(syscall) FORCE
	$(call filechk,syscalls)

sysnr_abi_unistd_nr := common,32,64
$(kapi)/unistd_nr.h: $(syscall) FORCE
	$(call filechk,sysnr)
