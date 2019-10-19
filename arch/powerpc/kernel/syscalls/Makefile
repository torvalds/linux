# SPDX-License-Identifier: GPL-2.0
kapi := arch/$(SRCARCH)/include/generated/asm
uapi := arch/$(SRCARCH)/include/generated/uapi/asm

_dummy := $(shell [ -d '$(uapi)' ] || mkdir -p '$(uapi)')	\
	  $(shell [ -d '$(kapi)' ] || mkdir -p '$(kapi)')

syscall := $(srctree)/$(src)/syscall.tbl
syshdr := $(srctree)/$(src)/syscallhdr.sh
systbl := $(srctree)/$(src)/syscalltbl.sh

quiet_cmd_syshdr = SYSHDR  $@
      cmd_syshdr = $(CONFIG_SHELL) '$(syshdr)' '$<' '$@'	\
		   '$(syshdr_abis_$(basetarget))'		\
		   '$(syshdr_pfx_$(basetarget))'		\
		   '$(syshdr_offset_$(basetarget))'

quiet_cmd_systbl = SYSTBL  $@
      cmd_systbl = $(CONFIG_SHELL) '$(systbl)' '$<' '$@'	\
		   '$(systbl_abis_$(basetarget))'		\
		   '$(systbl_abi_$(basetarget))'		\
		   '$(systbl_offset_$(basetarget))'

syshdr_abis_unistd_32 := common,nospu,32
$(uapi)/unistd_32.h: $(syscall) $(syshdr)
	$(call if_changed,syshdr)

syshdr_abis_unistd_64 := common,nospu,64
$(uapi)/unistd_64.h: $(syscall) $(syshdr)
	$(call if_changed,syshdr)

systbl_abis_syscall_table_32 := common,nospu,32
systbl_abi_syscall_table_32 := 32
$(kapi)/syscall_table_32.h: $(syscall) $(systbl)
	$(call if_changed,systbl)

systbl_abis_syscall_table_64 := common,nospu,64
systbl_abi_syscall_table_64 := 64
$(kapi)/syscall_table_64.h: $(syscall) $(systbl)
	$(call if_changed,systbl)

systbl_abis_syscall_table_c32 := common,nospu,32
systbl_abi_syscall_table_c32 := c32
$(kapi)/syscall_table_c32.h: $(syscall) $(systbl)
	$(call if_changed,systbl)

systbl_abis_syscall_table_spu := common,spu
systbl_abi_syscall_table_spu := spu
$(kapi)/syscall_table_spu.h: $(syscall) $(systbl)
	$(call if_changed,systbl)

uapisyshdr-y		+= unistd_32.h unistd_64.h
kapisyshdr-y		+= syscall_table_32.h		\
			   syscall_table_64.h		\
			   syscall_table_c32.h		\
			   syscall_table_spu.h

targets	+= $(uapisyshdr-y) $(kapisyshdr-y)

PHONY += all
all: $(addprefix $(uapi)/,$(uapisyshdr-y))
all: $(addprefix $(kapi)/,$(kapisyshdr-y))
	@:
