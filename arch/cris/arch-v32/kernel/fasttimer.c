/*
 * linux/arch/cris/kernel/fasttimer.c
 *
 * Fast timers for ETRAX FS
 *
 * Copyright (C) 2000-2006 Axis Communications AB, Lund, Sweden
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

#include <hwregs/reg_map.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/timer_defs.h>
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
/* #define FAST_TIMER_TEST */

#define FAST_TIMER_SANITY_CHECKS

#ifdef FAST_TIMER_SANITY_CHECKS
static int sanity_failed;
#endif

#define D1(x)
#define D2(x)
#define DP(x)

static unsigned int fast_timer_running;
static unsigned int fast_timers_added;
static unsigned int fast_timers_started;
static unsigned int fast_timers_expired;
static unsigned int fast_timers_deleted;
static unsigned int fast_timer_is_init;
static unsigned int fast_timer_ints;

struct fast_timer *fast_timer_list = NULL;

#ifdef DEBUG_LOG_INCLUDED
#define DEBUG_LOG_MAX 128
static const char * debug_log_string[DEBUG_LOG_MAX];
static unsigned long debug_log_value[DEBUG_LOG_MAX];
static unsigned int debug_log_cnt;
static unsigned int debug_log_cnt_wrapped;

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

struct work_struct fast_work;

static void
timer_trig_handler(struct work_struct *work);



/* Not true gettimeofday, only checks the jiffies (uptime) + useconds */
inline void do_gettimeofday_fast(struct fasttime_t *tv)
{
	tv->tv_jiff = jiffies;
	tv->tv_usec = GET_JIFFIES_USEC();
}

inline int fasttime_cmp(struct fasttime_t *t0, struct fasttime_t *t1)
{
	/* Compare jiffies. Takes care of wrapping */
	if (time_before(t0->tv_jiff, t1->tv_jiff))
		return -1;
	else if (time_after(t0->tv_jiff, t1->tv_jiff))
		return 1;

	/* Compare us */
	if (t0->tv_usec < t1->tv_usec)
		return -1;
	else if (t0->tv_usec > t1->tv_usec)
		return 1;
	return 0;
}

/* Called with ints off */
inline void start_timer_trig(unsigned long delay_us)
{
  reg_timer_rw_ack_intr ack_intr = { 0 };
  reg_timer_rw_intr_mask intr_mask;
  reg_timer_rw_trig trig;
  reg_timer_rw_trig_cfg trig_cfg = { 0 };
	reg_timer_r_time r_time0;
	reg_timer_r_time r_time1;
	unsigned char trig_wrap;
	unsigned char time_wrap;

	r_time0 = REG_RD(timer, regi_timer0, r_time);

  D1(printk("start_timer_trig : %d us freq: %i div: %i\n",
            delay_us, freq_index, div));
  /* Clear trig irq */
	intr_mask = REG_RD(timer, regi_timer0, rw_intr_mask);
  intr_mask.trig = 0;
	REG_WR(timer, regi_timer0, rw_intr_mask, intr_mask);

	/* Set timer values and check if trigger wraps. */
	/* r_time is 100MHz (10 ns resolution) */
	trig_wrap = (trig = r_time0 + delay_us*(1000/10)) < r_time0;

  timer_div_settings[fast_timers_started % NUM_TIMER_STATS] = trig;
  timer_delay_settings[fast_timers_started % NUM_TIMER_STATS] = delay_us;

  /* Ack interrupt */
  ack_intr.trig = 1;
	REG_WR(timer, regi_timer0, rw_ack_intr, ack_intr);

  /* Start timer */
	REG_WR(timer, regi_timer0, rw_trig, trig);
  trig_cfg.tmr = regk_timer_time;
	REG_WR(timer, regi_timer0, rw_trig_cfg, trig_cfg);

  /* Check if we have already passed the trig time */
	r_time1 = REG_RD(timer, regi_timer0, r_time);
	time_wrap = r_time1 < r_time0;

	if ((trig_wrap && !time_wrap) || (r_time1 < trig)) {
    /* No, Enable trig irq */
		intr_mask = REG_RD(timer, regi_timer0, rw_intr_mask);
    intr_mask.trig = 1;
		REG_WR(timer, regi_timer0, rw_intr_mask, intr_mask);
    fast_timers_started++;
    fast_timer_running = 1;
	} else {
    /* We have passed the time, disable trig point, ack intr */
    trig_cfg.tmr = regk_timer_off;
		REG_WR(timer, regi_timer0, rw_trig_cfg, trig_cfg);
		REG_WR(timer, regi_timer0, rw_ack_intr, ack_intr);
		/* call the int routine */
		INIT_WORK(&fast_work, timer_trig_handler);
		schedule_work(&fast_work);
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

#ifdef FAST_TIMER_SANITY_CHECKS
	/* Check so this is not in the list already... */
	while (tmp != NULL) {
		if (tmp == t) {
			printk(KERN_DEBUG
				"timer name: %s data: 0x%08lX already "
				"in list!\n", name, data);
			sanity_failed++;
			goto done;
		} else
			tmp = tmp->next;
	}
	tmp = fast_timer_list;
#endif

  t->delay_us = delay_us;
  t->function = function;
  t->data = data;
  t->name = name;

  t->tv_expires.tv_usec = t->tv_set.tv_usec + delay_us % 1000000;
	t->tv_expires.tv_jiff = t->tv_set.tv_jiff + delay_us / 1000000 / HZ;
	if (t->tv_expires.tv_usec > 1000000) {
    t->tv_expires.tv_usec -= 1000000;
		t->tv_expires.tv_jiff += HZ;
  }
#ifdef FAST_TIMER_LOG
  timer_added_log[fast_timers_added % NUM_TIMER_STATS] = *t;
#endif
  fast_timers_added++;

  /* Check if this should timeout before anything else */
  if (tmp == NULL || fasttime_cmp(&t->tv_expires, &tmp->tv_expires) < 0) {
    /* Put first in list and modify the timer value */
    t->prev = NULL;
    t->next = fast_timer_list;
    if (fast_timer_list)
      fast_timer_list->prev = t;
    fast_timer_list = t;
#ifdef FAST_TIMER_LOG
    timer_started_log[fast_timers_started % NUM_TIMER_STATS] = *t;
#endif
    start_timer_trig(delay_us);
  } else {
    /* Put in correct place in list */
    while (tmp->next &&
        fasttime_cmp(&t->tv_expires, &tmp->next->tv_expires) > 0)
      tmp = tmp->next;
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

done:
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
timer_trig_interrupt(int irq, void *dev_id)
{
  reg_timer_r_masked_intr masked_intr;
  /* Check if the timer interrupt is for us (a trig int) */
	masked_intr = REG_RD(timer, regi_timer0, r_masked_intr);
  if (!masked_intr.trig)
    return IRQ_NONE;
	timer_trig_handler(NULL);
  return IRQ_HANDLED;
}

static void timer_trig_handler(struct work_struct *work)
{
  reg_timer_rw_ack_intr ack_intr = { 0 };
  reg_timer_rw_intr_mask intr_mask;
  reg_timer_rw_trig_cfg trig_cfg = { 0 };
  struct fast_timer *t;
  unsigned long flags;

	/* We keep interrupts disabled not only when we modify the
	 * fast timer list, but any time we hold a reference to a
	 * timer in the list, since del_fast_timer may be called
	 * from (another) interrupt context.  Thus, the only time
	 * when interrupts are enabled is when calling the timer
	 * callback function.
	 */
  local_irq_save(flags);

  /* Clear timer trig interrupt */
	intr_mask = REG_RD(timer, regi_timer0, rw_intr_mask);
  intr_mask.trig = 0;
  REG_WR(timer, regi_timer0, rw_intr_mask, intr_mask);

  /* First stop timer, then ack interrupt */
  /* Stop timer */
  trig_cfg.tmr = regk_timer_off;
	REG_WR(timer, regi_timer0, rw_trig_cfg, trig_cfg);

  /* Ack interrupt */
  ack_intr.trig = 1;
	REG_WR(timer, regi_timer0, rw_ack_intr, ack_intr);

  fast_timer_running = 0;
  fast_timer_ints++;

	fast_timer_function_type *f;
	unsigned long d;

  t = fast_timer_list;
	while (t) {
		struct fasttime_t tv;

    /* Has it really expired? */
    do_gettimeofday_fast(&tv);
		D1(printk(KERN_DEBUG
			"t: %is %06ius\n", tv.tv_jiff, tv.tv_usec));

		if (fasttime_cmp(&t->tv_expires, &tv) <= 0) {
      /* Yes it has expired */
#ifdef FAST_TIMER_LOG
      timer_expired_log[fast_timers_expired % NUM_TIMER_STATS] = *t;
#endif
      fast_timers_expired++;

      /* Remove this timer before call, since it may reuse the timer */
      if (t->prev)
        t->prev->next = t->next;
      else
        fast_timer_list = t->next;
      if (t->next)
        t->next->prev = t->prev;
      t->prev = NULL;
      t->next = NULL;

			/* Save function callback data before enabling
			 * interrupts, since the timer may be removed and we
			 * don't know how it was allocated (e.g. ->function
			 * and ->data may become overwritten after deletion
			 * if the timer was stack-allocated).
			 */
			f = t->function;
			d = t->data;

			if (f != NULL) {
				/* Run the callback function with interrupts
				 * enabled. */
				local_irq_restore(flags);
				f(d);
				local_irq_save(flags);
			} else
        DEBUG_LOG("!trimertrig %i function==NULL!\n", fast_timer_ints);
		} else {
      /* Timer is to early, let's set it again using the normal routines */
      D1(printk(".\n"));
    }

		t = fast_timer_list;
		if (t != NULL) {
      /* Start next timer.. */
			long us = 0;
			struct fasttime_t tv;

      do_gettimeofday_fast(&tv);

			/* time_after_eq takes care of wrapping */
			if (time_after_eq(t->tv_expires.tv_jiff, tv.tv_jiff))
				us = ((t->tv_expires.tv_jiff - tv.tv_jiff) *
					1000000 / HZ + t->tv_expires.tv_usec -
					tv.tv_usec);

			if (us > 0) {
				if (!fast_timer_running) {
#ifdef FAST_TIMER_LOG
          timer_started_log[fast_timers_started % NUM_TIMER_STATS] = *t;
#endif
          start_timer_trig(us);
        }
        break;
			} else {
        /* Timer already expired, let's handle it better late than never.
         * The normal loop handles it
         */
        D1(printk("e! %d\n", us));
      }
    }
  }

	local_irq_restore(flags);

	if (!t)
    D1(printk("ttrig stop!\n"));
}

static void wake_up_func(unsigned long data)
{
  wait_queue_head_t  *sleep_wait_p = (wait_queue_head_t*)data;
  wake_up(sleep_wait_p);
}


/* Useful API */

void schedule_usleep(unsigned long us)
{
  struct fast_timer t;
  wait_queue_head_t sleep_wait;
  init_waitqueue_head(&sleep_wait);

  D1(printk("schedule_usleep(%d)\n", us));
  start_one_shot_timer(&t, wake_up_func, (unsigned long)&sleep_wait, us,
                       "usleep");
	/* Uninterruptible sleep on the fast timer. (The condition is
	 * somewhat redundant since the timer is what wakes us up.) */
	wait_event(sleep_wait, !fast_timer_pending(&t));

  D1(printk("done schedule_usleep(%d)\n", us));
}

#ifdef CONFIG_PROC_FS
static int proc_fasttimer_read(char *buf, char **start, off_t offset, int len
                       ,int *eof, void *data_unused);
static struct proc_dir_entry *fasttimer_proc_entry;
#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_PROC_FS

/* This value is very much based on testing */
#define BIG_BUF_SIZE (500 + NUM_TIMER_STATS * 300)

static int proc_fasttimer_read(char *buf, char **start, off_t offset, int len
                       ,int *eof, void *data_unused)
{
  unsigned long flags;
  int i = 0;
  int num_to_show;
	struct fasttime_t tv;
  struct fast_timer *t, *nextt;
  static char *bigbuf = NULL;
  static unsigned long used;

	if (!bigbuf) {
		bigbuf = vmalloc(BIG_BUF_SIZE);
		if (!bigbuf) {
			used = 0;
			if (buf)
				buf[0] = '\0';
			return 0;
		}
	}

	if (!offset || !used) {
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
			(unsigned long)tv.tv_jiff,
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
        i = debug_log_cnt;

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
				(unsigned long)t->tv_set.tv_jiff,
                      (unsigned long)t->tv_set.tv_usec,
				(unsigned long)t->tv_expires.tv_jiff,
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
				(unsigned long)t->tv_set.tv_jiff,
                      (unsigned long)t->tv_set.tv_usec,
				(unsigned long)t->tv_expires.tv_jiff,
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
				(unsigned long)t->tv_set.tv_jiff,
                      (unsigned long)t->tv_set.tv_usec,
				(unsigned long)t->tv_expires.tv_jiff,
                      (unsigned long)t->tv_expires.tv_usec,
                      t->delay_us,
                      t->data
                      );
    }
    used += sprintf(bigbuf + used, "\n");
#endif

    used += sprintf(bigbuf + used, "Active timers:\n");
    local_irq_save(flags);
    t = fast_timer_list;
    while (t != NULL && (used+100 < BIG_BUF_SIZE))
    {
      nextt = t->next;
      local_irq_restore(flags);
      used += sprintf(bigbuf + used, "%-14s s: %6lu.%06lu e: %6lu.%06lu "
			"d: %6li us data: 0x%08lX"
/*			" func: 0x%08lX" */
			"\n",
			t->name,
			(unsigned long)t->tv_set.tv_jiff,
			(unsigned long)t->tv_set.tv_usec,
			(unsigned long)t->tv_expires.tv_jiff,
			(unsigned long)t->tv_expires.tv_usec,
                      t->delay_us,
                      t->data
/*                      , t->function */
                      );
			local_irq_save(flags);
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
  *eof = 1;

  return len;
}
#endif /* PROC_FS */

#ifdef FAST_TIMER_TEST
static volatile unsigned long i = 0;
static volatile int num_test_timeout = 0;
static struct fast_timer tr[10];
static int exp_num[10];

static struct fasttime_t tv_exp[100];

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

	struct fasttime_t tv, tv0, tv1, tv2;

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
  printk(KERN_DEBUG "fast_timer_test() %is %06i\n", tv.tv_jiff, tv.tv_usec);

  for (j = 0; j < 1000; j++)
  {
    printk(KERN_DEBUG "%i %i %i %i %i\n",
      j_u[j], j_u[j+1], j_u[j+2], j_u[j+3], j_u[j+4]);
    j += 4;
  }
  for (j = 0; j < 100; j++)
  {
    printk(KERN_DEBUG "%i.%i %i.%i %i.%i %i.%i %i.%i\n",
			tv_exp[j].tv_jiff, tv_exp[j].tv_usec,
			tv_exp[j+1].tv_jiff, tv_exp[j+1].tv_usec,
			tv_exp[j+2].tv_jiff, tv_exp[j+2].tv_usec,
			tv_exp[j+3].tv_jiff, tv_exp[j+3].tv_usec,
			tv_exp[j+4].tv_jiff, tv_exp[j+4].tv_usec);
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
      prev_num = num_test_timeout;
  }
  do_gettimeofday_fast(&tv2);
	printk(KERN_INFO "Timers started    %is %06i\n",
		tv0.tv_jiff, tv0.tv_usec);
	printk(KERN_INFO "Timers started at %is %06i\n",
		tv1.tv_jiff, tv1.tv_usec);
	printk(KERN_INFO "Timers done       %is %06i\n",
		tv2.tv_jiff, tv2.tv_usec);
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
			t->tv_set.tv_jiff,
           t->tv_set.tv_usec,
			t->tv_expires.tv_jiff,
           t->tv_expires.tv_usec,
           t->data,
           t->function
           );

    printk("           del: %6ius     did exp: %6is %06ius as #%i error: %6li\n",
           t->delay_us,
			tv_exp[j].tv_jiff,
           tv_exp[j].tv_usec,
           exp_num[j],
			(tv_exp[j].tv_jiff - t->tv_expires.tv_jiff) *
				1000000 + tv_exp[j].tv_usec -
				t->tv_expires.tv_usec);
  }
  proc_fasttimer_read(buf5, NULL, 0, 0, 0);
  printk("buf5 after all done:\n");
  printk(buf5);
  printk("fast_timer_test() done\n");
}
#endif


int fast_timer_init(void)
{
  /* For some reason, request_irq() hangs when called froom time_init() */
  if (!fast_timer_is_init)
  {
    printk("fast_timer_init()\n");

#ifdef CONFIG_PROC_FS
    fasttimer_proc_entry = create_proc_entry("fasttimer", 0, 0);
    if (fasttimer_proc_entry)
      fasttimer_proc_entry->read_proc = proc_fasttimer_read;
#endif /* PROC_FS */
		if (request_irq(TIMER0_INTR_VECT, timer_trig_interrupt,
				IRQF_SHARED | IRQF_DISABLED,
				"fast timer int", &fast_timer_list))
			printk(KERN_ERR "err: fasttimer irq\n");
    fast_timer_is_init = 1;
#ifdef FAST_TIMER_TEST
    printk("do test\n");
    fast_timer_test();
#endif
  }
	return 0;
}
__initcall(fast_timer_init);
