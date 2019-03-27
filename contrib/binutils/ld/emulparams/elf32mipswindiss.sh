TEMPLATE_NAME=elf32
SCRIPT_NAME=elf
OUTPUT_FORMAT="elf32-bigmips"
BIG_OUTPUT_FORMAT="elf32-bigmips"
LITTLE_OUTPUT_FORMAT="elf32-littlemips"
ARCH=mips
MACHINE=
EMBEDDED=yes
MAXPAGESIZE=0x40000

# The data below is taken from the windiss.dld linker script that comes with
# the Diab linker.
TEXT_START_ADDR=0x100000
DATA_START_SYMBOLS='__DATA_ROM = .; __DATA_RAM = .;'
SDATA_START_SYMBOLS='_SDA_BASE_ = .; _gp = . + 0x7ff0;'
SDATA2_START_SYMBOLS='_SDA2_BASE_ = .;'
EXECUTABLE_SYMBOLS='__HEAP_START = .; __SP_INIT = 0x800000; __SP_END = __SP_INIT - 0x20000; __HEAP_END = __SP_END; __DATA_END = _edata; __BSS_START = __bss_start; __BSS_END = _end; __HEAP_START = _end;'

# The Diab tools use a different init/fini convention.  Initialization code
# is place in sections named ".init$NN".  These sections are then concatenated
# into the .init section.  It is important that .init$00 be first and .init$99
# be last. The other sections should be sorted, but the current linker script
# parse does not seem to allow that with the SORT keyword in this context.
INIT_START='*(.init$00); *(.init$0[1-9]); *(.init$[1-8][0-9]); *(.init$9[0-8])'
INIT_END='*(.init$99)'
FINI_START='*(.fini$00); *(.fini$0[1-9]); *(.fini$[1-8][0-9]); *(.fini$9[0-8])'
FINI_END='*(.fini$99)'
