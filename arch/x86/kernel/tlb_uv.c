/*
 *	SGI UltraViolet TLB flush routines.
 *
 *	(c) 2008 Cliff Wickman <cpw@sgi.com>, SGI.
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 */
#include <linux/mc146818rtc.h>
#include <linux/proc_fs.h>
#include <linux/kernel.h>

#include <asm/mach-bigsmp/mach_apic.h>
#include <asm/mmu_context.h>
#include <asm/idle.h>
#include <asm/genapic.h>
#include <asm/uv/uv_hub.h>
#include <asm/uv/uv_mmrs.h>
#include <asm/uv/uv_bau.h>

struct bau_control **uv_bau_table_bases;
static int uv_bau_retry_limit;
static int uv_nshift;		/* position of pnode (which is nasid>>1) */
static unsigned long uv_mmask;

char *status_table[] = {
	"IDLE",
	"ACTIVE",
	"DESTINATION TIMEOUT",
	"SOURCE TIMEOUT"
};

DEFINE_PER_CPU(struct ptc_stats, ptcstats);
DEFINE_PER_CPU(struct bau_control, bau_control);

/*
 * Free a software acknowledge hardware resource by clearing its Pending
 * bit. This will return a reply to the sender.
 * If the message has timed out, a reply has already been sent by the
 * hardware but the resource has not been released. In that case our
 * clear of the Timeout bit (as well) will free the resource. No reply will
 * be sent (the hardware will only do one reply per message).
 */
static void
uv_reply_to_message(int resource,
		    struct bau_payload_queue_entry *msg,
		    struct bau_msg_status *msp)
{
	int fw;

	fw = (1 << (resource + UV_SW_ACK_NPENDING)) | (1 << resource);
	msg->replied_to = 1;
	msg->sw_ack_vector = 0;
	if (msp)
		msp->seen_by.bits = 0;
	uv_write_local_mmr(UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS, fw);
	return;
}

/*
 * Do all the things a cpu should do for a TLB shootdown message.
 * Other cpu's may come here at the same time for this message.
 */
static void
uv_bau_process_message(struct bau_payload_queue_entry *msg,
		       int msg_slot, int sw_ack_slot)
{
	int cpu;
	unsigned long this_cpu_mask;
	struct bau_msg_status *msp;

	msp = __get_cpu_var(bau_control).msg_statuses + msg_slot;
	cpu = uv_blade_processor_id();
	msg->number_of_cpus =
	    uv_blade_nr_online_cpus(uv_node_to_blade_id(numa_node_id()));
	this_cpu_mask = (unsigned long)1 << cpu;
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
	return;
}

/*
 * Examine the payload queue on all the distribution nodes to see
 * which messages have not been seen, and which cpu(s) have not seen them.
 *
 * Returns the number of cpu's that have not responded.
 */
static int
uv_examine_destinations(struct bau_target_nodemask *distribution)
{
	int sender;
	int i;
	int j;
	int k;
	int count = 0;
	struct bau_control *bau_tablesp;
	struct bau_payload_queue_entry *msg;
	struct bau_msg_status *msp;

	sender = smp_processor_id();
	for (i = 0; i < (sizeof(struct bau_target_nodemask) * BITSPERBYTE);
	     i++) {
		if (bau_node_isset(i, distribution)) {
			bau_tablesp = uv_bau_table_bases[i];
			for (msg = bau_tablesp->va_queue_first, j = 0;
			     j < DESTINATION_PAYLOAD_QUEUE_SIZE; msg++, j++) {
				if ((msg->sending_cpu == sender) &&
				    (!msg->replied_to)) {
					msp = bau_tablesp->msg_statuses + j;
					printk(KERN_DEBUG
				"blade %d: address:%#lx %d of %d, not cpu(s): ",
					       i, msg->address,
					       msg->acknowledge_count,
					       msg->number_of_cpus);
					for (k = 0; k < msg->number_of_cpus;
					     k++) {
						if (!((long)1 << k & msp->
						      seen_by.bits)) {
							count++;
							printk("%d ", k);
						}
					}
					printk("\n");
				}
			}
		}
	}
	return count;
}

/**
 * uv_flush_tlb_others - globally purge translation cache of a virtual
 * address or all TLB's
 * @cpumaskp: mask of all cpu's in which the address is to be removed
 * @mm: mm_struct containing virtual address range
 * @va: virtual address to be removed (or TLB_FLUSH_ALL for all TLB's on cpu)
 *
 * This is the entry point for initiating any UV global TLB shootdown.
 *
 * Purges the translation caches of all specified processors of the given
 * virtual address, or purges all TLB's on specified processors.
 *
 * The caller has derived the cpumaskp from the mm_struct and has subtracted
 * the local cpu from the mask.  This function is called only if there
 * are bits set in the mask. (e.g. flush_tlb_page())
 *
 * The cpumaskp is converted into a nodemask of the nodes containing
 * the cpus.
 */
int
uv_flush_tlb_others(cpumask_t *cpumaskp, struct mm_struct *mm, unsigned long va)
{
	int i;
	int blade;
	int cpu;
	int bit;
	int right_shift;
	int this_blade;
	int exams = 0;
	int tries = 0;
	long source_timeouts = 0;
	long destination_timeouts = 0;
	unsigned long index;
	unsigned long mmr_offset;
	unsigned long descriptor_status;
	struct bau_activation_descriptor *bau_desc;
	ktime_t time1, time2;

	cpu = uv_blade_processor_id();
	this_blade = uv_numa_blade_id();
	bau_desc = __get_cpu_var(bau_control).descriptor_base;
	bau_desc += (UV_ITEMS_PER_DESCRIPTOR * cpu);

	bau_nodes_clear(&bau_desc->distribution, UV_DISTRIBUTION_SIZE);

	i = 0;
	for_each_cpu_mask(bit, *cpumaskp) {
		blade = uv_cpu_to_blade_id(bit);
		if (blade > (UV_DISTRIBUTION_SIZE - 1))
			BUG();
		if (blade == this_blade)
			continue;
		bau_node_set(blade, &bau_desc->distribution);
		/* leave the bits for the remote cpu's in the mask until
		   success; on failure we fall back to the IPI method */
		i++;
	}
	if (i == 0)
		goto none_to_flush;
	__get_cpu_var(ptcstats).requestor++;
	__get_cpu_var(ptcstats).ntargeted += i;

	bau_desc->payload.address = va;
	bau_desc->payload.sending_cpu = smp_processor_id();

	if (cpu < UV_CPUS_PER_ACT_STATUS) {
		mmr_offset = UVH_LB_BAU_SB_ACTIVATION_STATUS_0;
		right_shift = cpu * UV_ACT_STATUS_SIZE;
	} else {
		mmr_offset = UVH_LB_BAU_SB_ACTIVATION_STATUS_1;
		right_shift =
		    ((cpu - UV_CPUS_PER_ACT_STATUS) * UV_ACT_STATUS_SIZE);
	}
	time1 = ktime_get();

retry:
	tries++;
	index = ((unsigned long)
		 1 << UVH_LB_BAU_SB_ACTIVATION_CONTROL_PUSH_SHFT) | cpu;
	uv_write_local_mmr(UVH_LB_BAU_SB_ACTIVATION_CONTROL, index);

	while ((descriptor_status = (((unsigned long)
				      uv_read_local_mmr(mmr_offset) >>
				      right_shift) & UV_ACT_STATUS_MASK)) !=
	       DESC_STATUS_IDLE) {
		if (descriptor_status == DESC_STATUS_SOURCE_TIMEOUT) {
			source_timeouts++;
			if (source_timeouts > SOURCE_TIMEOUT_LIMIT)
				source_timeouts = 0;
			__get_cpu_var(ptcstats).s_retry++;
			goto retry;
		}
		/* spin here looking for progress at the destinations */
		if (descriptor_status == DESC_STATUS_DESTINATION_TIMEOUT) {
			destination_timeouts++;
			if (destination_timeouts > DESTINATION_TIMEOUT_LIMIT) {
				/* returns # of cpus not responding */
				if (uv_examine_destinations
				    (&bau_desc->distribution) == 0) {
					__get_cpu_var(ptcstats).d_retry++;
					goto retry;
				}
				exams++;
				if (exams >= uv_bau_retry_limit) {
					printk(KERN_DEBUG
					       "uv_flush_tlb_others");
					printk("giving up on cpu %d\n",
					       smp_processor_id());
					goto unsuccessful;
				}
				/* delays can hang up the simulator
				   udelay(1000);
				 */
				destination_timeouts = 0;
			}
		}
	}
	if (tries > 1)
		__get_cpu_var(ptcstats).retriesok++;
	/* on success, clear the remote cpu's from the mask so we don't
	   use the IPI method of shootdown on them */
	for_each_cpu_mask(bit, *cpumaskp) {
		blade = uv_cpu_to_blade_id(bit);
		if (blade == this_blade)
			continue;
		cpu_clear(bit, *cpumaskp);
	}

unsuccessful:
	time2 = ktime_get();
	__get_cpu_var(ptcstats).sflush_ns += (time2.tv64 - time1.tv64);

none_to_flush:
	if (cpus_empty(*cpumaskp))
		return 1;

	/* Cause the caller to do an IPI-style TLB shootdown on
	   the cpu's still in the mask */
	__get_cpu_var(ptcstats).ptc_i++;
	return 0;
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
void
uv_bau_message_interrupt(struct pt_regs *regs)
{
	struct bau_payload_queue_entry *pqp;
	struct bau_payload_queue_entry *msg;
	struct pt_regs *old_regs = set_irq_regs(regs);
	ktime_t time1, time2;
	int msg_slot;
	int sw_ack_slot;
	int fw;
	int count = 0;
	unsigned long local_pnode;

	ack_APIC_irq();
	exit_idle();
	irq_enter();

	time1 = ktime_get();

	local_pnode = uv_blade_to_pnode(uv_numa_blade_id());

	pqp = __get_cpu_var(bau_control).va_queue_first;
	msg = __get_cpu_var(bau_control).bau_msg_head;
	while (msg->sw_ack_vector) {
		count++;
		fw = msg->sw_ack_vector;
		msg_slot = msg - pqp;
		sw_ack_slot = ffs(fw) - 1;

		uv_bau_process_message(msg, msg_slot, sw_ack_slot);

		msg++;
		if (msg > __get_cpu_var(bau_control).va_queue_last)
			msg = __get_cpu_var(bau_control).va_queue_first;
		__get_cpu_var(bau_control).bau_msg_head = msg;
	}
	if (!count)
		__get_cpu_var(ptcstats).nomsg++;
	else if (count > 1)
		__get_cpu_var(ptcstats).multmsg++;

	time2 = ktime_get();
	__get_cpu_var(ptcstats).dflush_ns += (time2.tv64 - time1.tv64);

	irq_exit();
	set_irq_regs(old_regs);
	return;
}

static void
uv_enable_timeouts(void)
{
	int i;
	int blade;
	int last_blade;
	int pnode;
	int cur_cpu = 0;
	unsigned long apicid;

	/* better if we had each_online_blade */
	last_blade = -1;
	for_each_online_node(i) {
		blade = uv_node_to_blade_id(i);
		if (blade == last_blade)
			continue;
		last_blade = blade;
		apicid = per_cpu(x86_cpu_to_apicid, cur_cpu);
		pnode = uv_blade_to_pnode(blade);
		cur_cpu += uv_blade_nr_possible_cpus(i);
	}
	return;
}

static void *
uv_ptc_seq_start(struct seq_file *file, loff_t *offset)
{
	if (*offset < num_possible_cpus())
		return offset;
	return NULL;
}

static void *
uv_ptc_seq_next(struct seq_file *file, void *data, loff_t *offset)
{
	(*offset)++;
	if (*offset < num_possible_cpus())
		return offset;
	return NULL;
}

static void
uv_ptc_seq_stop(struct seq_file *file, void *data)
{
}

/*
 * Display the statistics thru /proc
 * data points to the cpu number
 */
static int
uv_ptc_seq_show(struct seq_file *file, void *data)
{
	struct ptc_stats *stat;
	int cpu;

	cpu = *(loff_t *)data;

	if (!cpu) {
		seq_printf(file,
		"# cpu requestor requestee one all sretry dretry ptc_i ");
		seq_printf(file,
		"sw_ack sflush_us dflush_us sok dnomsg dmult starget\n");
	}
	if (cpu < num_possible_cpus() && cpu_online(cpu)) {
		stat = &per_cpu(ptcstats, cpu);
		seq_printf(file, "cpu %d %ld %ld %ld %ld %ld %ld %ld ",
			   cpu, stat->requestor,
			   stat->requestee, stat->onetlb, stat->alltlb,
			   stat->s_retry, stat->d_retry, stat->ptc_i);
		seq_printf(file, "%lx %ld %ld %ld %ld %ld %ld\n",
			   uv_read_global_mmr64(uv_blade_to_pnode
					(uv_cpu_to_blade_id(cpu)),
					UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE),
			   stat->sflush_ns / 1000, stat->dflush_ns / 1000,
			   stat->retriesok, stat->nomsg,
			   stat->multmsg, stat->ntargeted);
	}

	return 0;
}

/*
 *  0: display meaning of the statistics
 * >0: retry limit
 */
static ssize_t
uv_ptc_proc_write(struct file *file, const char __user *user,
		  size_t count, loff_t *data)
{
	long newmode;
	char optstr[64];

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
		"sflush_us:  microseconds spent in uv_flush_tlb_others()\n");
		printk(KERN_DEBUG
		"dflush_us:  microseconds spent in handling flush requests\n");
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
	.start = uv_ptc_seq_start,
	.next = uv_ptc_seq_next,
	.stop = uv_ptc_seq_stop,
	.show = uv_ptc_seq_show
};

static int
uv_ptc_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &uv_ptc_seq_ops);
}

static const struct file_operations proc_uv_ptc_operations = {
	.open = uv_ptc_proc_open,
	.read = seq_read,
	.write = uv_ptc_proc_write,
	.llseek = seq_lseek,
	.release = seq_release,
};

static struct proc_dir_entry *proc_uv_ptc;

static int __init
uv_ptc_init(void)
{
	static struct proc_dir_entry *sgi_proc_dir;

	sgi_proc_dir = NULL;

	if (!is_uv_system())
		return 0;

	sgi_proc_dir = proc_mkdir("sgi_uv", NULL);
	if (!sgi_proc_dir)
		return -EINVAL;

	proc_uv_ptc = create_proc_entry(UV_PTC_BASENAME, 0444, NULL);
	if (!proc_uv_ptc) {
		printk(KERN_ERR "unable to create %s proc entry\n",
		       UV_PTC_BASENAME);
		return -EINVAL;
	}
	proc_uv_ptc->proc_fops = &proc_uv_ptc_operations;
	return 0;
}

static void __exit
uv_ptc_exit(void)
{
	remove_proc_entry(UV_PTC_BASENAME, NULL);
}

module_init(uv_ptc_init);
module_exit(uv_ptc_exit);

/*
 * Initialization of BAU-related structures
 */
int __init
uv_bau_init(void)
{
	int i;
	int j;
	int blade;
	int nblades;
	int *ip;
	int pnode;
	int last_blade;
	int cur_cpu = 0;
	unsigned long pa;
	unsigned long n;
	unsigned long m;
	unsigned long mmr_image;
	unsigned long apicid;
	char *cp;
	struct bau_control *bau_tablesp;
	struct bau_activation_descriptor *adp, *ad2;
	struct bau_payload_queue_entry *pqp;
	struct bau_msg_status *msp;
	struct bau_control *bcp;

	if (!is_uv_system())
		return 0;

	uv_bau_retry_limit = 1;

	if ((sizeof(struct bau_local_cpumask) * BITSPERBYTE) <
	    MAX_CPUS_PER_NODE) {
		printk(KERN_ERR
			"uv_bau_init: bau_local_cpumask.bits too small\n");
		BUG();
	}

	uv_nshift = uv_hub_info->n_val;
	uv_mmask = ((unsigned long)1 << uv_hub_info->n_val) - 1;
	nblades = 0;
	last_blade = -1;
	for_each_online_node(i) {
		blade = uv_node_to_blade_id(i);
		if (blade == last_blade)
			continue;
		last_blade = blade;
		nblades++;
	}

	uv_bau_table_bases = (struct bau_control **)
	    kmalloc(nblades * sizeof(struct bau_control *), GFP_KERNEL);
	if (!uv_bau_table_bases)
		BUG();

	/* better if we had each_online_blade */
	last_blade = -1;
	for_each_online_node(i) {
		blade = uv_node_to_blade_id(i);
		if (blade == last_blade)
			continue;
		last_blade = blade;

		bau_tablesp =
		    kmalloc_node(sizeof(struct bau_control), GFP_KERNEL, i);
		if (!bau_tablesp)
			BUG();

		bau_tablesp->msg_statuses =
		    kmalloc_node(sizeof(struct bau_msg_status) *
				 DESTINATION_PAYLOAD_QUEUE_SIZE, GFP_KERNEL, i);
		if (!bau_tablesp->msg_statuses)
			BUG();
		for (j = 0, msp = bau_tablesp->msg_statuses;
		     j < DESTINATION_PAYLOAD_QUEUE_SIZE; j++, msp++) {
			bau_cpubits_clear(&msp->seen_by, (int)
					  uv_blade_nr_possible_cpus(blade));
		}

		bau_tablesp->watching =
		    kmalloc_node(sizeof(int) * DESTINATION_NUM_RESOURCES,
				 GFP_KERNEL, i);
		if (!bau_tablesp->watching)
			BUG();
		for (j = 0, ip = bau_tablesp->watching;
		     j < DESTINATION_PAYLOAD_QUEUE_SIZE; j++, ip++) {
			*ip = 0;
		}

		uv_bau_table_bases[i] = bau_tablesp;

		pnode = uv_blade_to_pnode(blade);

		if (sizeof(struct bau_activation_descriptor) != 64)
			BUG();

		adp = (struct bau_activation_descriptor *)
		    kmalloc_node(16384, GFP_KERNEL, i);
		if (!adp)
			BUG();
		if ((unsigned long)adp & 0xfff)
			BUG();
		pa = __pa((unsigned long)adp);
		n = pa >> uv_nshift;
		m = pa & uv_mmask;

		mmr_image = uv_read_global_mmr64(pnode,
						 UVH_LB_BAU_SB_DESCRIPTOR_BASE);
		if (mmr_image)
			uv_write_global_mmr64(pnode, (unsigned long)
					      UVH_LB_BAU_SB_DESCRIPTOR_BASE,
					      (n << UV_DESC_BASE_PNODE_SHIFT |
					       m));
		for (j = 0, ad2 = adp; j < UV_ACTIVATION_DESCRIPTOR_SIZE;
		     j++, ad2++) {
			memset(ad2, 0,
			       sizeof(struct bau_activation_descriptor));
			ad2->header.sw_ack_flag = 1;
			ad2->header.base_dest_nodeid =
			    uv_blade_to_pnode(uv_cpu_to_blade_id(0));
			ad2->header.command = UV_NET_ENDPOINT_INTD;
			ad2->header.int_both = 1;
			/* all others need to be set to zero:
			   fairness chaining multilevel count replied_to */
		}

		pqp = (struct bau_payload_queue_entry *)
		    kmalloc_node((DESTINATION_PAYLOAD_QUEUE_SIZE + 1) *
				 sizeof(struct bau_payload_queue_entry),
				 GFP_KERNEL, i);
		if (!pqp)
			BUG();
		if (sizeof(struct bau_payload_queue_entry) != 32)
			BUG();
		if ((unsigned long)(&((struct bau_payload_queue_entry *)0)->
				    sw_ack_vector) != 15)
			BUG();

		cp = (char *)pqp + 31;
		pqp = (struct bau_payload_queue_entry *)
		    (((unsigned long)cp >> 5) << 5);
		bau_tablesp->va_queue_first = pqp;
		uv_write_global_mmr64(pnode,
				      UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST,
				      ((unsigned long)pnode <<
				       UV_PAYLOADQ_PNODE_SHIFT) |
				      uv_physnodeaddr(pqp));
		uv_write_global_mmr64(pnode, UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL,
				      uv_physnodeaddr(pqp));
		bau_tablesp->va_queue_last =
		    pqp + (DESTINATION_PAYLOAD_QUEUE_SIZE - 1);
		uv_write_global_mmr64(pnode, UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST,
				      (unsigned long)
				      uv_physnodeaddr(bau_tablesp->
						      va_queue_last));
		memset(pqp, 0, sizeof(struct bau_payload_queue_entry) *
		       DESTINATION_PAYLOAD_QUEUE_SIZE);

		/* this initialization can't be in firmware because the
		   messaging IRQ will be determined by the OS */
		apicid = per_cpu(x86_cpu_to_apicid, cur_cpu);
		pa = uv_read_global_mmr64(pnode, UVH_BAU_DATA_CONFIG);
		if ((pa & 0xff) != UV_BAU_MESSAGE) {
			uv_write_global_mmr64(pnode, UVH_BAU_DATA_CONFIG,
					      ((apicid << 32) |
					       UV_BAU_MESSAGE));
		}

		for (j = cur_cpu; j < (cur_cpu + uv_blade_nr_possible_cpus(i));
		     j++) {
			bcp = (struct bau_control *)&per_cpu(bau_control, j);
			bcp->bau_msg_head = bau_tablesp->va_queue_first;
			bcp->va_queue_first = bau_tablesp->va_queue_first;

			bcp->va_queue_last = bau_tablesp->va_queue_last;
			bcp->watching = bau_tablesp->watching;
			bcp->msg_statuses = bau_tablesp->msg_statuses;
			bcp->descriptor_base = adp;
		}
		cur_cpu += uv_blade_nr_possible_cpus(i);
	}

	set_intr_gate(UV_BAU_MESSAGE, uv_bau_message_intr1);

	uv_enable_timeouts();

	return 0;
}

__initcall(uv_bau_init);
