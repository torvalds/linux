
OUTPUT_ARCH(m32r)
ENTRY(startup)
SECTIONS
{
  . = CONFIG_MEMORY_START + 0x00400000;

  _text = .;
  .text : { *(.text) } = 0
  .rodata : { *(.rodata) *(.rodata.*) }
  _etext = .;

  . = ALIGN(32 / 8);
  .data : { *(.data) }
  . = ALIGN(32 / 8);
  _got = .;
  .got  : { *(.got) _egot = .; *(.got.*) }
  _edata  =  .;

  . = ALIGN(32 / 8);
  __bss_start = .;
  .bss : { *(.bss) *(.sbss) }
  . = ALIGN(32 / 8);
  _ebss = .;
  . = ALIGN(4096);
  . += 4096;
  end = . ;

  got_len = (_egot - _got);
}
