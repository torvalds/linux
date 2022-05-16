# SPDX-License-Identifier: GPL-2.0
kapi := arch/$(SRCARCH)/include/generated/asm
uapi := arch/$(SRCARCH)/include/generated/uapi/asm

_dummy := $(shell [ -d '$(uapi)' ] || mkdir -p '$(uapi)')	\
	  $(shell [ -d '$(kapi)' ] || mkdir -p '$(kapi)')

syscalln32 := $(srctree)/$(src)/syscall_n32.tbl
syscalln64 := $(srctree)/$(src)/syscall_n64.tbl
syscallo32 := $(srctree)/$(src)/syscall_o32.tbl
syshdr := $(srctree)/$(src)/syscallhdr.sh
sysnr := $(srctree)/$(src)/syscallnr.sh
systbl := $(srctree)/$(src)/syscalltbl.sh

quiet_cmd_syshdr = SYSHDR  $@
      cmd_syshdr = $(CONFIG_SHELL) '$(syshdr)' '$<' '$@'	\
		   '$(syshdr_abis_$(basetarget))'		\
		   '$(syshdr_pfx_$(basetarget))'		\
		   '$(syshdr_offset_$(basetarget))'

quiet_cmd_sysnr = SYSNR   $@
      cmd_sysnr = $(CONFIG_SHELL) '$(sysnr)' '$<' '$@'		\
		  '$(sysnr_abis_$(basetarget))'			\
		  '$(sysnr_pfx_$(basetarget))'			\
		  '$(sysnr_offset_$(basetarget))'

quiet_cmd_systbl = SYSTBL  $@
      cmd_systbl = $(CONFIG_SHELL) '$(systbl)' '$<' '$@'	\
		   '$(systbl_abis_$(basetarget))'		\
		   '$(systbl_abi_$(basetarget))'		\
		   '$(systbl_offset_$(basetarget))'

syshdr_offset_unistd_n32 := __NR_Linux
$(uapi)/unistd_n32.h: $(syscalln32) $(syshdr)
	$(call if_changed,syshdr)

syshdr_offset_unistd_n64 := __NR_Linux
$(uapi)/unistd_n64.h: $(syscalln64) $(syshdr)
	$(call if_changed,syshdr)

syshdr_offset_unistd_o32 := __NR_Linux
$(uapi)/unistd_o32.h: $(syscallo32) $(syshdr)
	$(call if_changed,syshdr)

sysnr_pfx_unistd_nr_n32 := N32
sysnr_offset_unistd_nr_n32 := 6000
$(uapi)/unistd_nr_n32.h: $(syscalln32) $(sysnr)
	$(call if_changed,sysnr)

sysnr_pfx_unistd_nr_n64 := 64
sysnr_offset_unistd_nr_n64 := 5000
$(uapi)/unistd_nr_n64.h: $(syscalln64) $(sysnr)
	$(call if_changed,sysnr)

sysnr_pfx_unistd_nr_o32 := O32
sysnr_offset_unistd_nr_o32 := 4000
$(uapi)/unistd_nr_o32.h: $(syscallo32) $(sysnr)
	$(call if_changed,sysnr)

systbl_abi_syscall_table_32_o32 := 32_o32
systbl_offset_syscall_table_32_o32 := 4000
$(kapi)/syscall_table_32_o32.h: $(syscallo32) $(systbl)
	$(call if_changed,systbl)

systbl_abi_syscall_table_64_n32 := 64_n32
systbl_offset_syscall_table_64_n32 := 6000
$(kapi)/syscall_table_64_n32.h: $(syscalln32) $(systbl)
	$(call if_changed,systbl)

systbl_abi_syscall_table_64_n64 := 64_n64
systbl_offset_syscall_table_64_n64 := 5000
$(kapi)/syscall_table_64_n64.h: $(syscalln64) $(systbl)
	$(call if_changed,systbl)

systbl_abi_syscall_table_64_o32 := 64_o32
systbl_offset_syscall_table_64_o32 := 4000
$(kapi)/syscall_table_64_o32.h: $(syscallo32) $(systbl)
	$(call if_changed,systbl)

uapisyshdr-y		+= unistd_n32.h			\
			   unistd_n64.h			\
			   unistd_o32.h			\
			   unistd_nr_n32.h		\
			   unistd_nr_n64.h		\
			   unistd_nr_o32.h
kapisyshdr-y		+= syscall_table_32_o32.h	\
			   syscall_table_64_n32.h	\
			   syscall_table_64_n64.h	\
			   syscall_table_64_o32.h

targets	+= $(uapisyshdr-y) $(kapisyshdr-y)

PHONY += all
all: $(addprefix $(uapi)/,$(uapisyshdr-y))
all: $(addprefix $(kapi)/,$(kapisyshdr-y))
	@:
