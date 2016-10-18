#
# linux/arch/arm/tools/Makefile
#
# Copyright (C) 2001 Russell King
#

gen := arch/$(ARCH)/include/generated
kapi := $(gen)/asm
uapi := $(gen)/uapi/asm
syshdr := $(srctree)/$(src)/syscallhdr.sh
sysnr := $(srctree)/$(src)/syscallnr.sh
systbl := $(srctree)/$(src)/syscalltbl.sh
syscall := $(srctree)/$(src)/syscall.tbl

gen-y := $(gen)/calls-oabi.S
gen-y += $(gen)/calls-eabi.S
kapi-hdrs-y := $(kapi)/unistd-nr.h
kapi-hdrs-y += $(kapi)/mach-types.h
uapi-hdrs-y := $(uapi)/unistd-common.h
uapi-hdrs-y += $(uapi)/unistd-oabi.h
uapi-hdrs-y += $(uapi)/unistd-eabi.h

targets += $(addprefix ../../../,$(gen-y) $(kapi-hdrs-y) $(uapi-hdrs-y))

PHONY += kapi uapi

kapi:	$(kapi-hdrs-y) $(gen-y)

uapi:	$(uapi-hdrs-y)

# Create output directory if not already present
_dummy := $(shell [ -d '$(kapi)' ] || mkdir -p '$(kapi)') \
          $(shell [ -d '$(uapi)' ] || mkdir -p '$(uapi)')

quiet_cmd_gen_mach = GEN     $@
      cmd_gen_mach = mkdir -p $(dir $@) && \
		     $(AWK) -f $(filter-out $(PHONY),$^) > $@ || \
		     { rm -f $@; /bin/false; }

$(kapi)/mach-types.h: $(src)/gen-mach-types $(src)/mach-types FORCE
	$(call if_changed,gen_mach)

quiet_cmd_syshdr = SYSHDR  $@
      cmd_syshdr = $(CONFIG_SHELL) '$(syshdr)' '$<' '$@' \
		   '$(syshdr_abi_$(basetarget))' \
		   '$(syshdr_pfx_$(basetarget))' \
		   '__NR_SYSCALL_BASE'

quiet_cmd_systbl = SYSTBL  $@
      cmd_systbl = $(CONFIG_SHELL) '$(systbl)' '$<' '$@' \
		   '$(systbl_abi_$(basetarget))'

quiet_cmd_sysnr  = SYSNR   $@
      cmd_sysnr  = $(CONFIG_SHELL) '$(sysnr)' '$<' '$@' \
		   '$(syshdr_abi_$(basetarget))'

syshdr_abi_unistd-common := common
$(uapi)/unistd-common.h: $(syscall) $(syshdr) FORCE
	$(call if_changed,syshdr)

syshdr_abi_unistd-oabi := oabi
$(uapi)/unistd-oabi.h: $(syscall) $(syshdr) FORCE
	$(call if_changed,syshdr)

syshdr_abi_unistd-eabi := eabi
$(uapi)/unistd-eabi.h: $(syscall) $(syshdr) FORCE
	$(call if_changed,syshdr)

sysnr_abi_unistd-nr := common,oabi,eabi,compat
$(kapi)/unistd-nr.h: $(syscall) $(sysnr) FORCE
	$(call if_changed,sysnr)

systbl_abi_calls-oabi := common,oabi
$(gen)/calls-oabi.S: $(syscall) $(systbl) FORCE
	$(call if_changed,systbl)

systbl_abi_calls-eabi := common,eabi
$(gen)/calls-eabi.S: $(syscall) $(systbl) FORCE
	$(call if_changed,systbl)
