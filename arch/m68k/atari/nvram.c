// SPDX-License-Identifier: GPL-2.0+
/*
 * CMOS/NV-RAM driver for Atari. Adapted from drivers/char/nvram.c.
 * Copyright (C) 1997 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 * idea by and with help from Richard Jelinek <rj@suse.de>
 * Portions copyright (c) 2001,2002 Sun Microsystems (thockin@sun.com)
 * Further contributions from Cesar Barros, Erik Gilling, Tim Hockin and
 * Wim Van Sebroeck.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/mc146818rtc.h>
#include <linux/module.h>
#include <linux/nvram.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/string_choices.h>
#include <linux/types.h>

#include <asm/atarihw.h>
#include <asm/atariints.h>

#define NVRAM_BYTES		50

/* It is worth noting that these functions all access bytes of general
 * purpose memory in the NVRAM - that is to say, they all add the
 * NVRAM_FIRST_BYTE offset. Pass them offsets into NVRAM as if you did not
 * know about the RTC cruft.
 */

/* Note that *all* calls to CMOS_READ and CMOS_WRITE must be done with
 * rtc_lock held. Due to the index-port/data-port design of the RTC, we
 * don't want two different things trying to get to it at once. (e.g. the
 * periodic 11 min sync from kernel/time/ntp.c vs. this driver.)
 */

static unsigned char __nvram_read_byte(int i)
{
	return CMOS_READ(NVRAM_FIRST_BYTE + i);
}

/* This races nicely with trying to read with checksum checking */
static void __nvram_write_byte(unsigned char c, int i)
{
	CMOS_WRITE(c, NVRAM_FIRST_BYTE + i);
}

/* On Ataris, the checksum is over all bytes except the checksum bytes
 * themselves; these are at the very end.
 */
#define ATARI_CKS_RANGE_START	0
#define ATARI_CKS_RANGE_END	47
#define ATARI_CKS_LOC		48

static int __nvram_check_checksum(void)
{
	int i;
	unsigned char sum = 0;

	for (i = ATARI_CKS_RANGE_START; i <= ATARI_CKS_RANGE_END; ++i)
		sum += __nvram_read_byte(i);
	return (__nvram_read_byte(ATARI_CKS_LOC) == (~sum & 0xff)) &&
	       (__nvram_read_byte(ATARI_CKS_LOC + 1) == (sum & 0xff));
}

static void __nvram_set_checksum(void)
{
	int i;
	unsigned char sum = 0;

	for (i = ATARI_CKS_RANGE_START; i <= ATARI_CKS_RANGE_END; ++i)
		sum += __nvram_read_byte(i);
	__nvram_write_byte(~sum, ATARI_CKS_LOC);
	__nvram_write_byte(sum, ATARI_CKS_LOC + 1);
}

long atari_nvram_set_checksum(void)
{
	spin_lock_irq(&rtc_lock);
	__nvram_set_checksum();
	spin_unlock_irq(&rtc_lock);
	return 0;
}

long atari_nvram_initialize(void)
{
	loff_t i;

	spin_lock_irq(&rtc_lock);
	for (i = 0; i < NVRAM_BYTES; ++i)
		__nvram_write_byte(0, i);
	__nvram_set_checksum();
	spin_unlock_irq(&rtc_lock);
	return 0;
}

ssize_t atari_nvram_read(char *buf, size_t count, loff_t *ppos)
{
	char *p = buf;
	loff_t i;

	spin_lock_irq(&rtc_lock);
	if (!__nvram_check_checksum()) {
		spin_unlock_irq(&rtc_lock);
		return -EIO;
	}
	for (i = *ppos; count > 0 && i < NVRAM_BYTES; --count, ++i, ++p)
		*p = __nvram_read_byte(i);
	spin_unlock_irq(&rtc_lock);

	*ppos = i;
	return p - buf;
}

ssize_t atari_nvram_write(char *buf, size_t count, loff_t *ppos)
{
	char *p = buf;
	loff_t i;

	spin_lock_irq(&rtc_lock);
	if (!__nvram_check_checksum()) {
		spin_unlock_irq(&rtc_lock);
		return -EIO;
	}
	for (i = *ppos; count > 0 && i < NVRAM_BYTES; --count, ++i, ++p)
		__nvram_write_byte(*p, i);
	__nvram_set_checksum();
	spin_unlock_irq(&rtc_lock);

	*ppos = i;
	return p - buf;
}

ssize_t atari_nvram_get_size(void)
{
	return NVRAM_BYTES;
}

#ifdef CONFIG_PROC_FS
static struct {
	unsigned char val;
	const char *name;
} boot_prefs[] = {
	{ 0x80, "TOS" },
	{ 0x40, "ASV" },
	{ 0x20, "NetBSD (?)" },
	{ 0x10, "Linux" },
	{ 0x00, "unspecified" },
};

static const char * const languages[] = {
	"English (US)",
	"German",
	"French",
	"English (UK)",
	"Spanish",
	"Italian",
	"6 (undefined)",
	"Swiss (French)",
	"Swiss (German)",
};

static const char * const dateformat[] = {
	"MM%cDD%cYY",
	"DD%cMM%cYY",
	"YY%cMM%cDD",
	"YY%cDD%cMM",
	"4 (undefined)",
	"5 (undefined)",
	"6 (undefined)",
	"7 (undefined)",
};

static const char * const colors[] = {
	"2", "4", "16", "256", "65536", "??", "??", "??"
};

static void atari_nvram_proc_read(unsigned char *nvram, struct seq_file *seq,
				  void *offset)
{
	int checksum;
	int i;
	unsigned int vmode;

	spin_lock_irq(&rtc_lock);
	checksum = __nvram_check_checksum();
	spin_unlock_irq(&rtc_lock);

	seq_printf(seq, "Checksum status  : %svalid\n", checksum ? "" : "not ");

	seq_puts(seq, "Boot preference  : ");
	for (i = ARRAY_SIZE(boot_prefs) - 1; i >= 0; --i)
		if (nvram[1] == boot_prefs[i].val) {
			seq_printf(seq, "%s\n", boot_prefs[i].name);
			break;
		}
	if (i < 0)
		seq_printf(seq, "0x%02x (undefined)\n", nvram[1]);

	seq_printf(seq, "SCSI arbitration : %s\n",
		   str_on_off(nvram[16] & 0x80));
	seq_puts(seq, "SCSI host ID     : ");
	if (nvram[16] & 0x80)
		seq_printf(seq, "%d\n", nvram[16] & 7);
	else
		seq_puts(seq, "n/a\n");

	if (!MACH_IS_FALCON)
		return;

	seq_puts(seq, "OS language      : ");
	if (nvram[6] < ARRAY_SIZE(languages))
		seq_printf(seq, "%s\n", languages[nvram[6]]);
	else
		seq_printf(seq, "%u (undefined)\n", nvram[6]);
	seq_puts(seq, "Keyboard language: ");
	if (nvram[7] < ARRAY_SIZE(languages))
		seq_printf(seq, "%s\n", languages[nvram[7]]);
	else
		seq_printf(seq, "%u (undefined)\n", nvram[7]);
	seq_puts(seq, "Date format      : ");
	seq_printf(seq, dateformat[nvram[8] & 7],
		   nvram[9] ? nvram[9] : '/', nvram[9] ? nvram[9] : '/');
	seq_printf(seq, ", %dh clock\n", nvram[8] & 16 ? 24 : 12);
	seq_puts(seq, "Boot delay       : ");
	if (nvram[10] == 0)
		seq_puts(seq, "default\n");
	else
		seq_printf(seq, "%ds%s\n", nvram[10],
			   nvram[10] < 8 ? ", no memory test" : "");

	vmode = (nvram[14] << 8) | nvram[15];
	seq_printf(seq,
		   "Video mode       : %s colors, %d columns, %s %s monitor\n",
		   colors[vmode & 7], vmode & 8 ? 80 : 40,
		   vmode & 16 ? "VGA" : "TV", vmode & 32 ? "PAL" : "NTSC");
	seq_printf(seq,
		   "                   %soverscan, compat. mode %s%s\n",
		   vmode & 64 ? "" : "no ", str_on_off(vmode & 128),
		   vmode & 256 ?
		   (vmode & 16 ? ", line doubling" : ", half screen") : "");
}

static int nvram_proc_read(struct seq_file *seq, void *offset)
{
	unsigned char contents[NVRAM_BYTES];
	int i;

	spin_lock_irq(&rtc_lock);
	for (i = 0; i < NVRAM_BYTES; ++i)
		contents[i] = __nvram_read_byte(i);
	spin_unlock_irq(&rtc_lock);

	atari_nvram_proc_read(contents, seq, offset);

	return 0;
}

static int __init atari_nvram_init(void)
{
	if (!(MACH_IS_ATARI && ATARIHW_PRESENT(TT_CLK)))
		return -ENODEV;

	if (!proc_create_single("driver/nvram", 0, NULL, nvram_proc_read)) {
		pr_err("nvram: can't create /proc/driver/nvram\n");
		return -ENOMEM;
	}

	return 0;
}
device_initcall(atari_nvram_init);
#endif /* CONFIG_PROC_FS */
