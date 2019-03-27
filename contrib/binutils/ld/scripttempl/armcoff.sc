# Linker script for ARM COFF.
# Based on i386coff.sc by Ian Taylor <ian@cygnus.com>.
test -z "$ENTRY" && ENTRY=_start
if test -z "${DATA_ADDR}"; then
  if test "$LD_FLAG" = "N" || test "$LD_FLAG" = "n"; then
    DATA_ADDR=.
  fi
fi

# These are substituted in as variables in order to get '}' in a shell
# conditional expansion.
CTOR='.ctor : {
    *(SORT(.ctors.*))
    *(.ctor)
  }'
DTOR='.dtor : {
    *(SORT(.dtors.*))
    *(.dtor)
  }'

cat <<EOF
OUTPUT_FORMAT("${OUTPUT_FORMAT}", "${BIG_OUTPUT_FORMAT}", "${LITTLE_OUTPUT_FORMAT}")
${LIB_SEARCH_DIRS}

ENTRY(${ENTRY})

SECTIONS
{
  /* We start at 0x8000 because gdb assumes it (see FRAME_CHAIN).
     This is an artifact of the ARM Demon monitor using the bottom 32k
     as workspace (shared with the FP instruction emulator if
     present): */
  .text ${RELOCATING+ 0x8000} : {
    *(.init)
    *(.text*)
    *(.glue_7t)
    *(.glue_7)
    *(.rdata)
    ${CONSTRUCTING+ ___CTOR_LIST__ = .; __CTOR_LIST__ = . ; 
			LONG (-1); *(.ctors); *(.ctor); LONG (0); }
    ${CONSTRUCTING+ ___DTOR_LIST__ = .; __DTOR_LIST__ = . ; 
			LONG (-1); *(.dtors); *(.dtor);  LONG (0); }
    *(.fini)
    ${RELOCATING+ etext  =  .;}
    ${RELOCATING+ _etext =  .;}
  }
  .data ${RELOCATING+${DATA_ADDR-0x40000 + (ALIGN(0x8) & 0xfffc0fff)}} : {
    ${RELOCATING+  __data_start__ = . ;}
    *(.data*)
        
    ${RELOCATING+*(.gcc_exc*)}
    ${RELOCATING+___EH_FRAME_BEGIN__ = . ;}
    ${RELOCATING+*(.eh_fram*)}
    ${RELOCATING+___EH_FRAME_END__ = . ;}
    ${RELOCATING+LONG(0);}
    
    ${RELOCATING+ __data_end__ = . ;}
    ${RELOCATING+ edata  =  .;}
    ${RELOCATING+ _edata  =  .;}
  }
  ${CONSTRUCTING+${RELOCATING-$CTOR}}
  ${CONSTRUCTING+${RELOCATING-$DTOR}}
  .bss ${RELOCATING+ ALIGN(0x8)} :
  { 					
    ${RELOCATING+ __bss_start__ = . ;}
    *(.bss)
    *(COMMON)
    ${RELOCATING+ __bss_end__ = . ;}
  }

  ${RELOCATING+ end = .;}
  ${RELOCATING+ _end = .;}
  ${RELOCATING+ __end__ = .;}

  .stab  0 ${RELOCATING+(NOLOAD)} : 
  {
    [ .stab ]
  }
  .stabstr  0 ${RELOCATING+(NOLOAD)} :
  {
    [ .stabstr ]
  }
}
EOF
