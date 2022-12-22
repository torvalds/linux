# SPDX-License-Identifier: GPL-2.0
PHONY += posttest

ifneq ($(findstring 1, $(KBUILD_VERBOSE)),)
  posttest_verbose = -v
else
  posttest_verbose =
endif

ifeq ($(CONFIG_64BIT),y)
  posttest_64bit = -y
else
  posttest_64bit = -n
endif

reformatter = $(srctree)/arch/x86/tools/objdump_reformat.awk
chkobjdump = $(srctree)/arch/x86/tools/chkobjdump.awk

quiet_cmd_posttest = TEST    $@
      cmd_posttest = ($(OBJDUMP) -v | $(AWK) -f $(chkobjdump)) || $(OBJDUMP) -d -j .text $(objtree)/vmlinux | $(AWK) -f $(reformatter) | $(obj)/insn_decoder_test $(posttest_64bit) $(posttest_verbose)

quiet_cmd_sanitytest = TEST    $@
      cmd_sanitytest = $(obj)/insn_sanity $(posttest_64bit) -m 1000000

posttest: $(obj)/insn_decoder_test vmlinux $(obj)/insn_sanity
	$(call cmd,posttest)
	$(call cmd,sanitytest)

hostprogs += insn_decoder_test insn_sanity

# -I needed for generated C source and C source which in the kernel tree.
HOSTCFLAGS_insn_decoder_test.o := -Wall -I$(srctree)/tools/arch/x86/lib/ -I$(srctree)/tools/arch/x86/include/ -I$(objtree)/arch/x86/lib/

HOSTCFLAGS_insn_sanity.o := -Wall -I$(srctree)/tools/arch/x86/lib/ -I$(srctree)/tools/arch/x86/include/ -I$(objtree)/arch/x86/lib/

# Dependencies are also needed.
$(obj)/insn_decoder_test.o: $(srctree)/tools/arch/x86/lib/insn.c $(srctree)/tools/arch/x86/lib/inat.c $(srctree)/tools/arch/x86/include/asm/inat_types.h $(srctree)/tools/arch/x86/include/asm/inat.h $(srctree)/tools/arch/x86/include/asm/insn.h $(objtree)/arch/x86/lib/inat-tables.c

$(obj)/insn_sanity.o: $(srctree)/tools/arch/x86/lib/insn.c $(srctree)/tools/arch/x86/lib/inat.c $(srctree)/tools/arch/x86/include/asm/inat_types.h $(srctree)/tools/arch/x86/include/asm/inat.h $(srctree)/tools/arch/x86/include/asm/insn.h $(objtree)/arch/x86/lib/inat-tables.c

HOST_EXTRACFLAGS += -I$(srctree)/tools/include
hostprogs	+= relocs
relocs-objs     := relocs_32.o relocs_64.o relocs_common.o
PHONY += relocs
relocs: $(obj)/relocs
	@:
