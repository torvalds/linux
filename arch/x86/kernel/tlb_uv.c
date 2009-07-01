/*
 *	SGI UltraViolet TLB flush routines.
 *
 *	(c) 2008 Cliff Wickman <cpw@sgi.com>, SGI.
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 */
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>

#include <asm/mmu_context.h>
#include <asm/uv/uv.h>
#include <asm/uv/uv_mmrs.h>
#include <asm/uv/uv_hub.h>
#include <asm/uv/uv_bau.h>
#include <asm/apic.h>
#include <asm/idle.h>
#include <asm/tsc.h>
#include <asm/irq_vectors.h>

static struct bau_control	**uv_bau_table_bases __read_mostly;
static int			uv_bau_retry_limit __read_mostly;

/* position of pnode (which is nasid>>1): */
static int			uv_nshift __read_mostly;
/* base pnode in this partition */
static int			uv_partition_base_pnode __read_mostly;

static unsigned long		uv_mmask __read_mostly;

static DEFINE_PER_CPU(struct ptc_stats, ptcstats);
static DEFINE_PER_CPU(struct bau_control, bau_control);

/*
 * Determine the first node on a blade.
 */
static int __init blade_to_first_node(int blade)
{
	int node, b;

	for_each_online_node(node) {
		b = uv_node_to_blade_id(node);
		if (blade == b)
			return node;
	}
	return -1; /* shouldn't happen */
}

/*
 * Determine the apicid of the first cpu on a blade.
 */
static int __init blade_to_first_apicid(int blade)
{
	int cpu;

	for_each_present_cpu(cpu)
		if (blade == uv_cpu_to_blade_id(cpu))
			return per_cpu(x86_cpu_to_apicid, cpu);
	return -1;
}

/*
 * Free a software acknowledge hardware resource by clearing its Pending
 * bit. This will return a reply to the sender.
 * If the message has timed out, a reply has already been sent by the
 * hardware but the resource has not been released. In that case our
 * clear of the Timeout bit (as well) will free the resource. No reply will
 * be sent (the hardware will only do one reply per message).
 */
static void uv_reply_to_message(int resource,
				struct bau_payload_queue_entry *msg,
				struct bau_msg_status *msp)
{
	unsigned long dw;

	dw = (1 << (resource + UV_SW_ACK_NPENDING)) | (1 << resource);
	msg->replied_to = 1;
	msg->sw_ack_vector = 0;
	if (msp)
		msp->seen_by.bits = 0;
	uv_write_local_mmr(UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS, dw);
}

/*
 * Do all the things a cpu should do for a TLB shootdown message.
 * Other cpu's may come here at the same time for this message.
 */
static void uv_bau_process_message(struct bau_payload_queue_entry *msg,
				   int msg_slot, int sw_ack_slot)
{
	unsigned long this_cpu_mask;
	struct bau_msg_status *msp;
	int cpu;

	msp = __get_cpu_var(bau_control).msg_statuses + msg_slot;
	cpu = uv_blade_processor_id();
	msg->number_of_cpus =
		uv_blade_nr_online_cpus(uv_node_to_blade_id(numa_node_id()));
	this_cpu_mask = 1UL << cpu;
	if (msp->seen_by.bits & this_cpu_mask)
		return;
	atomic_or_long(&msp->seen_by.bits, this_cpu_mask);

	if (msg->replied_to == 1)
		return;

	if (msg->address == TLB_FLUSH_ALL) {
		local_flush_tlb();
		__get_cpu_var(ptcstats).alltlb++;
	} else {
		__flush_tlb_one(msg->address);
		__get_cpu_var(ptcstats).onetlb++;
	}

	__get_cpu_var(ptcstats).requestee++;

	atomic_inc_short(&msg->acknowledge_count);
	if (msg->number_of_cpus == msg->acknowledge_count)
		uv_reply_to_message(sw_ack_slot, msg, msp);
}

/*
 * Examine the payload queue on one distribution node to see
 * which messages have not been seen, and which cpu(s) have not seen them.
 *
 * Returns the number of cpu's that have not responded.
 */
static int uv_examine_destination(struct bau_control *bau_tablesp, int sender)
{
	struct bau_payload_queue_entry *msg;
	struct bau_msg_status *msp;
	int count = 0;
	int i;
	int j;

	for (msg = bau_tablesp->va_queue_first, i = 0; i < DEST_Q_SIZE;
	     msg++, i++) {
		if ((msg->sending_cpu == sender) && (!msg->replied_to)) {
			msp = bau_tablesp->msg_statuses + i;
			printk(KERN_DEBUG
			       "blade %d: address:%#lx %d of %d, not cpu(s): ",
			       i, msg->address, msg->acknowledge_count,
			       msg->number_of_cpus);
			for (j = 0; j < msg->number_of_cpus; j++) {
				if (!((1L << j) & msp->seen_by.bits)) {
					count++;
					printk("%d ", j);
				}
			}
			printk("\n");
		}
	}
	return count;
}

/*
 * Examine the payload queue on all the distribution nodes to see
 * which messages have not been seen, and which cpu(s) have not seen them.
 *
 * Returns the number of cpu's that have not responded.
 */
static int uv_examine_destinations(struct bau_target_nodemask *distribution)
{
	int sender;
	int i;
	int count = 0;

	sender = smp_processor_id();
	for (i = 0; i < sizeof(struct bau_target_nodemask) * BITSPERBYTE; i++) {
		if (!bau_node_isset(i, distribution))
			continue;
		count += uv_examine_destination(uv_bau_table_bases[i], sender);
	}
	return count;
}

/*
 * wait for completion of a broadcast message
 *
 * return COMPLETE, RETRY or GIVEUP
 */
static int uv_wait_completion(struct bau_desc *bau_desc,
			      unsigned long mmr_offset, int right_shift)
{
	int exams = 0;
	long destination_timeouts = 0;
	long source_timeouts = 0;
	unsigned long descriptor_status;

	while ((descriptor_status = (((unsigned long)
		uv_read_local_mmr(mmr_offset) >>
			right_shift) & UV_ACT_STATUS_MASK)) !=
			DESC_STATUS_IDLE) {
		if (descriptor_status == DESC_STATUS_SOURCE_TIMEOUT) {
			source_timeouts++;
			if (source_timeouts > SOURCE_TIMEOUT_LIMIT)
				source_timeouts = 0;
			__get_cpu_var(ptcstats).s_retry++;
			return FLUSH_RETRY;
		}
		/*
		 * spin here looking for progress at the destinations
		 */
		if (descriptor_status == DESC_STATUS_DESTINATION_TIMEOUT) {
			destination_timeouts++;
			if (destination_timeouts > DESTINATION_TIMEOUT_LIMIT) {
				/*
				 * returns number of cpus not responding
				 */
				if (uv_examine_destinations
				    (&bau_desc->distribution) == 0) {
					__get_cpu_var(ptcstats).d_retry++;
					return FLUSH_RETRY;
				}
				exams++;
				if (exams >= uv_bau_retry_limit) {
					printk(KERN_DEBUG
					       "uv_flush_tlb_others");
					printk("giving up on cpu %d\n",
					       smp_processor_id());
					return FLUSH_GIVEUP;
				}
				/*
				 * delays can hang the simulator
				   udelay(1000);
				 */
				destination_timeouts = 0;
			}
		}
		cpu_relax();
	}
	return FLUSH_COMPLETE;
}

/**
 * uv_flush_send_and_wait
 *
 * Send a broadcast and wait for a broadcast message to complete.
 *
 * The flush_mask contains the cpus the broadcast was sent to.
 *
 * Returns NULL if all remote flushing was done. The mask is zeroed.
 * Returns @flush_mask if some remote flushing remains to be done. The
 * mask will have some bits still set.
 */
const struct cpumask *uv_flush_send_and_wait(int cpu, int this_pnode,
					     struct bau_desc *bau_desc,
					     struct cpumask *flush_mask)
{
	int completion_status = 0;
	int right_shift;
	int tries = 0;
	int pnode;
	int bit;
	unsigned long mmr_offset;
	unsigned long index;
	cycles_t time1;
	cycles_t time2;

	if (cpu < UV_CPUS_PER_ACT_STATUS) {
		mmr_offset = UVH_LB_BAU_SB_ACTIVATION_STATUS_0;
		right_shift = cpu * UV_ACT_STATUS_SIZE;
	} else {
		mmr_offset = UVH_LB_BAU_SB_ACTIVATION_STATUS_1;
		right_shift =
		    ((cpu - UV_CPUS_PER_ACT_STATUS) * UV_ACT_STATUS_SIZE);
	}
	time1 = get_cycles();
	do {
		tries++;
		index = (1UL << UVH_LB_BAU_SB_ACTIVATION_CONTROL_PUSH_SHFT) |
			cpu;
		uv_write_local_mmr(UVH_LB_BAU_SB_ACTIVATION_CONTROL, index);
		completion_status = uv_wait_completion(bau_desc, mmr_offset,
					right_shift);
	} while (completion_status == FLUSH_RETRY);
	time2 = get_cycles();
	__get_cpu_var(ptcstats).sflush += (time2 - time1);
	if (tries > 1)
		__get_cpu_var(ptcstats).retriesok++;

	if (completion_status == FLUSH_GIVEUP) {
		/*
		 * Cause the caller to do an IPI-style TLB shootdown on
		 * the cpu's, all of which are still in the mask.
		 */
		__get_cpu_var(ptcstats).ptc_i++;
		return flush_mask;
	}

	/*
	 * Success, so clear the remote cpu's from the mask so we don't
	 * use the IPI method of shootdown on them.
	 */
	for_each_cpu(bit, flush_mask) {
		pnode = uv_cpu_to_pnode(bit);
		if (pnode == this_pnode)
			continue;
		cpumask_clear_cpu(bit, flush_mask);
	}
	if (!cpumask_empty(flush_mask))
		return flush_mask;
	return NULL;
}

static DEFINE_PER_CPU(cpumask_var_t, uv_flush_tlb_mask);

/**
 * uv_flush_tlb_others - globally purge translation cache of a virtual
 * address or all TLB's
 * @cpumask: mask of all cpu's in which the address is to be removed
 * @mm: mm_struct containing virtual address range
 * @va: virtual address to be removed (or TLB_FLUSH_ALL for all TLB's on cpu)
 * @cpu: the current cpu
 *
 * This is the entry point for initiating any UV global TLB shootdown.
 *
 * Purges the translation caches of all specified processors of the given
 * virtual address, or purges all TLB's on specified processors.
 *
 * The caller has derived the cpumask from the mm_struct.  This function
 * is called only if there are bits set in the mask. (e.g. flush_tlb_page())
 *
 * The cpumask is converted into a nodemask of the nodes containing
 * the cpus.
 *
 * Note that this function should be called with preemption disabled.
 *
 * Returns NULL if all remote flushing was done.
 * Returns pointer to cpumask if some remote flushing remains to be
 * done.  The returned pointer is valid till preemption is re-enabled.
 */
const struct cpumask *uv_flush_tlb_others(const struct cpumask *cpumask,
					  struct mm_struct *mm,
					  unsigned long va, unsigned int cpu)
{
	struct cpumask *flush_mask = __get_cpu_var(uv_flush_tlb_mask);
	int i;
	int bit;
	int pnode;
	int uv_cpu;
	int this_pnode;
	int locals = 0;
	struct bau_desc *bau_desc;

	cpumask_andnot(flush_mask, cpumask, cpumask_of(cpu));

	uv_cpu = uv_blade_processor_id();
	this_pnode = uv_hub_info->pnode;
	bau_desc = __get_cpu_var(bau_control).descriptor_base;
	bau_desc += UV_ITEMS_PER_DESCRIPTOR * uv_cpu;

	bau_nodes_clear(&bau_desc->distribution, UV_DISTRIBUTION_SIZE);

	i = 0;
	for_each_cpu(bit, flush_mask) {
		pnode = uv_cpu_to_pnode(bit);
		BUG_ON(pnode > (UV_DISTRIBUTION_SIZE - 1));
		if (pnode == this_pnode) {
			locals++;
			continue;
		}
		bau_node_set(pnode - uv_partition_base_pnode,
				&bau_desc->distribution);
		i++;
	}
	if (i == 0) {
		/*
		 * no off_node flushing; return status for local node
		 */
		if (locals)
			return flush_mask;
		else
			return NULL;
	}
	__get_cpu_var(ptcstats).requestor++;
	__get_cpu_var(ptcstats).ntargeted += i;

	bau_desc->payload.address = va;
	bau_desc->payload.sending_cpu = cpu;

	return uv_flush_send_and_wait(uv_cpu, this_pnode, bau_desc, flush_mask);
}

/*
 * The BAU message interrupt comes here. (registered by set_intr_gate)
 * See entry_64.S
 *
 * We received a broadcast assist message.
 *
 * Interrupts may have been disabled; this interrupt could represent
 * the receipt of several messages.
 *
 * All cores/threads on this node get this interrupt.
 * The last one to see it does the s/w ack.
 * (the resource will not be freed until noninterruptable cpus see this
 *  interrupt; hardware will timeout the s/w ack and reply ERROR)
 */
void uv_bau_message_interrupt(struct pt_regs *regs)
{
	struct bau_payload_queue_entry *va_queue_first;
	struct bau_payload_queue_entry *va_queue_last;
	struct bau_payload_queue_entry *msg;
	struct pt_regs *old_regs = set_irq_regs(regs);
	cycles_t time1;
	cycles_t time2;
	int msg_slot;
	int sw_ack_slot;
	int fw;
	int count = 0;
	unsigned long local_pnode;

	ack_APIC_irq();
	exit_idle();
	irq_enter();

	time1 = get_cycles();

	local_pnode = uv_blade_to_pnode(uv_numa_blade_id());

	va_queue_first = __get_cpu_var(bau_control).va_queue_first;
	va_queue_last = __get_cpu_var(bau_control).va_queue_last;

	msg = __get_cpu_var(bau_control).bau_msg_head;
	while (msg->sw_ack_vector) {
		count++;
		fw = msg->sw_ack_vector;
		msg_slot = msg - va_queue_first;
		sw_ack_slot = ffs(fw) - 1;

		uv_bau_process_message(msg, msg_slot, sw_ack_slot);

		msg++;
		if (msg > va_queue_last)
			msg = va_queue_first;
		__get_cpu_var(bau_control).bau_msg_head = msg;
	}
	if (!count)
		__get_cpu_var(ptcstats).nomsg++;
	else if (count > 1)
		__get_cpu_var(ptcstats).multmsg++;

	time2 = get_cycles();
	__get_cpu_var(ptcstats).dflush += (time2 - time1);

	irq_exit();
	set_irq_regs(old_regs);
}

/*
 * uv_enable_timeouts
 *
 * Each target blade (i.e. blades that have cpu's) needs to have
 * shootdown message timeouts enabled.  The timeout does not cause
 * an interrupt, but causes an error message to be returned to
 * the sender.
 */
static void uv_enable_timeouts(void)
{
	int blade;
	int nblades;
	int pnode;
	unsigned long mmr_image;

	nblades = uv_num_possible_blades();

	for (blade = 0; blade < nblades; blade++) {
		if (!uv_blade_nr_possible_cpus(blade))
			continue;

		pnode = uv_blade_to_pnode(blade);
		mmr_image =
		    uv_read_global_mmr64(pnode, UVH_LB_BAU_MISC_CONTROL);
		/*
		 * Set the timeout period and then lock it in, in three
		 * steps; captures and locks in the period.
		 *
		 * To program the period, the SOFT_ACK_MODE must be off.
		 */
		mmr_image &= ~((unsigned long)1 <<
			       UV_ENABLE_INTD_SOFT_ACK_MODE_SHIFT);
		uv_write_global_mmr64
		    (pnode, UVH_LB_BAU_MISC_CONTROL, mmr_image);
		/*
		 * Set the 4-bit period.
		 */
		mmr_image &= ~((unsigned long)0xf <<
			UV_INTD_SOFT_ACK_TIMEOUT_PERIOD_SHIFT);
		mmr_image |= (UV_INTD_SOFT_ACK_TIMEOUT_PERIOD <<
			     UV_INTD_SOFT_ACK_TIMEOUT_PERIOD_SHIFT);
		uv_write_global_mmr64
		    (pnode, UVH_LB_BAU_MISC_CONTROL, mmr_image);
		/*
		 * Subsequent reversals of the timebase bit (3) cause an
		 * immediate timeout of one or all INTD resources as
		 * indicated in bits 2:0 (7 causes all of them to timeout).
		 */
		mmr_image |= ((unsigned long)1 <<
			      UV_ENABLE_INTD_SOFT_ACK_MODE_SHIFT);
		uv_write_global_mmr64
		    (pnode, UVH_LB_BAU_MISC_CONTROL, mmr_image);
	}
}

static void *uv_ptc_seq_start(struct seq_file *file, loff_t *offset)
{
	if (*offset < num_possible_cpus())
		return offset;
	return NULL;
}

static void *uv_ptc_seq_next(struct seq_file *file, void *data, loff_t *offset)
{
	(*offset)++;
	if (*offset < num_possible_cpus())
		return offset;
	return NULL;
}

static void uv_ptc_seq_stop(struct seq_file *file, void *data)
{
}

/*
 * Display the statistics thru /proc
 * data points to the cpu number
 */
static int uv_ptc_seq_show(struct seq_file *file, void *data)
{
	struct ptc_stats *stat;
	int cpu;

	cpu = *(loff_t *)data;

	if (!cpu) {
		seq_printf(file,
		"# cpu requestor requestee one all sretry dretry ptc_i ");
		seq_printf(file,
		"sw_ack sflush dflush sok dnomsg dmult starget\n");
	}
	if (cpu < num_possible_cpus() && cpu_online(cpu)) {
		stat = &per_cpu(ptcstats, cpu);
		seq_printf(file, "cpu %d %ld %ld %ld %ld %ld %ld %ld ",
			   cpu, stat->requestor,
			   stat->requestee, stat->onetlb, stat->alltlb,
			   stat->s_retry, stat->d_retry, stat->ptc_i);
		seq_printf(file, "%lx %ld %ld %ld %ld %ld %ld\n",
			   uv_read_global_mmr64(uv_cpu_to_pnode(cpu),
					UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE),
			   stat->sflush, stat->dflush,
			   stat->retriesok, stat->nomsg,
			   stat->multmsg, stat->ntargeted);
	}

	return 0;
}

/*
 *  0: display meaning of the statistics
 * >0: retry limit
 */
static ssize_t uv_ptc_proc_write(struct file *file, const char __user *user,
				 size_t count, loff_t *data)
{
	long newmode;
	char optstr[64];

	if (count == 0 || count > sizeof(optstr))
		return -EINVAL;
	if (copy_from_user(optstr, user, count))
		return -EFAULT;
	optstr[count - 1] = '\0';
	if (strict_strtoul(optstr, 10, &newmode) < 0) {
		printk(KERN_DEBUG "%s is invalid\n", optstr);
		return -EINVAL;
	}

	if (newmode == 0) {
		printk(KERN_DEBUG "# cpu:      cpu number\n");
		printk(KERN_DEBUG
		"requestor:  times this cpu was the flush requestor\n");
		printk(KERN_DEBUG
		"requestee:  times this cpu was requested to flush its TLBs\n");
		printk(KERN_DEBUG
		"one:        times requested to flush a single address\n");
		printk(KERN_DEBUG
		"all:        times requested to flush all TLB's\n");
		printk(KERN_DEBUG
		"sretry:     number of retries of source-side timeouts\n");
		printk(KERN_DEBUG
		"dretry:     number of retries of destination-side timeouts\n");
		printk(KERN_DEBUG
		"ptc_i:      times UV fell through to IPI-style flushes\n");
		printk(KERN_DEBUG
		"sw_ack:     image of UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE\n");
		printk(KERN_DEBUG
		"sflush_us:  cycles spent in uv_flush_tlb_others()\n");
		printk(KERN_DEBUG
		"dflush_us:  cycles spent in handling flush requests\n");
		printk(KERN_DEBUG "sok:        successes on retry\n");
		printk(KERN_DEBUG "dnomsg:     interrupts with no message\n");
		printk(KERN_DEBUG
		"dmult:      interrupts with multiple messages\n");
		printk(KERN_DEBUG "starget:    nodes targeted\n");
	} else {
		uv_bau_retry_limit = newmode;
		printk(KERN_DEBUG "timeout retry limit:%d\n",
		       uv_bau_retry_limit);
	}

	return count;
}

static const struct seq_operations uv_ptc_seq_ops = {
	.start		= uv_ptc_seq_start,
	.next		= uv_ptc_seq_next,
	.stop		= uv_ptc_seq_stop,
	.show		= uv_ptc_seq_show
};

static int uv_ptc_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &uv_ptc_seq_ops);
}

static const struct file_operations proc_uv_ptc_operations = {
	.open		= uv_ptc_proc_open,
	.read		= seq_read,
	.write		= uv_ptc_proc_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init uv_ptc_init(void)
{
	struct proc_dir_entry *proc_uv_ptc;

	if (!is_uv_system())
		return 0;

	proc_uv_ptc = create_proc_entry(UV_PTC_BASENAME, 0444, NULL);
	if (!proc_uv_ptc) {
		printk(KERN_ERR "unable to create %s proc entry\n",
		       UV_PTC_BASENAME);
		return -EINVAL;
	}
	proc_uv_ptc->proc_fops = &proc_uv_ptc_operations;
	return 0;
}

/*
 * begin the initialization of the per-blade control structures
 */
static struct bau_control * __init uv_table_bases_init(int blade, int node)
{
	int i;
	struct bau_msg_status *msp;
	struct bau_control *bau_tabp;

	bau_tabp =
	    kmalloc_node(sizeof(struct bau_control), GFP_KERNEL, node);
	BUG_ON(!bau_tabp);

	bau_tabp->msg_statuses =
	    kmalloc_node(sizeof(struct bau_msg_status) *
			 DEST_Q_SIZE, GFP_KERNEL, node);
	BUG_ON(!bau_tabp->msg_statuses);

	for (i = 0, msp = bau_tabp->msg_statuses; i < DEST_Q_SIZE; i++, msp++)
		bau_cpubits_clear(&msp->seen_by, (int)
				  uv_blade_nr_possible_cpus(blade));

	uv_bau_table_bases[blade] = bau_tabp;

	return bau_tabp;
}

/*
 * finish the initialization of the per-blade control structures
 */
static void __init
uv_table_bases_finish(int blade,
		      struct bau_control *bau_tablesp,
		      struct bau_desc *adp)
{
	struct bau_control *bcp;
	int cpu;

	for_each_present_cpu(cpu) {
		if (blade != uv_cpu_to_blade_id(cpu))
			continue;

		bcp = (struct bau_control *)&per_cpu(bau_control, cpu);
		bcp->bau_msg_head	= bau_tablesp->va_queue_first;
		bcp->va_queue_first	= bau_tablesp->va_queue_first;
		bcp->va_queue_last	= bau_tablesp->va_queue_last;
		bcp->msg_statuses	= bau_tablesp->msg_statuses;
		bcp->descriptor_base	= adp;
	}
}

/*
 * initialize the sending side's sending buffers
 */
static struct bau_desc * __init
uv_activation_descriptor_init(int node, int pnode)
{
	int i;
	unsigned long pa;
	unsigned long m;
	unsigned long n;
	struct bau_desc *adp;
	struct bau_desc *ad2;

	/*
	 * each bau_desc is 64 bytes; there are 8 (UV_ITEMS_PER_DESCRIPTOR)
	 * per cpu; and up to 32 (UV_ADP_SIZE) cpu's per blade
	 */
	adp = (struct bau_desc *)kmalloc_node(sizeof(struct bau_desc)*
		UV_ADP_SIZE*UV_ITEMS_PER_DESCRIPTOR, GFP_KERNEL, node);
	BUG_ON(!adp);

	pa = uv_gpa(adp); /* need the real nasid*/
	n = pa >> uv_nshift;
	m = pa & uv_mmask;

	uv_write_global_mmr64(pnode, UVH_LB_BAU_SB_DESCRIPTOR_BASE,
			      (n << UV_DESC_BASE_PNODE_SHIFT | m));

	/*
	 * initializing all 8 (UV_ITEMS_PER_DESCRIPTOR) descriptors for each
	 * cpu even though we only use the first one; one descriptor can
	 * describe a broadcast to 256 nodes.
	 */
	for (i = 0, ad2 = adp; i < (UV_ADP_SIZE*UV_ITEMS_PER_DESCRIPTOR);
		i++, ad2++) {
		memset(ad2, 0, sizeof(struct bau_desc));
		ad2->header.sw_ack_flag = 1;
		/*
		 * base_dest_nodeid is the first node in the partition, so
		 * the bit map will indicate partition-relative node numbers.
		 * note that base_dest_nodeid is actually a nasid.
		 */
		ad2->header.base_dest_nodeid = uv_partition_base_pnode << 1;
		ad2->header.command = UV_NET_ENDPOINT_INTD;
		ad2->header.int_both = 1;
		/*
		 * all others need to be set to zero:
		 *   fairness chaining multilevel count replied_to
		 */
	}
	return adp;
}

/*
 * initialize the destination side's receiving buffers
 */
static struct bau_payload_queue_entry * __init
uv_payload_queue_init(int node, int pnode, struct bau_control *bau_tablesp)
{
	struct bau_payload_queue_entry *pqp;
	unsigned long pa;
	int pn;
	char *cp;

	pqp = (struct bau_payload_queue_entry *) kmalloc_node(
		(DEST_Q_SIZE + 1) * sizeof(struct bau_payload_queue_entry),
		GFP_KERNEL, node);
	BUG_ON(!pqp);

	cp = (char *)pqp + 31;
	pqp = (struct bau_payload_queue_entry *)(((unsigned long)cp >> 5) << 5);
	bau_tablesp->va_queue_first = pqp;
	/*
	 * need the pnode of where the memory was really allocated
	 */
	pa = uv_gpa(pqp);
	pn = pa >> uv_nshift;
	uv_write_global_mmr64(pnode,
			      UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST,
			      ((unsigned long)pn << UV_PAYLOADQ_PNODE_SHIFT) |
			      uv_physnodeaddr(pqp));
	uv_write_global_mmr64(pnode, UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL,
			      uv_physnodeaddr(pqp));
	bau_tablesp->va_queue_last = pqp + (DEST_Q_SIZE - 1);
	uv_write_global_mmr64(pnode, UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST,
			      (unsigned long)
			      uv_physnodeaddr(bau_tablesp->va_queue_last));
	memset(pqp, 0, sizeof(struct bau_payload_queue_entry) * DEST_Q_SIZE);

	return pqp;
}

/*
 * Initialization of each UV blade's structures
 */
static int __init uv_init_blade(int blade)
{
	int node;
	int pnode;
	unsigned long pa;
	unsigned long apicid;
	struct bau_desc *adp;
	struct bau_payload_queue_entry *pqp;
	struct bau_control *bau_tablesp;

	node = blade_to_first_node(blade);
	bau_tablesp = uv_table_bases_init(blade, node);
	pnode = uv_blade_to_pnode(blade);
	adp = uv_activation_descriptor_init(node, pnode);
	pqp = uv_payload_queue_init(node, pnode, bau_tablesp);
	uv_table_bases_finish(blade, bau_tablesp, adp);
	/*
	 * the below initialization can't be in firmware because the
	 * messaging IRQ will be determined by the OS
	 */
	apicid = blade_to_first_apicid(blade);
	pa = uv_read_global_mmr64(pnode, UVH_BAU_DATA_CONFIG);
	if ((pa & 0xff) != UV_BAU_MESSAGE) {
		uv_write_global_mmr64(pnode, UVH_BAU_DATA_CONFIG,
				      ((apicid << 32) | UV_BAU_MESSAGE));
	}
	return 0;
}

/*
 * Initialization of BAU-related structures
 */
static int __init uv_bau_init(void)
{
	int blade;
	int nblades;
	int cur_cpu;

	if (!is_uv_system())
		return 0;

	for_each_possible_cpu(cur_cpu)
		zalloc_cpumask_var_node(&per_cpu(uv_flush_tlb_mask, cur_cpu),
				       GFP_KERNEL, cpu_to_node(cur_cpu));

	uv_bau_retry_limit = 1;
	uv_nshift = uv_hub_info->n_val;
	uv_mmask = (1UL << uv_hub_info->n_val) - 1;
	nblades = uv_num_possible_blades();

	uv_bau_table_bases = (struct bau_control **)
	    kmalloc(nblades * sizeof(struct bau_control *), GFP_KERNEL);
	BUG_ON(!uv_bau_table_bases);

	uv_partition_base_pnode = 0x7fffffff;
	for (blade = 0; blade < nblades; blade++)
		if (uv_blade_nr_possible_cpus(blade) &&
			(uv_blade_to_pnode(blade) < uv_partition_base_pnode))
			uv_partition_base_pnode = uv_blade_to_pnode(blade);
	for (blade = 0; blade < nblades; blade++)
		if (uv_blade_nr_possible_cpus(blade))
			uv_init_blade(blade);

	alloc_intr_gate(UV_BAU_MESSAGE, uv_bau_message_intr1);
	uv_enable_timeouts();

	return 0;
}
__initcall(uv_bau_init);
__initcall(uv_ptc_init);
