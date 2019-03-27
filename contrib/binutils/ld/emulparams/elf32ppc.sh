# If you change this file, please also look at files which source this one:
# elf32lppcnto.sh elf32lppc.sh elf32ppclinux.sh elf32ppcnto.sh 
# elf32ppcsim.sh

. ${srcdir}/emulparams/elf32ppccommon.sh
# Yes, we want duplicate .got and .plt sections.  The linker chooses the
# appropriate one magically in ppc_after_open
DATA_GOT=
SDATA_GOT=
SEPARATE_GOTPLT=0
BSS_PLT=
GOT=".got          ${RELOCATING-0} : SPECIAL { *(.got) }"
PLT=".plt          ${RELOCATING-0} : SPECIAL { *(.plt) }"
GOTPLT="${PLT}"
OTHER_TEXT_SECTIONS="*(.glink)"
EXTRA_EM_FILE=ppc32elf
