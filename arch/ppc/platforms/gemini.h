/*
 *  Onboard registers and descriptions for Synergy Microsystems'
 *  "Gemini" boards.
 *
 */
#ifdef __KERNEL__
#ifndef __PPC_GEMINI_H
#define __PPC_GEMINI_H

/*  Registers  */

#define GEMINI_SERIAL_B     (0xffeffb00)
#define GEMINI_SERIAL_A     (0xffeffb08)
#define GEMINI_USWITCH      (0xffeffd00)
#define GEMINI_BREV         (0xffeffe00)
#define GEMINI_BECO         (0xffeffe08)
#define GEMINI_FEAT         (0xffeffe10)
#define GEMINI_BSTAT        (0xffeffe18)
#define GEMINI_CPUSTAT      (0xffeffe20)
#define GEMINI_L2CFG        (0xffeffe30)
#define GEMINI_MEMCFG       (0xffeffe38)
#define GEMINI_FLROM        (0xffeffe40)
#define GEMINI_P0PCI        (0xffeffe48)
#define GEMINI_FLWIN        (0xffeffe50)
#define GEMINI_P0INTMASK    (0xffeffe60)
#define GEMINI_P0INTAP      (0xffeffe68)
#define GEMINI_PCIERR       (0xffeffe70)
#define GEMINI_LEDBASE      (0xffeffe80)
#define GEMINI_RTC          (0xffe9fff8)
#define GEMINI_LEDS         8
#define GEMINI_SWITCHES     8


/* Flash ROM bit definitions */
#define GEMINI_FLS_WEN      (1<<0)
#define GEMINI_FLS_JMP      (1<<6)
#define GEMINI_FLS_BOOT     (1<<7)

/* Memory bit definitions */
#define GEMINI_MEM_TYPE_MASK 0xc0
#define GEMINI_MEM_SIZE_MASK 0x38
#define GEMINI_MEM_BANK_MASK 0x07

/* L2 cache bit definitions */
#define GEMINI_L2_SIZE_MASK  0xc0
#define GEMINI_L2_RATIO_MASK 0x03

/* Timebase register bit definitons */
#define GEMINI_TIMEB0_EN     (1<<0)
#define GEMINI_TIMEB1_EN     (1<<1)
#define GEMINI_TIMEB2_EN     (1<<2)
#define GEMINI_TIMEB3_EN     (1<<3)

/* CPU status bit definitions */
#define GEMINI_CPU_ID_MASK   0x03
#define GEMINI_CPU_COUNT_MASK 0x0c
#define GEMINI_CPU0_HALTED   (1<<4)
#define GEMINI_CPU1_HALTED   (1<<5)
#define GEMINI_CPU2_HALTED   (1<<6)
#define GEMINI_CPU3_HALTED   (1<<7)

/* Board status bit definitions */
#define GEMINI_BRD_FAIL      (1<<0)   /* FAIL led is lit */
#define GEMINI_BRD_BUS_MASK  0x0c     /* PowerPC bus speed */

/* Board family/feature bit descriptions */
#define GEMINI_FEAT_HAS_FLASH (1<<0)
#define GEMINI_FEAT_HAS_ETH   (1<<1)
#define GEMINI_FEAT_HAS_SCSI  (1<<2)
#define GEMINI_FEAT_HAS_P0    (1<<3)
#define GEMINI_FEAT_FAM_MASK  0xf0

/* Mod/ECO bit definitions */
#define GEMINI_ECO_LEVEL_MASK 0x0f
#define GEMINI_MOD_MASK       0xf0

/* Type/revision bit definitions */
#define GEMINI_REV_MASK       0x0f
#define GEMINI_TYPE_MASK      0xf0

/* User switch definitions */
#define GEMINI_SWITCH_VERBOSE    1     /* adds "debug" to boot cmd line */
#define GEMINI_SWITCH_SINGLE_USER 7    /* boots into "single-user" mode */

#define SGS_RTC_CONTROL  0
#define SGS_RTC_SECONDS  1
#define SGS_RTC_MINUTES  2
#define SGS_RTC_HOURS    3
#define SGS_RTC_DAY      4
#define SGS_RTC_DAY_OF_MONTH 5
#define SGS_RTC_MONTH    6
#define SGS_RTC_YEAR     7

#define SGS_RTC_SET  0x80
#define SGS_RTC_IS_STOPPED 0x80

#define GRACKLE_CONFIG_ADDR_ADDR  (0xfec00000)
#define GRACKLE_CONFIG_DATA_ADDR  (0xfee00000)

#define GEMINI_BOOT_INIT  (0xfff00100)

#ifndef __ASSEMBLY__

static inline void grackle_write( unsigned long addr, unsigned long data )
{
  __asm__ __volatile__(
  " stwbrx %1, 0, %0\n \
    sync\n \
    stwbrx %3, 0, %2\n \
    sync "
  : /* no output */
  : "r" (GRACKLE_CONFIG_ADDR_ADDR), "r" (addr),
    "r" (GRACKLE_CONFIG_DATA_ADDR), "r" (data));
}

static inline unsigned long grackle_read( unsigned long addr )
{
  unsigned long val;

  __asm__ __volatile__(
  " stwbrx %1, 0, %2\n \
    sync\n \
    lwbrx %0, 0, %3\n \
    sync "
  : "=r" (val)
  : "r" (addr), "r" (GRACKLE_CONFIG_ADDR_ADDR),
    "r" (GRACKLE_CONFIG_DATA_ADDR));

  return val;
}

static inline void gemini_led_on( int led )
{
  if (led >= 0 && led < GEMINI_LEDS)
    *(unsigned char *)(GEMINI_LEDBASE + (led<<3)) = 1;
}

static inline void gemini_led_off(int led)
{
  if (led >= 0 && led < GEMINI_LEDS)
    *(unsigned char *)(GEMINI_LEDBASE + (led<<3)) = 0;
}

static inline int gemini_led_val(int led)
{
  int val = 0;
  if (led >= 0 && led < GEMINI_LEDS)
    val = *(unsigned char *)(GEMINI_LEDBASE + (led<<3));
  return (val & 0x1);
}

/* returns processor id from the board */
static inline int gemini_processor(void)
{
  unsigned char cpu = *(unsigned char *)(GEMINI_CPUSTAT);
  return (int) ((cpu == 0) ? 4 : (cpu & GEMINI_CPU_ID_MASK));
}


extern void _gemini_reboot(void);
extern void gemini_prom_init(void);
extern void gemini_init_l2(void);
#endif /* __ASSEMBLY__ */
#endif
#endif /* __KERNEL__ */
