# SPDX-License-Identifier: GPL-2.0
#
# linux/arch/arm/tools/Makefile
#
# Copyright (C) 2001 Russell King
#

gen := arch/$(ARCH)/include/generated
kapi := $(gen)/asm
uapi := $(gen)/uapi/asm
syshdr := $(srctree)/scripts/syscallhdr.sh
sysnr := $(srctree)/$(src)/syscallnr.sh
systbl := $(srctree)/scripts/syscalltbl.sh
syscall := $(src)/syscall.tbl

gen-y := $(gen)/calls-oabi.S
gen-y += $(gen)/calls-eabi.S
kapi-hdrs-y := $(kapi)/unistd-nr.h
kapi-hdrs-y += $(kapi)/mach-types.h
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
      cmd_gen_mach = $(AWK) -f $(real-prereqs) > $@

$(kapi)/mach-types.h: $(src)/gen-mach-types $(src)/mach-types FORCE
	$(call if_changed,gen_mach)

quiet_cmd_syshdr = SYSHDR  $@
      cmd_syshdr = $(CONFIG_SHELL) $(syshdr) --abis common,$* \
		   --offset __NR_SYSCALL_BASE $< $@

quiet_cmd_systbl = SYSTBL  $@
      cmd_systbl = $(CONFIG_SHELL) $(systbl) --abis common,$* $< $@

quiet_cmd_sysnr  = SYSNR   $@
      cmd_sysnr  = $(CONFIG_SHELL) $(sysnr) $< $@

$(uapi)/unistd-%.h: $(syscall) $(syshdr) FORCE
	$(call if_changed,syshdr)

$(kapi)/unistd-nr.h: $(syscall) $(sysnr) FORCE
	$(call if_changed,sysnr)

$(gen)/calls-%.S: $(syscall) $(systbl) FORCE
	$(call if_changed,systbl)
