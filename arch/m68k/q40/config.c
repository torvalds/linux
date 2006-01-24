/*
 *  arch/m68k/q40/config.c
 *
 *  Copyright (C) 1999 Richard Zidlicky
 *
 * originally based on:
 *
 *  linux/bvme/config.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/linkage.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/serial_reg.h>
#include <linux/rtc.h>
#include <linux/vt_kern.h>

#include <asm/io.h>
#include <asm/rtc.h>
#include <asm/bootinfo.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/q40_master.h>

extern irqreturn_t q40_process_int (int level, struct pt_regs *regs);
extern irqreturn_t (*q40_default_handler[]) (int, void *, struct pt_regs *);  /* added just for debugging */
extern void q40_init_IRQ (void);
extern void q40_free_irq (unsigned int, void *);
extern int  show_q40_interrupts (struct seq_file *, void *);
extern void q40_enable_irq (unsigned int);
extern void q40_disable_irq (unsigned int);
static void q40_get_model(char *model);
static int  q40_get_hardware_list(char *buffer);
extern int  q40_request_irq(unsigned int irq, irqreturn_t (*handler)(int, void *, struct pt_regs *), unsigned long flags, const char *devname, void *dev_id);
extern void q40_sched_init(irqreturn_t (*handler)(int, void *, struct pt_regs *));

extern unsigned long q40_gettimeoffset (void);
extern int q40_hwclk (int, struct rtc_time *);
extern unsigned int q40_get_ss (void);
extern int q40_set_clock_mmss (unsigned long);
static int q40_get_rtc_pll(struct rtc_pll_info *pll);
static int q40_set_rtc_pll(struct rtc_pll_info *pll);
extern void q40_reset (void);
void q40_halt(void);
extern void q40_waitbut(void);
void q40_set_vectors (void);

extern void q40_mksound(unsigned int /*freq*/, unsigned int /*ticks*/ );

extern char m68k_debug_device[];
static void q40_mem_console_write(struct console *co, const char *b,
				    unsigned int count);

extern int ql_ticks;

static struct console q40_console_driver = {
	.name =		"debug",
	.flags =	CON_PRINTBUFFER,
	.index =	-1,
};


/* early debugging function:*/
extern char *q40_mem_cptr; /*=(char *)0xff020000;*/
static int _cpleft;

static void q40_mem_console_write(struct console *co, const char *s,
				  unsigned int count)
{
  char *p=(char *)s;

  if (count<_cpleft)
    while (count-- >0){
      *q40_mem_cptr=*p++;
      q40_mem_cptr+=4;
      _cpleft--;
    }
}
#if 0
void printq40(char *str)
{
  int l=strlen(str);
  char *p=q40_mem_cptr;

  while (l-- >0 && _cpleft-- >0)
    {
      *p=*str++;
      p+=4;
    }
  q40_mem_cptr=p;
}
#endif

static int halted=0;

#ifdef CONFIG_HEARTBEAT
static void q40_heartbeat(int on)
{
  if (halted) return;

  if (on)
    Q40_LED_ON();
  else
    Q40_LED_OFF();
}
#endif

void q40_reset(void)
{
        halted=1;
        printk ("\n\n*******************************************\n"
		"Called q40_reset : press the RESET button!! \n"
		"*******************************************\n");
	Q40_LED_ON();
	while(1) ;
}
void q40_halt(void)
{
        halted=1;
        printk ("\n\n*******************\n"
		    "  Called q40_halt\n"
		    "*******************\n");
	Q40_LED_ON();
	while(1) ;
}

static void q40_get_model(char *model)
{
    sprintf(model, "Q40");
}

/* No hardware options on Q40? */

static int q40_get_hardware_list(char *buffer)
{
    *buffer = '\0';
    return 0;
}

static unsigned int serports[]={0x3f8,0x2f8,0x3e8,0x2e8,0};
void q40_disable_irqs(void)
{
  unsigned i,j;

  j=0;
  while((i=serports[j++])) outb(0,i+UART_IER);
  master_outb(0,EXT_ENABLE_REG);
  master_outb(0,KEY_IRQ_ENABLE_REG);
}

void __init config_q40(void)
{
    mach_sched_init      = q40_sched_init;

    mach_init_IRQ        = q40_init_IRQ;
    mach_gettimeoffset   = q40_gettimeoffset;
    mach_hwclk           = q40_hwclk;
    mach_get_ss          = q40_get_ss;
    mach_get_rtc_pll     = q40_get_rtc_pll;
    mach_set_rtc_pll     = q40_set_rtc_pll;
    mach_set_clock_mmss	 = q40_set_clock_mmss;

    mach_reset		 = q40_reset;
    mach_free_irq	 = q40_free_irq;
    mach_process_int	 = q40_process_int;
    mach_get_irq_list	 = show_q40_interrupts;
    mach_request_irq	 = q40_request_irq;
    enable_irq		 = q40_enable_irq;
    disable_irq          = q40_disable_irq;
    mach_default_handler = &q40_default_handler;
    mach_get_model       = q40_get_model;
    mach_get_hardware_list = q40_get_hardware_list;

#if defined(CONFIG_INPUT_M68K_BEEP) || defined(CONFIG_INPUT_M68K_BEEP_MODULE)
    mach_beep            = q40_mksound;
#endif
#ifdef CONFIG_HEARTBEAT
    mach_heartbeat = q40_heartbeat;
#endif
    mach_halt = q40_halt;

    /* disable a few things that SMSQ might have left enabled */
    q40_disable_irqs();

    /* no DMA at all, but ide-scsi requires it.. make sure
     * all physical RAM fits into the boundary - otherwise
     * allocator may play costly and useless tricks */
    mach_max_dma_address = 1024*1024*1024;

    /* useful for early debugging stages - writes kernel messages into SRAM */
    if (!strncmp( m68k_debug_device,"mem",3 ))
      {
	/*printk("using NVRAM debug, q40_mem_cptr=%p\n",q40_mem_cptr);*/
	_cpleft=2000-((long)q40_mem_cptr-0xff020000)/4;
	q40_console_driver.write = q40_mem_console_write;
	register_console(&q40_console_driver);
      }
}


int q40_parse_bootinfo(const struct bi_record *rec)
{
  return 1;
}


static inline unsigned char bcd2bin (unsigned char b)
{
	return ((b>>4)*10 + (b&15));
}

static inline unsigned char bin2bcd (unsigned char b)
{
	return (((b/10)*16) + (b%10));
}


unsigned long q40_gettimeoffset (void)
{
    return 5000*(ql_ticks!=0);
}


/*
 * Looks like op is non-zero for setting the clock, and zero for
 * reading the clock.
 *
 *  struct hwclk_time {
 *         unsigned        sec;       0..59
 *         unsigned        min;       0..59
 *         unsigned        hour;      0..23
 *         unsigned        day;       1..31
 *         unsigned        mon;       0..11
 *         unsigned        year;      00...
 *         int             wday;      0..6, 0 is Sunday, -1 means unknown/don't set
 * };
 */

int q40_hwclk(int op, struct rtc_time *t)
{
        if (op)
	{	/* Write.... */
	        Q40_RTC_CTRL |= Q40_RTC_WRITE;

		Q40_RTC_SECS = bin2bcd(t->tm_sec);
		Q40_RTC_MINS = bin2bcd(t->tm_min);
		Q40_RTC_HOUR = bin2bcd(t->tm_hour);
		Q40_RTC_DATE = bin2bcd(t->tm_mday);
		Q40_RTC_MNTH = bin2bcd(t->tm_mon + 1);
		Q40_RTC_YEAR = bin2bcd(t->tm_year%100);
		if (t->tm_wday >= 0)
			Q40_RTC_DOW = bin2bcd(t->tm_wday+1);

	        Q40_RTC_CTRL &= ~(Q40_RTC_WRITE);
	}
	else
	{	/* Read....  */
	  Q40_RTC_CTRL |= Q40_RTC_READ;

	  t->tm_year = bcd2bin (Q40_RTC_YEAR);
	  t->tm_mon  = bcd2bin (Q40_RTC_MNTH)-1;
	  t->tm_mday = bcd2bin (Q40_RTC_DATE);
	  t->tm_hour = bcd2bin (Q40_RTC_HOUR);
	  t->tm_min  = bcd2bin (Q40_RTC_MINS);
	  t->tm_sec  = bcd2bin (Q40_RTC_SECS);

	  Q40_RTC_CTRL &= ~(Q40_RTC_READ);

	  if (t->tm_year < 70)
	    t->tm_year += 100;
	  t->tm_wday = bcd2bin(Q40_RTC_DOW)-1;

	}

	return 0;
}

unsigned int q40_get_ss(void)
{
	return bcd2bin(Q40_RTC_SECS);
}

/*
 * Set the minutes and seconds from seconds value 'nowtime'.  Fail if
 * clock is out by > 30 minutes.  Logic lifted from atari code.
 */

int q40_set_clock_mmss (unsigned long nowtime)
{
	int retval = 0;
	short real_seconds = nowtime % 60, real_minutes = (nowtime / 60) % 60;

	int rtc_minutes;


	rtc_minutes = bcd2bin (Q40_RTC_MINS);

	if ((rtc_minutes < real_minutes
		? real_minutes - rtc_minutes
			: rtc_minutes - real_minutes) < 30)
	{
	        Q40_RTC_CTRL |= Q40_RTC_WRITE;
		Q40_RTC_MINS = bin2bcd(real_minutes);
		Q40_RTC_SECS = bin2bcd(real_seconds);
		Q40_RTC_CTRL &= ~(Q40_RTC_WRITE);
	}
	else
		retval = -1;


	return retval;
}


/* get and set PLL calibration of RTC clock */
#define Q40_RTC_PLL_MASK ((1<<5)-1)
#define Q40_RTC_PLL_SIGN (1<<5)

static int q40_get_rtc_pll(struct rtc_pll_info *pll)
{
	int tmp=Q40_RTC_CTRL;
	pll->pll_value = tmp & Q40_RTC_PLL_MASK;
	if (tmp & Q40_RTC_PLL_SIGN)
		pll->pll_value = -pll->pll_value;
	pll->pll_max=31;
	pll->pll_min=-31;
	pll->pll_posmult=512;
	pll->pll_negmult=256;
	pll->pll_clock=125829120;
	return 0;
}

static int q40_set_rtc_pll(struct rtc_pll_info *pll)
{
	if (!pll->pll_ctrl){
		/* the docs are a bit unclear so I am doublesetting */
		/* RTC_WRITE here ... */
		int tmp = (pll->pll_value & 31) | (pll->pll_value<0 ? 32 : 0) |
			  Q40_RTC_WRITE;
		Q40_RTC_CTRL |= Q40_RTC_WRITE;
		Q40_RTC_CTRL = tmp;
		Q40_RTC_CTRL &= ~(Q40_RTC_WRITE);
		return 0;
	} else
		return -EINVAL;
}
