# Linker script for Alpha systems.
# Ian Lance Taylor <ian@cygnus.com>.
# These variables may be overridden by the emulation file.  The
# defaults are appropriate for an Alpha running OSF/1.
test -z "$ENTRY" && ENTRY=__start
test -z "$TEXT_START_ADDR" && TEXT_START_ADDR="0x120000000 + SIZEOF_HEADERS"
if test "x$LD_FLAG" = "xn" -o "x$LD_FLAG" = "xN"; then
  DATA_ADDR=.
else
  test -z "$DATA_ADDR" && DATA_ADDR=0x140000000
fi
cat <<EOF
OUTPUT_FORMAT("${OUTPUT_FORMAT}")
${LIB_SEARCH_DIRS}

ENTRY(${ENTRY})

SECTIONS
{
  ${RELOCATING+. = ${TEXT_START_ADDR};}
  .text : {
    ${RELOCATING+ _ftext = . };
    ${RELOCATING+ __istart = . };
    ${RELOCATING+ *(.init) }
    ${RELOCATING+ LONG (0x6bfa8001)}
    ${RELOCATING+ eprol  =  .};
    *(.text)
    ${RELOCATING+ __fstart = . };
    ${RELOCATING+ *(.fini)}
    ${RELOCATING+ LONG (0x6bfa8001)}
    ${RELOCATING+ _etext  =  .};
  }
  .rdata : {
    *(.rdata)
  }
  .rconst : {
    *(.rconst)
  }
  .pdata : {
    ${RELOCATING+ _fpdata = .;}
    *(.pdata)
  }
  ${RELOCATING+. = ${DATA_ADDR};}
  .data : {
    ${RELOCATING+ _fdata = .;}
    *(.data)
    ${CONSTRUCTING+CONSTRUCTORS}
  }
  .xdata : {
    *(.xdata)
  }
  ${RELOCATING+ _gp = ALIGN (16) + 0x8000;}
  .lit8 : {
    *(.lit8)
  }
  .lita : {
    *(.lita)
  }
  .sdata : {
    *(.sdata)
  }
  ${RELOCATING+ _EDATA  =  .;}
  ${RELOCATING+ _FBSS = .;}
  .sbss : {
    *(.sbss)
    *(.scommon)
  }
  .bss : {
    *(.bss)
    *(COMMON)
  }
  ${RELOCATING+ _end = .;}
}
EOF
