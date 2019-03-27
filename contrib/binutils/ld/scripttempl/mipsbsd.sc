cat <<EOF
OUTPUT_FORMAT("${OUTPUT_FORMAT}", "${BIG_OUTPUT_FORMAT}",
	      "${LITTLE_OUTPUT_FORMAT}")
OUTPUT_ARCH(${ARCH})

${RELOCATING+${LIB_SEARCH_DIRS}}
SECTIONS
{
  ${RELOCATING+. = ${TEXT_START_ADDR};}
  .text :
  {
    CREATE_OBJECT_SYMBOLS
    *(.text)
    ${RELOCATING+etext = ${DATA_ALIGNMENT};}
  }
  ${RELOCATING+. = ${DATA_ALIGNMENT};}
  .data :
  {
    *(.data)
    ${CONSTRUCTING+CONSTRUCTORS}
    ${RELOCATING+edata  =  .;}
  }
  .bss :
  {
   *(.bss)
   *(COMMON)
   ${RELOCATING+end = . };
  }
}
EOF
