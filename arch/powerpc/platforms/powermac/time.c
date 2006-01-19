/*
 * Support for periodic interrupts (100 per second) and for getting
 * the current time from the RTC on Power Macintoshes.
 *
 * We use the decrementer register for our periodic interrupts.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996 Paul Mackerras.
 * Copyright (C) 2003-2005 Benjamin Herrenschmidt.
 *
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/adb.h>
#include <linux/cuda.h>
#include <linux/pmu.h>
#include <linux/interrupt.h>
#include <linux/hardirq.h>
#include <linux/rtc.h>

#include <asm/sections.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/machdep.h>
#include <asm/time.h>
#include <asm/nvram.h>
#include <asm/smu.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

/* Apparently the RTC stores seconds since 1 Jan 1904 */
#define RTC_OFFSET	2082844800

/*
 * Calibrate the decrementer frequency with the VIA timer 1.
 */
#define VIA_TIMER_FREQ_6	4700000	/* time 1 frequency * 6 */

/* VIA registers */
#define RS		0x200		/* skip between registers */
#define T1CL		(4*RS)		/* Timer 1 ctr/latch (low 8 bits) */
#define T1CH		(5*RS)		/* Timer 1 counter (high 8 bits) */
#define T1LL		(6*RS)		/* Timer 1 latch (low 8 bits) */
#define T1LH		(7*RS)		/* Timer 1 latch (high 8 bits) */
#define ACR		(11*RS)		/* Auxiliary control register */
#define IFR		(13*RS)		/* Interrupt flag register */

/* Bits in ACR */
#define T1MODE		0xc0		/* Timer 1 mode */
#define T1MODE_CONT	0x40		/*  continuous interrupts */

/* Bits in IFR and IER */
#define T1_INT		0x40		/* Timer 1 interrupt */

long __init pmac_time_init(void)
{
	s32 delta = 0;
#ifdef CONFIG_NVRAM
	int dst;
	
	delta = ((s32)pmac_xpram_read(PMAC_XPRAM_MACHINE_LOC + 0x9)) << 16;
	delta |= ((s32)pmac_xpram_read(PMAC_XPRAM_MACHINE_LOC + 0xa)) << 8;
	delta |= pmac_xpram_read(PMAC_XPRAM_MACHINE_LOC + 0xb);
	if (delta & 0x00800000UL)
		delta |= 0xFF000000UL;
	dst = ((pmac_xpram_read(PMAC_XPRAM_MACHINE_LOC + 0x8) & 0x80) != 0);
	printk("GMT Delta read from XPRAM: %d minutes, DST: %s\n", delta/60,
		dst ? "on" : "off");
#endif
	return delta;
}

static void to_rtc_time(unsigned long now, struct rtc_time *tm)
{
	to_tm(now, tm);
	tm->tm_year -= 1900;
	tm->tm_mon -= 1;
}

static unsigned long from_rtc_time(struct rtc_time *tm)
{
	return mktime(tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
		      tm->tm_hour, tm->tm_min, tm->tm_sec);
}

#ifdef CONFIG_ADB_CUDA
static unsigned long cuda_get_time(void)
{
	struct adb_request req;
	unsigned int now;

	if (cuda_request(&req, NULL, 2, CUDA_PACKET, CUDA_GET_TIME) < 0)
		return 0;
	while (!req.complete)
		cuda_poll();
	if (req.reply_len != 7)
		printk(KERN_ERR "cuda_get_time: got %d byte reply\n",
		       req.reply_len);
	now = (req.reply[3] << 24) + (req.reply[4] << 16)
		+ (req.reply[5] << 8) + req.reply[6];
	return ((unsigned long)now) - RTC_OFFSET;
}

#define cuda_get_rtc_time(tm)	to_rtc_time(cuda_get_time(), (tm))

static int cuda_set_rtc_time(struct rtc_time *tm)
{
	unsigned int nowtime;
	struct adb_request req;

	nowtime = from_rtc_time(tm) + RTC_OFFSET;
	if (cuda_request(&req, NULL, 6, CUDA_PACKET, CUDA_SET_TIME,
			 nowtime >> 24, nowtime >> 16, nowtime >> 8,
			 nowtime) < 0)
		return -ENXIO;
	while (!req.complete)
		cuda_poll();
	if ((req.reply_len != 3) && (req.reply_len != 7))
		printk(KERN_ERR "cuda_set_rtc_time: got %d byte reply\n",
		       req.reply_len);
	return 0;
}

#else
#define cuda_get_time()		0
#define cuda_get_rtc_time(tm)
#define cuda_set_rtc_time(tm)	0
#endif

#ifdef CONFIG_ADB_PMU
static unsigned long pmu_get_time(void)
{
	struct adb_request req;
	unsigned int now;

	if (pmu_request(&req, NULL, 1, PMU_READ_RTC) < 0)
		return 0;
	pmu_wait_complete(&req);
	if (req.reply_len != 4)
		printk(KERN_ERR "pmu_get_time: got %d byte reply from PMU\n",
		       req.reply_len);
	now = (req.reply[0] << 24) + (req.reply[1] << 16)
		+ (req.reply[2] << 8) + req.reply[3];
	return ((unsigned long)now) - RTC_OFFSET;
}

#define pmu_get_rtc_time(tm)	to_rtc_time(pmu_get_time(), (tm))

static int pmu_set_rtc_time(struct rtc_time *tm)
{
	unsigned int nowtime;
	struct adb_request req;

	nowtime = from_rtc_time(tm) + RTC_OFFSET;
	if (pmu_request(&req, NULL, 5, PMU_SET_RTC, nowtime >> 24,
			nowtime >> 16, nowtime >> 8, nowtime) < 0)
		return -ENXIO;
	pmu_wait_complete(&req);
	if (req.reply_len != 0)
		printk(KERN_ERR "pmu_set_rtc_time: %d byte reply from PMU\n",
		       req.reply_len);
	return 0;
}

#else
#define pmu_get_time()		0
#define pmu_get_rtc_time(tm)
#define pmu_set_rtc_time(tm)	0
#endif

#ifdef CONFIG_PMAC_SMU
static unsigned long smu_get_time(void)
{
	struct rtc_time tm;

	if (smu_get_rtc_time(&tm, 1))
		return 0;
	return from_rtc_time(&tm);
}

#else
#define smu_get_time()			0
#define smu_get_rtc_time(tm, spin)
#define smu_set_rtc_time(tm, spin)	0
#endif

/* Can't be __init, it's called when suspending and resuming */
unsigned long pmac_get_boot_time(void)
{
	/* Get the time from the RTC, used only at boot time */
	switch (sys_ctrler) {
	case SYS_CTRLER_CUDA:
		return cuda_get_time();
	case SYS_CTRLER_PMU:
		return pmu_get_time();
	case SYS_CTRLER_SMU:
		return smu_get_time();
	default:
		return 0;
	}
}

void pmac_get_rtc_time(struct rtc_time *tm)
{
	/* Get the time from the RTC, used only at boot time */
	switch (sys_ctrler) {
	case SYS_CTRLER_CUDA:
		cuda_get_rtc_time(tm);
		break;
	case SYS_CTRLER_PMU:
		pmu_get_rtc_time(tm);
		break;
	case SYS_CTRLER_SMU:
		smu_get_rtc_time(tm, 1);
		break;
	default:
		;
	}
}

int pmac_set_rtc_time(struct rtc_time *tm)
{
	switch (sys_ctrler) {
	case SYS_CTRLER_CUDA:
		return cuda_set_rtc_time(tm);
	case SYS_CTRLER_PMU:
		return pmu_set_rtc_time(tm);
	case SYS_CTRLER_SMU:
		return smu_set_rtc_time(tm, 1);
	default:
		return -ENODEV;
	}
}

#ifdef CONFIG_PPC32
/*
 * Calibrate the decrementer register using VIA timer 1.
 * This is used both on powermacs and CHRP machines.
 */
int __init via_calibrate_decr(void)
{
	struct device_node *vias;
	volatile unsigned char __iomem *via;
	int count = VIA_TIMER_FREQ_6 / 100;
	unsigned int dstart, dend;
	struct resource rsrc;

	vias = of_find_node_by_name(NULL, "via-cuda");
	if (vias == 0)
		vias = of_find_node_by_name(NULL, "via-pmu");
	if (vias == 0)
		vias = of_find_node_by_name(NULL, "via");
	if (vias == 0 || of_address_to_resource(vias, 0, &rsrc))
		return 0;
	via = ioremap(rsrc.start, rsrc.end - rsrc.start + 1);
	if (via == NULL) {
		printk(KERN_ERR "Failed to map VIA for timer calibration !\n");
		return 0;
	}

	/* set timer 1 for continuous interrupts */
	out_8(&via[ACR], (via[ACR] & ~T1MODE) | T1MODE_CONT);
	/* set the counter to a small value */
	out_8(&via[T1CH], 2);
	/* set the latch to `count' */
	out_8(&via[T1LL], count);
	out_8(&via[T1LH], count >> 8);
	/* wait until it hits 0 */
	while ((in_8(&via[IFR]) & T1_INT) == 0)
		;
	dstart = get_dec();
	/* clear the interrupt & wait until it hits 0 again */
	in_8(&via[T1CL]);
	while ((in_8(&via[IFR]) & T1_INT) == 0)
		;
	dend = get_dec();

	ppc_tb_freq = (dstart - dend) * 100 / 6;

	iounmap(via);
	
	return 1;
}
#endif

#ifdef CONFIG_PM
/*
 * Reset the time after a sleep.
 */
static int
time_sleep_notify(struct pmu_sleep_notifier *self, int when)
{
	static unsigned long time_diff;
	unsigned long flags;
	unsigned long seq;
	struct timespec tv;

	switch (when) {
	case PBOOK_SLEEP_NOW:
		do {
			seq = read_seqbegin_irqsave(&xtime_lock, flags);
			time_diff = xtime.tv_sec - pmac_get_boot_time();
		} while (read_seqretry_irqrestore(&xtime_lock, seq, flags));
		break;
	case PBOOK_WAKE:
		tv.tv_sec = pmac_get_boot_time() + time_diff;
		tv.tv_nsec = 0;
		do_settimeofday(&tv);
		break;
	}
	return PBOOK_SLEEP_OK;
}

static struct pmu_sleep_notifier time_sleep_notifier = {
	time_sleep_notify, SLEEP_LEVEL_MISC,
};
#endif /* CONFIG_PM */

/*
 * Query the OF and get the decr frequency.
 */
void __init pmac_calibrate_decr(void)
{
#ifdef CONFIG_PM
	/* XXX why here? */
	pmu_register_sleep_notifier(&time_sleep_notifier);
#endif /* CONFIG_PM */

	generic_calibrate_decr();

#ifdef CONFIG_PPC32
	/* We assume MacRISC2 machines have correct device-tree
	 * calibration. That's better since the VIA itself seems
	 * to be slightly off. --BenH
	 */
	if (!machine_is_compatible("MacRISC2") &&
	    !machine_is_compatible("MacRISC3") &&
	    !machine_is_compatible("MacRISC4"))
		if (via_calibrate_decr())
			return;

	/* Special case: QuickSilver G4s seem to have a badly calibrated
	 * timebase-frequency in OF, VIA is much better on these. We should
	 * probably implement calibration based on the KL timer on these
	 * machines anyway... -BenH
	 */
	if (machine_is_compatible("PowerMac3,5"))
		if (via_calibrate_decr())
			return;
#endif
}
