cat <<EOF
OUTPUT_FORMAT("${OUTPUT_FORMAT}")
OUTPUT_ARCH(${ARCH})

${RELOCATING+${LIB_SEARCH_DIRS}}
${STACKZERO+${RELOCATING+${STACKZERO}}}
SECTIONS
{
  .text   ${RELOCATING+${TEXT_START_ADDR}} :
  {
    CREATE_OBJECT_SYMBOLS
    ${RELOCATING+__stext_ = .;}
    *(.text)
    ${PAD_TEXT+${RELOCATING+. = ${DATA_ALIGNMENT};}}
    ${RELOCATING+_etext = ${DATA_ALIGNMENT};}
    ${RELOCATING+__etext = ${DATA_ALIGNMENT};}
  }
  .data ${RELOCATING+${DATA_ALIGNMENT}} :
  {
    ${RELOCATING+__sdata_ = .;}
    *(.data)
    ${CONSTRUCTING+CONSTRUCTORS}
    ${RELOCATING+_edata  =  ${DATA_ALIGNMENT};}
    ${RELOCATING+__edata  =  ${DATA_ALIGNMENT};}
  }
  .bss ${RELOCATING+${DATA_ALIGNMENT}} :
  {
   ${RELOCATING+ __bss_start = .};
   *(.bss)
   *(COMMON)
   ${RELOCATING+_end = ALIGN(4) };
   ${RELOCATING+__end = ALIGN(4) };
  }
}
EOF
