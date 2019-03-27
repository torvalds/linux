SCRIPT_NAME=elf
ELFSIZE=64
OUTPUT_FORMAT="elf64-s390"
TEXT_START_ADDR=0x80000000
MAXPAGESIZE="CONSTANT (MAXPAGESIZE)"
COMMONPAGESIZE="CONSTANT (COMMONPAGESIZE)"
NONPAGED_TEXT_START_ADDR=0x80000000
ARCH="s390:64-bit"
MACHINE=
NOP=0x07070707
TEMPLATE_NAME=elf32
GENERATE_SHLIB_SCRIPT=yes 
GENERATE_PIE_SCRIPT=yes
NO_SMALL_DATA=yes

# Treat a host that matches the target with the possible exception of "x"
# in the name as if it were native.
if test `echo "$host" | sed -e s/390x/390/` \
   = `echo "$target" | sed -e s/390x/390/`; then
  case " $EMULATION_LIBPATH " in
    *" ${EMULATION_NAME} "*)
      NATIVE=yes
  esac
fi

# Look for 64 bit target libraries in /lib64, /usr/lib64 etc., first
# on Linux.
case "$target" in
  s390*-linux*)
    case "$EMULATION_NAME" in
      *64*)
	LIBPATH_SUFFIX=64 ;;
    esac
    ;;
esac
