/* $Id: console.c,v 1.25 2001/10/30 04:54:22 davem Exp $
 * console.c: Routines that deal with sending and receiving IO
 *            to/from the current console device using the PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 Pete Zaitcev <zaitcev@yahoo.com>
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/openprom.h>
#include <asm/sun4prom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <linux/string.h>

extern void restore_current(void);

static char con_name_jmc[] = "/obio/su@"; /* "/obio/su@0,3002f8"; */
#define CON_SIZE_JMC	(sizeof(con_name_jmc))

/* Non blocking get character from console input device, returns -1
 * if no input was taken.  This can be used for polling.
 */
int
prom_nbgetchar(void)
{
	static char inc;
	int i = -1;
	unsigned long flags;

	spin_lock_irqsave(&prom_lock, flags);
	switch(prom_vers) {
	case PROM_V0:
	case PROM_SUN4:
		i = (*(romvec->pv_nbgetchar))();
		break;
	case PROM_V2:
	case PROM_V3:
		if( (*(romvec->pv_v2devops).v2_dev_read)(*romvec->pv_v2bootargs.fd_stdin , &inc, 0x1) == 1) {
			i = inc;
		} else {
			i = -1;
		}
		break;
	default:
		i = -1;
		break;
	};
	restore_current();
	spin_unlock_irqrestore(&prom_lock, flags);
	return i; /* Ugh, we could spin forever on unsupported proms ;( */
}

/* Non blocking put character to console device, returns -1 if
 * unsuccessful.
 */
int
prom_nbputchar(char c)
{
	static char outc;
	unsigned long flags;
	int i = -1;

	spin_lock_irqsave(&prom_lock, flags);
	switch(prom_vers) {
	case PROM_V0:
	case PROM_SUN4:
		i = (*(romvec->pv_nbputchar))(c);
		break;
	case PROM_V2:
	case PROM_V3:
		outc = c;
		if( (*(romvec->pv_v2devops).v2_dev_write)(*romvec->pv_v2bootargs.fd_stdout, &outc, 0x1) == 1)
			i = 0;
		else
			i = -1;
		break;
	default:
		i = -1;
		break;
	};
	restore_current();
	spin_unlock_irqrestore(&prom_lock, flags);
	return i; /* Ugh, we could spin forever on unsupported proms ;( */
}

/* Blocking version of get character routine above. */
char
prom_getchar(void)
{
	int character;
	while((character = prom_nbgetchar()) == -1) ;
	return (char) character;
}

/* Blocking version of put character routine above. */
void
prom_putchar(char c)
{
	while(prom_nbputchar(c) == -1) ;
	return;
}

/* Query for input device type */
enum prom_input_device
prom_query_input_device(void)
{
	unsigned long flags;
	int st_p;
	char propb[64];
	char *p;
	int propl;

	switch(prom_vers) {
	case PROM_V0:
	case PROM_V2:
	case PROM_SUN4:
	default:
		switch(*romvec->pv_stdin) {
		case PROMDEV_KBD:	return PROMDEV_IKBD;
		case PROMDEV_TTYA:	return PROMDEV_ITTYA;
		case PROMDEV_TTYB:	return PROMDEV_ITTYB;
		default:
			return PROMDEV_I_UNK;
		};
	case PROM_V3:
		spin_lock_irqsave(&prom_lock, flags);
		st_p = (*romvec->pv_v2devops.v2_inst2pkg)(*romvec->pv_v2bootargs.fd_stdin);
		restore_current();
		spin_unlock_irqrestore(&prom_lock, flags);
		if(prom_node_has_property(st_p, "keyboard"))
			return PROMDEV_IKBD;
		if (prom_getproperty(st_p, "name", propb, sizeof(propb)) != -1) {
			if(strncmp(propb, "keyboard", sizeof("serial")) == 0)
				return PROMDEV_IKBD;
		}
		if (prom_getproperty(st_p, "device_type", propb, sizeof(propb)) != -1) {
		if(strncmp(propb, "serial", sizeof("serial")))
			return PROMDEV_I_UNK;
		}
		propl = prom_getproperty(prom_root_node, "stdin-path", propb, sizeof(propb));
		if(propl > 2) {
			p = propb;
			while(*p) p++; p -= 2;
			if(p[0] == ':') {
				if(p[1] == 'a')
					return PROMDEV_ITTYA;
				else if(p[1] == 'b')
					return PROMDEV_ITTYB;
			}
		}
		return PROMDEV_I_UNK;
	}
}

/* Query for output device type */

enum prom_output_device
prom_query_output_device(void)
{
	unsigned long flags;
	int st_p;
	char propb[64];
	char *p;
	int propl;

	switch(prom_vers) {
	case PROM_V0:
	case PROM_SUN4:
		switch(*romvec->pv_stdin) {
		case PROMDEV_SCREEN:	return PROMDEV_OSCREEN;
		case PROMDEV_TTYA:	return PROMDEV_OTTYA;
		case PROMDEV_TTYB:	return PROMDEV_OTTYB;
		};
		break;
	case PROM_V2:
	case PROM_V3:
		spin_lock_irqsave(&prom_lock, flags);
		st_p = (*romvec->pv_v2devops.v2_inst2pkg)(*romvec->pv_v2bootargs.fd_stdout);
		restore_current();
		spin_unlock_irqrestore(&prom_lock, flags);
		propl = prom_getproperty(st_p, "device_type", propb, sizeof(propb));
		if (propl == sizeof("display") &&
			strncmp("display", propb, sizeof("display")) == 0)
		{
			return PROMDEV_OSCREEN;
		}
		if(prom_vers == PROM_V3) {
			if(propl >= 0 &&
			    strncmp("serial", propb, sizeof("serial")) != 0)
				return PROMDEV_O_UNK;
			propl = prom_getproperty(prom_root_node, "stdout-path",
						 propb, sizeof(propb));
			if(propl == CON_SIZE_JMC &&
			    strncmp(propb, con_name_jmc, CON_SIZE_JMC) == 0)
				return PROMDEV_OTTYA;
			if(propl > 2) {
				p = propb;
				while(*p) p++; p-= 2;
				if(p[0]==':') {
					if(p[1] == 'a')
						return PROMDEV_OTTYA;
					else if(p[1] == 'b')
						return PROMDEV_OTTYB;
				}
			}
		} else {
			switch(*romvec->pv_stdin) {
			case PROMDEV_TTYA:	return PROMDEV_OTTYA;
			case PROMDEV_TTYB:	return PROMDEV_OTTYB;
			};
		}
		break;
	default:
		;
	};
	return PROMDEV_O_UNK;
}
