# SPDX-License-Identifier: GPL-2.0

gen	:= arch/$(ARCH)/include/generated
kapi	:= $(gen)/asm
uapi	:= $(gen)/uapi/asm

syscall	:= $(src)/syscall.tbl
systbl	:= $(src)/syscalltbl

gen-y := $(kapi)/syscall_table.h
kapi-hdrs-y := $(kapi)/unistd_nr.h
uapi-hdrs-y := $(uapi)/unistd_32.h
uapi-hdrs-y += $(uapi)/unistd_64.h

targets += $(addprefix ../../../../,$(gen-y) $(kapi-hdrs-y) $(uapi-hdrs-y))

PHONY += kapi uapi

kapi:	$(gen-y) $(kapi-hdrs-y)
uapi:	$(uapi-hdrs-y)


# Create output directory if not already present
$(shell mkdir -p $(uapi) $(kapi))

quiet_cmd_syshdr = SYSHDR  $@
      cmd_syshdr = $(CONFIG_SHELL) '$(systbl)' -H -a $(syshdr_abi_$(basetarget)) -f "$@" < $< > $@

quiet_cmd_sysnr = SYSNR   $@
      cmd_sysnr = $(CONFIG_SHELL) '$(systbl)' -N -a $(sysnr_abi_$(basetarget)) < $< > $@

quiet_cmd_syscalls = SYSTBL  $@
      cmd_syscalls = $(CONFIG_SHELL) '$(systbl)' -S < $< > $@

syshdr_abi_unistd_32 := common,32
$(uapi)/unistd_32.h: $(syscall) $(systbl) FORCE
	$(call if_changed,syshdr)

syshdr_abi_unistd_64 := common,64
$(uapi)/unistd_64.h: $(syscall) $(systbl) FORCE
	$(call if_changed,syshdr)

$(kapi)/syscall_table.h: $(syscall) $(systbl) FORCE
	$(call if_changed,syscalls)

sysnr_abi_unistd_nr := common,32,64
$(kapi)/unistd_nr.h: $(syscall) $(systbl) FORCE
	$(call if_changed,sysnr)
