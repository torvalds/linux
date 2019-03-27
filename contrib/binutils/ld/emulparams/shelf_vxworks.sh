# If you change this file, please also look at files which source this one:
# shlelf_vxworks.sh

SCRIPT_NAME=elf
BIG_OUTPUT_FORMAT="elf32-sh-vxworks"
LITTLE_OUTPUT_FORMAT="elf32-shl-vxworks"
OUTPUT_FORMAT="$BIG_OUTPUT_FORMAT"
TEXT_START_ADDR=0x1000
MAXPAGESIZE='CONSTANT (MAXPAGESIZE)'
ARCH=sh
MACHINE=
TEMPLATE_NAME=elf32
GENERATE_SHLIB_SCRIPT=yes
ENTRY=__start
SYMPREFIX=_
GOT=".got          ${RELOCATING-0} : {
  PROVIDE(__GLOBAL_OFFSET_TABLE_ = .);
  *(.got.plt) *(.got) }"
. ${srcdir}/emulparams/vxworks.sh
