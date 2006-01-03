/*
 * Copyright (C) 1996 Paul Mackerras.
 * Copyright (C) 2000 Dan Malek.
 * Quick hack of Paul's code to make XMON work on 8xx processors.  Lots
 * of assumptions, like the SMC1 is used, it has been initialized by the
 * loader at some point, and we can just stuff and suck bytes.
 * We rely upon the 8xx uart driver to support us, as the interface
 * changes between boot up and operational phases of the kernel.
 */
#include <linux/string.h>
#include <asm/machdep.h>
#include <asm/io.h>
#include <asm/page.h>
#include <linux/kernel.h>
#include <asm/8xx_immap.h>
#include <asm/mpc8xx.h>
#include <asm/commproc.h>
#include "nonstdio.h"

extern int xmon_8xx_write(char *str, int nb);
extern int xmon_8xx_read_poll(void);
extern int xmon_8xx_read_char(void);

void xmon_map_scc(void)
{
	cpmp = (cpm8xx_t *)&(((immap_t *)IMAP_ADDR)->im_cpm);
}

void xmon_init_scc(void);

int xmon_write(void *ptr, int nb)
{
	return(xmon_8xx_write(ptr, nb));
}

int xmon_readchar(void)
{
	return xmon_8xx_read_char();
}

int xmon_read_poll(void)
{
	return(xmon_8xx_read_poll());
}
