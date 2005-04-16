/* $Id: console.c,v 1.9 1997/10/29 07:41:43 ecd Exp $
 * console.c: Routines that deal with sending and receiving IO
 *            to/from the current console device using the PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <linux/string.h>

extern int prom_stdin, prom_stdout;

/* Non blocking get character from console input device, returns -1
 * if no input was taken.  This can be used for polling.
 */
__inline__ int
prom_nbgetchar(void)
{
	char inc;

	if (p1275_cmd("read", P1275_ARG(1,P1275_ARG_OUT_BUF)|
			      P1275_INOUT(3,1),
			      prom_stdin, &inc, P1275_SIZE(1)) == 1)
		return inc;
	else
		return -1;
}

/* Non blocking put character to console device, returns -1 if
 * unsuccessful.
 */
__inline__ int
prom_nbputchar(char c)
{
	char outc;
	
	outc = c;
	if (p1275_cmd("write", P1275_ARG(1,P1275_ARG_IN_BUF)|
			       P1275_INOUT(3,1),
			       prom_stdout, &outc, P1275_SIZE(1)) == 1)
		return 0;
	else
		return -1;
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
	prom_nbputchar(c);
	return;
}

void
prom_puts(char *s, int len)
{
	p1275_cmd("write", P1275_ARG(1,P1275_ARG_IN_BUF)|
			   P1275_INOUT(3,1),
			   prom_stdout, s, P1275_SIZE(len));
}

/* Query for input device type */
enum prom_input_device
prom_query_input_device(void)
{
	int st_p;
	char propb[64];

	st_p = prom_inst2pkg(prom_stdin);
	if(prom_node_has_property(st_p, "keyboard"))
		return PROMDEV_IKBD;
	prom_getproperty(st_p, "device_type", propb, sizeof(propb));
	if(strncmp(propb, "serial", 6))
		return PROMDEV_I_UNK;
	/* FIXME: Is there any better way how to find out? */	
	memset(propb, 0, sizeof(propb));
	st_p = prom_finddevice ("/options");
	prom_getproperty(st_p, "input-device", propb, sizeof(propb));

	/*
	 * If we get here with propb == 'keyboard', we are on ttya, as
	 * the PROM defaulted to this due to 'no input device'.
	 */
	if (!strncmp(propb, "keyboard", 8))
		return PROMDEV_ITTYA;

	if (strncmp (propb, "tty", 3) || !propb[3])
		return PROMDEV_I_UNK;
	switch (propb[3]) {
		case 'a': return PROMDEV_ITTYA;
		case 'b': return PROMDEV_ITTYB;
		default: return PROMDEV_I_UNK;
	}
}

/* Query for output device type */

enum prom_output_device
prom_query_output_device(void)
{
	int st_p;
	char propb[64];
	int propl;

	st_p = prom_inst2pkg(prom_stdout);
	propl = prom_getproperty(st_p, "device_type", propb, sizeof(propb));
	if (propl >= 0 && propl == sizeof("display") &&
	    strncmp("display", propb, sizeof("display")) == 0)
		return PROMDEV_OSCREEN;
	if(strncmp("serial", propb, 6))
		return PROMDEV_O_UNK;
	/* FIXME: Is there any better way how to find out? */	
	memset(propb, 0, sizeof(propb));
	st_p = prom_finddevice ("/options");
	prom_getproperty(st_p, "output-device", propb, sizeof(propb));

	/*
	 * If we get here with propb == 'screen', we are on ttya, as
	 * the PROM defaulted to this due to 'no input device'.
	 */
	if (!strncmp(propb, "screen", 6))
		return PROMDEV_OTTYA;

	if (strncmp (propb, "tty", 3) || !propb[3])
		return PROMDEV_O_UNK;
	switch (propb[3]) {
		case 'a': return PROMDEV_OTTYA;
		case 'b': return PROMDEV_OTTYB;
		default: return PROMDEV_O_UNK;
	}
}
