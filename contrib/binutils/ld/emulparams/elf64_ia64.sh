# See genscripts.sh and ../scripttempl/elf.sc for the meaning of these.
SCRIPT_NAME=elf
ELFSIZE=64
TEMPLATE_NAME=elf32
EXTRA_EM_FILE=ia64elf
OUTPUT_FORMAT="elf64-ia64-little"
ARCH=ia64
MACHINE=
MAXPAGESIZE="CONSTANT (MAXPAGESIZE)"
# FIXME: It interferes with linker relaxation. Disable it until it is
# fixed.
if test "0" = "1" -a -n "$CREATE_SHLIB"; then
  # Optimize shared libraries for 16K page size
  COMMONPAGESIZE="CONSTANT (COMMONPAGESIZE)"
fi
TEXT_START_ADDR="0x4000000000000000"
DATA_ADDR="0x6000000000000000 + (. & (${MAXPAGESIZE} - 1))"
GENERATE_SHLIB_SCRIPT=yes
GENERATE_PIE_SCRIPT=yes
NOP=0x00300000010070000002000001000400  # a bundle full of nops
OTHER_GOT_SECTIONS="
  .IA_64.pltoff ${RELOCATING-0} : { *(.IA_64.pltoff) }"
OTHER_PLT_RELOC_SECTIONS="
  .rela.IA_64.pltoff ${RELOCATING-0} : { *(.rela.IA_64.pltoff) }"
OTHER_READONLY_SECTIONS=
OTHER_READWRITE_SECTIONS=
test -z "$CREATE_PIE" && OTHER_READONLY_SECTIONS="
  .opd          ${RELOCATING-0} : { *(.opd) }"
test -n "$CREATE_PIE" && OTHER_READWRITE_SECTIONS="
  .opd          ${RELOCATING-0} : { *(.opd) }"
test -n "$CREATE_PIE" && OTHER_GOT_RELOC_SECTIONS="
  .rela.opd     ${RELOCATING-0} : { *(.rela.opd) }"
OTHER_READONLY_SECTIONS="${OTHER_READONLY_SECTIONS}
  .IA_64.unwind_info ${RELOCATING-0} : { *(.IA_64.unwind_info${RELOCATING+* .gnu.linkonce.ia64unwi.*}) }
  .IA_64.unwind ${RELOCATING-0} : { *(.IA_64.unwind${RELOCATING+* .gnu.linkonce.ia64unw.*}) }"
# Intel C++ compiler, prior to 9.0, puts small data in .ctors and
# .dtors.  They have to be next to .sbss/.sbss2/.sdata/.sdata2.
SMALL_DATA_CTOR=" "
SMALL_DATA_DTOR=" "
