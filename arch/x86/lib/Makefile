#
# Makefile for x86 specific library files.
#

inat_tables_script = $(srctree)/arch/x86/tools/gen-insn-attr-x86.awk
inat_tables_maps = $(srctree)/arch/x86/lib/x86-opcode-map.txt
quiet_cmd_inat_tables = GEN     $@
      cmd_inat_tables = $(AWK) -f $(inat_tables_script) $(inat_tables_maps) > $@ || rm -f $@

$(obj)/inat-tables.c: $(inat_tables_script) $(inat_tables_maps)
	$(call cmd,inat_tables)

$(obj)/inat.o: $(obj)/inat-tables.c

clean-files := inat-tables.c

obj-$(CONFIG_SMP) += msr-smp.o cache-smp.o

lib-y := delay.o misc.o cmdline.o
lib-y += usercopy_$(BITS).o usercopy.o getuser.o putuser.o
lib-y += memcpy_$(BITS).o
lib-$(CONFIG_RWSEM_XCHGADD_ALGORITHM) += rwsem.o
lib-$(CONFIG_INSTRUCTION_DECODER) += insn.o inat.o
lib-$(CONFIG_RETPOLINE) += retpoline.o

obj-y += msr.o msr-reg.o msr-reg-export.o hweight.o

ifeq ($(CONFIG_X86_32),y)
        obj-y += atomic64_32.o
        lib-y += atomic64_cx8_32.o
        lib-y += checksum_32.o
        lib-y += strstr_32.o
        lib-y += string_32.o
ifneq ($(CONFIG_X86_CMPXCHG64),y)
        lib-y += cmpxchg8b_emu.o atomic64_386_32.o
endif
        lib-$(CONFIG_X86_USE_3DNOW) += mmx_32.o
else
        obj-y += iomap_copy_64.o
        lib-y += csum-partial_64.o csum-copy_64.o csum-wrappers_64.o
        lib-y += clear_page_64.o copy_page_64.o
        lib-y += memmove_64.o memset_64.o
        lib-y += copy_user_64.o
	lib-y += cmpxchg16b_emu.o
endif
