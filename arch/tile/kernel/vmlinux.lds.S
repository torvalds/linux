#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <hv/hypervisor.h>

/* Text loads starting from the supervisor interrupt vector address. */
#define TEXT_OFFSET MEM_SV_INTRPT

OUTPUT_ARCH(tile)
ENTRY(_start)
jiffies = jiffies_64;

PHDRS
{
  intrpt1 PT_LOAD ;
  text PT_LOAD ;
  data PT_LOAD ;
}
SECTIONS
{
  /* Text is loaded with a different VA than data; start with text. */
  #undef LOAD_OFFSET
  #define LOAD_OFFSET TEXT_OFFSET

  /* Interrupt vectors */
  .intrpt1 (LOAD_OFFSET) : AT ( 0 )   /* put at the start of physical memory */
  {
    _text = .;
    _stext = .;
    *(.intrpt1)
  } :intrpt1 =0

  /* Hypervisor call vectors */
  #include "hvglue.lds"

  /* Now the real code */
  . = ALIGN(0x20000);
  .text : AT (ADDR(.text) - LOAD_OFFSET) {
    HEAD_TEXT
    SCHED_TEXT
    LOCK_TEXT
    __fix_text_end = .;   /* tile-cpack won't rearrange before this */
    TEXT_TEXT
    *(.text.*)
    *(.coldtext*)
    *(.fixup)
    *(.gnu.warning)
  } :text =0
  _etext = .;

  /* "Init" is divided into two areas with very different virtual addresses. */
  INIT_TEXT_SECTION(PAGE_SIZE)

  /* Now we skip back to PAGE_OFFSET for the data. */
  . = (. - TEXT_OFFSET + PAGE_OFFSET);
  #undef LOAD_OFFSET
  #define LOAD_OFFSET PAGE_OFFSET

  . = ALIGN(PAGE_SIZE);
  VMLINUX_SYMBOL(_sinitdata) = .;
  INIT_DATA_SECTION(16) :data =0
  PERCPU(L2_CACHE_BYTES, PAGE_SIZE)
  . = ALIGN(PAGE_SIZE);
  VMLINUX_SYMBOL(_einitdata) = .;

  _sdata = .;                   /* Start of data section */

  RO_DATA_SECTION(PAGE_SIZE)

  /* initially writeable, then read-only */
  . = ALIGN(PAGE_SIZE);
  __w1data_begin = .;
  .w1data : AT(ADDR(.w1data) - LOAD_OFFSET) {
    VMLINUX_SYMBOL(__w1data_begin) = .;
    *(.w1data)
    VMLINUX_SYMBOL(__w1data_end) = .;
  }

  RW_DATA_SECTION(L2_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)

  _edata = .;

  EXCEPTION_TABLE(L2_CACHE_BYTES)
  NOTES


  BSS_SECTION(8, PAGE_SIZE, 1)
  _end = . ;

  STABS_DEBUG
  DWARF_DEBUG

  DISCARDS
}
