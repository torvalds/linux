/*
 * misc.c:  Miscellaneous prom functions that don't belong
 *          anywhere else.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/module.h>

#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/ldc.h>

static int prom_service_exists(const char *service_name)
{
	unsigned long args[5];

	args[0] = (unsigned long) "test";
	args[1] = 1;
	args[2] = 1;
	args[3] = (unsigned long) service_name;
	args[4] = (unsigned long) -1;

	p1275_cmd_direct(args);

	if (args[4])
		return 0;
	return 1;
}

void prom_sun4v_guest_soft_state(void)
{
	const char *svc = "SUNW,soft-state-supported";
	unsigned long args[3];

	if (!prom_service_exists(svc))
		return;
	args[0] = (unsigned long) svc;
	args[1] = 0;
	args[2] = 0;
	p1275_cmd_direct(args);
}

/* Reset and reboot the machine with the command 'bcommand'. */
void prom_reboot(const char *bcommand)
{
	unsigned long args[4];

#ifdef CONFIG_SUN_LDOMS
	if (ldom_domaining_enabled)
		ldom_reboot(bcommand);
#endif
	args[0] = (unsigned long) "boot";
	args[1] = 1;
	args[2] = 0;
	args[3] = (unsigned long) bcommand;

	p1275_cmd_direct(args);
}

/* Forth evaluate the expression contained in 'fstring'. */
void prom_feval(const char *fstring)
{
	unsigned long args[5];

	if (!fstring || fstring[0] == 0)
		return;
	args[0] = (unsigned long) "interpret";
	args[1] = 1;
	args[2] = 1;
	args[3] = (unsigned long) fstring;
	args[4] = (unsigned long) -1;

	p1275_cmd_direct(args);
}
EXPORT_SYMBOL(prom_feval);

#ifdef CONFIG_SMP
extern void smp_capture(void);
extern void smp_release(void);
#endif

/* Drop into the prom, with the chance to continue with the 'go'
 * prom command.
 */
void prom_cmdline(void)
{
	unsigned long args[3];
	unsigned long flags;

	local_irq_save(flags);

#ifdef CONFIG_SMP
	smp_capture();
#endif

	args[0] = (unsigned long) "enter";
	args[1] = 0;
	args[2] = 0;

	p1275_cmd_direct(args);

#ifdef CONFIG_SMP
	smp_release();
#endif

	local_irq_restore(flags);
}

/* Drop into the prom, but completely terminate the program.
 * No chance of continuing.
 */
void notrace prom_halt(void)
{
	unsigned long args[3];

#ifdef CONFIG_SUN_LDOMS
	if (ldom_domaining_enabled)
		ldom_power_off();
#endif
again:
	args[0] = (unsigned long) "exit";
	args[1] = 0;
	args[2] = 0;
	p1275_cmd_direct(args);
	goto again; /* PROM is out to get me -DaveM */
}

void prom_halt_power_off(void)
{
	unsigned long args[3];

#ifdef CONFIG_SUN_LDOMS
	if (ldom_domaining_enabled)
		ldom_power_off();
#endif
	args[0] = (unsigned long) "SUNW,power-off";
	args[1] = 0;
	args[2] = 0;
	p1275_cmd_direct(args);

	/* if nothing else helps, we just halt */
	prom_halt();
}

/* Get the idprom and stuff it into buffer 'idbuf'.  Returns the
 * format type.  'num_bytes' is the number of bytes that your idbuf
 * has space for.  Returns 0xff on error.
 */
unsigned char prom_get_idprom(char *idbuf, int num_bytes)
{
	int len;

	len = prom_getproplen(prom_root_node, "idprom");
	if ((len >num_bytes) || (len == -1))
		return 0xff;
	if (!prom_getproperty(prom_root_node, "idprom", idbuf, num_bytes))
		return idbuf[0];

	return 0xff;
}

int prom_get_mmu_ihandle(void)
{
	phandle node;
	int ret;

	if (prom_mmu_ihandle_cache != 0)
		return prom_mmu_ihandle_cache;

	node = prom_finddevice(prom_chosen_path);
	ret = prom_getint(node, prom_mmu_name);
	if (ret == -1 || ret == 0)
		prom_mmu_ihandle_cache = -1;
	else
		prom_mmu_ihandle_cache = ret;

	return ret;
}

static int prom_get_memory_ihandle(void)
{
	static int memory_ihandle_cache;
	phandle node;
	int ret;

	if (memory_ihandle_cache != 0)
		return memory_ihandle_cache;

	node = prom_finddevice("/chosen");
	ret = prom_getint(node, "memory");
	if (ret == -1 || ret == 0)
		memory_ihandle_cache = -1;
	else
		memory_ihandle_cache = ret;

	return ret;
}

/* Load explicit I/D TLB entries. */
static long tlb_load(const char *type, unsigned long index,
		     unsigned long tte_data, unsigned long vaddr)
{
	unsigned long args[9];

	args[0] = (unsigned long) prom_callmethod_name;
	args[1] = 5;
	args[2] = 1;
	args[3] = (unsigned long) type;
	args[4] = (unsigned int) prom_get_mmu_ihandle();
	args[5] = vaddr;
	args[6] = tte_data;
	args[7] = index;
	args[8] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (long) args[8];
}

long prom_itlb_load(unsigned long index,
		    unsigned long tte_data,
		    unsigned long vaddr)
{
	return tlb_load("SUNW,itlb-load", index, tte_data, vaddr);
}

long prom_dtlb_load(unsigned long index,
		    unsigned long tte_data,
		    unsigned long vaddr)
{
	return tlb_load("SUNW,dtlb-load", index, tte_data, vaddr);
}

int prom_map(int mode, unsigned long size,
	     unsigned long vaddr, unsigned long paddr)
{
	unsigned long args[11];
	int ret;

	args[0] = (unsigned long) prom_callmethod_name;
	args[1] = 7;
	args[2] = 1;
	args[3] = (unsigned long) prom_map_name;
	args[4] = (unsigned int) prom_get_mmu_ihandle();
	args[5] = (unsigned int) mode;
	args[6] = size;
	args[7] = vaddr;
	args[8] = 0;
	args[9] = paddr;
	args[10] = (unsigned long) -1;

	p1275_cmd_direct(args);

	ret = (int) args[10];
	if (ret == 0)
		ret = -1;
	return ret;
}

void prom_unmap(unsigned long size, unsigned long vaddr)
{
	unsigned long args[7];

	args[0] = (unsigned long) prom_callmethod_name;
	args[1] = 4;
	args[2] = 0;
	args[3] = (unsigned long) prom_unmap_name;
	args[4] = (unsigned int) prom_get_mmu_ihandle();
	args[5] = size;
	args[6] = vaddr;

	p1275_cmd_direct(args);
}

/* Set aside physical memory which is not touched or modified
 * across soft resets.
 */
int prom_retain(const char *name, unsigned long size,
		unsigned long align, unsigned long *paddr)
{
	unsigned long args[11];

	args[0] = (unsigned long) prom_callmethod_name;
	args[1] = 5;
	args[2] = 3;
	args[3] = (unsigned long) "SUNW,retain";
	args[4] = (unsigned int) prom_get_memory_ihandle();
	args[5] = align;
	args[6] = size;
	args[7] = (unsigned long) name;
	args[8] = (unsigned long) -1;
	args[9] = (unsigned long) -1;
	args[10] = (unsigned long) -1;

	p1275_cmd_direct(args);

	if (args[8])
		return (int) args[8];

	/* Next we get "phys_high" then "phys_low".  On 64-bit
	 * the phys_high cell is don't care since the phys_low
	 * cell has the full value.
	 */
	*paddr = args[10];

	return 0;
}

/* Get "Unumber" string for the SIMM at the given
 * memory address.  Usually this will be of the form
 * "Uxxxx" where xxxx is a decimal number which is
 * etched into the motherboard next to the SIMM slot
 * in question.
 */
int prom_getunumber(int syndrome_code,
		    unsigned long phys_addr,
		    char *buf, int buflen)
{
	unsigned long args[12];

	args[0] = (unsigned long) prom_callmethod_name;
	args[1] = 7;
	args[2] = 2;
	args[3] = (unsigned long) "SUNW,get-unumber";
	args[4] = (unsigned int) prom_get_memory_ihandle();
	args[5] = buflen;
	args[6] = (unsigned long) buf;
	args[7] = 0;
	args[8] = phys_addr;
	args[9] = (unsigned int) syndrome_code;
	args[10] = (unsigned long) -1;
	args[11] = (unsigned long) -1;

	p1275_cmd_direct(args);

	return (int) args[10];
}

/* Power management extensions. */
void prom_sleepself(void)
{
	unsigned long args[3];

	args[0] = (unsigned long) "SUNW,sleep-self";
	args[1] = 0;
	args[2] = 0;
	p1275_cmd_direct(args);
}

int prom_sleepsystem(void)
{
	unsigned long args[4];

	args[0] = (unsigned long) "SUNW,sleep-system";
	args[1] = 0;
	args[2] = 1;
	args[3] = (unsigned long) -1;
	p1275_cmd_direct(args);

	return (int) args[3];
}

int prom_wakeupsystem(void)
{
	unsigned long args[4];

	args[0] = (unsigned long) "SUNW,wakeup-system";
	args[1] = 0;
	args[2] = 1;
	args[3] = (unsigned long) -1;
	p1275_cmd_direct(args);

	return (int) args[3];
}

#ifdef CONFIG_SMP
void prom_startcpu(int cpunode, unsigned long pc, unsigned long arg)
{
	unsigned long args[6];

	args[0] = (unsigned long) "SUNW,start-cpu";
	args[1] = 3;
	args[2] = 0;
	args[3] = (unsigned int) cpunode;
	args[4] = pc;
	args[5] = arg;
	p1275_cmd_direct(args);
}

void prom_startcpu_cpuid(int cpuid, unsigned long pc, unsigned long arg)
{
	unsigned long args[6];

	args[0] = (unsigned long) "SUNW,start-cpu-by-cpuid";
	args[1] = 3;
	args[2] = 0;
	args[3] = (unsigned int) cpuid;
	args[4] = pc;
	args[5] = arg;
	p1275_cmd_direct(args);
}

void prom_stopcpu_cpuid(int cpuid)
{
	unsigned long args[4];

	args[0] = (unsigned long) "SUNW,stop-cpu-by-cpuid";
	args[1] = 1;
	args[2] = 0;
	args[3] = (unsigned int) cpuid;
	p1275_cmd_direct(args);
}

void prom_stopself(void)
{
	unsigned long args[3];

	args[0] = (unsigned long) "SUNW,stop-self";
	args[1] = 0;
	args[2] = 0;
	p1275_cmd_direct(args);
}

void prom_idleself(void)
{
	unsigned long args[3];

	args[0] = (unsigned long) "SUNW,idle-self";
	args[1] = 0;
	args[2] = 0;
	p1275_cmd_direct(args);
}

void prom_resumecpu(int cpunode)
{
	unsigned long args[4];

	args[0] = (unsigned long) "SUNW,resume-cpu";
	args[1] = 1;
	args[2] = 0;
	args[3] = (unsigned int) cpunode;
	p1275_cmd_direct(args);
}
#endif
