/*
 * Copyright (C) 1996 The Australian National University.
 * Copyright (C) 1996 Fujitsu Laboratories Limited
 * Copyright (C) 1997 Michael A. Griffith (grif@acm.org)
 * Copyright (C) 1997 Sun Weenie (ko@ko.reno.nv.us)
 * Copyright (C) 1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * 
 * This software may be distributed under the terms of the Gnu
 * Public License version 2 or later
 *
 * fake a really simple Sun prom for the SUN4
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/oplib.h>
#include <asm/idprom.h> 
#include <asm/machines.h> 
#include <asm/sun4prom.h>
#include <asm/asi.h>
#include <asm/contregs.h>
#include <linux/init.h>

static struct linux_romvec sun4romvec;
static struct idprom sun4_idprom;

struct property {
	char *name;
	char *value;
	int length;
};

struct node {
	int level;
	struct property *properties;
};

struct property null_properties = { NULL, NULL, -1 };

struct property root_properties[] = {
	{"device_type", "cpu", 4},
	{"idprom", (char *)&sun4_idprom, sizeof(struct idprom)},
	{NULL, NULL, -1}
};

struct node nodes[] = {
	{ 0, &null_properties }, 
	{ 0, root_properties },
	{ -1,&null_properties }
};


static int no_nextnode(int node)
{
	if (nodes[node].level == nodes[node+1].level)
		return node+1;
	return -1;
}

static int no_child(int node)
{
	if (nodes[node].level == nodes[node+1].level-1)
		return node+1;
	return -1;
}

static struct property *find_property(int node,char *name)
{
	struct property *prop = &nodes[node].properties[0];
	while (prop && prop->name) {
		if (strcmp(prop->name,name) == 0) return prop;
		prop++;
	}
	return NULL;
}

static int no_proplen(int node,char *name)
{
	struct property *prop = find_property(node,name);
	if (prop) return prop->length;
	return -1;
}

static int no_getprop(int node,char *name,char *value)
{
	struct property *prop = find_property(node,name);
	if (prop) {
		memcpy(value,prop->value,prop->length);
		return 1;
	}
	return -1;
}

static int no_setprop(int node,char *name,char *value,int len)
{
	return -1;
}

static char *no_nextprop(int node,char *name)
{
	struct property *prop = find_property(node,name);
	if (prop) return prop[1].name;
	return NULL;
}

static struct linux_nodeops sun4_nodeops = {
	no_nextnode,
	no_child,
	no_proplen,
	no_getprop,
	no_setprop,
	no_nextprop
};
	
static int synch_hook;

struct linux_romvec * __init sun4_prom_init(void)
{
	int i;
	unsigned char x;
	char *p;
                                
	p = (char *)&sun4_idprom;
	for (i = 0; i < sizeof(sun4_idprom); i++) {
		__asm__ __volatile__ ("lduba [%1] %2, %0" : "=r" (x) :
				      "r" (AC_IDPROM + i), "i" (ASI_CONTROL));
		*p++ = x;
	}

	memset(&sun4romvec,0,sizeof(sun4romvec));

	sun4_romvec = (linux_sun4_romvec *) SUN4_PROM_VECTOR;

	sun4romvec.pv_romvers = 40;
	sun4romvec.pv_nodeops = &sun4_nodeops;
	sun4romvec.pv_reboot = sun4_romvec->reboot;
	sun4romvec.pv_abort = sun4_romvec->abortentry;
	sun4romvec.pv_halt = sun4_romvec->exittomon;
	sun4romvec.pv_synchook = (void (**)(void))&synch_hook;
	sun4romvec.pv_setctxt = sun4_romvec->setcxsegmap;
	sun4romvec.pv_v0bootargs = sun4_romvec->bootParam;
	sun4romvec.pv_nbgetchar = sun4_romvec->mayget;
	sun4romvec.pv_nbputchar = sun4_romvec->mayput;
	sun4romvec.pv_stdin = sun4_romvec->insource;
	sun4romvec.pv_stdout = sun4_romvec->outsink;
	
	/*
	 * We turn on the LEDs to let folks without monitors or
	 * terminals know we booted.   Nothing too fancy now.  They
	 * are all on, except for LED 5, which blinks.   When we
	 * have more time, we can teach the penguin to say "By your
	 * command" or "Activating turbo boost, Michael". :-)
	 */
	sun4_romvec->setLEDs(0x0);
	
	printk("PROMLIB: Old Sun4 boot PROM monitor %s, romvec version %d\n",
		sun4_romvec->monid,
		sun4_romvec->romvecversion);

	return &sun4romvec;
}
