. ${srcdir}/emulparams/elf32bmipn32-defs.sh
COMMONPAGESIZE="CONSTANT (COMMONPAGESIZE)"

# elf32bmipn32-defs.sh use .reginfo, n64 ABI should use .MIPS.options,
# override INITIAL_READONLY_SECTIONS to do this.
INITIAL_READONLY_SECTIONS=
if test -z "${CREATE_SHLIB}"; then
  INITIAL_READONLY_SECTIONS=".interp       ${RELOCATING-0} : { *(.interp) }"
fi
INITIAL_READONLY_SECTIONS="${INITIAL_READONLY_SECTIONS}
  .MIPS.options      ${RELOCATING-0} : { *(.MIPS.options) }"
