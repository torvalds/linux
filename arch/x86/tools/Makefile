PHONY += posttest
quiet_cmd_posttest = TEST    $@
      cmd_posttest = $(OBJDUMP) -d -j .text $(objtree)/vmlinux | awk -f $(srctree)/arch/x86/tools/distill.awk | $(obj)/test_get_len

posttest: $(obj)/test_get_len vmlinux
	$(call cmd,posttest)

hostprogs-y	:= test_get_len

# -I needed for generated C source and C source which in the kernel tree.
HOSTCFLAGS_test_get_len.o := -Wall -I$(objtree)/arch/x86/lib/ -I$(srctree)/arch/x86/include/ -I$(srctree)/arch/x86/lib/

# Dependancies are also needed.
$(obj)/test_get_len.o: $(srctree)/arch/x86/lib/insn.c $(srctree)/arch/x86/lib/inat.c $(srctree)/arch/x86/include/asm/inat_types.h $(srctree)/arch/x86/include/asm/inat.h $(srctree)/arch/x86/include/asm/insn.h $(objtree)/arch/x86/lib/inat-tables.c

