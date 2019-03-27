SCRIPT_NAME=elf
ELFSIZE=64
OUTPUT_FORMAT="elf64-x86-64"
TEXT_START_ADDR=0x400000
MAXPAGESIZE="CONSTANT (MAXPAGESIZE)"
COMMONPAGESIZE="CONSTANT (COMMONPAGESIZE)"
NONPAGED_TEXT_START_ADDR=0x400000
ARCH="i386:x86-64"
MACHINE=
NOP=0xCCCCCCCC
TEMPLATE_NAME=elf32
GENERATE_SHLIB_SCRIPT=yes
GENERATE_PIE_SCRIPT=yes
NO_SMALL_DATA=yes
LARGE_SECTIONS=yes
SEPARATE_GOTPLT=24

if [ "x${host}" = "x${target}" ]; then
  case " $EMULATION_LIBPATH " in
    *" ${EMULATION_NAME} "*)
      NATIVE=yes
  esac
fi

# Linux/Solaris modify the default library search path to first include
# a 64-bit specific directory.
case "$target" in
  x86_64*-linux*|i[3-7]86-*-linux-*)
    case "$EMULATION_NAME" in
      *64*) LIBPATH_SUFFIX=64 ;;
    esac
    ;;
  *-*-solaris2*) 
    LIBPATH_SUFFIX=/amd64
    ELF_INTERPRETER_NAME=\"/lib/amd64/ld.so.1\"
  ;;
esac
