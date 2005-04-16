/*
 * Copyright 2000 MontaVista Software Inc.
 * Author: MontaVista Software, Inc.
 *         	ppopov@mvista.com or source@mvista.com
 *
 * This file was derived from Carsten Langgaard's
 * arch/mips/mips-boards/generic/generic.c
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
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <linux/string.h>
#include <linux/kernel.h>

#include <asm/addrspace.h>
#include <asm/bootinfo.h>
#include <asm/gt64120.h>


/* Environment variable */

typedef struct {
	char *name;
	char *val;
} t_env_var;

int prom_argc;
char **prom_argv, **prom_envp;

int init_debug = 0;

char * __init prom_getcmdline(void)
{
	return &(arcs_cmdline[0]);
}

unsigned long __init prom_free_prom_memory(void)
{
	return 0;
}

void  __init prom_init_cmdline(void)
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
	 */

	t_env_var *env = (t_env_var *) prom_envp;
	int i;

	i = strlen(envname);

	while (env->name) {
		if (strncmp(envname, env->name, i) == 0) {
			return (env->val);
		}
		env++;
	}
	return (NULL);
}

static inline unsigned char str2hexnum(unsigned char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return 0;		/* foo */
}

static inline void str2eaddr(unsigned char *ea, unsigned char *str)
{
	int i;

	for (i = 0; i < 6; i++) {
		unsigned char num;

		if ((*str == '.') || (*str == ':'))
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

	if (init_debug > 1) {
		int i;
		printk("get_ethernet_addr: ");
		for (i = 0; i < 5; i++)
			printk("%02x:",
			       (unsigned char) *(ethernet_addr + i));
		printk("%02x\n", *(ethernet_addr + i));
	}

	return 0;
}

const char *get_system_type(void)
{
	return "Galileo EV96100";
}

void __init prom_init(void)
{
	volatile unsigned char *uart;
	char ppbuf[8];

	prom_argc = fw_arg0;
	prom_argv = (char **) fw_arg1;
	prom_envp = (char **) fw_arg2;

	mips_machgroup = MACH_GROUP_GALILEO;
	mips_machtype = MACH_EV96100;

	prom_init_cmdline();

	/* 32 MB upgradable */
	add_memory_region(0, 32 << 20, BOOT_MEM_RAM);
}
