MACHINE=
SCRIPT_NAME=mep
OUTPUT_FORMAT="elf32-mep"
TEXT_START_ADDR=0x1000
ARCH=mep
MAXPAGESIZE=256
ENTRY=_start
EMBEDDED=yes
TEMPLATE_NAME=elf32
DATA_START_SYMBOLS='__data_start = . ;'
OTHER_GOT_SYMBOLS='
  . = ALIGN(4);
  __sdabase = . + 0x8000;
  .srodata : { *(.srodata) *(.srodata.*) *(.gnu.linkonce.srd.*) }
'
OTHER_SDATA_SECTIONS='
  PROVIDE (__sdabase = .);
  __assert_tiny_size = ASSERT ((. < __sdabase) || ((. - __sdabase) <= 0x8000),
			      "tiny section overflow");
'
OTHER_READONLY_SECTIONS='
  __stack = 0x001ffff0;
  __stack_size = 0x100000;
  __stack0  = (__stack - (0 *  (__stack_size / 1)) + 15) / 16 * 16;

  .rostacktab : 
  {
    /* Emit a table describing the location of the different stacks.
       Only 1 processor in the default configuration.  */
    . = ALIGN(4);
    __stack_table = .;
    LONG (__stack0);
  }
'
OTHER_END_SYMBOLS='
  PROVIDE (__heap = _end);
  PROVIDE (__heap_end = 0);
'
OTHER_TEXT_SECTIONS='
  *(.ftext) *(.ftext.*) *(.gnu.linkonce.ft.*)
  . = ALIGN(8);
  *(.vftext) *(.vftext.*) *(.gnu.linkonce.vf.*)
  *(.frodata) *(.frodata.*) *(.gnu.linkonce.frd.*)
'
OTHER_READWRITE_SECTIONS='
  . = ALIGN(4);
  __tpbase = .;
  .based : { *(.based) *(.based.*) *(.gnu.linkonce.based.*) }
  __assert_based_size = ASSERT ((. - __tpbase) <= 0x80, "based section overflow");
  .far : { *(.far) *(.far.*) *(.gnu.linkonce.far.*) }
'
OTHER_BSS_SECTIONS='
  __assert_near_size = ASSERT (. <= 0x1000000, "near section overflow");
  .farbss : { PROVIDE (__farbss_start = .); *(.farbss) *(.farbss.*) PROVIDE (__farbss_end = .); }
'
