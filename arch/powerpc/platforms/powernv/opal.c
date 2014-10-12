/*
 * PowerNV OPAL high level interfaces
 *
 * Copyright 2011 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kobject.h>
#include <linux/delay.h>
#include <linux/memblock.h>

#include <asm/machdep.h>
#include <asm/opal.h>
#include <asm/firmware.h>
#include <asm/mce.h>

#include "powernv.h"

/* /sys/firmware/opal */
struct kobject *opal_kobj;

struct opal {
	u64 base;
	u64 entry;
	u64 size;
} opal;

struct mcheck_recoverable_range {
	u64 start_addr;
	u64 end_addr;
	u64 recover_addr;
};

static struct mcheck_recoverable_range *mc_recoverable_range;
static int mc_recoverable_range_len;

struct device_node *opal_node;
static DEFINE_SPINLOCK(opal_write_lock);
extern u64 opal_mc_secondary_handler[];
static unsigned int *opal_irqs;
static unsigned int opal_irq_count;
static ATOMIC_NOTIFIER_HEAD(opal_notifier_head);
static struct atomic_notifier_head opal_msg_notifier_head[OPAL_MSG_TYPE_MAX];
static DEFINE_SPINLOCK(opal_notifier_lock);
static uint64_t last_notified_mask = 0x0ul;
static atomic_t opal_notifier_hold = ATOMIC_INIT(0);

static void opal_reinit_cores(void)
{
	/* Do the actual re-init, This will clobber all FPRs, VRs, etc...
	 *
	 * It will preserve non volatile GPRs and HSPRG0/1. It will
	 * also restore HIDs and other SPRs to their original value
	 * but it might clobber a bunch.
	 */
#ifdef __BIG_ENDIAN__
	opal_reinit_cpus(OPAL_REINIT_CPUS_HILE_BE);
#else
	opal_reinit_cpus(OPAL_REINIT_CPUS_HILE_LE);
#endif
}

int __init early_init_dt_scan_opal(unsigned long node,
				   const char *uname, int depth, void *data)
{
	const void *basep, *entryp, *sizep;
	int basesz, entrysz, runtimesz;

	if (depth != 1 || strcmp(uname, "ibm,opal") != 0)
		return 0;

	basep  = of_get_flat_dt_prop(node, "opal-base-address", &basesz);
	entryp = of_get_flat_dt_prop(node, "opal-entry-address", &entrysz);
	sizep = of_get_flat_dt_prop(node, "opal-runtime-size", &runtimesz);

	if (!basep || !entryp || !sizep)
		return 1;

	opal.base = of_read_number(basep, basesz/4);
	opal.entry = of_read_number(entryp, entrysz/4);
	opal.size = of_read_number(sizep, runtimesz/4);

	pr_debug("OPAL Base  = 0x%llx (basep=%p basesz=%d)\n",
		 opal.base, basep, basesz);
	pr_debug("OPAL Entry = 0x%llx (entryp=%p basesz=%d)\n",
		 opal.entry, entryp, entrysz);
	pr_debug("OPAL Entry = 0x%llx (sizep=%p runtimesz=%d)\n",
		 opal.size, sizep, runtimesz);

	powerpc_firmware_features |= FW_FEATURE_OPAL;
	if (of_flat_dt_is_compatible(node, "ibm,opal-v3")) {
		powerpc_firmware_features |= FW_FEATURE_OPALv2;
		powerpc_firmware_features |= FW_FEATURE_OPALv3;
		pr_info("OPAL V3 detected !\n");
	} else if (of_flat_dt_is_compatible(node, "ibm,opal-v2")) {
		powerpc_firmware_features |= FW_FEATURE_OPALv2;
		pr_info("OPAL V2 detected !\n");
	} else {
		pr_info("OPAL V1 detected !\n");
	}

	/* Reinit all cores with the right endian */
	opal_reinit_cores();

	/* Restore some bits */
	if (cur_cpu_spec->cpu_restore)
		cur_cpu_spec->cpu_restore();

	return 1;
}

int __init early_init_dt_scan_recoverable_ranges(unsigned long node,
				   const char *uname, int depth, void *data)
{
	int i, psize, size;
	const __be32 *prop;

	if (depth != 1 || strcmp(uname, "ibm,opal") != 0)
		return 0;

	prop = of_get_flat_dt_prop(node, "mcheck-recoverable-ranges", &psize);

	if (!prop)
		return 1;

	pr_debug("Found machine check recoverable ranges.\n");

	/*
	 * Calculate number of available entries.
	 *
	 * Each recoverable address range entry is (start address, len,
	 * recovery address), 2 cells each for start and recovery address,
	 * 1 cell for len, totalling 5 cells per entry.
	 */
	mc_recoverable_range_len = psize / (sizeof(*prop) * 5);

	/* Sanity check */
	if (!mc_recoverable_range_len)
		return 1;

	/* Size required to hold all the entries. */
	size = mc_recoverable_range_len *
			sizeof(struct mcheck_recoverable_range);

	/*
	 * Allocate a buffer to hold the MC recoverable ranges. We would be
	 * accessing them in real mode, hence it needs to be within
	 * RMO region.
	 */
	mc_recoverable_range =__va(memblock_alloc_base(size, __alignof__(u64),
							ppc64_rma_size));
	memset(mc_recoverable_range, 0, size);

	for (i = 0; i < mc_recoverable_range_len; i++) {
		mc_recoverable_range[i].start_addr =
					of_read_number(prop + (i * 5) + 0, 2);
		mc_recoverable_range[i].end_addr =
					mc_recoverable_range[i].start_addr +
					of_read_number(prop + (i * 5) + 2, 1);
		mc_recoverable_range[i].recover_addr =
					of_read_number(prop + (i * 5) + 3, 2);

		pr_debug("Machine check recoverable range: %llx..%llx: %llx\n",
				mc_recoverable_range[i].start_addr,
				mc_recoverable_range[i].end_addr,
				mc_recoverable_range[i].recover_addr);
	}
	return 1;
}

static int __init opal_register_exception_handlers(void)
{
#ifdef __BIG_ENDIAN__
	u64 glue;

	if (!(powerpc_firmware_features & FW_FEATURE_OPAL))
		return -ENODEV;

	/* Hookup some exception handlers except machine check. We use the
	 * fwnmi area at 0x7000 to provide the glue space to OPAL
	 */
	glue = 0x7000;
	opal_register_exception_handler(OPAL_SOFTPATCH_HANDLER, 0, glue);
#endif

	return 0;
}
machine_early_initcall(powernv, opal_register_exception_handlers);

int opal_notifier_register(struct notifier_block *nb)
{
	if (!nb) {
		pr_warning("%s: Invalid argument (%p)\n",
			   __func__, nb);
		return -EINVAL;
	}

	atomic_notifier_chain_register(&opal_notifier_head, nb);
	return 0;
}
EXPORT_SYMBOL_GPL(opal_notifier_register);

int opal_notifier_unregister(struct notifier_block *nb)
{
	if (!nb) {
		pr_warning("%s: Invalid argument (%p)\n",
			   __func__, nb);
		return -EINVAL;
	}

	atomic_notifier_chain_unregister(&opal_notifier_head, nb);
	return 0;
}
EXPORT_SYMBOL_GPL(opal_notifier_unregister);

static void opal_do_notifier(uint64_t events)
{
	unsigned long flags;
	uint64_t changed_mask;

	if (atomic_read(&opal_notifier_hold))
		return;

	spin_lock_irqsave(&opal_notifier_lock, flags);
	changed_mask = last_notified_mask ^ events;
	last_notified_mask = events;
	spin_unlock_irqrestore(&opal_notifier_lock, flags);

	/*
	 * We feed with the event bits and changed bits for
	 * enough information to the callback.
	 */
	atomic_notifier_call_chain(&opal_notifier_head,
				   events, (void *)changed_mask);
}

void opal_notifier_update_evt(uint64_t evt_mask,
			      uint64_t evt_val)
{
	unsigned long flags;

	spin_lock_irqsave(&opal_notifier_lock, flags);
	last_notified_mask &= ~evt_mask;
	last_notified_mask |= evt_val;
	spin_unlock_irqrestore(&opal_notifier_lock, flags);
}

void opal_notifier_enable(void)
{
	int64_t rc;
	__be64 evt = 0;

	atomic_set(&opal_notifier_hold, 0);

	/* Process pending events */
	rc = opal_poll_events(&evt);
	if (rc == OPAL_SUCCESS && evt)
		opal_do_notifier(be64_to_cpu(evt));
}

void opal_notifier_disable(void)
{
	atomic_set(&opal_notifier_hold, 1);
}

/*
 * Opal message notifier based on message type. Allow subscribers to get
 * notified for specific messgae type.
 */
int opal_message_notifier_register(enum OpalMessageType msg_type,
					struct notifier_block *nb)
{
	if (!nb) {
		pr_warning("%s: Invalid argument (%p)\n",
			   __func__, nb);
		return -EINVAL;
	}
	if (msg_type > OPAL_MSG_TYPE_MAX) {
		pr_warning("%s: Invalid message type argument (%d)\n",
			   __func__, msg_type);
		return -EINVAL;
	}
	return atomic_notifier_chain_register(
				&opal_msg_notifier_head[msg_type], nb);
}

static void opal_message_do_notify(uint32_t msg_type, void *msg)
{
	/* notify subscribers */
	atomic_notifier_call_chain(&opal_msg_notifier_head[msg_type],
					msg_type, msg);
}

static void opal_handle_message(void)
{
	s64 ret;
	/*
	 * TODO: pre-allocate a message buffer depending on opal-msg-size
	 * value in /proc/device-tree.
	 */
	static struct opal_msg msg;
	u32 type;

	ret = opal_get_msg(__pa(&msg), sizeof(msg));
	/* No opal message pending. */
	if (ret == OPAL_RESOURCE)
		return;

	/* check for errors. */
	if (ret) {
		pr_warning("%s: Failed to retrieve opal message, err=%lld\n",
				__func__, ret);
		return;
	}

	type = be32_to_cpu(msg.msg_type);

	/* Sanity check */
	if (type > OPAL_MSG_TYPE_MAX) {
		pr_warning("%s: Unknown message type: %u\n", __func__, type);
		return;
	}
	opal_message_do_notify(type, (void *)&msg);
}

static int opal_message_notify(struct notifier_block *nb,
			  unsigned long events, void *change)
{
	if (events & OPAL_EVENT_MSG_PENDING)
		opal_handle_message();
	return 0;
}

static struct notifier_block opal_message_nb = {
	.notifier_call	= opal_message_notify,
	.next		= NULL,
	.priority	= 0,
};

static int __init opal_message_init(void)
{
	int ret, i;

	for (i = 0; i < OPAL_MSG_TYPE_MAX; i++)
		ATOMIC_INIT_NOTIFIER_HEAD(&opal_msg_notifier_head[i]);

	ret = opal_notifier_register(&opal_message_nb);
	if (ret) {
		pr_err("%s: Can't register OPAL event notifier (%d)\n",
		       __func__, ret);
		return ret;
	}
	return 0;
}
machine_early_initcall(powernv, opal_message_init);

int opal_get_chars(uint32_t vtermno, char *buf, int count)
{
	s64 rc;
	__be64 evt, len;

	if (!opal.entry)
		return -ENODEV;
	opal_poll_events(&evt);
	if ((be64_to_cpu(evt) & OPAL_EVENT_CONSOLE_INPUT) == 0)
		return 0;
	len = cpu_to_be64(count);
	rc = opal_console_read(vtermno, &len, buf);
	if (rc == OPAL_SUCCESS)
		return be64_to_cpu(len);
	return 0;
}

int opal_put_chars(uint32_t vtermno, const char *data, int total_len)
{
	int written = 0;
	__be64 olen;
	s64 len, rc;
	unsigned long flags;
	__be64 evt;

	if (!opal.entry)
		return -ENODEV;

	/* We want put_chars to be atomic to avoid mangling of hvsi
	 * packets. To do that, we first test for room and return
	 * -EAGAIN if there isn't enough.
	 *
	 * Unfortunately, opal_console_write_buffer_space() doesn't
	 * appear to work on opal v1, so we just assume there is
	 * enough room and be done with it
	 */
	spin_lock_irqsave(&opal_write_lock, flags);
	if (firmware_has_feature(FW_FEATURE_OPALv2)) {
		rc = opal_console_write_buffer_space(vtermno, &olen);
		len = be64_to_cpu(olen);
		if (rc || len < total_len) {
			spin_unlock_irqrestore(&opal_write_lock, flags);
			/* Closed -> drop characters */
			if (rc)
				return total_len;
			opal_poll_events(NULL);
			return -EAGAIN;
		}
	}

	/* We still try to handle partial completions, though they
	 * should no longer happen.
	 */
	rc = OPAL_BUSY;
	while(total_len > 0 && (rc == OPAL_BUSY ||
				rc == OPAL_BUSY_EVENT || rc == OPAL_SUCCESS)) {
		olen = cpu_to_be64(total_len);
		rc = opal_console_write(vtermno, &olen, data);
		len = be64_to_cpu(olen);

		/* Closed or other error drop */
		if (rc != OPAL_SUCCESS && rc != OPAL_BUSY &&
		    rc != OPAL_BUSY_EVENT) {
			written = total_len;
			break;
		}
		if (rc == OPAL_SUCCESS) {
			total_len -= len;
			data += len;
			written += len;
		}
		/* This is a bit nasty but we need that for the console to
		 * flush when there aren't any interrupts. We will clean
		 * things a bit later to limit that to synchronous path
		 * such as the kernel console and xmon/udbg
		 */
		do
			opal_poll_events(&evt);
		while(rc == OPAL_SUCCESS &&
			(be64_to_cpu(evt) & OPAL_EVENT_CONSOLE_OUTPUT));
	}
	spin_unlock_irqrestore(&opal_write_lock, flags);
	return written;
}

static int opal_recover_mce(struct pt_regs *regs,
					struct machine_check_event *evt)
{
	int recovered = 0;
	uint64_t ea = get_mce_fault_addr(evt);

	if (!(regs->msr & MSR_RI)) {
		/* If MSR_RI isn't set, we cannot recover */
		recovered = 0;
	} else if (evt->disposition == MCE_DISPOSITION_RECOVERED) {
		/* Platform corrected itself */
		recovered = 1;
	} else if (ea && !is_kernel_addr(ea)) {
		/*
		 * Faulting address is not in kernel text. We should be fine.
		 * We need to find which process uses this address.
		 * For now, kill the task if we have received exception when
		 * in userspace.
		 *
		 * TODO: Queue up this address for hwpoisioning later.
		 */
		if (user_mode(regs) && !is_global_init(current)) {
			_exception(SIGBUS, regs, BUS_MCEERR_AR, regs->nip);
			recovered = 1;
		} else
			recovered = 0;
	} else if (user_mode(regs) && !is_global_init(current) &&
		evt->severity == MCE_SEV_ERROR_SYNC) {
		/*
		 * If we have received a synchronous error when in userspace
		 * kill the task.
		 */
		_exception(SIGBUS, regs, BUS_MCEERR_AR, regs->nip);
		recovered = 1;
	}
	return recovered;
}

int opal_machine_check(struct pt_regs *regs)
{
	struct machine_check_event evt;

	if (!get_mce_event(&evt, MCE_EVENT_RELEASE))
		return 0;

	/* Print things out */
	if (evt.version != MCE_V1) {
		pr_err("Machine Check Exception, Unknown event version %d !\n",
		       evt.version);
		return 0;
	}
	machine_check_print_event_info(&evt);

	if (opal_recover_mce(regs, &evt))
		return 1;
	return 0;
}

/* Early hmi handler called in real mode. */
int opal_hmi_exception_early(struct pt_regs *regs)
{
	s64 rc;

	/*
	 * call opal hmi handler. Pass paca address as token.
	 * The return value OPAL_SUCCESS is an indication that there is
	 * an HMI event generated waiting to pull by Linux.
	 */
	rc = opal_handle_hmi();
	if (rc == OPAL_SUCCESS) {
		local_paca->hmi_event_available = 1;
		return 1;
	}
	return 0;
}

/* HMI exception handler called in virtual mode during check_irq_replay. */
int opal_handle_hmi_exception(struct pt_regs *regs)
{
	s64 rc;
	__be64 evt = 0;

	/*
	 * Check if HMI event is available.
	 * if Yes, then call opal_poll_events to pull opal messages and
	 * process them.
	 */
	if (!local_paca->hmi_event_available)
		return 0;

	local_paca->hmi_event_available = 0;
	rc = opal_poll_events(&evt);
	if (rc == OPAL_SUCCESS && evt)
		opal_do_notifier(be64_to_cpu(evt));

	return 1;
}

static uint64_t find_recovery_address(uint64_t nip)
{
	int i;

	for (i = 0; i < mc_recoverable_range_len; i++)
		if ((nip >= mc_recoverable_range[i].start_addr) &&
		    (nip < mc_recoverable_range[i].end_addr))
		    return mc_recoverable_range[i].recover_addr;
	return 0;
}

bool opal_mce_check_early_recovery(struct pt_regs *regs)
{
	uint64_t recover_addr = 0;

	if (!opal.base || !opal.size)
		goto out;

	if ((regs->nip >= opal.base) &&
			(regs->nip <= (opal.base + opal.size)))
		recover_addr = find_recovery_address(regs->nip);

	/*
	 * Setup regs->nip to rfi into fixup address.
	 */
	if (recover_addr)
		regs->nip = recover_addr;

out:
	return !!recover_addr;
}

static irqreturn_t opal_interrupt(int irq, void *data)
{
	__be64 events;

	opal_handle_interrupt(virq_to_hw(irq), &events);

	opal_do_notifier(be64_to_cpu(events));

	return IRQ_HANDLED;
}

static int opal_sysfs_init(void)
{
	opal_kobj = kobject_create_and_add("opal", firmware_kobj);
	if (!opal_kobj) {
		pr_warn("kobject_create_and_add opal failed\n");
		return -ENOMEM;
	}

	return 0;
}

static void __init opal_dump_region_init(void)
{
	void *addr;
	uint64_t size;
	int rc;

	/* Register kernel log buffer */
	addr = log_buf_addr_get();
	size = log_buf_len_get();
	rc = opal_register_dump_region(OPAL_DUMP_REGION_LOG_BUF,
				       __pa(addr), size);
	/* Don't warn if this is just an older OPAL that doesn't
	 * know about that call
	 */
	if (rc && rc != OPAL_UNSUPPORTED)
		pr_warn("DUMP: Failed to register kernel log buffer. "
			"rc = %d\n", rc);
}
static int __init opal_init(void)
{
	struct device_node *np, *consoles;
	const __be32 *irqs;
	int rc, i, irqlen;

	opal_node = of_find_node_by_path("/ibm,opal");
	if (!opal_node) {
		pr_warn("opal: Node not found\n");
		return -ENODEV;
	}

	/* Register OPAL consoles if any ports */
	if (firmware_has_feature(FW_FEATURE_OPALv2))
		consoles = of_find_node_by_path("/ibm,opal/consoles");
	else
		consoles = of_node_get(opal_node);
	if (consoles) {
		for_each_child_of_node(consoles, np) {
			if (strcmp(np->name, "serial"))
				continue;
			of_platform_device_create(np, NULL, NULL);
		}
		of_node_put(consoles);
	}

	/* Find all OPAL interrupts and request them */
	irqs = of_get_property(opal_node, "opal-interrupts", &irqlen);
	pr_debug("opal: Found %d interrupts reserved for OPAL\n",
		 irqs ? (irqlen / 4) : 0);
	opal_irq_count = irqlen / 4;
	opal_irqs = kzalloc(opal_irq_count * sizeof(unsigned int), GFP_KERNEL);
	for (i = 0; irqs && i < (irqlen / 4); i++, irqs++) {
		unsigned int hwirq = be32_to_cpup(irqs);
		unsigned int irq = irq_create_mapping(NULL, hwirq);
		if (irq == NO_IRQ) {
			pr_warning("opal: Failed to map irq 0x%x\n", hwirq);
			continue;
		}
		rc = request_irq(irq, opal_interrupt, 0, "opal", NULL);
		if (rc)
			pr_warning("opal: Error %d requesting irq %d"
				   " (0x%x)\n", rc, irq, hwirq);
		opal_irqs[i] = irq;
	}

	/* Create "opal" kobject under /sys/firmware */
	rc = opal_sysfs_init();
	if (rc == 0) {
		/* Setup dump region interface */
		opal_dump_region_init();
		/* Setup error log interface */
		rc = opal_elog_init();
		/* Setup code update interface */
		opal_flash_init();
		/* Setup platform dump extract interface */
		opal_platform_dump_init();
		/* Setup system parameters interface */
		opal_sys_param_init();
		/* Setup message log interface. */
		opal_msglog_init();
	}

	return 0;
}
machine_subsys_initcall(powernv, opal_init);

void opal_shutdown(void)
{
	unsigned int i;
	long rc = OPAL_BUSY;

	/* First free interrupts, which will also mask them */
	for (i = 0; i < opal_irq_count; i++) {
		if (opal_irqs[i])
			free_irq(opal_irqs[i], NULL);
		opal_irqs[i] = 0;
	}

	/*
	 * Then sync with OPAL which ensure anything that can
	 * potentially write to our memory has completed such
	 * as an ongoing dump retrieval
	 */
	while (rc == OPAL_BUSY || rc == OPAL_BUSY_EVENT) {
		rc = opal_sync_host_reboot();
		if (rc == OPAL_BUSY)
			opal_poll_events(NULL);
		else
			mdelay(10);
	}

	/* Unregister memory dump region */
	opal_unregister_dump_region(OPAL_DUMP_REGION_LOG_BUF);
}

/* Export this so that test modules can use it */
EXPORT_SYMBOL_GPL(opal_invalid_call);

/* Convert a region of vmalloc memory to an opal sg list */
struct opal_sg_list *opal_vmalloc_to_sg_list(void *vmalloc_addr,
					     unsigned long vmalloc_size)
{
	struct opal_sg_list *sg, *first = NULL;
	unsigned long i = 0;

	sg = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!sg)
		goto nomem;

	first = sg;

	while (vmalloc_size > 0) {
		uint64_t data = vmalloc_to_pfn(vmalloc_addr) << PAGE_SHIFT;
		uint64_t length = min(vmalloc_size, PAGE_SIZE);

		sg->entry[i].data = cpu_to_be64(data);
		sg->entry[i].length = cpu_to_be64(length);
		i++;

		if (i >= SG_ENTRIES_PER_NODE) {
			struct opal_sg_list *next;

			next = kzalloc(PAGE_SIZE, GFP_KERNEL);
			if (!next)
				goto nomem;

			sg->length = cpu_to_be64(
					i * sizeof(struct opal_sg_entry) + 16);
			i = 0;
			sg->next = cpu_to_be64(__pa(next));
			sg = next;
		}

		vmalloc_addr += length;
		vmalloc_size -= length;
	}

	sg->length = cpu_to_be64(i * sizeof(struct opal_sg_entry) + 16);

	return first;

nomem:
	pr_err("%s : Failed to allocate memory\n", __func__);
	opal_free_sg_list(first);
	return NULL;
}

void opal_free_sg_list(struct opal_sg_list *sg)
{
	while (sg) {
		uint64_t next = be64_to_cpu(sg->next);

		kfree(sg);

		if (next)
			sg = __va(next);
		else
			sg = NULL;
	}
}
