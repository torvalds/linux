/*
 *
 * BRIEF MODULE DESCRIPTION
 *    PROM library initialisation code, assuming a version of
 *    pmon is the boot code.
 *
 * Copyright 2000,2001 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * This file was derived from Carsten Langgaard's
 * arch/mips/mips-boards/xx files.
 *
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999,2000 MIPS Technologies, Inc.  All rights reserved.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>

#include <asm/bootinfo.h>

/* #define DEBUG_CMDLINE */

extern int prom_argc;
extern char **prom_argv, **prom_envp;

typedef struct
{
    char *name;
/*    char *val; */
}t_env_var;


char * prom_getcmdline(void)
{
	return &(arcs_cmdline[0]);
}

void  prom_init_cmdline(void)
{
	char *cp;
	int actr;

	actr = 1; /* Always ignore argv[0] */

	cp = &(arcs_cmdline[0]);
	while(actr < prom_argc) {
	        strcpy(cp, prom_argv[actr]);
		cp += strlen(prom_argv[actr]);
		*cp++ = ' ';
		actr++;
	}
	if (cp != &(arcs_cmdline[0])) /* get rid of trailing space */
		--cp;
	*cp = '\0';

}


char *prom_getenv(char *envname)
{
	/*
	 * Return a pointer to the given environment variable.
	 * Environment variables are stored in the form of "memsize=64".
	 */

	t_env_var *env = (t_env_var *)prom_envp;
	int i;

	i = strlen(envname);

	while(env->name) {
		if(strncmp(envname, env->name, i) == 0) {
			return(env->name + strlen(envname) + 1);
		}
		env++;
	}
	return(NULL);
}

inline unsigned char str2hexnum(unsigned char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return 0; /* foo */
}

inline void str2eaddr(unsigned char *ea, unsigned char *str)
{
	int i;

	for(i = 0; i < 6; i++) {
		unsigned char num;

		if((*str == '.') || (*str == ':'))
			str++;
		num = str2hexnum(*str++) << 4;
		num |= (str2hexnum(*str++));
		ea[i] = num;
	}
}

int get_ethernet_addr(char *ethernet_addr)
{
        char *ethaddr_str;

        ethaddr_str = prom_getenv("ethaddr");
	if (!ethaddr_str) {
	        printk("ethaddr not set in boot prom\n");
		return -1;
	}
	str2eaddr(ethernet_addr, ethaddr_str);

#if 0
	{
		int i;

	printk("get_ethernet_addr: ");
	for (i=0; i<5; i++)
		printk("%02x:", (unsigned char)*(ethernet_addr+i));
	printk("%02x\n", *(ethernet_addr+i));
	}
#endif

	return 0;
}

unsigned long __init prom_free_prom_memory(void)
{
	return 0;
}

EXPORT_SYMBOL(prom_getcmdline);
EXPORT_SYMBOL(get_ethernet_addr);
EXPORT_SYMBOL(str2eaddr);
