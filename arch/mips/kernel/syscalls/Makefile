# SPDX-License-Identifier: GPL-2.0
kapi := arch/$(SRCARCH)/include/generated/asm
uapi := arch/$(SRCARCH)/include/generated/uapi/asm

_dummy := $(shell [ -d '$(uapi)' ] || mkdir -p '$(uapi)')	\
	  $(shell [ -d '$(kapi)' ] || mkdir -p '$(kapi)')

syscalln32 := $(src)/syscall_n32.tbl
syscalln64 := $(src)/syscall_n64.tbl
syscallo32 := $(src)/syscall_o32.tbl
syshdr := $(srctree)/scripts/syscallhdr.sh
sysnr := $(srctree)/$(src)/syscallnr.sh
systbl := $(srctree)/scripts/syscalltbl.sh

quiet_cmd_syshdr = SYSHDR  $@
      cmd_syshdr = $(CONFIG_SHELL) $(syshdr) --offset __NR_Linux $< $@

quiet_cmd_sysnr = SYSNR   $@
      cmd_sysnr = $(CONFIG_SHELL) '$(sysnr)' '$<' '$@'		\
		  '$(sysnr_abis_$(basetarget))'			\
		  '$(sysnr_pfx_$(basetarget))'			\
		  '$(sysnr_offset_$(basetarget))'

quiet_cmd_systbl = SYSTBL  $@
      cmd_systbl = $(CONFIG_SHELL) $(systbl) $< $@

$(uapi)/unistd_n32.h: $(syscalln32) $(syshdr) FORCE
	$(call if_changed,syshdr)

$(uapi)/unistd_n64.h: $(syscalln64) $(syshdr) FORCE
	$(call if_changed,syshdr)

$(uapi)/unistd_o32.h: $(syscallo32) $(syshdr) FORCE
	$(call if_changed,syshdr)

sysnr_pfx_unistd_nr_n32 := N32
sysnr_offset_unistd_nr_n32 := 6000
$(kapi)/unistd_nr_n32.h: $(syscalln32) $(sysnr) FORCE
	$(call if_changed,sysnr)

sysnr_pfx_unistd_nr_n64 := 64
sysnr_offset_unistd_nr_n64 := 5000
$(kapi)/unistd_nr_n64.h: $(syscalln64) $(sysnr) FORCE
	$(call if_changed,sysnr)

sysnr_pfx_unistd_nr_o32 := O32
sysnr_offset_unistd_nr_o32 := 4000
$(kapi)/unistd_nr_o32.h: $(syscallo32) $(sysnr) FORCE
	$(call if_changed,sysnr)

$(kapi)/syscall_table_n32.h: $(syscalln32) $(systbl) FORCE
	$(call if_changed,systbl)

$(kapi)/syscall_table_n64.h: $(syscalln64) $(systbl) FORCE
	$(call if_changed,systbl)

$(kapi)/syscall_table_o32.h: $(syscallo32) $(systbl) FORCE
	$(call if_changed,systbl)

uapisyshdr-y		+= unistd_n32.h			\
			   unistd_n64.h			\
			   unistd_o32.h
kapisyshdr-y		+= syscall_table_n32.h		\
			   syscall_table_n64.h		\
			   syscall_table_o32.h		\
			   unistd_nr_n32.h		\
			   unistd_nr_n64.h		\
			   unistd_nr_o32.h

uapisyshdr-y	:= $(addprefix $(uapi)/, $(uapisyshdr-y))
kapisyshdr-y	:= $(addprefix $(kapi)/, $(kapisyshdr-y))
targets		+= $(addprefix ../../../../, $(uapisyshdr-y) $(kapisyshdr-y))

PHONY += all
all: $(uapisyshdr-y) $(kapisyshdr-y)
	@:
