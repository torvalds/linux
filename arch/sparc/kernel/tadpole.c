/* tadpole.c: Probing for the tadpole clock stopping h/w at boot time.
 *
 * Copyright (C) 1996 David Redman (djhr@tadpole.co.uk)
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>

#include <asm/asi.h>
#include <asm/oplib.h>
#include <asm/io.h>

#define MACIO_SCSI_CSR_ADDR	0x78400000
#define MACIO_EN_DMA		0x00000200
#define CLOCK_INIT_DONE		1

static int clk_state;
static volatile unsigned char *clk_ctrl;
void (*cpu_pwr_save)(void);

static inline unsigned int ldphys(unsigned int addr)
{
	unsigned long data;
    
	__asm__ __volatile__("\n\tlda [%1] %2, %0\n\t" : 
			     "=r" (data) :
			     "r" (addr), "i" (ASI_M_BYPASS));
	return data;
}

static void clk_init(void)
{
	__asm__ __volatile__("mov 0x6c, %%g1\n\t"
			     "mov 0x4c, %%g2\n\t"
			     "mov 0xdf, %%g3\n\t"
			     "stb %%g1, [%0+3]\n\t"
			     "stb %%g2, [%0+3]\n\t"
			     "stb %%g3, [%0+3]\n\t" : :
			     "r" (clk_ctrl) :
			     "g1", "g2", "g3");
}

static void clk_slow(void)
{
	__asm__ __volatile__("mov 0xcc, %%g2\n\t"
			     "mov 0x4c, %%g3\n\t"
			     "mov 0xcf, %%g4\n\t"
			     "mov 0xdf, %%g5\n\t"
			     "stb %%g2, [%0+3]\n\t"
			     "stb %%g3, [%0+3]\n\t"
			     "stb %%g4, [%0+3]\n\t"
			     "stb %%g5, [%0+3]\n\t" : :
			     "r" (clk_ctrl) :
			     "g2", "g3", "g4", "g5");
}

/*
 * Tadpole is guaranteed to be UP, using local_irq_save.
 */
static void tsu_clockstop(void)
{
	unsigned int mcsr;
	unsigned long flags;

	if (!clk_ctrl)
		return;
	if (!(clk_state & CLOCK_INIT_DONE)) {
		local_irq_save(flags);
		clk_init();
		clk_state |= CLOCK_INIT_DONE;       /* all done */
		local_irq_restore(flags);
		return;
	}
	if (!(clk_ctrl[2] & 1))
		return;               /* no speed up yet */

	local_irq_save(flags);

	/* if SCSI DMA in progress, don't slow clock */
	mcsr = ldphys(MACIO_SCSI_CSR_ADDR);
	if ((mcsr&MACIO_EN_DMA) != 0) {
		local_irq_restore(flags);
		return;
	}
	/* TODO... the minimum clock setting ought to increase the
	 * memory refresh interval..
	 */
	clk_slow();
	local_irq_restore(flags);
}

static void swift_clockstop(void)
{
	if (!clk_ctrl)
		return;
	clk_ctrl[0] = 0;
}

void __init clock_stop_probe(void)
{
	unsigned int node, clk_nd;
	char name[20];
    
	prom_getstring(prom_root_node, "name", name, sizeof(name));
	if (strncmp(name, "Tadpole", 7))
		return;
	node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "obio");
	node = prom_getchild(node);
	clk_nd = prom_searchsiblings(node, "clk-ctrl");
	if (!clk_nd)
		return;
	printk("Clock Stopping h/w detected... ");
	clk_ctrl = (char *) prom_getint(clk_nd, "address");
	clk_state = 0;
	if (name[10] == '\0') {
		cpu_pwr_save = tsu_clockstop;
		printk("enabled (S3)\n");
	} else if ((name[10] == 'X') || (name[10] == 'G')) {
		cpu_pwr_save = swift_clockstop;
		printk("enabled (%s)\n",name+7);
	} else
		printk("disabled %s\n",name+7);
}
