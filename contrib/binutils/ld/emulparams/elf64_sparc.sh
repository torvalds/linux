SCRIPT_NAME=elf
ELFSIZE=64
TEMPLATE_NAME=elf32
OUTPUT_FORMAT="elf64-sparc"
MAXPAGESIZE="CONSTANT (MAXPAGESIZE)"
COMMONPAGESIZE="CONSTANT (COMMONPAGESIZE)"
ARCH="sparc:v9"
MACHINE=
DATA_PLT=
GENERATE_SHLIB_SCRIPT=yes
GENERATE_PIE_SCRIPT=yes
NOP=0x01000000
NO_SMALL_DATA=yes

case "$target" in
  sparc*-solaris*)
    TEXT_START_ADDR=0x100000000
    NONPAGED_TEXT_START_ADDR=0x100000000
    ;;
  *)
    TEXT_START_ADDR=0x100000
    NONPAGED_TEXT_START_ADDR=0x100000
    ;;
esac

# Treat a host that matches the target with the possible exception of "64"
# and "v7", "v8", "v9" in the name as if it were native.
if test `echo "$host" | sed -e 's/64//;s/v[789]//'` \
 = `echo "$target" | sed -e 's/64//;s/v[789]//'`; then
  case " $EMULATION_LIBPATH " in
    *" ${EMULATION_NAME} "*)
      NATIVE=yes
      ;;
  esac
fi

# Linux and Solaris modify the default library search path
# to first include a 64-bit specific directory.  It's put
# in slightly different places on the two systems.
# Look for 64 bit target libraries in /lib64, /usr/lib64 etc., first
# on Linux and /lib/sparcv9, /usr/lib/sparcv9 etc. on Solaris.
case "$EMULATION_NAME" in
  *64*)
    case "$target" in
      sparc*-linux*)
	LIBPATH_SUFFIX=64 ;;
      sparc*-solaris*)
	LIBPATH_SUFFIX=/sparcv9 ;;
    esac
    ;;
esac
