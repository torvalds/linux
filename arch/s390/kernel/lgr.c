/*
 * Linux Guest Relocation (LGR) detection
 *
 * Copyright IBM Corp. 2012
 * Author(s): Michael Holzheu <holzheu@linux.vnet.ibm.com>
 */

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <asm/facility.h>
#include <asm/sysinfo.h>
#include <asm/ebcdic.h>
#include <asm/debug.h>
#include <asm/ipl.h>

#define LGR_TIMER_INTERVAL_SECS (30 * 60)
#define VM_LEVEL_MAX 2 /* Maximum is 8, but we only record two levels */

/*
 * LGR info: Contains stfle and stsi data
 */
struct lgr_info {
	/* Bit field with facility information: 4 DWORDs are stored */
	u64 stfle_fac_list[4];
	/* Level of system (1 = CEC, 2 = LPAR, 3 = z/VM */
	u32 level;
	/* Level 1: CEC info (stsi 1.1.1) */
	char manufacturer[16];
	char type[4];
	char sequence[16];
	char plant[4];
	char model[16];
	/* Level 2: LPAR info (stsi 2.2.2) */
	u16 lpar_number;
	char name[8];
	/* Level 3: VM info (stsi 3.2.2) */
	u8 vm_count;
	struct {
		char name[8];
		char cpi[16];
	} vm[VM_LEVEL_MAX];
} __packed __aligned(8);

/*
 * LGR globals
 */
static void *lgr_page;
static struct lgr_info lgr_info_last;
static struct lgr_info lgr_info_cur;
static struct debug_info *lgr_dbf;

/*
 * Return number of valid stsi levels
 */
static inline int stsi_0(void)
{
	int rc = stsi(NULL, 0, 0, 0);

	return rc == -ENOSYS ? rc : (((unsigned int) rc) >> 28);
}

/*
 * Copy buffer and then convert it to ASCII
 */
static void cpascii(char *dst, char *src, int size)
{
	memcpy(dst, src, size);
	EBCASC(dst, size);
}

/*
 * Fill LGR info with 1.1.1 stsi data
 */
static void lgr_stsi_1_1_1(struct lgr_info *lgr_info)
{
	struct sysinfo_1_1_1 *si = lgr_page;

	if (stsi(si, 1, 1, 1) == -ENOSYS)
		return;
	cpascii(lgr_info->manufacturer, si->manufacturer,
		sizeof(si->manufacturer));
	cpascii(lgr_info->type, si->type, sizeof(si->type));
	cpascii(lgr_info->model, si->model, sizeof(si->model));
	cpascii(lgr_info->sequence, si->sequence, sizeof(si->sequence));
	cpascii(lgr_info->plant, si->plant, sizeof(si->plant));
}

/*
 * Fill LGR info with 2.2.2 stsi data
 */
static void lgr_stsi_2_2_2(struct lgr_info *lgr_info)
{
	struct sysinfo_2_2_2 *si = lgr_page;

	if (stsi(si, 2, 2, 2) == -ENOSYS)
		return;
	cpascii(lgr_info->name, si->name, sizeof(si->name));
	memcpy(&lgr_info->lpar_number, &si->lpar_number,
	       sizeof(lgr_info->lpar_number));
}

/*
 * Fill LGR info with 3.2.2 stsi data
 */
static void lgr_stsi_3_2_2(struct lgr_info *lgr_info)
{
	struct sysinfo_3_2_2 *si = lgr_page;
	int i;

	if (stsi(si, 3, 2, 2) == -ENOSYS)
		return;
	for (i = 0; i < min_t(u8, si->count, VM_LEVEL_MAX); i++) {
		cpascii(lgr_info->vm[i].name, si->vm[i].name,
			sizeof(si->vm[i].name));
		cpascii(lgr_info->vm[i].cpi, si->vm[i].cpi,
			sizeof(si->vm[i].cpi));
	}
	lgr_info->vm_count = si->count;
}

/*
 * Fill LGR info with current data
 */
static void lgr_info_get(struct lgr_info *lgr_info)
{
	memset(lgr_info, 0, sizeof(*lgr_info));
	stfle(lgr_info->stfle_fac_list, ARRAY_SIZE(lgr_info->stfle_fac_list));
	lgr_info->level = stsi_0();
	if (lgr_info->level == -ENOSYS)
		return;
	if (lgr_info->level >= 1)
		lgr_stsi_1_1_1(lgr_info);
	if (lgr_info->level >= 2)
		lgr_stsi_2_2_2(lgr_info);
	if (lgr_info->level >= 3)
		lgr_stsi_3_2_2(lgr_info);
}

/*
 * Check if LGR info has changed and if yes log new LGR info to s390dbf
 */
void lgr_info_log(void)
{
	static DEFINE_SPINLOCK(lgr_info_lock);
	unsigned long flags;

	if (!spin_trylock_irqsave(&lgr_info_lock, flags))
		return;
	lgr_info_get(&lgr_info_cur);
	if (memcmp(&lgr_info_last, &lgr_info_cur, sizeof(lgr_info_cur)) != 0) {
		debug_event(lgr_dbf, 1, &lgr_info_cur, sizeof(lgr_info_cur));
		lgr_info_last = lgr_info_cur;
	}
	spin_unlock_irqrestore(&lgr_info_lock, flags);
}
EXPORT_SYMBOL_GPL(lgr_info_log);

static void lgr_timer_set(void);

/*
 * LGR timer callback
 */
static void lgr_timer_fn(unsigned long ignored)
{
	lgr_info_log();
	lgr_timer_set();
}

static struct timer_list lgr_timer =
	TIMER_DEFERRED_INITIALIZER(lgr_timer_fn, 0, 0);

/*
 * Setup next LGR timer
 */
static void lgr_timer_set(void)
{
	mod_timer(&lgr_timer, jiffies + LGR_TIMER_INTERVAL_SECS * HZ);
}

/*
 * Initialize LGR: Add s390dbf, write initial lgr_info and setup timer
 */
static int __init lgr_init(void)
{
	lgr_page = (void *) __get_free_pages(GFP_KERNEL, 0);
	if (!lgr_page)
		return -ENOMEM;
	lgr_dbf = debug_register("lgr", 1, 1, sizeof(struct lgr_info));
	if (!lgr_dbf) {
		free_page((unsigned long) lgr_page);
		return -ENOMEM;
	}
	debug_register_view(lgr_dbf, &debug_hex_ascii_view);
	lgr_info_get(&lgr_info_last);
	debug_event(lgr_dbf, 1, &lgr_info_last, sizeof(lgr_info_last));
	lgr_timer_set();
	return 0;
}
module_init(lgr_init);
