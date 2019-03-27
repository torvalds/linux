TEMPLATE_NAME=elf32
EXTRA_EM_FILE=ppc64elf
ELFSIZE=64
GENERATE_SHLIB_SCRIPT=yes
GENERATE_PIE_SCRIPT=yes
SCRIPT_NAME=elf
OUTPUT_FORMAT="elf64-powerpc"
TEXT_START_ADDR=0x10000000
#SEGMENT_SIZE=0x10000000
MAXPAGESIZE="CONSTANT (MAXPAGESIZE)"
COMMONPAGESIZE="CONSTANT (COMMONPAGESIZE)"
ARCH=powerpc:common64
MACHINE=
NOP=0x60000000
OTHER_TEXT_SECTIONS="*(.sfpr .glink)"
BSS_PLT=
OTHER_BSS_SYMBOLS="
  .tocbss	${RELOCATING-0} :${RELOCATING+ ALIGN(8)} { *(.tocbss)}"
OTHER_PLT_RELOC_SECTIONS="
  .rela.tocbss	${RELOCATING-0} : { *(.rela.tocbss) }"

if test x${RELOCATING+set} = xset; then
  GOT="
  .got		: ALIGN(8) { *(.got .toc) }"
else
  GOT="
  .got		0 : { *(.got) }
  .toc		0 : { *(.toc) }"
fi
OTHER_GOT_RELOC_SECTIONS="
  .rela.toc	${RELOCATING-0} : { *(.rela.toc) }"
OTHER_READWRITE_SECTIONS="
  .toc1		${RELOCATING-0} :${RELOCATING+ ALIGN(8)} { *(.toc1) }
  .opd		${RELOCATING-0} :${RELOCATING+ ALIGN(8)} { KEEP (*(.opd)) }
  .branch_lt	${RELOCATING-0} :${RELOCATING+ ALIGN(8)} { *(.branch_lt) }"

# Treat a host that matches the target with the possible exception of "64"
# in the name as if it were native.
if test `echo "$host" | sed -e s/64//` = `echo "$target" | sed -e s/64//`; then
  case " $EMULATION_LIBPATH " in
    *" ${EMULATION_NAME} "*)
      NATIVE=yes
      ;;
  esac
fi

# Look for 64 bit target libraries in /lib64, /usr/lib64 etc., first.
case "$EMULATION_NAME" in
  *64*) LIBPATH_SUFFIX=64 ;;
esac
