# If you change this file, please also look at files which source this one:
# elf64bmip.sh elf64btsmip.sh elf32btsmipn32.sh elf32bmipn32.sh

# This is an ELF platform.
SCRIPT_NAME=elf

# Handle both big- and little-ended 32-bit MIPS objects.
ARCH=mips
OUTPUT_FORMAT="elf32-bigmips"
BIG_OUTPUT_FORMAT="elf32-bigmips"
LITTLE_OUTPUT_FORMAT="elf32-littlemips"

TEMPLATE_NAME=elf32
EXTRA_EM_FILE=mipself

case "$EMULATION_NAME" in
elf32*n32*) ELFSIZE=32 ;;
elf64*) ELFSIZE=64 ;;
*) echo $0: unhandled emulation $EMULATION_NAME >&2; exit 1 ;;
esac

if test `echo "$host" | sed -e s/64//` = `echo "$target" | sed -e s/64//`; then
  case " $EMULATION_LIBPATH " in
    *" ${EMULATION_NAME} "*)
      NATIVE=yes
      ;;
  esac
fi

# Look for 64 bit target libraries in /lib64, /usr/lib64 etc., first.
LIBPATH_SUFFIX=$ELFSIZE

GENERATE_SHLIB_SCRIPT=yes
GENERATE_PIE_SCRIPT=yes

TEXT_START_ADDR=0x10000000
MAXPAGESIZE="CONSTANT (MAXPAGESIZE)"
ENTRY=__start

# Unlike most targets, the MIPS backend puts all dynamic relocations
# in a single dynobj section, which it also calls ".rel.dyn".  It does
# this so that it can easily sort all dynamic relocations before the
# output section has been populated.
OTHER_GOT_RELOC_SECTIONS="
  .rel.dyn      ${RELOCATING-0} : { *(.rel.dyn) }
"
# GOT-related settings.  
# If the output has a GOT section, there must be exactly 0x7ff0 bytes
# between .got and _gp.  The ". = ." below stops the orphan code from
# inserting other sections between the assignment to _gp and the start
# of .got.
OTHER_GOT_SYMBOLS='
  . = .;
  _gp = ALIGN(16) + 0x7ff0;
'
OTHER_SDATA_SECTIONS="
  .lit8         ${RELOCATING-0} : { *(.lit8) }
  .lit4         ${RELOCATING-0} : { *(.lit4) }
  .srdata       ${RELOCATING-0} : { *(.srdata) }
"

# Magic symbols.
TEXT_START_SYMBOLS='_ftext = . ;'
DATA_START_SYMBOLS='_fdata = . ;'
OTHER_BSS_SYMBOLS='_fbss = .;'

INITIAL_READONLY_SECTIONS=
if test -z "${CREATE_SHLIB}"; then
  INITIAL_READONLY_SECTIONS=".interp       ${RELOCATING-0} : { *(.interp) }"
fi
INITIAL_READONLY_SECTIONS="${INITIAL_READONLY_SECTIONS}
  .reginfo      ${RELOCATING-0} : { *(.reginfo) }"
# Discard any .MIPS.content* or .MIPS.events* sections.  The linker
# doesn't know how to adjust them.
OTHER_SECTIONS="/DISCARD/ : { *(.MIPS.content*) *(.MIPS.events*) }"

TEXT_DYNAMIC=
