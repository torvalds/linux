/* $Id: fasttimer.c,v 1.11 2005/01/04 11:15:46 starvik Exp $
 * linux/arch/cris/kernel/fasttimer.c
 *
 * Fast timers for ETRAX FS
 * This may be useful in other OS than Linux so use 2 space indentation...
 *
 * $Log: fasttimer.c,v $
 * Revision 1.11  2005/01/04 11:15:46  starvik
 * Don't share timer IRQ.
 *
 * Revision 1.10  2004/12/07 09:19:38  starvik
 * Corrected includes.
 * Use correct interrupt macros.
 *
 * Revision 1.9  2004/05/14 10:18:58  starvik
 * Export fast_timer_list
 *
 * Revision 1.8  2004/05/14 07:58:03  starvik
 * Merge of changes from 2.4
 *
 * Revision 1.7  2003/07/10 12:06:14  starvik
 * Return IRQ_NONE if irq wasn't handled
 *
 * Revision 1.6  2003/07/04 08:27:49  starvik
 * Merge of Linux 2.5.74
 *
 * Revision 1.5  2003/06/05 10:16:22  johana
 * New INTR_VECT macros.
 *
 * Revision 1.4  2003/06/03 08:49:45  johana
 * Fixed typo.
 *
 * Revision 1.3  2003/06/02 12:51:27  johana
 * Now compiles.
 * Commented some include files that probably can be removed.
 *
 * Revision 1.2  2003/06/02 12:09:41  johana
 * Ported to ETRAX FS using the trig interrupt instead of timer1.
 *
 * Revision 1.3  2002/12/12 08:26:32  starvik
 * Don't use C-comments inside CVS comments
 *
 * Revision 1.2  2002/12/11 15:42:02  starvik
 * Extracted v10 (ETRAX 100LX) specific stuff from arch/cris/kernel/
 *
 * Revision 1.1  2002/11/18 07:58:06  starvik
 * Fast timers (from Linux 2.4)
 *
 * Revision 1.5  2002/10/15 06:21:39  starvik
 * Added call to init_waitqueue_head
 *
 * Revision 1.4  2002/05/28 17:47:59  johana
 * Added del_fast_timer()
 *
 * Revision 1.3  2002/05/28 16:16:07  johana
 * Handle empty fast_timer_list
 *
 * Revision 1.2  2002/05/27 15:38:42  johana
 * Made it compile without warnings on Linux 2.4.
 * (includes, wait_queue, PROC_FS and snprintf)
 *
 * Revision 1.1  2002/05/27 15:32:25  johana
 * arch/etrax100/kernel/fasttimer.c v1.8 from the elinux tree.
 *
 * Revision 1.8  2001/11/27 13:50:40  pkj
 * Disable interrupts while stopping the timer and while modifying the
 * list of active timers in timer1_handler() as it may be interrupted
 * by other interrupts (e.g., the serial interrupt) which may add fast
 * timers.
 *
 * Revision 1.7  2001/11/22 11:50:32  pkj
 * * Only store information about the last 16 timers.
 * * proc_fasttimer_read() now uses an allocated buffer, since it
 *   requires more space than just a page even for only writing the
 *   last 16 timers. The buffer is only allocated on request, so
 *   unless /proc/fasttimer is read, it is never allocated.
 * * Renamed fast_timer_started to fast_timers_started to match
 *   fast_timers_added and fast_timers_expired.
 * * Some clean-up.
 *
 * Revision 1.6  2000/12/13 14:02:08  johana
 * Removed volatile for fast_timer_list
 *
 * Revision 1.5  2000/12/13 13:55:35  johana
 * Added DEBUG_LOG, added som cli() and cleanup
 *
 * Revision 1.4  2000/12/05 13:48:50  johana
 * Added range check when writing proc file, modified timer int handling
 *
 * Revision 1.3  2000/11/23 10:10:20  johana
 * More debug/logging possibilities.
 * Moved GET_JIFFIES_USEC() to timex.h and time.c
 *
 * Revision 1.2  2000/11/01 13:41:04  johana
 * Clean up and bugfixes.
 * Created new do_gettimeofday_fast() that gets a timeval struct
 * with time based on jiffies and *R_TIMER0_DATA, uses a table
 * for fast conversion of timer value to microseconds.
 * (Much faster the standard do_gettimeofday() and we don't really
 * wan't to use the true time - we wan't the "uptime" so timers don't screw up
 * when we change the time.
 * TODO: Add efficient support for continuous timers as well.
 *
 * Revision 1.1  2000/10/26 15:49:16  johana
 * Added fasttimer, highresolution timers.
 *
 * Copyright (C) 2000,2001 2002, 2003 Axis Communications AB, Lund, Sweden
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>

#include <asm/irq.h>
#include <asm/system.h>

#include <linux/config.h>
#include <linux/version.h>

#include <asm/arch/hwregs/reg_map.h>
#include <asm/arch/hwregs/reg_rdwr.h>
#include <asm/arch/hwregs/timer_defs.h>
#include <asm/fasttimer.h>
#include <linux/proc_fs.h>

/*
 * timer0 is running at 100MHz and generating jiffies timer ticks
 * at 100 or 1000 HZ.
 * fasttimer gives an API that gives timers that expire "between" the jiffies
 * giving microsecond resolution (10 ns).
 * fasttimer uses reg_timer_rw_trig register to get interrupt when
 * r_time reaches a certain value.
 */


#define DEBUG_LOG_INCLUDED
#define FAST_TIMER_LOG
//#define FAST_TIMER_TEST

#define FAST_TIMER_SANITY_CHECKS

#ifdef FAST_TIMER_SANITY_CHECKS
#define SANITYCHECK(x) x
static int sanity_failed = 0;
#else
#define SANITYCHECK(x)
#endif

#define D1(x)
#define D2(x)
#define DP(x)

#define __INLINE__ inline

static int fast_timer_running = 0;
static int fast_timers_added = 0;
static int fast_timers_started = 0;
static int fast_timers_expired = 0;
static int fast_timers_deleted = 0;
static int fast_timer_is_init = 0;
static int fast_timer_ints = 0;

struct fast_timer *fast_timer_list = NULL;

#ifdef DEBUG_LOG_INCLUDED
#define DEBUG_LOG_MAX 128
static const char * debug_log_string[DEBUG_LOG_MAX];
static unsigned long debug_log_value[DEBUG_LOG_MAX];
static int debug_log_cnt = 0;
static int debug_log_cnt_wrapped = 0;

#define DEBUG_LOG(string, value) \
{ \
  unsigned long log_flags; \
  local_irq_save(log_flags); \
  debug_log_string[debug_log_cnt] = (string); \
  debug_log_value[debug_log_cnt] = (unsigned long)(value); \
  if (++debug_log_cnt >= DEBUG_LOG_MAX) \
  { \
    debug_log_cnt = debug_log_cnt % DEBUG_LOG_MAX; \
    debug_log_cnt_wrapped = 1; \
  } \
  local_irq_restore(log_flags); \
}
#else
#define DEBUG_LOG(string, value)
#endif


#define NUM_TIMER_STATS 16
#ifdef FAST_TIMER_LOG
struct fast_timer timer_added_log[NUM_TIMER_STATS];
struct fast_timer timer_started_log[NUM_TIMER_STATS];
struct fast_timer timer_expired_log[NUM_TIMER_STATS];
#endif

int timer_div_settings[NUM_TIMER_STATS];
int timer_delay_settings[NUM_TIMER_STATS];


static void
timer_trig_handler(void);



/* Not true gettimeofday, only checks the jiffies (uptime) + useconds */
void __INLINE__ do_gettimeofday_fast(struct timeval *tv)
{
  unsigned long sec = jiffies;
  unsigned long usec = GET_JIFFIES_USEC();

  usec += (sec % HZ) * (1000000 / HZ);
  sec = sec / HZ;

  if (usec > 1000000)
  {
    usec -= 1000000;
    sec++;
  }
  tv->tv_sec = sec;
  tv->tv_usec = usec;
}

int __INLINE__ timeval_cmp(struct timeval *t0, struct timeval *t1)
{
  if (t0->tv_sec < t1->tv_sec)
  {
    return -1;
  }
  else if (t0->tv_sec > t1->tv_sec)
  {
    return 1;
  }
  if (t0->tv_usec < t1->tv_usec)
  {
    return -1;
  }
  else if (t0->tv_usec > t1->tv_usec)
  {
    return 1;
  }
  return 0;
}

/* Called with ints off */
void __INLINE__ start_timer_trig(unsigned long delay_us)
{
  reg_timer_rw_ack_intr ack_intr = { 0 };
  reg_timer_rw_intr_mask intr_mask;
  reg_timer_rw_trig trig;
  reg_timer_rw_trig_cfg trig_cfg = { 0 };
  reg_timer_r_time r_time;

  r_time = REG_RD(timer, regi_timer, r_time);

  D1(printk("start_timer_trig : %d us freq: %i div: %i\n",
            delay_us, freq_index, div));
  /* Clear trig irq */
  intr_mask = REG_RD(timer, regi_timer, rw_intr_mask);
  intr_mask.trig = 0;
  REG_WR(timer, regi_timer, rw_intr_mask, intr_mask);

  /* Set timer values */
  /* r_time is 100MHz (10 ns resolution) */
  trig = r_time + delay_us*(1000/10);

  timer_div_settings[fast_timers_started % NUM_TIMER_STATS] = trig;
  timer_delay_settings[fast_timers_started % NUM_TIMER_STATS] = delay_us;

  /* Ack interrupt */
  ack_intr.trig = 1;
  REG_WR(timer, regi_timer, rw_ack_intr, ack_intr);

  /* Start timer */
  REG_WR(timer, regi_timer, rw_trig, trig);
  trig_cfg.tmr = regk_timer_time;
  REG_WR(timer, regi_timer, rw_trig_cfg, trig_cfg);

  /* Check if we have already passed the trig time */
  r_time = REG_RD(timer, regi_timer, r_time);
  if (r_time < trig) {
    /* No, Enable trig irq */
    intr_mask = REG_RD(timer, regi_timer, rw_intr_mask);
    intr_mask.trig = 1;
    REG_WR(timer, regi_timer, rw_intr_mask, intr_mask);
    fast_timers_started++;
    fast_timer_running = 1;
  }
  else
  {
    /* We have passed the time, disable trig point, ack intr */
    trig_cfg.tmr = regk_timer_off;
    REG_WR(timer, regi_timer, rw_trig_cfg, trig_cfg);
    REG_WR(timer, regi_timer, rw_ack_intr, ack_intr);
    /* call the int routine directly */
    timer_trig_handler();
  }

}

/* In version 1.4 this function takes 27 - 50 us */
void start_one_shot_timer(struct fast_timer *t,
                          fast_timer_function_type *function,
                          unsigned long data,
                          unsigned long delay_us,
                          const char *name)
{
  unsigned long flags;
  struct fast_timer *tmp;

  D1(printk("sft %s %d us\n", name, delay_us));

  local_irq_save(flags);

  do_gettimeofday_fast(&t->tv_set);
  tmp = fast_timer_list;

  SANITYCHECK({ /* Check so this is not in the list already... */
    while (tmp != NULL)
    {
      if (tmp == t)
      {
        printk("timer name: %s data: 0x%08lX already in list!\n", name, data);
        sanity_failed++;
        return;
      }
      else
      {
        tmp = tmp->next;
      }
    }
    tmp = fast_timer_list;
  });

  t->delay_us = delay_us;
  t->function = function;
  t->data = data;
  t->name = name;

  t->tv_expires.tv_usec = t->tv_set.tv_usec + delay_us % 1000000;
  t->tv_expires.tv_sec  = t->tv_set.tv_sec  + delay_us / 1000000;
  if (t->tv_expires.tv_usec > 1000000)
  {
    t->tv_expires.tv_usec -= 1000000;
    t->tv_expires.tv_sec++;
  }
#ifdef FAST_TIMER_LOG
  timer_added_log[fast_timers_added % NUM_TIMER_STATS] = *t;
#endif
  fast_timers_added++;

  /* Check if this should timeout before anything else */
  if (tmp == NULL || timeval_cmp(&t->tv_expires, &tmp->tv_expires) < 0)
  {
    /* Put first in list and modify the timer value */
    t->prev = NULL;
    t->next = fast_timer_list;
    if (fast_timer_list)
    {
      fast_timer_list->prev = t;
    }
    fast_timer_list = t;
#ifdef FAST_TIMER_LOG
    timer_started_log[fast_timers_started % NUM_TIMER_STATS] = *t;
#endif
    start_timer_trig(delay_us);
  } else {
    /* Put in correct place in list */
    while (tmp->next &&
           timeval_cmp(&t->tv_expires, &tmp->next->tv_expires) > 0)
    {
      tmp = tmp->next;
    }
    /* Insert t after tmp */
    t->prev = tmp;
    t->next = tmp->next;
    if (tmp->next)
    {
      tmp->next->prev = t;
    }
    tmp->next = t;
  }

  D2(printk("start_one_shot_timer: %d us done\n", delay_us));

  local_irq_restore(flags);
} /* start_one_shot_timer */

static inline int fast_timer_pending (const struct fast_timer * t)
{
  return (t->next != NULL) || (t->prev != NULL) || (t == fast_timer_list);
}

static inline int detach_fast_timer (struct fast_timer *t)
{
  struct fast_timer *next, *prev;
  if (!fast_timer_pending(t))
    return 0;
  next = t->next;
  prev = t->prev;
  if (next)
    next->prev = prev;
  if (prev)
    prev->next = next;
  else
    fast_timer_list = next;
  fast_timers_deleted++;
  return 1;
}

int del_fast_timer(struct fast_timer * t)
{
  unsigned long flags;
  int ret;

  local_irq_save(flags);
  ret = detach_fast_timer(t);
  t->next = t->prev = NULL;
  local_irq_restore(flags);
  return ret;
} /* del_fast_timer */


/* Interrupt routines or functions called in interrupt context */

/* Timer interrupt handler for trig interrupts */

static irqreturn_t
timer_trig_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
  reg_timer_r_masked_intr masked_intr;

  /* Check if the timer interrupt is for us (a trig int) */
  masked_intr = REG_RD(timer, regi_timer, r_masked_intr);
  if (!masked_intr.trig)
    return IRQ_NONE;
  timer_trig_handler();
  return IRQ_HANDLED;
}

static void timer_trig_handler(void)
{
  reg_timer_rw_ack_intr ack_intr = { 0 };
  reg_timer_rw_intr_mask intr_mask;
  reg_timer_rw_trig_cfg trig_cfg = { 0 };
  struct fast_timer *t;
  unsigned long flags;

  local_irq_save(flags);

  /* Clear timer trig interrupt */
  intr_mask = REG_RD(timer, regi_timer, rw_intr_mask);
  intr_mask.trig = 0;
  REG_WR(timer, regi_timer, rw_intr_mask, intr_mask);

  /* First stop timer, then ack interrupt */
  /* Stop timer */
  trig_cfg.tmr = regk_timer_off;
  REG_WR(timer, regi_timer, rw_trig_cfg, trig_cfg);

  /* Ack interrupt */
  ack_intr.trig = 1;
  REG_WR(timer, regi_timer, rw_ack_intr, ack_intr);

  fast_timer_running = 0;
  fast_timer_ints++;

  local_irq_restore(flags);

  t = fast_timer_list;
  while (t)
  {
    struct timeval tv;

    /* Has it really expired? */
    do_gettimeofday_fast(&tv);
    D1(printk("t: %is %06ius\n", tv.tv_sec, tv.tv_usec));

    if (timeval_cmp(&t->tv_expires, &tv) <= 0)
    {
      /* Yes it has expired */
#ifdef FAST_TIMER_LOG
      timer_expired_log[fast_timers_expired % NUM_TIMER_STATS] = *t;
#endif
      fast_timers_expired++;

      /* Remove this timer before call, since it may reuse the timer */
      local_irq_save(flags);
      if (t->prev)
      {
        t->prev->next = t->next;
      }
      else
      {
        fast_timer_list = t->next;
      }
      if (t->next)
      {
        t->next->prev = t->prev;
      }
      t->prev = NULL;
      t->next = NULL;
      local_irq_restore(flags);

      if (t->function != NULL)
      {
        t->function(t->data);
      }
      else
      {
        DEBUG_LOG("!trimertrig %i function==NULL!\n", fast_timer_ints);
      }
    }
    else
    {
      /* Timer is to early, let's set it again using the normal routines */
      D1(printk(".\n"));
    }

    local_irq_save(flags);
    if ((t = fast_timer_list) != NULL)
    {
      /* Start next timer.. */
      long us;
      struct timeval tv;

      do_gettimeofday_fast(&tv);
      us = ((t->tv_expires.tv_sec - tv.tv_sec) * 1000000 +
            t->tv_expires.tv_usec - tv.tv_usec);
      if (us > 0)
      {
        if (!fast_timer_running)
        {
#ifdef FAST_TIMER_LOG
          timer_started_log[fast_timers_started % NUM_TIMER_STATS] = *t;
#endif
          start_timer_trig(us);
        }
        local_irq_restore(flags);
        break;
      }
      else
      {
        /* Timer already expired, let's handle it better late than never.
         * The normal loop handles it
         */
        D1(printk("e! %d\n", us));
      }
    }
    local_irq_restore(flags);
  }

  if (!t)
  {
    D1(printk("ttrig stop!\n"));
  }
}

static void wake_up_func(unsigned long data)
{
#ifdef DECLARE_WAITQUEUE
  wait_queue_head_t  *sleep_wait_p = (wait_queue_head_t*)data;
#else
  struct wait_queue **sleep_wait_p = (struct wait_queue **)data;
#endif
  wake_up(sleep_wait_p);
}


/* Useful API */

void schedule_usleep(unsigned long us)
{
  struct fast_timer t;
#ifdef DECLARE_WAITQUEUE
  wait_queue_head_t sleep_wait;
  init_waitqueue_head(&sleep_wait);
  {
  DECLARE_WAITQUEUE(wait, current);
#else
  struct wait_queue *sleep_wait = NULL;
  struct wait_queue wait = { current, NULL };
#endif

  D1(printk("schedule_usleep(%d)\n", us));
  add_wait_queue(&sleep_wait, &wait);
  set_current_state(TASK_INTERRUPTIBLE);
  start_one_shot_timer(&t, wake_up_func, (unsigned long)&sleep_wait, us,
                       "usleep");
  schedule();
  set_current_state(TASK_RUNNING);
  remove_wait_queue(&sleep_wait, &wait);
  D1(printk("done schedule_usleep(%d)\n", us));
#ifdef DECLARE_WAITQUEUE
  }
#endif
}

#ifdef CONFIG_PROC_FS
static int proc_fasttimer_read(char *buf, char **start, off_t offset, int len
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
                       ,int *eof, void *data_unused
#else
                        ,int unused
#endif
                               );
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
static struct proc_dir_entry *fasttimer_proc_entry;
#else
static struct proc_dir_entry fasttimer_proc_entry =
{
  0, 9, "fasttimer",
  S_IFREG | S_IRUGO, 1, 0, 0,
  0, NULL /* ops -- default to array */,
  &proc_fasttimer_read /* get_info */,
};
#endif
#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_PROC_FS

/* This value is very much based on testing */
#define BIG_BUF_SIZE (500 + NUM_TIMER_STATS * 300)

static int proc_fasttimer_read(char *buf, char **start, off_t offset, int len
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
                       ,int *eof, void *data_unused
#else
                        ,int unused
#endif
                               )
{
  unsigned long flags;
  int i = 0;
  int num_to_show;
  struct timeval tv;
  struct fast_timer *t, *nextt;
  static char *bigbuf = NULL;
  static unsigned long used;

  if (!bigbuf && !(bigbuf = vmalloc(BIG_BUF_SIZE)))
  {
    used = 0;
    bigbuf[0] = '\0';
    return 0;
  }

  if (!offset || !used)
  {
    do_gettimeofday_fast(&tv);

    used = 0;
    used += sprintf(bigbuf + used, "Fast timers added:     %i\n",
                    fast_timers_added);
    used += sprintf(bigbuf + used, "Fast timers started:   %i\n",
                    fast_timers_started);
    used += sprintf(bigbuf + used, "Fast timer interrupts: %i\n",
                    fast_timer_ints);
    used += sprintf(bigbuf + used, "Fast timers expired:   %i\n",
                    fast_timers_expired);
    used += sprintf(bigbuf + used, "Fast timers deleted:   %i\n",
                    fast_timers_deleted);
    used += sprintf(bigbuf + used, "Fast timer running:    %s\n",
                    fast_timer_running ? "yes" : "no");
    used += sprintf(bigbuf + used, "Current time:          %lu.%06lu\n",
                    (unsigned long)tv.tv_sec,
                    (unsigned long)tv.tv_usec);
#ifdef FAST_TIMER_SANITY_CHECKS
    used += sprintf(bigbuf + used, "Sanity failed:         %i\n",
                    sanity_failed);
#endif
    used += sprintf(bigbuf + used, "\n");

#ifdef DEBUG_LOG_INCLUDED
    {
      int end_i = debug_log_cnt;
      i = 0;

      if (debug_log_cnt_wrapped)
      {
        i = debug_log_cnt;
      }

      while ((i != end_i || (debug_log_cnt_wrapped && !used)) &&
             used+100 < BIG_BUF_SIZE)
      {
        used += sprintf(bigbuf + used, debug_log_string[i],
                        debug_log_value[i]);
        i = (i+1) % DEBUG_LOG_MAX;
      }
    }
    used += sprintf(bigbuf + used, "\n");
#endif

    num_to_show = (fast_timers_started < NUM_TIMER_STATS ? fast_timers_started:
                   NUM_TIMER_STATS);
    used += sprintf(bigbuf + used, "Timers started: %i\n", fast_timers_started);
    for (i = 0; i < num_to_show && (used+100 < BIG_BUF_SIZE) ; i++)
    {
      int cur = (fast_timers_started - i - 1) % NUM_TIMER_STATS;

#if 1 //ndef FAST_TIMER_LOG
      used += sprintf(bigbuf + used, "div: %i delay: %i"
                      "\n",
                      timer_div_settings[cur],
                      timer_delay_settings[cur]
                      );
#endif
#ifdef FAST_TIMER_LOG
      t = &timer_started_log[cur];
      used += sprintf(bigbuf + used, "%-14s s: %6lu.%06lu e: %6lu.%06lu "
                      "d: %6li us data: 0x%08lX"
                      "\n",
                      t->name,
                      (unsigned long)t->tv_set.tv_sec,
                      (unsigned long)t->tv_set.tv_usec,
                      (unsigned long)t->tv_expires.tv_sec,
                      (unsigned long)t->tv_expires.tv_usec,
                      t->delay_us,
                      t->data
                      );
#endif
    }
    used += sprintf(bigbuf + used, "\n");

#ifdef FAST_TIMER_LOG
    num_to_show = (fast_timers_added < NUM_TIMER_STATS ? fast_timers_added:
                   NUM_TIMER_STATS);
    used += sprintf(bigbuf + used, "Timers added: %i\n", fast_timers_added);
    for (i = 0; i < num_to_show && (used+100 < BIG_BUF_SIZE); i++)
    {
      t = &timer_added_log[(fast_timers_added - i - 1) % NUM_TIMER_STATS];
      used += sprintf(bigbuf + used, "%-14s s: %6lu.%06lu e: %6lu.%06lu "
                      "d: %6li us data: 0x%08lX"
                      "\n",
                      t->name,
                      (unsigned long)t->tv_set.tv_sec,
                      (unsigned long)t->tv_set.tv_usec,
                      (unsigned long)t->tv_expires.tv_sec,
                      (unsigned long)t->tv_expires.tv_usec,
                      t->delay_us,
                      t->data
                      );
    }
    used += sprintf(bigbuf + used, "\n");

    num_to_show = (fast_timers_expired < NUM_TIMER_STATS ? fast_timers_expired:
                   NUM_TIMER_STATS);
    used += sprintf(bigbuf + used, "Timers expired: %i\n", fast_timers_expired);
    for (i = 0; i < num_to_show && (used+100 < BIG_BUF_SIZE); i++)
    {
      t = &timer_expired_log[(fast_timers_expired - i - 1) % NUM_TIMER_STATS];
      used += sprintf(bigbuf + used, "%-14s s: %6lu.%06lu e: %6lu.%06lu "
                      "d: %6li us data: 0x%08lX"
                      "\n",
                      t->name,
                      (unsigned long)t->tv_set.tv_sec,
                      (unsigned long)t->tv_set.tv_usec,
                      (unsigned long)t->tv_expires.tv_sec,
                      (unsigned long)t->tv_expires.tv_usec,
                      t->delay_us,
                      t->data
                      );
    }
    used += sprintf(bigbuf + used, "\n");
#endif

    used += sprintf(bigbuf + used, "Active timers:\n");
    local_irq_save(flags);
    local_irq_save(flags);
    t = fast_timer_list;
    while (t != NULL && (used+100 < BIG_BUF_SIZE))
    {
      nextt = t->next;
      local_irq_restore(flags);
      used += sprintf(bigbuf + used, "%-14s s: %6lu.%06lu e: %6lu.%06lu "
                      "d: %6li us data: 0x%08lX"
/*                      " func: 0x%08lX" */
                      "\n",
                      t->name,
                      (unsigned long)t->tv_set.tv_sec,
                      (unsigned long)t->tv_set.tv_usec,
                      (unsigned long)t->tv_expires.tv_sec,
                      (unsigned long)t->tv_expires.tv_usec,
                      t->delay_us,
                      t->data
/*                      , t->function */
                      );
      local_irq_disable();
      if (t->next != nextt)
      {
        printk("timer removed!\n");
      }
      t = nextt;
    }
    local_irq_restore(flags);
  }

  if (used - offset < len)
  {
    len = used - offset;
  }

  memcpy(buf, bigbuf + offset, len);
  *start = buf;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
  *eof = 1;
#endif

  return len;
}
#endif /* PROC_FS */

#ifdef FAST_TIMER_TEST
static volatile unsigned long i = 0;
static volatile int num_test_timeout = 0;
static struct fast_timer tr[10];
static int exp_num[10];

static struct timeval tv_exp[100];

static void test_timeout(unsigned long data)
{
  do_gettimeofday_fast(&tv_exp[data]);
  exp_num[data] = num_test_timeout;

  num_test_timeout++;
}

static void test_timeout1(unsigned long data)
{
  do_gettimeofday_fast(&tv_exp[data]);
  exp_num[data] = num_test_timeout;
  if (data < 7)
  {
    start_one_shot_timer(&tr[i], test_timeout1, i, 1000, "timeout1");
    i++;
  }
  num_test_timeout++;
}

DP(
static char buf0[2000];
static char buf1[2000];
static char buf2[2000];
static char buf3[2000];
static char buf4[2000];
);

static char buf5[6000];
static int j_u[1000];

static void fast_timer_test(void)
{
  int prev_num;
  int j;

  struct timeval tv, tv0, tv1, tv2;

  printk("fast_timer_test() start\n");
  do_gettimeofday_fast(&tv);

  for (j = 0; j < 1000; j++)
  {
    j_u[j] = GET_JIFFIES_USEC();
  }
  for (j = 0; j < 100; j++)
  {
    do_gettimeofday_fast(&tv_exp[j]);
  }
  printk("fast_timer_test() %is %06i\n", tv.tv_sec, tv.tv_usec);

  for (j = 0; j < 1000; j++)
  {
    printk("%i %i %i %i %i\n",j_u[j], j_u[j+1], j_u[j+2], j_u[j+3], j_u[j+4]);
    j += 4;
  }
  for (j = 0; j < 100; j++)
  {
    printk("%i.%i %i.%i %i.%i %i.%i %i.%i\n",
           tv_exp[j].tv_sec,tv_exp[j].tv_usec,
           tv_exp[j+1].tv_sec,tv_exp[j+1].tv_usec,
           tv_exp[j+2].tv_sec,tv_exp[j+2].tv_usec,
           tv_exp[j+3].tv_sec,tv_exp[j+3].tv_usec,
           tv_exp[j+4].tv_sec,tv_exp[j+4].tv_usec);
    j += 4;
  }
  do_gettimeofday_fast(&tv0);
  start_one_shot_timer(&tr[i], test_timeout, i, 50000, "test0");
  DP(proc_fasttimer_read(buf0, NULL, 0, 0, 0));
  i++;
  start_one_shot_timer(&tr[i], test_timeout, i, 70000, "test1");
  DP(proc_fasttimer_read(buf1, NULL, 0, 0, 0));
  i++;
  start_one_shot_timer(&tr[i], test_timeout, i, 40000, "test2");
  DP(proc_fasttimer_read(buf2, NULL, 0, 0, 0));
  i++;
  start_one_shot_timer(&tr[i], test_timeout, i, 60000, "test3");
  DP(proc_fasttimer_read(buf3, NULL, 0, 0, 0));
  i++;
  start_one_shot_timer(&tr[i], test_timeout1, i, 55000, "test4xx");
  DP(proc_fasttimer_read(buf4, NULL, 0, 0, 0));
  i++;
  do_gettimeofday_fast(&tv1);

  proc_fasttimer_read(buf5, NULL, 0, 0, 0);

  prev_num = num_test_timeout;
  while (num_test_timeout < i)
  {
    if (num_test_timeout != prev_num)
    {
      prev_num = num_test_timeout;
    }
  }
  do_gettimeofday_fast(&tv2);
  printk("Timers started    %is %06i\n", tv0.tv_sec, tv0.tv_usec);
  printk("Timers started at %is %06i\n", tv1.tv_sec, tv1.tv_usec);
  printk("Timers done       %is %06i\n", tv2.tv_sec, tv2.tv_usec);
  DP(printk("buf0:\n");
     printk(buf0);
     printk("buf1:\n");
     printk(buf1);
     printk("buf2:\n");
     printk(buf2);
     printk("buf3:\n");
     printk(buf3);
     printk("buf4:\n");
     printk(buf4);
  );
  printk("buf5:\n");
  printk(buf5);

  printk("timers set:\n");
  for(j = 0; j<i; j++)
  {
    struct fast_timer *t = &tr[j];
    printk("%-10s set: %6is %06ius exp: %6is %06ius "
           "data: 0x%08X func: 0x%08X\n",
           t->name,
           t->tv_set.tv_sec,
           t->tv_set.tv_usec,
           t->tv_expires.tv_sec,
           t->tv_expires.tv_usec,
           t->data,
           t->function
           );

    printk("           del: %6ius     did exp: %6is %06ius as #%i error: %6li\n",
           t->delay_us,
           tv_exp[j].tv_sec,
           tv_exp[j].tv_usec,
           exp_num[j],
           (tv_exp[j].tv_sec - t->tv_expires.tv_sec)*1000000 + tv_exp[j].tv_usec - t->tv_expires.tv_usec);
  }
  proc_fasttimer_read(buf5, NULL, 0, 0, 0);
  printk("buf5 after all done:\n");
  printk(buf5);
  printk("fast_timer_test() done\n");
}
#endif


void fast_timer_init(void)
{
  /* For some reason, request_irq() hangs when called froom time_init() */
  if (!fast_timer_is_init)
  {
    printk("fast_timer_init()\n");

#ifdef CONFIG_PROC_FS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
   if ((fasttimer_proc_entry = create_proc_entry( "fasttimer", 0, 0 )))
     fasttimer_proc_entry->read_proc = proc_fasttimer_read;
#else
    proc_register_dynamic(&proc_root, &fasttimer_proc_entry);
#endif
#endif /* PROC_FS */
    if(request_irq(TIMER_INTR_VECT, timer_trig_interrupt, SA_INTERRUPT,
                   "fast timer int", NULL))
    {
      printk("err: timer1 irq\n");
    }
    fast_timer_is_init = 1;
#ifdef FAST_TIMER_TEST
    printk("do test\n");
    fast_timer_test();
#endif
  }
}
