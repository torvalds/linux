# Linker script for PE.

if test -z "${RELOCATEABLE_OUTPUT_FORMAT}"; then
  RELOCATEABLE_OUTPUT_FORMAT=${OUTPUT_FORMAT}
fi

# We can't easily and portably get an unquoted $ in a shell
# substitution, so we do this instead.
# Sorting of the .foo$* sections is required by the definition of
# grouped sections in PE.
# Sorting of the file names in R_IDATA is required by the
# current implementation of dlltool (this could probably be changed to
# use grouped sections instead).
if test "${RELOCATING}"; then
  R_TEXT='*(SORT(.text$*))'
  R_DATA='*(SORT(.data$*))'
  R_RDATA='*(SORT(.rdata$*))'
  R_IDATA='
    SORT(*)(.idata$2)
    SORT(*)(.idata$3)
    /* These zeroes mark the end of the import list.  */
    LONG (0); LONG (0); LONG (0); LONG (0); LONG (0);
    SORT(*)(.idata$4)
    SORT(*)(.idata$5)
    SORT(*)(.idata$6)
    SORT(*)(.idata$7)'
  R_CRT_XC='*(SORT(.CRT$XC*))  /* C initialization */'
  R_CRT_XI='*(SORT(.CRT$XI*))  /* C++ initialization */'
  R_CRT_XL='*(SORT(.CRT$XL*))  /* TLS callbacks */'
  R_CRT_XP='*(SORT(.CRT$XP*))  /* Pre-termination */'
  R_CRT_XT='*(SORT(.CRT$XT*))  /* Termination */'
  R_TLS='
    *(.tls)
    *(.tls$)
    *(SORT(.tls$*))'
  R_RSRC='*(SORT(.rsrc$*))'
else
  R_TEXT=
  R_DATA=
  R_RDATA=
  R_IDATA=
  R_CRT=
  R_RSRC=
fi

cat <<EOF
${RELOCATING+OUTPUT_FORMAT(${OUTPUT_FORMAT})}
${RELOCATING-OUTPUT_FORMAT(${RELOCATEABLE_OUTPUT_FORMAT})}
${OUTPUT_ARCH+OUTPUT_ARCH(${OUTPUT_ARCH})}

${LIB_SEARCH_DIRS}

SECTIONS
{
  ${RELOCATING+/* Make the virtual address and file offset synced if the alignment is}
  ${RELOCATING+   lower than the target page size. */}
  ${RELOCATING+. = SIZEOF_HEADERS;}
  ${RELOCATING+. = ALIGN(__section_alignment__);}
  .text ${RELOCATING+ __image_base__ + ( __section_alignment__ < ${TARGET_PAGE_SIZE} ? . : __section_alignment__ )} : 
  {
    ${RELOCATING+ *(.init)}
    *(.text)
    ${R_TEXT}
    *(.glue_7t)
    *(.glue_7)
    ${CONSTRUCTING+ ___CTOR_LIST__ = .; __CTOR_LIST__ = . ; 
			LONG (-1);*(.ctors); *(.ctor); *(SORT(.ctors.*));  LONG (0); }
    ${CONSTRUCTING+ ___DTOR_LIST__ = .; __DTOR_LIST__ = . ; 
			LONG (-1); *(.dtors); *(.dtor); *(SORT(.dtors.*));  LONG (0); }
    ${RELOCATING+ *(.fini)}
    /* ??? Why is .gcc_exc here?  */
    ${RELOCATING+ *(.gcc_exc)}
    ${RELOCATING+PROVIDE (etext = .);}
    *(.gcc_except_table)
  }

  /* The Cygwin32 library uses a section to avoid copying certain data
     on fork.  This used to be named ".data$nocopy".  The linker used
     to include this between __data_start__ and __data_end__, but that
     breaks building the cygwin32 dll.  Instead, we name the section
     ".data_cygwin_nocopy" and explictly include it after __data_end__. */

  .data ${RELOCATING+BLOCK(__section_alignment__)} : 
  {
    ${RELOCATING+__data_start__ = . ;}
    *(.data)
    *(.data2)
    ${R_DATA}
    *(.jcr)
    ${RELOCATING+__data_end__ = . ;}
    ${RELOCATING+*(.data_cygwin_nocopy)}
  }

  .rdata ${RELOCATING+BLOCK(__section_alignment__)} :
  {
    *(.rdata)
    ${R_RDATA}
    *(.eh_frame)
    ${RELOCATING+___RUNTIME_PSEUDO_RELOC_LIST__ = .;}
    ${RELOCATING+__RUNTIME_PSEUDO_RELOC_LIST__ = .;}
    *(.rdata_runtime_pseudo_reloc)
    ${RELOCATING+___RUNTIME_PSEUDO_RELOC_LIST_END__ = .;}
    ${RELOCATING+__RUNTIME_PSEUDO_RELOC_LIST_END__ = .;}
  }

  .pdata ${RELOCATING+BLOCK(__section_alignment__)} :
  {
    *(.pdata)
  }

  .bss ${RELOCATING+BLOCK(__section_alignment__)} :
  {
    ${RELOCATING+__bss_start__ = . ;}
    *(.bss)
    *(COMMON)
    ${RELOCATING+__bss_end__ = . ;}
  }

  .edata ${RELOCATING+BLOCK(__section_alignment__)} :
  {
    *(.edata)
  }

  /DISCARD/ :
  {
    *(.debug\$S)
    *(.debug\$T)
    *(.debug\$F)
    *(.drectve)
  }

  .idata ${RELOCATING+BLOCK(__section_alignment__)} :
  {
    /* This cannot currently be handled with grouped sections.
	See pe.em:sort_sections.  */
    ${R_IDATA}
  }
  .CRT ${RELOCATING+BLOCK(__section_alignment__)} :
  { 					
    ${RELOCATING+___crt_xc_start__ = . ;}
    ${R_CRT_XC}
    ${RELOCATING+___crt_xc_end__ = . ;}
    ${RELOCATING+___crt_xi_start__ = . ;}
    ${R_CRT_XI}
    ${RELOCATING+___crt_xi_end__ = . ;}
    ${RELOCATING+___crt_xl_start__ = . ;}
    ${R_CRT_XL}
    /* ___crt_xl_end__ is defined in the TLS Directory support code */
    ${RELOCATING+___crt_xp_start__ = . ;}
    ${R_CRT_XP}
    ${RELOCATING+___crt_xp_end__ = . ;}
    ${RELOCATING+___crt_xt_start__ = . ;}
    ${R_CRT_XT}
    ${RELOCATING+___crt_xt_end__ = . ;}
  }

  .tls ${RELOCATING+BLOCK(__section_alignment__)} :
  { 					
    ${RELOCATING+___tls_start__ = . ;}
    ${R_TLS}
    ${RELOCATING+___tls_end__ = . ;}
  }

  .endjunk ${RELOCATING+BLOCK(__section_alignment__)} :
  {
    /* end is deprecated, don't use it */
    ${RELOCATING+PROVIDE (end = .);}
    ${RELOCATING+PROVIDE ( _end = .);}
    ${RELOCATING+ __end__ = .;}
  }

  .rsrc ${RELOCATING+BLOCK(__section_alignment__)} :
  { 					
    *(.rsrc)
    ${R_RSRC}
  }

  .reloc ${RELOCATING+BLOCK(__section_alignment__)} :
  { 					
    *(.reloc)
  }

  .stab ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.stab)
  }

  .stabstr ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.stabstr)
  }

  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section.  Unlike other targets that fake this by putting the
     section VMA at 0, the PE format will not allow it.  */
     
  /* DWARF 1.1 and DWARF 2.  */
  .debug_aranges ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_aranges)
  }

  .debug_pubnames ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_pubnames)
  }

  /* DWARF 2.  */
  .debug_info ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_info) *(.gnu.linkonce.wi.*)
  }

  .debug_abbrev ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_abbrev)
  }

  .debug_line ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_line)
  }

  .debug_frame ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_frame)
  }

  .debug_str ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_str)
  }

  .debug_loc ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_loc)
  }

  .debug_macinfo ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_macinfo)
  }

  /* SGI/MIPS DWARF 2 extensions.  */
  .debug_weaknames ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_weaknames)
  }

  .debug_funcnames ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_funcnames)
  }

  .debug_typenames ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_typenames)
  }

  .debug_varnames ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_varnames)
  }

  /* DWARF 3.  */
  .debug_ranges ${RELOCATING+BLOCK(__section_alignment__)} ${RELOCATING+(NOLOAD)} :
  {
    *(.debug_ranges)
  }
}
EOF
