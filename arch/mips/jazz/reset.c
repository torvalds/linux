// SPDX-License-Identifier: GPL-2.0
/*
 * Reset a Jazz machine.
 *
 * We don't trust the firmware so we do it the classic way by poking and
 * stabbing at the keyboard controller ...
 */
#include <linux/jiffies.h>
#include <asm/jazz.h>

#define KBD_STAT_IBF		0x02	/* Keyboard input buffer full */

static void jazz_write_output(unsigned char val)
{
	int status;

	do {
		status = jazz_kh->command;
	} while (status & KBD_STAT_IBF);
	jazz_kh->data = val;
}

static void jazz_write_command(unsigned char val)
{
	int status;

	do {
		status = jazz_kh->command;
	} while (status & KBD_STAT_IBF);
	jazz_kh->command = val;
}

static unsigned char jazz_read_status(void)
{
	return jazz_kh->command;
}

static inline void kb_wait(void)
{
	unsigned long start = jiffies;
	unsigned long timeout = start + HZ/2;

	do {
		if (! (jazz_read_status() & 0x02))
			return;
	} while (time_before_eq(jiffies, timeout));
}

void jazz_machine_restart(char *command)
{
	while(1) {
		kb_wait();
		jazz_write_command(0xd1);
		kb_wait();
		jazz_write_output(0x00);
	}
}
