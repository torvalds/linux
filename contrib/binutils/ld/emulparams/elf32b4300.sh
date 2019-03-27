# If you change this file, please also look at files which source this one:
# elf32l4300.sh

EMBEDDED=yes
. ${srcdir}/emulparams/elf32bmip.sh
TEXT_START_ADDR=0xa0020000
unset NONPAGED_TEXT_START_ADDR
unset SHLIB_TEXT_START_ADDR
EXECUTABLE_SYMBOLS='_DYNAMIC_LINK = 0;'
DYNAMIC_LINK=FALSE
