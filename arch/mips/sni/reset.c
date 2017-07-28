/*
 *  linux/arch/mips/sni/process.c
 *
 *  Reset a SNI machine.
 */
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/reboot.h>
#include <asm/sni.h>

/*
 * This routine reboots the machine by asking the keyboard
 * controller to pulse the reset-line low. We try that for a while,
 * and if it doesn't work, we do some other stupid things.
 */
static inline void kb_wait(void)
{
	int i;

	for (i = 0; i < 0x10000; i++)
		if ((inb_p(0x64) & 0x02) == 0)
			break;
}

/* XXX This ends up at the ARC firmware prompt ...  */
void sni_machine_restart(char *command)
{
	int i;

	/* This does a normal via the keyboard controller like a PC.
	   We can do that easier ...  */
	local_irq_disable();
	for (;;) {
		for (i = 0; i < 100; i++) {
			kb_wait();
			udelay(50);
			outb_p(0xfe, 0x64);	 /* pulse reset low */
			udelay(50);
		}
	}
}

void sni_machine_power_off(void)
{
	*(volatile unsigned char *)PCIMT_CSWCSM = 0xfd;
}
