/*
 * Memory arbiter functions. Allocates bandwidth through the
 * arbiter and sets up arbiter breakpoints.
 *
 * The algorithm first assigns slots to the clients that has specified
 * bandwidth (e.g. ethernet) and then the remaining slots are divided
 * on all the active clients.
 *
 * Copyright (c) 2004, 2005 Axis Communications AB.
 */

#include <asm/arch/hwregs/reg_map.h>
#include <asm/arch/hwregs/reg_rdwr.h>
#include <asm/arch/hwregs/marb_defs.h>
#include <asm/arch/arbiter.h>
#include <asm/arch/hwregs/intr_vect.h>
#include <linux/interrupt.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <asm/io.h>

struct crisv32_watch_entry
{
  unsigned long instance;
  watch_callback* cb;
  unsigned long start;
  unsigned long end;
  int used;
};

#define NUMBER_OF_BP 4
#define NBR_OF_CLIENTS 14
#define NBR_OF_SLOTS 64
#define SDRAM_BANDWIDTH 100000000 /* Some kind of expected value */
#define INTMEM_BANDWIDTH 400000000
#define NBR_OF_REGIONS 2

static struct crisv32_watch_entry watches[NUMBER_OF_BP] =
{
  {regi_marb_bp0},
  {regi_marb_bp1},
  {regi_marb_bp2},
  {regi_marb_bp3}
};

static int requested_slots[NBR_OF_REGIONS][NBR_OF_CLIENTS];
static int active_clients[NBR_OF_REGIONS][NBR_OF_CLIENTS];
static int max_bandwidth[NBR_OF_REGIONS] = {SDRAM_BANDWIDTH, INTMEM_BANDWIDTH};

DEFINE_SPINLOCK(arbiter_lock);

static irqreturn_t
crisv32_arbiter_irq(int irq, void* dev_id, struct pt_regs* regs);

static void crisv32_arbiter_config(int region)
{
	int slot;
	int client;
	int interval = 0;
	int val[NBR_OF_SLOTS];

	for (slot = 0; slot < NBR_OF_SLOTS; slot++)
	    val[slot] = NBR_OF_CLIENTS + 1;

	for (client = 0; client < NBR_OF_CLIENTS; client++)
	{
	    int pos;
	    if (!requested_slots[region][client])
	       continue;
	    interval = NBR_OF_SLOTS / requested_slots[region][client];
	    pos = 0;
	    while (pos < NBR_OF_SLOTS)
	    {
		if (val[pos] != NBR_OF_CLIENTS + 1)
		   pos++;
		else
		{
			val[pos] = client;
			pos += interval;
		}
	    }
	}

	client = 0;
	for (slot = 0; slot < NBR_OF_SLOTS; slot++)
	{
		if (val[slot] == NBR_OF_CLIENTS + 1)
		{
			int first = client;
			while(!active_clients[region][client]) {
				client = (client + 1) % NBR_OF_CLIENTS;
				if (client == first)
				   break;
			}
			val[slot] = client;
			client = (client + 1) % NBR_OF_CLIENTS;
		}
		if (region == EXT_REGION)
		   REG_WR_INT_VECT(marb, regi_marb, rw_ext_slots, slot, val[slot]);
		else if (region == INT_REGION)
		   REG_WR_INT_VECT(marb, regi_marb, rw_int_slots, slot, val[slot]);
	}
}

extern char _stext, _etext;

static void crisv32_arbiter_init(void)
{
	static int initialized = 0;

	if (initialized)
		return;

	initialized = 1;

	/* CPU caches are active. */
	active_clients[EXT_REGION][10] = active_clients[EXT_REGION][11] = 1;
        crisv32_arbiter_config(EXT_REGION);
        crisv32_arbiter_config(INT_REGION);

	if (request_irq(MEMARB_INTR_VECT, crisv32_arbiter_irq, IRQF_DISABLED,
                        "arbiter", NULL))
		printk(KERN_ERR "Couldn't allocate arbiter IRQ\n");

#ifndef CONFIG_ETRAX_KGDB
        /* Global watch for writes to kernel text segment. */
        crisv32_arbiter_watch(virt_to_phys(&_stext), &_etext - &_stext,
                              arbiter_all_clients, arbiter_all_write, NULL);
#endif
}



int crisv32_arbiter_allocate_bandwidth(int client, int region,
				       unsigned long bandwidth)
{
	int i;
	int total_assigned = 0;
	int total_clients = 0;
	int req;

	crisv32_arbiter_init();

	for (i = 0; i < NBR_OF_CLIENTS; i++)
	{
		total_assigned += requested_slots[region][i];
		total_clients += active_clients[region][i];
	}
	req = NBR_OF_SLOTS / (max_bandwidth[region] / bandwidth);

	if (total_assigned + total_clients + req + 1 > NBR_OF_SLOTS)
	   return -ENOMEM;

	active_clients[region][client] = 1;
	requested_slots[region][client] = req;
	crisv32_arbiter_config(region);

	return 0;
}

int crisv32_arbiter_watch(unsigned long start, unsigned long size,
                          unsigned long clients, unsigned long accesses,
                          watch_callback* cb)
{
	int i;

	crisv32_arbiter_init();

	if (start > 0x80000000) {
		printk("Arbiter: %lX doesn't look like a physical address", start);
		return -EFAULT;
	}

	spin_lock(&arbiter_lock);

	for (i = 0; i < NUMBER_OF_BP; i++) {
		if (!watches[i].used) {
			reg_marb_rw_intr_mask intr_mask = REG_RD(marb, regi_marb, rw_intr_mask);

			watches[i].used = 1;
			watches[i].start = start;
			watches[i].end = start + size;
			watches[i].cb = cb;

			REG_WR_INT(marb_bp, watches[i].instance, rw_first_addr, watches[i].start);
			REG_WR_INT(marb_bp, watches[i].instance, rw_last_addr, watches[i].end);
			REG_WR_INT(marb_bp, watches[i].instance, rw_op, accesses);
			REG_WR_INT(marb_bp, watches[i].instance, rw_clients, clients);

			if (i == 0)
				intr_mask.bp0 = regk_marb_yes;
			else if (i == 1)
				intr_mask.bp1 = regk_marb_yes;
			else if (i == 2)
				intr_mask.bp2 = regk_marb_yes;
			else if (i == 3)
				intr_mask.bp3 = regk_marb_yes;

			REG_WR(marb, regi_marb, rw_intr_mask, intr_mask);
			spin_unlock(&arbiter_lock);

			return i;
		}
	}
	spin_unlock(&arbiter_lock);
	return -ENOMEM;
}

int crisv32_arbiter_unwatch(int id)
{
	reg_marb_rw_intr_mask intr_mask = REG_RD(marb, regi_marb, rw_intr_mask);

	crisv32_arbiter_init();

	spin_lock(&arbiter_lock);

	if ((id < 0) || (id >= NUMBER_OF_BP) || (!watches[id].used)) {
		spin_unlock(&arbiter_lock);
		return -EINVAL;
	}

	memset(&watches[id], 0, sizeof(struct crisv32_watch_entry));

	if (id == 0)
		intr_mask.bp0 = regk_marb_no;
	else if (id == 1)
		intr_mask.bp2 = regk_marb_no;
	else if (id == 2)
		intr_mask.bp2 = regk_marb_no;
	else if (id == 3)
		intr_mask.bp3 = regk_marb_no;

	REG_WR(marb, regi_marb, rw_intr_mask, intr_mask);

	spin_unlock(&arbiter_lock);
	return 0;
}

extern void show_registers(struct pt_regs *regs);

static irqreturn_t
crisv32_arbiter_irq(int irq, void* dev_id, struct pt_regs* regs)
{
	reg_marb_r_masked_intr masked_intr = REG_RD(marb, regi_marb, r_masked_intr);
	reg_marb_bp_r_brk_clients r_clients;
	reg_marb_bp_r_brk_addr r_addr;
	reg_marb_bp_r_brk_op r_op;
	reg_marb_bp_r_brk_first_client r_first;
	reg_marb_bp_r_brk_size r_size;
	reg_marb_bp_rw_ack ack = {0};
	reg_marb_rw_ack_intr ack_intr = {.bp0=1,.bp1=1,.bp2=1,.bp3=1};
	struct crisv32_watch_entry* watch;

	if (masked_intr.bp0) {
		watch = &watches[0];
		ack_intr.bp0 = regk_marb_yes;
	} else if (masked_intr.bp1) {
		watch = &watches[1];
		ack_intr.bp1 = regk_marb_yes;
	} else if (masked_intr.bp2) {
		watch = &watches[2];
		ack_intr.bp2 = regk_marb_yes;
	} else if (masked_intr.bp3) {
		watch = &watches[3];
		ack_intr.bp3 = regk_marb_yes;
	} else {
		return IRQ_NONE;
	}

	/* Retrieve all useful information and print it. */
	r_clients = REG_RD(marb_bp, watch->instance, r_brk_clients);
	r_addr = REG_RD(marb_bp, watch->instance, r_brk_addr);
	r_op = REG_RD(marb_bp, watch->instance, r_brk_op);
	r_first = REG_RD(marb_bp, watch->instance, r_brk_first_client);
	r_size = REG_RD(marb_bp, watch->instance, r_brk_size);

	printk("Arbiter IRQ\n");
	printk("Clients %X addr %X op %X first %X size %X\n",
	       REG_TYPE_CONV(int, reg_marb_bp_r_brk_clients, r_clients),
	       REG_TYPE_CONV(int, reg_marb_bp_r_brk_addr, r_addr),
	       REG_TYPE_CONV(int, reg_marb_bp_r_brk_op, r_op),
	       REG_TYPE_CONV(int, reg_marb_bp_r_brk_first_client, r_first),
	       REG_TYPE_CONV(int, reg_marb_bp_r_brk_size, r_size));

	REG_WR(marb_bp, watch->instance, rw_ack, ack);
	REG_WR(marb, regi_marb, rw_ack_intr, ack_intr);

	printk("IRQ occured at %lX\n", regs->erp);

	if (watch->cb)
		watch->cb();


	return IRQ_HANDLED;
}
