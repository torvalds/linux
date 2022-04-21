# SPDX-License-Identifier: GPL-2.0
kapi := arch/$(SRCARCH)/include/generated/asm
uapi := arch/$(SRCARCH)/include/generated/uapi/asm

$(shell mkdir -p $(uapi) $(kapi))

syscall := $(src)/syscall.tbl
syshdr := $(srctree)/scripts/syscallhdr.sh
systbl := $(srctree)/scripts/syscalltbl.sh

quiet_cmd_syshdr = SYSHDR  $@
      cmd_syshdr = $(CONFIG_SHELL) $(syshdr) --emit-nr $< $@

quiet_cmd_systbl = SYSTBL  $@
      cmd_systbl = $(CONFIG_SHELL) $(systbl) $< $@

$(uapi)/unistd_32.h: $(syscall) $(syshdr) FORCE
	$(call if_changed,syshdr)

$(kapi)/syscall_table.h: $(syscall) $(systbl) FORCE
	$(call if_changed,systbl)

uapisyshdr-y		+= unistd_32.h
kapisyshdr-y		+= syscall_table.h

uapisyshdr-y	:= $(addprefix $(uapi)/, $(uapisyshdr-y))
kapisyshdr-y	:= $(addprefix $(kapi)/, $(kapisyshdr-y))
targets		+= $(addprefix ../../../../, $(uapisyshdr-y) $(kapisyshdr-y))

PHONY += all
all: $(uapisyshdr-y) $(kapisyshdr-y)
	@:
