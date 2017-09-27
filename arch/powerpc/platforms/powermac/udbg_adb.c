#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/bitops.h>
#include <linux/ptrace.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include <linux/cuda.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/xmon.h>
#include <asm/prom.h>
#include <asm/bootx.h>
#include <asm/errno.h>
#include <asm/pmac_feature.h>
#include <asm/processor.h>
#include <asm/delay.h>
#include <asm/btext.h>
#include <asm/time.h>
#include <asm/udbg.h>

/*
 * This implementation is "special", it can "patch" the current
 * udbg implementation and work on top of it. It must thus be
 * initialized last
 */

static void (*udbg_adb_old_putc)(char c);
static int (*udbg_adb_old_getc)(void);
static int (*udbg_adb_old_getc_poll)(void);

static enum {
	input_adb_none,
	input_adb_pmu,
	input_adb_cuda,
} input_type = input_adb_none;

int xmon_wants_key, xmon_adb_keycode;

static inline void udbg_adb_poll(void)
{
#ifdef CONFIG_ADB_PMU
	if (input_type == input_adb_pmu)
		pmu_poll_adb();
#endif /* CONFIG_ADB_PMU */
#ifdef CONFIG_ADB_CUDA
	if (input_type == input_adb_cuda)
		cuda_poll();
#endif /* CONFIG_ADB_CUDA */
}

#ifdef CONFIG_BOOTX_TEXT

static int udbg_adb_use_btext;
static int xmon_adb_shiftstate;

static unsigned char xmon_keytab[128] =
	"asdfhgzxcv\000bqwer"				/* 0x00 - 0x0f */
	"yt123465=97-80]o"				/* 0x10 - 0x1f */
	"u[ip\rlj'k;\\,/nm."				/* 0x20 - 0x2f */
	"\t `\177\0\033\0\0\0\0\0\0\0\0\0\0"		/* 0x30 - 0x3f */
	"\0.\0*\0+\0\0\0\0\0/\r\0-\0"			/* 0x40 - 0x4f */
	"\0\0000123456789\0\0\0";			/* 0x50 - 0x5f */

static unsigned char xmon_shift_keytab[128] =
	"ASDFHGZXCV\000BQWER"				/* 0x00 - 0x0f */
	"YT!@#$^%+(&_*)}O"				/* 0x10 - 0x1f */
	"U{IP\rLJ\"K:|<?NM>"				/* 0x20 - 0x2f */
	"\t ~\177\0\033\0\0\0\0\0\0\0\0\0\0"		/* 0x30 - 0x3f */
	"\0.\0*\0+\0\0\0\0\0/\r\0-\0"			/* 0x40 - 0x4f */
	"\0\0000123456789\0\0\0";			/* 0x50 - 0x5f */

static int udbg_adb_local_getc(void)
{
	int k, t, on;

	xmon_wants_key = 1;
	for (;;) {
		xmon_adb_keycode = -1;
		t = 0;
		on = 0;
		k = -1;
		do {
			if (--t < 0) {
				on = 1 - on;
				btext_drawchar(on? 0xdb: 0x20);
				btext_drawchar('\b');
				t = 200000;
			}
			udbg_adb_poll();
			if (udbg_adb_old_getc_poll)
				k = udbg_adb_old_getc_poll();
		} while (k == -1 && xmon_adb_keycode == -1);
		if (on)
			btext_drawstring(" \b");
		if (k != -1)
			return k;
		k = xmon_adb_keycode;

		/* test for shift keys */
		if ((k & 0x7f) == 0x38 || (k & 0x7f) == 0x7b) {
			xmon_adb_shiftstate = (k & 0x80) == 0;
			continue;
		}
		if (k >= 0x80)
			continue;	/* ignore up transitions */
		k = (xmon_adb_shiftstate? xmon_shift_keytab: xmon_keytab)[k];
		if (k != 0)
			break;
	}
	xmon_wants_key = 0;
	return k;
}
#endif /* CONFIG_BOOTX_TEXT */

static int udbg_adb_getc(void)
{
#ifdef CONFIG_BOOTX_TEXT
	if (udbg_adb_use_btext && input_type != input_adb_none)
		return udbg_adb_local_getc();
#endif
	if (udbg_adb_old_getc)
		return udbg_adb_old_getc();
	return -1;
}

/* getc_poll() is not really used, unless you have the xmon-over modem
 * hack that doesn't quite concern us here, thus we just poll the low level
 * ADB driver to prevent it from timing out and call back the original poll
 * routine.
 */
static int udbg_adb_getc_poll(void)
{
	udbg_adb_poll();

	if (udbg_adb_old_getc_poll)
		return udbg_adb_old_getc_poll();
	return -1;
}

static void udbg_adb_putc(char c)
{
#ifdef CONFIG_BOOTX_TEXT
	if (udbg_adb_use_btext)
		btext_drawchar(c);
#endif
	if (udbg_adb_old_putc)
		return udbg_adb_old_putc(c);
}

void __init udbg_adb_init_early(void)
{
#ifdef CONFIG_BOOTX_TEXT
	if (btext_find_display(1) == 0) {
		udbg_adb_use_btext = 1;
		udbg_putc = udbg_adb_putc;
	}
#endif
}

int __init udbg_adb_init(int force_btext)
{
	struct device_node *np;

	/* Capture existing callbacks */
	udbg_adb_old_putc = udbg_putc;
	udbg_adb_old_getc = udbg_getc;
	udbg_adb_old_getc_poll = udbg_getc_poll;

	/* Check if our early init was already called */
	if (udbg_adb_old_putc == udbg_adb_putc)
		udbg_adb_old_putc = NULL;
#ifdef CONFIG_BOOTX_TEXT
	if (udbg_adb_old_putc == btext_drawchar)
		udbg_adb_old_putc = NULL;
#endif

	/* Set ours as output */
	udbg_putc = udbg_adb_putc;
	udbg_getc = udbg_adb_getc;
	udbg_getc_poll = udbg_adb_getc_poll;

#ifdef CONFIG_BOOTX_TEXT
	/* Check if we should use btext output */
	if (btext_find_display(force_btext) == 0)
		udbg_adb_use_btext = 1;
#endif

	/* See if there is a keyboard in the device tree with a parent
	 * of type "adb". If not, we return a failure, but we keep the
	 * bext output set for now
	 */
	for_each_node_by_name(np, "keyboard") {
		struct device_node *parent = of_get_parent(np);
		int found = (parent && strcmp(parent->type, "adb") == 0);
		of_node_put(parent);
		if (found)
			break;
	}
	if (np == NULL)
		return -ENODEV;
	of_node_put(np);

#ifdef CONFIG_ADB_PMU
	if (find_via_pmu())
		input_type = input_adb_pmu;
#endif
#ifdef CONFIG_ADB_CUDA
	if (find_via_cuda())
		input_type = input_adb_cuda;
#endif

	/* Same as above: nothing found, keep btext set for output */
	if (input_type == input_adb_none)
		return -ENODEV;

	return 0;
}
