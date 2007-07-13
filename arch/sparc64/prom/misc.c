/* $Id: misc.c,v 1.20 2001/09/21 03:17:07 kanoj Exp $
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
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/ldc.h>

int prom_service_exists(const char *service_name)
{
	int err = p1275_cmd("test", P1275_ARG(0, P1275_ARG_IN_STRING) |
			    P1275_INOUT(1, 1), service_name);

	if (err)
		return 0;
	return 1;
}

void prom_sun4v_guest_soft_state(void)
{
	const char *svc = "SUNW,soft-state-supported";

	if (!prom_service_exists(svc))
		return;
	p1275_cmd(svc, P1275_INOUT(0, 0));
}

/* Reset and reboot the machine with the command 'bcommand'. */
void prom_reboot(const char *bcommand)
{
#ifdef CONFIG_SUN_LDOMS
	if (ldom_domaining_enabled)
		ldom_reboot(bcommand);
#endif
	p1275_cmd("boot", P1275_ARG(0, P1275_ARG_IN_STRING) |
		  P1275_INOUT(1, 0), bcommand);
}

/* Forth evaluate the expression contained in 'fstring'. */
void prom_feval(const char *fstring)
{
	if (!fstring || fstring[0] == 0)
		return;
	p1275_cmd("interpret", P1275_ARG(0, P1275_ARG_IN_STRING) |
		  P1275_INOUT(1, 1), fstring);
}

/* We want to do this more nicely some day. */
extern void (*prom_palette)(int);

#ifdef CONFIG_SMP
extern void smp_capture(void);
extern void smp_release(void);
#endif

/* Drop into the prom, with the chance to continue with the 'go'
 * prom command.
 */
void prom_cmdline(void)
{
	unsigned long flags;

	local_irq_save(flags);

	if (!serial_console && prom_palette)
		prom_palette(1);

#ifdef CONFIG_SMP
	smp_capture();
#endif

	p1275_cmd("enter", P1275_INOUT(0, 0));

#ifdef CONFIG_SMP
	smp_release();
#endif

	if (!serial_console && prom_palette)
		prom_palette(0);

	local_irq_restore(flags);
}

/* Drop into the prom, but completely terminate the program.
 * No chance of continuing.
 */
void prom_halt(void)
{
#ifdef CONFIG_SUN_LDOMS
	if (ldom_domaining_enabled)
		ldom_power_off();
#endif
again:
	p1275_cmd("exit", P1275_INOUT(0, 0));
	goto again; /* PROM is out to get me -DaveM */
}

void prom_halt_power_off(void)
{
#ifdef CONFIG_SUN_LDOMS
	if (ldom_domaining_enabled)
		ldom_power_off();
#endif
	p1275_cmd("SUNW,power-off", P1275_INOUT(0, 0));

	/* if nothing else helps, we just halt */
	prom_halt();
}

/* Set prom sync handler to call function 'funcp'. */
void prom_setcallback(callback_func_t funcp)
{
	if (!funcp)
		return;
	p1275_cmd("set-callback", P1275_ARG(0, P1275_ARG_IN_FUNCTION) |
		  P1275_INOUT(1, 1), funcp);
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

/* Install Linux trap table so PROM uses that instead of its own. */
void prom_set_trap_table(unsigned long tba)
{
	p1275_cmd("SUNW,set-trap-table",
		  (P1275_ARG(0, P1275_ARG_IN_64B) |
		   P1275_INOUT(1, 0)), tba);
}

void prom_set_trap_table_sun4v(unsigned long tba, unsigned long mmfsa)
{
	p1275_cmd("SUNW,set-trap-table",
		  (P1275_ARG(0, P1275_ARG_IN_64B) |
		   P1275_ARG(1, P1275_ARG_IN_64B) |
		   P1275_INOUT(2, 0)), tba, mmfsa);
}

int prom_get_mmu_ihandle(void)
{
	int node, ret;

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
	int node, ret;

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
long prom_itlb_load(unsigned long index,
		    unsigned long tte_data,
		    unsigned long vaddr)
{
	return p1275_cmd(prom_callmethod_name,
			 (P1275_ARG(0, P1275_ARG_IN_STRING) |
			  P1275_ARG(2, P1275_ARG_IN_64B) |
			  P1275_ARG(3, P1275_ARG_IN_64B) |
			  P1275_INOUT(5, 1)),
			 "SUNW,itlb-load",
			 prom_get_mmu_ihandle(),
			 /* And then our actual args are pushed backwards. */
			 vaddr,
			 tte_data,
			 index);
}

long prom_dtlb_load(unsigned long index,
		    unsigned long tte_data,
		    unsigned long vaddr)
{
	return p1275_cmd(prom_callmethod_name,
			 (P1275_ARG(0, P1275_ARG_IN_STRING) |
			  P1275_ARG(2, P1275_ARG_IN_64B) |
			  P1275_ARG(3, P1275_ARG_IN_64B) |
			  P1275_INOUT(5, 1)),
			 "SUNW,dtlb-load",
			 prom_get_mmu_ihandle(),
			 /* And then our actual args are pushed backwards. */
			 vaddr,
			 tte_data,
			 index);
}

int prom_map(int mode, unsigned long size,
	     unsigned long vaddr, unsigned long paddr)
{
	int ret = p1275_cmd(prom_callmethod_name,
			    (P1275_ARG(0, P1275_ARG_IN_STRING) |
			     P1275_ARG(3, P1275_ARG_IN_64B) |
			     P1275_ARG(4, P1275_ARG_IN_64B) |
			     P1275_ARG(6, P1275_ARG_IN_64B) |
			     P1275_INOUT(7, 1)),
			    prom_map_name,
			    prom_get_mmu_ihandle(),
			    mode,
			    size,
			    vaddr,
			    0,
			    paddr);

	if (ret == 0)
		ret = -1;
	return ret;
}

void prom_unmap(unsigned long size, unsigned long vaddr)
{
	p1275_cmd(prom_callmethod_name,
		  (P1275_ARG(0, P1275_ARG_IN_STRING) |
		   P1275_ARG(2, P1275_ARG_IN_64B) |
		   P1275_ARG(3, P1275_ARG_IN_64B) |
		   P1275_INOUT(4, 0)),
		  prom_unmap_name,
		  prom_get_mmu_ihandle(),
		  size,
		  vaddr);
}

/* Set aside physical memory which is not touched or modified
 * across soft resets.
 */
unsigned long prom_retain(const char *name,
			  unsigned long pa_low, unsigned long pa_high,
			  long size, long align)
{
	/* XXX I don't think we return multiple values correctly.
	 * XXX OBP supposedly returns pa_low/pa_high here, how does
	 * XXX it work?
	 */

	/* If align is zero, the pa_low/pa_high args are passed,
	 * else they are not.
	 */
	if (align == 0)
		return p1275_cmd("SUNW,retain",
				 (P1275_ARG(0, P1275_ARG_IN_BUF) | P1275_INOUT(5, 2)),
				 name, pa_low, pa_high, size, align);
	else
		return p1275_cmd("SUNW,retain",
				 (P1275_ARG(0, P1275_ARG_IN_BUF) | P1275_INOUT(3, 2)),
				 name, size, align);
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
	return p1275_cmd(prom_callmethod_name,
			 (P1275_ARG(0, P1275_ARG_IN_STRING)	|
			  P1275_ARG(3, P1275_ARG_OUT_BUF)	|
			  P1275_ARG(6, P1275_ARG_IN_64B)	|
			  P1275_INOUT(8, 2)),
			 "SUNW,get-unumber", prom_get_memory_ihandle(),
			 buflen, buf, P1275_SIZE(buflen),
			 0, phys_addr, syndrome_code);
}

/* Power management extensions. */
void prom_sleepself(void)
{
	p1275_cmd("SUNW,sleep-self", P1275_INOUT(0, 0));
}

int prom_sleepsystem(void)
{
	return p1275_cmd("SUNW,sleep-system", P1275_INOUT(0, 1));
}

int prom_wakeupsystem(void)
{
	return p1275_cmd("SUNW,wakeup-system", P1275_INOUT(0, 1));
}

#ifdef CONFIG_SMP
void prom_startcpu(int cpunode, unsigned long pc, unsigned long arg)
{
	p1275_cmd("SUNW,start-cpu", P1275_INOUT(3, 0), cpunode, pc, arg);
}

void prom_startcpu_cpuid(int cpuid, unsigned long pc, unsigned long arg)
{
	p1275_cmd("SUNW,start-cpu-by-cpuid", P1275_INOUT(3, 0),
		  cpuid, pc, arg);
}

void prom_stopcpu_cpuid(int cpuid)
{
	p1275_cmd("SUNW,stop-cpu-by-cpuid", P1275_INOUT(1, 0),
		  cpuid);
}

void prom_stopself(void)
{
	p1275_cmd("SUNW,stop-self", P1275_INOUT(0, 0));
}

void prom_idleself(void)
{
	p1275_cmd("SUNW,idle-self", P1275_INOUT(0, 0));
}

void prom_resumecpu(int cpunode)
{
	p1275_cmd("SUNW,resume-cpu", P1275_INOUT(1, 0), cpunode);
}
#endif
