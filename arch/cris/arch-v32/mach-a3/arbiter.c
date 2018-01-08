// SPDX-License-Identifier: GPL-2.0
/*
 * Memory arbiter functions. Allocates bandwidth through the
 * arbiter and sets up arbiter breakpoints.
 *
 * The algorithm first assigns slots to the clients that has specified
 * bandwidth (e.g. ethernet) and then the remaining slots are divided
 * on all the active clients.
 *
 * Copyright (c) 2004-2007 Axis Communications AB.
 *
 * The artpec-3 has two arbiters. The memory hierarchy looks like this:
 *
 *
 * CPU DMAs
 *  |   |
 *  |   |
 * --------------    ------------------
 * | foo arbiter|----| Internal memory|
 * --------------    ------------------
 *      |
 * --------------
 * | L2 cache   |
 * --------------
 *             |
 * h264 etc    |
 *    |        |
 *    |        |
 * --------------
 * | bar arbiter|
 * --------------
 *       |
 * ---------
 * | SDRAM |
 * ---------
 *
 */

#include <hwregs/reg_map.h>
#include <hwregs/reg_rdwr.h>
#include <hwregs/marb_foo_defs.h>
#include <hwregs/marb_bar_defs.h>
#include <arbiter.h>
#include <hwregs/intr_vect.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <asm/irq_regs.h>

#define D(x)

struct crisv32_watch_entry {
  unsigned long instance;
  watch_callback *cb;
  unsigned long start;
  unsigned long end;
  int used;
};

#define NUMBER_OF_BP 4
#define SDRAM_BANDWIDTH 400000000
#define INTMEM_BANDWIDTH 400000000
#define NBR_OF_SLOTS 64
#define NBR_OF_REGIONS 2
#define NBR_OF_CLIENTS 15
#define ARBITERS 2
#define UNASSIGNED 100

struct arbiter {
  unsigned long instance;
  int nbr_regions;
  int nbr_clients;
  int requested_slots[NBR_OF_REGIONS][NBR_OF_CLIENTS];
  int active_clients[NBR_OF_REGIONS][NBR_OF_CLIENTS];
};

static struct crisv32_watch_entry watches[ARBITERS][NUMBER_OF_BP] =
{
  {
  {regi_marb_foo_bp0},
  {regi_marb_foo_bp1},
  {regi_marb_foo_bp2},
  {regi_marb_foo_bp3}
  },
  {
  {regi_marb_bar_bp0},
  {regi_marb_bar_bp1},
  {regi_marb_bar_bp2},
  {regi_marb_bar_bp3}
  }
};

struct arbiter arbiters[ARBITERS] =
{
  { /* L2 cache arbiter */
    .instance = regi_marb_foo,
    .nbr_regions = 2,
    .nbr_clients = 15
  },
  { /* DDR2 arbiter */
    .instance = regi_marb_bar,
    .nbr_regions = 1,
    .nbr_clients = 9
  }
};

static int max_bandwidth[NBR_OF_REGIONS] = {SDRAM_BANDWIDTH, INTMEM_BANDWIDTH};

DEFINE_SPINLOCK(arbiter_lock);

static irqreturn_t
crisv32_foo_arbiter_irq(int irq, void *dev_id);
static irqreturn_t
crisv32_bar_arbiter_irq(int irq, void *dev_id);

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

static void crisv32_arbiter_config(int arbiter, int region, int unused_slots)
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

	for (client = 0; client < arbiters[arbiter].nbr_clients; client++) {
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
	    if (!arbiters[arbiter].requested_slots[region][client]) {
		/*
		 * Skip inactive clients. Also skip zero-slot
		 * allocations in this pass when there are no known
		 * free slots.
		 */
		if (!arbiters[arbiter].active_clients[region][client] ||
				unused_slots <= 0)
			continue;

		unused_slots--;

		/* Only allocate one slot for this client. */
		interval = NBR_OF_SLOTS;
	    } else
		interval = NBR_OF_SLOTS /
			arbiters[arbiter].requested_slots[region][client];

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
			while (!arbiters[arbiter].active_clients[region][client]) {
				client = (client + 1) %
					arbiters[arbiter].nbr_clients;
				if (client == first)
				   break;
			}
			val[slot] = client;
			client = (client + 1) % arbiters[arbiter].nbr_clients;
		}
		if (arbiter == 0) {
			if (region == EXT_REGION)
				REG_WR_INT_VECT(marb_foo, regi_marb_foo,
					rw_l2_slots, slot, val[slot]);
			else if (region == INT_REGION)
				REG_WR_INT_VECT(marb_foo, regi_marb_foo,
					rw_intm_slots, slot, val[slot]);
		} else {
			REG_WR_INT_VECT(marb_bar, regi_marb_bar,
				rw_ddr2_slots, slot, val[slot]);
		}
	}
}

extern char _stext[], _etext[];

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
	arbiters[0].active_clients[EXT_REGION][11] = 1;
	arbiters[0].active_clients[EXT_REGION][12] = 1;
	crisv32_arbiter_config(0, EXT_REGION, 0);
	crisv32_arbiter_config(0, INT_REGION, 0);
	crisv32_arbiter_config(1, EXT_REGION, 0);

	if (request_irq(MEMARB_FOO_INTR_VECT, crisv32_foo_arbiter_irq,
			0, "arbiter", NULL))
		printk(KERN_ERR "Couldn't allocate arbiter IRQ\n");

	if (request_irq(MEMARB_BAR_INTR_VECT, crisv32_bar_arbiter_irq,
			0, "arbiter", NULL))
		printk(KERN_ERR "Couldn't allocate arbiter IRQ\n");

#ifndef CONFIG_ETRAX_KGDB
	/* Global watch for writes to kernel text segment. */
	crisv32_arbiter_watch(virt_to_phys(_stext), _etext - _stext,
		MARB_CLIENTS(arbiter_all_clients, arbiter_bar_all_clients),
			      arbiter_all_write, NULL);
#endif

	/* Set up max burst sizes by default */
	REG_WR_INT(marb_bar, regi_marb_bar, rw_h264_rd_burst, 3);
	REG_WR_INT(marb_bar, regi_marb_bar, rw_h264_wr_burst, 3);
	REG_WR_INT(marb_bar, regi_marb_bar, rw_ccd_burst, 3);
	REG_WR_INT(marb_bar, regi_marb_bar, rw_vin_wr_burst, 3);
	REG_WR_INT(marb_bar, regi_marb_bar, rw_vin_rd_burst, 3);
	REG_WR_INT(marb_bar, regi_marb_bar, rw_sclr_rd_burst, 3);
	REG_WR_INT(marb_bar, regi_marb_bar, rw_vout_burst, 3);
	REG_WR_INT(marb_bar, regi_marb_bar, rw_sclr_fifo_burst, 3);
	REG_WR_INT(marb_bar, regi_marb_bar, rw_l2cache_burst, 3);
}

int crisv32_arbiter_allocate_bandwidth(int client, int region,
				      unsigned long bandwidth)
{
	int i;
	int total_assigned = 0;
	int total_clients = 0;
	int req;
	int arbiter = 0;

	crisv32_arbiter_init();

	if (client & 0xffff0000) {
		arbiter = 1;
		client >>= 16;
	}

	for (i = 0; i < arbiters[arbiter].nbr_clients; i++) {
		total_assigned += arbiters[arbiter].requested_slots[region][i];
		total_clients += arbiters[arbiter].active_clients[region][i];
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

	arbiters[arbiter].active_clients[region][client] = 1;
	arbiters[arbiter].requested_slots[region][client] = req;
	crisv32_arbiter_config(arbiter, region, NBR_OF_SLOTS - total_assigned);

	/* Propagate allocation from foo to bar */
	if (arbiter == 0)
		crisv32_arbiter_allocate_bandwidth(8 << 16,
			EXT_REGION, bandwidth);
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
	int arbiter = 0;

	if (client & 0xffff0000)
		arbiter = 1;

	arbiters[arbiter].requested_slots[region][client] = 0;
	arbiters[arbiter].active_clients[region][client] = 0;

	for (i = 0; i < arbiters[arbiter].nbr_clients; i++)
		total_assigned += arbiters[arbiter].requested_slots[region][i];

	crisv32_arbiter_config(arbiter, region, NBR_OF_SLOTS - total_assigned);
}

int crisv32_arbiter_watch(unsigned long start, unsigned long size,
			  unsigned long clients, unsigned long accesses,
			  watch_callback *cb)
{
	int i;
	int arbiter;
	int used[2];
	int ret = 0;

	crisv32_arbiter_init();

	if (start > 0x80000000) {
		printk(KERN_ERR "Arbiter: %lX doesn't look like a "
			"physical address", start);
		return -EFAULT;
	}

	spin_lock(&arbiter_lock);

	if (clients & 0xffff)
		used[0] = 1;
	if (clients & 0xffff0000)
		used[1] = 1;

	for (arbiter = 0; arbiter < ARBITERS; arbiter++) {
		if (!used[arbiter])
			continue;

		for (i = 0; i < NUMBER_OF_BP; i++) {
			if (!watches[arbiter][i].used) {
				unsigned intr_mask;
				if (arbiter)
					intr_mask = REG_RD_INT(marb_bar,
						regi_marb_bar, rw_intr_mask);
				else
					intr_mask = REG_RD_INT(marb_foo,
						regi_marb_foo, rw_intr_mask);

				watches[arbiter][i].used = 1;
				watches[arbiter][i].start = start;
				watches[arbiter][i].end = start + size;
				watches[arbiter][i].cb = cb;

				ret |= (i + 1) << (arbiter + 8);
				if (arbiter) {
					REG_WR_INT(marb_bar_bp,
						watches[arbiter][i].instance,
						rw_first_addr,
						watches[arbiter][i].start);
					REG_WR_INT(marb_bar_bp,
						watches[arbiter][i].instance,
						rw_last_addr,
						watches[arbiter][i].end);
					REG_WR_INT(marb_bar_bp,
						watches[arbiter][i].instance,
						rw_op, accesses);
					REG_WR_INT(marb_bar_bp,
						watches[arbiter][i].instance,
						rw_clients,
						clients & 0xffff);
				} else {
					REG_WR_INT(marb_foo_bp,
						watches[arbiter][i].instance,
						rw_first_addr,
						watches[arbiter][i].start);
					REG_WR_INT(marb_foo_bp,
						watches[arbiter][i].instance,
						rw_last_addr,
						watches[arbiter][i].end);
					REG_WR_INT(marb_foo_bp,
						watches[arbiter][i].instance,
						rw_op, accesses);
					REG_WR_INT(marb_foo_bp,
						watches[arbiter][i].instance,
						rw_clients, clients >> 16);
				}

				if (i == 0)
					intr_mask |= 1;
				else if (i == 1)
					intr_mask |= 2;
				else if (i == 2)
					intr_mask |= 4;
				else if (i == 3)
					intr_mask |= 8;

				if (arbiter)
					REG_WR_INT(marb_bar, regi_marb_bar,
						rw_intr_mask, intr_mask);
				else
					REG_WR_INT(marb_foo, regi_marb_foo,
						rw_intr_mask, intr_mask);

				spin_unlock(&arbiter_lock);

				break;
			}
		}
	}
	spin_unlock(&arbiter_lock);
	if (ret)
		return ret;
	else
		return -ENOMEM;
}

int crisv32_arbiter_unwatch(int id)
{
	int arbiter;
	int intr_mask;

	crisv32_arbiter_init();

	spin_lock(&arbiter_lock);

	for (arbiter = 0; arbiter < ARBITERS; arbiter++) {
		int id2;

		if (arbiter)
			intr_mask = REG_RD_INT(marb_bar, regi_marb_bar,
				rw_intr_mask);
		else
			intr_mask = REG_RD_INT(marb_foo, regi_marb_foo,
				rw_intr_mask);

		id2 = (id & (0xff << (arbiter + 8))) >> (arbiter + 8);
		if (id2 == 0)
			continue;
		id2--;
		if ((id2 >= NUMBER_OF_BP) || (!watches[arbiter][id2].used)) {
			spin_unlock(&arbiter_lock);
			return -EINVAL;
		}

		memset(&watches[arbiter][id2], 0,
			sizeof(struct crisv32_watch_entry));

		if (id2 == 0)
			intr_mask &= ~1;
		else if (id2 == 1)
			intr_mask &= ~2;
		else if (id2 == 2)
			intr_mask &= ~4;
		else if (id2 == 3)
			intr_mask &= ~8;

		if (arbiter)
			REG_WR_INT(marb_bar, regi_marb_bar, rw_intr_mask,
				intr_mask);
		else
			REG_WR_INT(marb_foo, regi_marb_foo, rw_intr_mask,
				intr_mask);
	}

	spin_unlock(&arbiter_lock);
	return 0;
}

extern void show_registers(struct pt_regs *regs);


static irqreturn_t
crisv32_foo_arbiter_irq(int irq, void *dev_id)
{
	reg_marb_foo_r_masked_intr masked_intr =
		REG_RD(marb_foo, regi_marb_foo, r_masked_intr);
	reg_marb_foo_bp_r_brk_clients r_clients;
	reg_marb_foo_bp_r_brk_addr r_addr;
	reg_marb_foo_bp_r_brk_op r_op;
	reg_marb_foo_bp_r_brk_first_client r_first;
	reg_marb_foo_bp_r_brk_size r_size;
	reg_marb_foo_bp_rw_ack ack = {0};
	reg_marb_foo_rw_ack_intr ack_intr = {
		.bp0 = 1, .bp1 = 1, .bp2 = 1, .bp3 = 1
	};
	struct crisv32_watch_entry *watch;
	unsigned arbiter = (unsigned)dev_id;

	masked_intr = REG_RD(marb_foo, regi_marb_foo, r_masked_intr);

	if (masked_intr.bp0)
		watch = &watches[arbiter][0];
	else if (masked_intr.bp1)
		watch = &watches[arbiter][1];
	else if (masked_intr.bp2)
		watch = &watches[arbiter][2];
	else if (masked_intr.bp3)
		watch = &watches[arbiter][3];
	else
		return IRQ_NONE;

	/* Retrieve all useful information and print it. */
	r_clients = REG_RD(marb_foo_bp, watch->instance, r_brk_clients);
	r_addr = REG_RD(marb_foo_bp, watch->instance, r_brk_addr);
	r_op = REG_RD(marb_foo_bp, watch->instance, r_brk_op);
	r_first = REG_RD(marb_foo_bp, watch->instance, r_brk_first_client);
	r_size = REG_RD(marb_foo_bp, watch->instance, r_brk_size);

	printk(KERN_DEBUG "Arbiter IRQ\n");
	printk(KERN_DEBUG "Clients %X addr %X op %X first %X size %X\n",
	       REG_TYPE_CONV(int, reg_marb_foo_bp_r_brk_clients, r_clients),
	       REG_TYPE_CONV(int, reg_marb_foo_bp_r_brk_addr, r_addr),
	       REG_TYPE_CONV(int, reg_marb_foo_bp_r_brk_op, r_op),
	       REG_TYPE_CONV(int, reg_marb_foo_bp_r_brk_first_client, r_first),
	       REG_TYPE_CONV(int, reg_marb_foo_bp_r_brk_size, r_size));

	REG_WR(marb_foo_bp, watch->instance, rw_ack, ack);
	REG_WR(marb_foo, regi_marb_foo, rw_ack_intr, ack_intr);

	printk(KERN_DEBUG "IRQ occurred at %X\n", (unsigned)get_irq_regs());

	if (watch->cb)
		watch->cb();

	return IRQ_HANDLED;
}

static irqreturn_t
crisv32_bar_arbiter_irq(int irq, void *dev_id)
{
	reg_marb_bar_r_masked_intr masked_intr =
		REG_RD(marb_bar, regi_marb_bar, r_masked_intr);
	reg_marb_bar_bp_r_brk_clients r_clients;
	reg_marb_bar_bp_r_brk_addr r_addr;
	reg_marb_bar_bp_r_brk_op r_op;
	reg_marb_bar_bp_r_brk_first_client r_first;
	reg_marb_bar_bp_r_brk_size r_size;
	reg_marb_bar_bp_rw_ack ack = {0};
	reg_marb_bar_rw_ack_intr ack_intr = {
		.bp0 = 1, .bp1 = 1, .bp2 = 1, .bp3 = 1
	};
	struct crisv32_watch_entry *watch;
	unsigned arbiter = (unsigned)dev_id;

	masked_intr = REG_RD(marb_bar, regi_marb_bar, r_masked_intr);

	if (masked_intr.bp0)
		watch = &watches[arbiter][0];
	else if (masked_intr.bp1)
		watch = &watches[arbiter][1];
	else if (masked_intr.bp2)
		watch = &watches[arbiter][2];
	else if (masked_intr.bp3)
		watch = &watches[arbiter][3];
	else
		return IRQ_NONE;

	/* Retrieve all useful information and print it. */
	r_clients = REG_RD(marb_bar_bp, watch->instance, r_brk_clients);
	r_addr = REG_RD(marb_bar_bp, watch->instance, r_brk_addr);
	r_op = REG_RD(marb_bar_bp, watch->instance, r_brk_op);
	r_first = REG_RD(marb_bar_bp, watch->instance, r_brk_first_client);
	r_size = REG_RD(marb_bar_bp, watch->instance, r_brk_size);

	printk(KERN_DEBUG "Arbiter IRQ\n");
	printk(KERN_DEBUG "Clients %X addr %X op %X first %X size %X\n",
	       REG_TYPE_CONV(int, reg_marb_bar_bp_r_brk_clients, r_clients),
	       REG_TYPE_CONV(int, reg_marb_bar_bp_r_brk_addr, r_addr),
	       REG_TYPE_CONV(int, reg_marb_bar_bp_r_brk_op, r_op),
	       REG_TYPE_CONV(int, reg_marb_bar_bp_r_brk_first_client, r_first),
	       REG_TYPE_CONV(int, reg_marb_bar_bp_r_brk_size, r_size));

	REG_WR(marb_bar_bp, watch->instance, rw_ack, ack);
	REG_WR(marb_bar, regi_marb_bar, rw_ack_intr, ack_intr);

	printk(KERN_DEBUG "IRQ occurred at %X\n", (unsigned)get_irq_regs()->erp);

	if (watch->cb)
		watch->cb();

	return IRQ_HANDLED;
}

