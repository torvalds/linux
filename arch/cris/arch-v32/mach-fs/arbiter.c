/*
 * Memory arbiter functions. Allocates bandwidth through the
 * arbiter and sets up arbiter breakpoints.
 *
 * The algorithm first assigns slots to the clients that has specified
 * bandwidth (e.g. ethernet) and then the remaining slots are divided
 * on all the active clients.
 *
 * Copyright (c) 2004-2007 Axis Communications AB.
 */

#include <hwregs/reg_map.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/marb_defs.h>
#include <arbiter.h>
#include <hwregs/intr_vect.h>
#include <linux/interrupt.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <asm/irq_regs.h>

struct crisv32_watch_entry {
	unsigned long instance;
	watch_callback *cb;
	unsigned long start;
	unsigned long end;
	int used;
};

#define NUMBER_OF_BP 4
#define NBR_OF_CLIENTS 14
#define NBR_OF_SLOTS 64
#define SDRAM_BANDWIDTH 100000000	/* Some kind of expected value */
#define INTMEM_BANDWIDTH 400000000
#define NBR_OF_REGIONS 2

static struct crisv32_watch_entry watches[NUMBER_OF_BP] = {
	{regi_marb_bp0},
	{regi_marb_bp1},
	{regi_marb_bp2},
	{regi_marb_bp3}
};

static u8 requested_slots[NBR_OF_REGIONS][NBR_OF_CLIENTS];
static u8 active_clients[NBR_OF_REGIONS][NBR_OF_CLIENTS];
static int max_bandwidth[NBR_OF_REGIONS] =
    { SDRAM_BANDWIDTH, INTMEM_BANDWIDTH };

DEFINE_SPINLOCK(arbiter_lock);

static irqreturn_t crisv32_arbiter_irq(int irq, void *dev_id);

/*
 * "I'm the arbiter, I know the score.
 *  From square one I'll be watching all 64."
 * (memory arbiter slots, that is)
 *
 *  Or in other words:
 * Program the memory arbiter slots for "region" according to what's
 * in requested_slots[] and active_clients[], while minimizing
 * latency. A caller may pass a non-zero positive amount for
 * "unused_slots", which must then be the unallocated, remaining
 * number of slots, free to hand out to any client.
 */

static void crisv32_arbiter_config(int region, int unused_slots)
{
	int slot;
	int client;
	int interval = 0;

	/*
	 * This vector corresponds to the hardware arbiter slots (see
	 * the hardware documentation for semantics). We initialize
	 * each slot with a suitable sentinel value outside the valid
	 * range {0 .. NBR_OF_CLIENTS - 1} and replace them with
	 * client indexes. Then it's fed to the hardware.
	 */
	s8 val[NBR_OF_SLOTS];

	for (slot = 0; slot < NBR_OF_SLOTS; slot++)
		val[slot] = -1;

	for (client = 0; client < NBR_OF_CLIENTS; client++) {
		int pos;
		/* Allocate the requested non-zero number of slots, but
		 * also give clients with zero-requests one slot each
		 * while stocks last. We do the latter here, in client
		 * order. This makes sure zero-request clients are the
		 * first to get to any spare slots, else those slots
		 * could, when bandwidth is allocated close to the limit,
		 * all be allocated to low-index non-zero-request clients
		 * in the default-fill loop below. Another positive but
		 * secondary effect is a somewhat better spread of the
		 * zero-bandwidth clients in the vector, avoiding some of
		 * the latency that could otherwise be caused by the
		 * partitioning of non-zero-bandwidth clients at low
		 * indexes and zero-bandwidth clients at high
		 * indexes. (Note that this spreading can only affect the
		 * unallocated bandwidth.)  All the above only matters for
		 * memory-intensive situations, of course.
		 */
		if (!requested_slots[region][client]) {
			/*
			 * Skip inactive clients. Also skip zero-slot
			 * allocations in this pass when there are no known
			 * free slots.
			 */
			if (!active_clients[region][client]
			    || unused_slots <= 0)
				continue;

			unused_slots--;

			/* Only allocate one slot for this client. */
			interval = NBR_OF_SLOTS;
		} else
			interval =
			    NBR_OF_SLOTS / requested_slots[region][client];

		pos = 0;
		while (pos < NBR_OF_SLOTS) {
			if (val[pos] >= 0)
				pos++;
			else {
				val[pos] = client;
				pos += interval;
			}
		}
	}

	client = 0;
	for (slot = 0; slot < NBR_OF_SLOTS; slot++) {
		/*
		 * Allocate remaining slots in round-robin
		 * client-number order for active clients. For this
		 * pass, we ignore requested bandwidth and previous
		 * allocations.
		 */
		if (val[slot] < 0) {
			int first = client;
			while (!active_clients[region][client]) {
				client = (client + 1) % NBR_OF_CLIENTS;
				if (client == first)
					break;
			}
			val[slot] = client;
			client = (client + 1) % NBR_OF_CLIENTS;
		}
		if (region == EXT_REGION)
			REG_WR_INT_VECT(marb, regi_marb, rw_ext_slots, slot,
					val[slot]);
		else if (region == INT_REGION)
			REG_WR_INT_VECT(marb, regi_marb, rw_int_slots, slot,
					val[slot]);
	}
}

extern char _stext, _etext;

static void crisv32_arbiter_init(void)
{
	static int initialized;

	if (initialized)
		return;

	initialized = 1;

	/*
	 * CPU caches are always set to active, but with zero
	 * bandwidth allocated. It should be ok to allocate zero
	 * bandwidth for the caches, because DMA for other channels
	 * will supposedly finish, once their programmed amount is
	 * done, and then the caches will get access according to the
	 * "fixed scheme" for unclaimed slots. Though, if for some
	 * use-case somewhere, there's a maximum CPU latency for
	 * e.g. some interrupt, we have to start allocating specific
	 * bandwidth for the CPU caches too.
	 */
	active_clients[EXT_REGION][10] = active_clients[EXT_REGION][11] = 1;
	crisv32_arbiter_config(EXT_REGION, 0);
	crisv32_arbiter_config(INT_REGION, 0);

	if (request_irq(MEMARB_INTR_VECT, crisv32_arbiter_irq, IRQF_DISABLED,
			"arbiter", NULL))
		printk(KERN_ERR "Couldn't allocate arbiter IRQ\n");

#ifndef CONFIG_ETRAX_KGDB
	/* Global watch for writes to kernel text segment. */
	crisv32_arbiter_watch(virt_to_phys(&_stext), &_etext - &_stext,
			      arbiter_all_clients, arbiter_all_write, NULL);
#endif
}

/* Main entry for bandwidth allocation. */

int crisv32_arbiter_allocate_bandwidth(int client, int region,
				       unsigned long bandwidth)
{
	int i;
	int total_assigned = 0;
	int total_clients = 0;
	int req;

	crisv32_arbiter_init();

	for (i = 0; i < NBR_OF_CLIENTS; i++) {
		total_assigned += requested_slots[region][i];
		total_clients += active_clients[region][i];
	}

	/* Avoid division by 0 for 0-bandwidth requests. */
	req = bandwidth == 0
	    ? 0 : NBR_OF_SLOTS / (max_bandwidth[region] / bandwidth);

	/*
	 * We make sure that there are enough slots only for non-zero
	 * requests. Requesting 0 bandwidth *may* allocate slots,
	 * though if all bandwidth is allocated, such a client won't
	 * get any and will have to rely on getting memory access
	 * according to the fixed scheme that's the default when one
	 * of the slot-allocated clients doesn't claim their slot.
	 */
	if (total_assigned + req > NBR_OF_SLOTS)
		return -ENOMEM;

	active_clients[region][client] = 1;
	requested_slots[region][client] = req;
	crisv32_arbiter_config(region, NBR_OF_SLOTS - total_assigned);

	return 0;
}

/*
 * Main entry for bandwidth deallocation.
 *
 * Strictly speaking, for a somewhat constant set of clients where
 * each client gets a constant bandwidth and is just enabled or
 * disabled (somewhat dynamically), no action is necessary here to
 * avoid starvation for non-zero-allocation clients, as the allocated
 * slots will just be unused. However, handing out those unused slots
 * to active clients avoids needless latency if the "fixed scheme"
 * would give unclaimed slots to an eager low-index client.
 */

void crisv32_arbiter_deallocate_bandwidth(int client, int region)
{
	int i;
	int total_assigned = 0;

	requested_slots[region][client] = 0;
	active_clients[region][client] = 0;

	for (i = 0; i < NBR_OF_CLIENTS; i++)
		total_assigned += requested_slots[region][i];

	crisv32_arbiter_config(region, NBR_OF_SLOTS - total_assigned);
}

int crisv32_arbiter_watch(unsigned long start, unsigned long size,
			  unsigned long clients, unsigned long accesses,
			  watch_callback *cb)
{
	int i;

	crisv32_arbiter_init();

	if (start > 0x80000000) {
		printk(KERN_ERR "Arbiter: %lX doesn't look like a "
			"physical address", start);
		return -EFAULT;
	}

	spin_lock(&arbiter_lock);

	for (i = 0; i < NUMBER_OF_BP; i++) {
		if (!watches[i].used) {
			reg_marb_rw_intr_mask intr_mask =
			    REG_RD(marb, regi_marb, rw_intr_mask);

			watches[i].used = 1;
			watches[i].start = start;
			watches[i].end = start + size;
			watches[i].cb = cb;

			REG_WR_INT(marb_bp, watches[i].instance, rw_first_addr,
				   watches[i].start);
			REG_WR_INT(marb_bp, watches[i].instance, rw_last_addr,
				   watches[i].end);
			REG_WR_INT(marb_bp, watches[i].instance, rw_op,
				   accesses);
			REG_WR_INT(marb_bp, watches[i].instance, rw_clients,
				   clients);

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

static irqreturn_t crisv32_arbiter_irq(int irq, void *dev_id)
{
	reg_marb_r_masked_intr masked_intr =
	    REG_RD(marb, regi_marb, r_masked_intr);
	reg_marb_bp_r_brk_clients r_clients;
	reg_marb_bp_r_brk_addr r_addr;
	reg_marb_bp_r_brk_op r_op;
	reg_marb_bp_r_brk_first_client r_first;
	reg_marb_bp_r_brk_size r_size;
	reg_marb_bp_rw_ack ack = { 0 };
	reg_marb_rw_ack_intr ack_intr = {
		.bp0 = 1, .bp1 = 1, .bp2 = 1, .bp3 = 1
	};
	struct crisv32_watch_entry *watch;

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

	printk(KERN_INFO "Arbiter IRQ\n");
	printk(KERN_INFO "Clients %X addr %X op %X first %X size %X\n",
	       REG_TYPE_CONV(int, reg_marb_bp_r_brk_clients, r_clients),
	       REG_TYPE_CONV(int, reg_marb_bp_r_brk_addr, r_addr),
	       REG_TYPE_CONV(int, reg_marb_bp_r_brk_op, r_op),
	       REG_TYPE_CONV(int, reg_marb_bp_r_brk_first_client, r_first),
	       REG_TYPE_CONV(int, reg_marb_bp_r_brk_size, r_size));

	REG_WR(marb_bp, watch->instance, rw_ack, ack);
	REG_WR(marb, regi_marb, rw_ack_intr, ack_intr);

	printk(KERN_INFO "IRQ occured at %lX\n", get_irq_regs()->erp);

	if (watch->cb)
		watch->cb();

	return IRQ_HANDLED;
}
