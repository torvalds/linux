#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/workqueue.h>
#include <linux/memblock.h>

#include <asm/proto.h>

/*
 * Some BIOSes seem to corrupt the low 64k of memory during events
 * like suspend/resume and unplugging an HDMI cable.  Reserve all
 * remaining free memory in that area and fill it with a distinct
 * pattern.
 */
#define MAX_SCAN_AREAS	8

static int __read_mostly memory_corruption_check = -1;

static unsigned __read_mostly corruption_check_size = 64*1024;
static unsigned __read_mostly corruption_check_period = 60; /* seconds */

static struct scan_area {
	u64 addr;
	u64 size;
} scan_areas[MAX_SCAN_AREAS];
static int num_scan_areas;

static __init int set_corruption_check(char *arg)
{
	ssize_t ret;
	unsigned long val;

	ret = kstrtoul(arg, 10, &val);
	if (ret)
		return ret;

	memory_corruption_check = val;
	return 0;
}
early_param("memory_corruption_check", set_corruption_check);

static __init int set_corruption_check_period(char *arg)
{
	ssize_t ret;
	unsigned long val;

	ret = kstrtoul(arg, 10, &val);
	if (ret)
		return ret;

	corruption_check_period = val;
	return 0;
}
early_param("memory_corruption_check_period", set_corruption_check_period);

static __init int set_corruption_check_size(char *arg)
{
	char *end;
	unsigned size;

	size = memparse(arg, &end);

	if (*end == '\0')
		corruption_check_size = size;

	return (size == corruption_check_size) ? 0 : -EINVAL;
}
early_param("memory_corruption_check_size", set_corruption_check_size);


void __init setup_bios_corruption_check(void)
{
	phys_addr_t start, end;
	u64 i;

	if (memory_corruption_check == -1) {
		memory_corruption_check =
#ifdef CONFIG_X86_BOOTPARAM_MEMORY_CORRUPTION_CHECK
			1
#else
			0
#endif
			;
	}

	if (corruption_check_size == 0)
		memory_corruption_check = 0;

	if (!memory_corruption_check)
		return;

	corruption_check_size = round_up(corruption_check_size, PAGE_SIZE);

	for_each_free_mem_range(i, NUMA_NO_NODE, &start, &end, NULL) {
		start = clamp_t(phys_addr_t, round_up(start, PAGE_SIZE),
				PAGE_SIZE, corruption_check_size);
		end = clamp_t(phys_addr_t, round_down(end, PAGE_SIZE),
			      PAGE_SIZE, corruption_check_size);
		if (start >= end)
			continue;

		memblock_reserve(start, end - start);
		scan_areas[num_scan_areas].addr = start;
		scan_areas[num_scan_areas].size = end - start;

		/* Assume we've already mapped this early memory */
		memset(__va(start), 0, end - start);

		if (++num_scan_areas >= MAX_SCAN_AREAS)
			break;
	}

	if (num_scan_areas)
		printk(KERN_INFO "Scanning %d areas for low memory corruption\n", num_scan_areas);
}


void check_for_bios_corruption(void)
{
	int i;
	int corruption = 0;

	if (!memory_corruption_check)
		return;

	for (i = 0; i < num_scan_areas; i++) {
		unsigned long *addr = __va(scan_areas[i].addr);
		unsigned long size = scan_areas[i].size;

		for (; size; addr++, size -= sizeof(unsigned long)) {
			if (!*addr)
				continue;
			printk(KERN_ERR "Corrupted low memory at %p (%lx phys) = %08lx\n",
			       addr, __pa(addr), *addr);
			corruption = 1;
			*addr = 0;
		}
	}

	WARN_ONCE(corruption, KERN_ERR "Memory corruption detected in low memory\n");
}

static void check_corruption(struct work_struct *dummy);
static DECLARE_DELAYED_WORK(bios_check_work, check_corruption);

static void check_corruption(struct work_struct *dummy)
{
	check_for_bios_corruption();
	schedule_delayed_work(&bios_check_work,
		round_jiffies_relative(corruption_check_period*HZ));
}

static int start_periodic_check_for_corruption(void)
{
	if (!num_scan_areas || !memory_corruption_check || corruption_check_period == 0)
		return 0;

	printk(KERN_INFO "Scanning for low memory corruption every %d seconds\n",
	       corruption_check_period);

	/* First time we run the checks right away */
	schedule_delayed_work(&bios_check_work, 0);
	return 0;
}

module_init(start_periodic_check_for_corruption);

