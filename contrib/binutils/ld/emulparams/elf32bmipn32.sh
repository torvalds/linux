. ${srcdir}/emulparams/elf32bmipn32-defs.sh
OUTPUT_FORMAT="elf32-nbigmips"
BIG_OUTPUT_FORMAT="elf32-nbigmips"
LITTLE_OUTPUT_FORMAT="elf32-nlittlemips"
SHLIB_TEXT_START_ADDR=0x5ffe0000
COMMONPAGESIZE="CONSTANT (COMMONPAGESIZE)"

# IRIX6 defines these symbols.  0x34 is the size of the ELF header.
EXECUTABLE_SYMBOLS="
  __dso_displacement = 0;
  __elf_header = ${TEXT_START_ADDR};
  __program_header_table = ${TEXT_START_ADDR} + 0x34;
"

# There are often dynamic relocations against the .rodata section.
# Setting DT_TEXTREL in the .dynamic section does not convince the
# IRIX6 linker to permit relocations against the text segment.
# Following the IRIX linker, we simply put .rodata in the data
# segment.
WRITABLE_RODATA=

EXTRA_EM_FILE=irix
