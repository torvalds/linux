/*
 * i8253 PIT clocksource
 */
#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/timex.h>

#include <asm/i8253.h>

/*
 * Since the PIT overflows every tick, its not very useful
 * to just read by itself. So use jiffies to emulate a free
 * running counter:
 */
static cycle_t i8253_read(struct clocksource *cs)
{
	static int old_count;
	static u32 old_jifs;
	unsigned long flags;
	int count;
	u32 jifs;

	raw_spin_lock_irqsave(&i8253_lock, flags);
	/*
	 * Although our caller may have the read side of xtime_lock,
	 * this is now a seqlock, and we are cheating in this routine
	 * by having side effects on state that we cannot undo if
	 * there is a collision on the seqlock and our caller has to
	 * retry.  (Namely, old_jifs and old_count.)  So we must treat
	 * jiffies as volatile despite the lock.  We read jiffies
	 * before latching the timer count to guarantee that although
	 * the jiffies value might be older than the count (that is,
	 * the counter may underflow between the last point where
	 * jiffies was incremented and the point where we latch the
	 * count), it cannot be newer.
	 */
	jifs = jiffies;
	outb_pit(0x00, PIT_MODE);	/* latch the count ASAP */
	count = inb_pit(PIT_CH0);	/* read the latched count */
	count |= inb_pit(PIT_CH0) << 8;

	/* VIA686a test code... reset the latch if count > max + 1 */
	if (count > LATCH) {
		outb_pit(0x34, PIT_MODE);
		outb_pit(PIT_LATCH & 0xff, PIT_CH0);
		outb_pit(PIT_LATCH >> 8, PIT_CH0);
		count = PIT_LATCH - 1;
	}

	/*
	 * It's possible for count to appear to go the wrong way for a
	 * couple of reasons:
	 *
	 *  1. The timer counter underflows, but we haven't handled the
	 *     resulting interrupt and incremented jiffies yet.
	 *  2. Hardware problem with the timer, not giving us continuous time,
	 *     the counter does small "jumps" upwards on some Pentium systems,
	 *     (see c't 95/10 page 335 for Neptun bug.)
	 *
	 * Previous attempts to handle these cases intelligently were
	 * buggy, so we just do the simple thing now.
	 */
	if (count > old_count && jifs == old_jifs)
		count = old_count;

	old_count = count;
	old_jifs = jifs;

	raw_spin_unlock_irqrestore(&i8253_lock, flags);

	count = (PIT_LATCH - 1) - count;

	return (cycle_t)(jifs * PIT_LATCH) + count;
}

static struct clocksource i8253_cs = {
	.name		= "pit",
	.rating		= 110,
	.read		= i8253_read,
	.mask		= CLOCKSOURCE_MASK(32),
};

int __init clocksource_i8253_init(void)
{
	return clocksource_register_hz(&i8253_cs, PIT_TICK_RATE);
}
