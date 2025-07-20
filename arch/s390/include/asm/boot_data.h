/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_BOOT_DATA_H

#include <linux/string.h>
#include <asm/setup.h>
#include <asm/ipl.h>

extern char early_command_line[COMMAND_LINE_SIZE];
extern struct ipl_parameter_block ipl_block;
extern int ipl_block_valid;
extern int ipl_secure_flag;

extern unsigned long ipl_cert_list_addr;
extern unsigned long ipl_cert_list_size;

extern unsigned long early_ipl_comp_list_addr;
extern unsigned long early_ipl_comp_list_size;

extern char boot_rb[PAGE_SIZE * 2];
extern bool boot_earlyprintk;
extern size_t boot_rb_off;
extern char bootdebug_filter[128];
extern bool bootdebug;

#define boot_rb_foreach(cb)							\
	do {									\
		size_t off = boot_rb_off + strlen(boot_rb + boot_rb_off) + 1;	\
		size_t len;							\
		for (; off < sizeof(boot_rb) && (len = strlen(boot_rb + off)); off += len + 1) \
			cb(boot_rb + off);					\
		for (off = 0; off < boot_rb_off && (len = strlen(boot_rb + off)); off += len + 1) \
			cb(boot_rb + off);					\
	} while (0)

/*
 * bootdebug_filter is a comma separated list of strings,
 * where each string can be a prefix of the message.
 */
static inline bool bootdebug_filter_match(const char *buf)
{
	char *p = bootdebug_filter, *s;
	char *end;

	if (!*p)
		return true;

	end = p + strlen(p);
	while (p < end) {
		p = skip_spaces(p);
		s = memscan(p, ',', end - p);
		if (!strncmp(p, buf, s - p))
			return true;
		p = s + 1;
	}
	return false;
}

static inline const char *skip_timestamp(const char *buf)
{
#ifdef CONFIG_PRINTK_TIME
	const char *p = memchr(buf, ']', strlen(buf));

	if (p && p[1] == ' ')
		return p + 2;
#endif
	return buf;
}

#endif /* _ASM_S390_BOOT_DATA_H */
