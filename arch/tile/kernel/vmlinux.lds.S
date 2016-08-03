#include <asm-generic/vmlinux.lds.h>
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <hv/hypervisor.h>

/* Text loads starting from the supervisor interrupt vector address. */
#define TEXT_OFFSET MEM_SV_START

OUTPUT_ARCH(tile)
ENTRY(_start)
jiffies = jiffies_64;

PHDRS
{
  intrpt PT_LOAD ;
  text PT_LOAD ;
  data PT_LOAD ;
}
SECTIONS
{
  /* Text is loaded with a different VA than data; start with text. */
  #undef LOAD_OFFSET
  #define LOAD_OFFSET TEXT_OFFSET

  /* Interrupt vectors */
  .intrpt (LOAD_OFFSET) : AT ( 0 )   /* put at the start of physical memory */
  {
    _text = .;
    *(.intrpt)
  } :intrpt =0

  /* Hypervisor call vectors */
  . = ALIGN(0x10000);
  .hvglue : AT (ADDR(.hvglue) - LOAD_OFFSET) {
    *(.hvglue)
  } :NONE

  /* Now the real code */
  . = ALIGN(0x20000);
  _stext = .;
  .text : AT (ADDR(.text) - LOAD_OFFSET) {
    HEAD_TEXT
    SCHED_TEXT
    LOCK_TEXT
    KPROBES_TEXT
    IRQENTRY_TEXT
    SOFTIRQENTRY_TEXT
    __fix_text_end = .;   /* tile-cpack won't rearrange before this */
    ALIGN_FUNCTION();
    *(.hottext*)
    TEXT_TEXT
    *(.text.*)
    *(.coldtext*)
    *(.fixup)
    *(.gnu.warning)
  } :text =0
  _etext = .;

  /* "Init" is divided into two areas with very different virtual addresses. */
  INIT_TEXT_SECTION(PAGE_SIZE)

  /*
   * Some things, like the __jump_table, may contain symbol references
   * to __exit text, so include such text in the final image if so.
   * In that case we also override the _einittext from INIT_TEXT_SECTION.
   */
#ifdef CONFIG_JUMP_LABEL
  .exit.text : {
    EXIT_TEXT
    _einittext = .;
  }
#endif

  /* Now we skip back to PAGE_OFFSET for the data. */
  . = (. - TEXT_OFFSET + PAGE_OFFSET);
  #undef LOAD_OFFSET
  #define LOAD_OFFSET PAGE_OFFSET

  . = ALIGN(PAGE_SIZE);
  __init_begin = .;
  INIT_DATA_SECTION(16) :data =0
  PERCPU_SECTION(L2_CACHE_BYTES)
  . = ALIGN(PAGE_SIZE);
  __init_end = .;

  _sdata = .;                   /* Start of data section */
  RO_DATA_SECTION(PAGE_SIZE)
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
