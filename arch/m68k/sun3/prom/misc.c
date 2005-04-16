/* $Id: misc.c,v 1.15 1997/05/14 20:45:00 davem Exp $
 * misc.c:  Miscellaneous prom functions that don't belong
 *          anywhere else.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/sun3-head.h>
#include <asm/idprom.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/movs.h>

/* Reset and reboot the machine with the command 'bcommand'. */
void
prom_reboot(char *bcommand)
{
	unsigned long flags;
	local_irq_save(flags);
	(*(romvec->pv_reboot))(bcommand);
	local_irq_restore(flags);
}

/* Drop into the prom, with the chance to continue with the 'go'
 * prom command.
 */
void
prom_cmdline(void)
{
}

/* Drop into the prom, but completely terminate the program.
 * No chance of continuing.
 */
void
prom_halt(void)
{
	unsigned long flags;
again:
	local_irq_save(flags);
	(*(romvec->pv_halt))();
	local_irq_restore(flags);
	goto again; /* PROM is out to get me -DaveM */
}

typedef void (*sfunc_t)(void);

/* Get the idprom and stuff it into buffer 'idbuf'.  Returns the
 * format type.  'num_bytes' is the number of bytes that your idbuf
 * has space for.  Returns 0xff on error.
 */
unsigned char
prom_get_idprom(char *idbuf, int num_bytes)
{
	int i, oldsfc;
	GET_SFC(oldsfc);
	SET_SFC(FC_CONTROL);
	for(i=0;i<num_bytes; i++)
	{
		/* There is a problem with the GET_CONTROL_BYTE
		macro; defining the extra variable
		gets around it.
		*/
		int c;
		GET_CONTROL_BYTE(SUN3_IDPROM_BASE + i, c);
		idbuf[i] = c;
	}
	SET_SFC(oldsfc);
	return idbuf[0];
}

/* Get the major prom version number. */
int
prom_version(void)
{
	return romvec->pv_romvers;
}

/* Get the prom plugin-revision. */
int
prom_getrev(void)
{
	return prom_rev;
}

/* Get the prom firmware print revision. */
int
prom_getprev(void)
{
	return prom_prev;
}
