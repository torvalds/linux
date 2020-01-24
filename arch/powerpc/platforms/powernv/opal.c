// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PowerNV OPAL high level interfaces
 *
 * Copyright 2011 IBM Corp.
 */

#define pr_fmt(fmt)	"opal: " fmt

#include <linux/printk.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kobject.h>
#include <linux/delay.h>
#include <linux/memblock.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/kmsg_dump.h>
#include <linux/console.h>
#include <linux/sched/debug.h>

#include <asm/machdep.h>
#include <asm/opal.h>
#include <asm/firmware.h>
#include <asm/mce.h>
#include <asm/imc-pmu.h>
#include <asm/bug.h>

#include "powernv.h"

#define OPAL_MSG_QUEUE_MAX 16

struct opal_msg_node {
	struct list_head	list;
	struct opal_msg		msg;
};

static DEFINE_SPINLOCK(msg_list_lock);
static LIST_HEAD(msg_list);

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

static int msg_list_size;

static struct mcheck_recoverable_range *mc_recoverable_range;
static int mc_recoverable_range_len;

struct device_node *opal_node;
static DEFINE_SPINLOCK(opal_write_lock);
static struct atomic_notifier_head opal_msg_notifier_head[OPAL_MSG_TYPE_MAX];
static uint32_t opal_heartbeat;
static struct task_struct *kopald_tsk;
static struct opal_msg *opal_msg;
static u32 opal_msg_size __ro_after_init;

void opal_configure_cores(void)
{
	u64 reinit_flags = 0;

	/* Do the actual re-init, This will clobber all FPRs, VRs, etc...
	 *
	 * It will preserve non volatile GPRs and HSPRG0/1. It will
	 * also restore HIDs and other SPRs to their original value
	 * but it might clobber a bunch.
	 */
#ifdef __BIG_ENDIAN__
	reinit_flags |= OPAL_REINIT_CPUS_HILE_BE;
#else
	reinit_flags |= OPAL_REINIT_CPUS_HILE_LE;
#endif

	/*
	 * POWER9 always support running hash:
	 *  ie. Host hash  supports  hash guests
	 *      Host radix supports  hash/radix guests
	 */
	if (early_cpu_has_feature(CPU_FTR_ARCH_300)) {
		reinit_flags |= OPAL_REINIT_CPUS_MMU_HASH;
		if (early_radix_enabled())
			reinit_flags |= OPAL_REINIT_CPUS_MMU_RADIX;
	}

	opal_reinit_cpus(reinit_flags);

	/* Restore some bits */
	if (cur_cpu_spec->cpu_restore)
		cur_cpu_spec->cpu_restore();
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

	if (of_flat_dt_is_compatible(node, "ibm,opal-v3")) {
		powerpc_firmware_features |= FW_FEATURE_OPAL;
		pr_debug("OPAL detected !\n");
	} else {
		panic("OPAL != V3 detected, no longer supported.\n");
	}

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
	 * Allocate a buffer to hold the MC recoverable ranges.
	 */
	mc_recoverable_range = memblock_alloc(size, __alignof__(u64));
	if (!mc_recoverable_range)
		panic("%s: Failed to allocate %u bytes align=0x%lx\n",
		      __func__, size, __alignof__(u64));

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

	/*
	 * Only ancient OPAL firmware requires this.
	 * Specifically, firmware from FW810.00 (released June 2014)
	 * through FW810.20 (Released October 2014).
	 *
	 * Check if we are running on newer (post Oct 2014) firmware that
	 * exports the OPAL_HANDLE_HMI token. If yes, then don't ask OPAL to
	 * patch the HMI interrupt and we catch it directly in Linux.
	 *
	 * For older firmware (i.e < FW810.20), we fallback to old behavior and
	 * let OPAL patch the HMI vector and handle it inside OPAL firmware.
	 *
	 * For newer firmware we catch/handle the HMI directly in Linux.
	 */
	if (!opal_check_token(OPAL_HANDLE_HMI)) {
		pr_info("Old firmware detected, OPAL handles HMIs.\n");
		opal_register_exception_handler(
				OPAL_HYPERVISOR_MAINTENANCE_HANDLER,
				0, glue);
		glue += 128;
	}

	/*
	 * Only applicable to ancient firmware, all modern
	 * (post March 2015/skiboot 5.0) firmware will just return
	 * OPAL_UNSUPPORTED.
	 */
	opal_register_exception_handler(OPAL_SOFTPATCH_HANDLER, 0, glue);
#endif

	return 0;
}
machine_early_initcall(powernv, opal_register_exception_handlers);

static void queue_replay_msg(void *msg)
{
	struct opal_msg_node *msg_node;

	if (msg_list_size < OPAL_MSG_QUEUE_MAX) {
		msg_node = kzalloc(sizeof(*msg_node), GFP_ATOMIC);
		if (msg_node) {
			INIT_LIST_HEAD(&msg_node->list);
			memcpy(&msg_node->msg, msg, sizeof(struct opal_msg));
			list_add_tail(&msg_node->list, &msg_list);
			msg_list_size++;
		} else
			pr_warn_once("message queue no memory\n");

		if (msg_list_size >= OPAL_MSG_QUEUE_MAX)
			pr_warn_once("message queue full\n");
	}
}

static void dequeue_replay_msg(enum opal_msg_type msg_type)
{
	struct opal_msg_node *msg_node, *tmp;

	list_for_each_entry_safe(msg_node, tmp, &msg_list, list) {
		if (be32_to_cpu(msg_node->msg.msg_type) != msg_type)
			continue;

		atomic_notifier_call_chain(&opal_msg_notifier_head[msg_type],
					msg_type,
					&msg_node->msg);

		list_del(&msg_node->list);
		kfree(msg_node);
		msg_list_size--;
	}
}

/*
 * Opal message notifier based on message type. Allow subscribers to get
 * notified for specific messgae type.
 */
int opal_message_notifier_register(enum opal_msg_type msg_type,
					struct notifier_block *nb)
{
	int ret;
	unsigned long flags;

	if (!nb || msg_type >= OPAL_MSG_TYPE_MAX) {
		pr_warn("%s: Invalid arguments, msg_type:%d\n",
			__func__, msg_type);
		return -EINVAL;
	}

	spin_lock_irqsave(&msg_list_lock, flags);
	ret = atomic_notifier_chain_register(
		&opal_msg_notifier_head[msg_type], nb);

	/*
	 * If the registration succeeded, replay any queued messages that came
	 * in prior to the notifier chain registration. msg_list_lock held here
	 * to ensure they're delivered prior to any subsequent messages.
	 */
	if (ret == 0)
		dequeue_replay_msg(msg_type);

	spin_unlock_irqrestore(&msg_list_lock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(opal_message_notifier_register);

int opal_message_notifier_unregister(enum opal_msg_type msg_type,
				     struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(
			&opal_msg_notifier_head[msg_type], nb);
}
EXPORT_SYMBOL_GPL(opal_message_notifier_unregister);

static void opal_message_do_notify(uint32_t msg_type, void *msg)
{
	unsigned long flags;
	bool queued = false;

	spin_lock_irqsave(&msg_list_lock, flags);
	if (opal_msg_notifier_head[msg_type].head == NULL) {
		/*
		 * Queue up the msg since no notifiers have registered
		 * yet for this msg_type.
		 */
		queue_replay_msg(msg);
		queued = true;
	}
	spin_unlock_irqrestore(&msg_list_lock, flags);

	if (queued)
		return;

	/* notify subscribers */
	atomic_notifier_call_chain(&opal_msg_notifier_head[msg_type],
					msg_type, msg);
}

static void opal_handle_message(void)
{
	s64 ret;
	u32 type;

	ret = opal_get_msg(__pa(opal_msg), opal_msg_size);
	/* No opal message pending. */
	if (ret == OPAL_RESOURCE)
		return;

	/* check for errors. */
	if (ret) {
		pr_warn("%s: Failed to retrieve opal message, err=%lld\n",
			__func__, ret);
		return;
	}

	type = be32_to_cpu(opal_msg->msg_type);

	/* Sanity check */
	if (type >= OPAL_MSG_TYPE_MAX) {
		pr_warn_once("%s: Unknown message type: %u\n", __func__, type);
		return;
	}
	opal_message_do_notify(type, (void *)opal_msg);
}

static irqreturn_t opal_message_notify(int irq, void *data)
{
	opal_handle_message();
	return IRQ_HANDLED;
}

static int __init opal_message_init(struct device_node *opal_node)
{
	int ret, i, irq;

	ret = of_property_read_u32(opal_node, "opal-msg-size", &opal_msg_size);
	if (ret) {
		pr_notice("Failed to read opal-msg-size property\n");
		opal_msg_size = sizeof(struct opal_msg);
	}

	opal_msg = kmalloc(opal_msg_size, GFP_KERNEL);
	if (!opal_msg) {
		opal_msg_size = sizeof(struct opal_msg);
		/* Try to allocate fixed message size */
		opal_msg = kmalloc(opal_msg_size, GFP_KERNEL);
		BUG_ON(opal_msg == NULL);
	}

	for (i = 0; i < OPAL_MSG_TYPE_MAX; i++)
		ATOMIC_INIT_NOTIFIER_HEAD(&opal_msg_notifier_head[i]);

	irq = opal_event_request(ilog2(OPAL_EVENT_MSG_PENDING));
	if (!irq) {
		pr_err("%s: Can't register OPAL event irq (%d)\n",
		       __func__, irq);
		return irq;
	}

	ret = request_irq(irq, opal_message_notify,
			IRQ_TYPE_LEVEL_HIGH, "opal-msg", NULL);
	if (ret) {
		pr_err("%s: Can't request OPAL event irq (%d)\n",
		       __func__, ret);
		return ret;
	}

	return 0;
}

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

static int __opal_put_chars(uint32_t vtermno, const char *data, int total_len, bool atomic)
{
	unsigned long flags = 0 /* shut up gcc */;
	int written;
	__be64 olen;
	s64 rc;

	if (!opal.entry)
		return -ENODEV;

	if (atomic)
		spin_lock_irqsave(&opal_write_lock, flags);
	rc = opal_console_write_buffer_space(vtermno, &olen);
	if (rc || be64_to_cpu(olen) < total_len) {
		/* Closed -> drop characters */
		if (rc)
			written = total_len;
		else
			written = -EAGAIN;
		goto out;
	}

	/* Should not get a partial write here because space is available. */
	olen = cpu_to_be64(total_len);
	rc = opal_console_write(vtermno, &olen, data);
	if (rc == OPAL_BUSY || rc == OPAL_BUSY_EVENT) {
		if (rc == OPAL_BUSY_EVENT)
			opal_poll_events(NULL);
		written = -EAGAIN;
		goto out;
	}

	/* Closed or other error drop */
	if (rc != OPAL_SUCCESS) {
		written = opal_error_code(rc);
		goto out;
	}

	written = be64_to_cpu(olen);
	if (written < total_len) {
		if (atomic) {
			/* Should not happen */
			pr_warn("atomic console write returned partial "
				"len=%d written=%d\n", total_len, written);
		}
		if (!written)
			written = -EAGAIN;
	}

out:
	if (atomic)
		spin_unlock_irqrestore(&opal_write_lock, flags);

	return written;
}

int opal_put_chars(uint32_t vtermno, const char *data, int total_len)
{
	return __opal_put_chars(vtermno, data, total_len, false);
}

/*
 * opal_put_chars_atomic will not perform partial-writes. Data will be
 * atomically written to the terminal or not at all. This is not strictly
 * true at the moment because console space can race with OPAL's console
 * writes.
 */
int opal_put_chars_atomic(uint32_t vtermno, const char *data, int total_len)
{
	return __opal_put_chars(vtermno, data, total_len, true);
}

static s64 __opal_flush_console(uint32_t vtermno)
{
	s64 rc;

	if (!opal_check_token(OPAL_CONSOLE_FLUSH)) {
		__be64 evt;

		/*
		 * If OPAL_CONSOLE_FLUSH is not implemented in the firmware,
		 * the console can still be flushed by calling the polling
		 * function while it has OPAL_EVENT_CONSOLE_OUTPUT events.
		 */
		WARN_ONCE(1, "opal: OPAL_CONSOLE_FLUSH missing.\n");

		opal_poll_events(&evt);
		if (!(be64_to_cpu(evt) & OPAL_EVENT_CONSOLE_OUTPUT))
			return OPAL_SUCCESS;
		return OPAL_BUSY;

	} else {
		rc = opal_console_flush(vtermno);
		if (rc == OPAL_BUSY_EVENT) {
			opal_poll_events(NULL);
			rc = OPAL_BUSY;
		}
		return rc;
	}

}

/*
 * opal_flush_console spins until the console is flushed
 */
int opal_flush_console(uint32_t vtermno)
{
	for (;;) {
		s64 rc = __opal_flush_console(vtermno);

		if (rc == OPAL_BUSY || rc == OPAL_PARTIAL) {
			mdelay(1);
			continue;
		}

		return opal_error_code(rc);
	}
}

/*
 * opal_flush_chars is an hvc interface that sleeps until the console is
 * flushed if wait, otherwise it will return -EBUSY if the console has data,
 * -EAGAIN if it has data and some of it was flushed.
 */
int opal_flush_chars(uint32_t vtermno, bool wait)
{
	for (;;) {
		s64 rc = __opal_flush_console(vtermno);

		if (rc == OPAL_BUSY || rc == OPAL_PARTIAL) {
			if (wait) {
				msleep(OPAL_BUSY_DELAY_MS);
				continue;
			}
			if (rc == OPAL_PARTIAL)
				return -EAGAIN;
		}

		return opal_error_code(rc);
	}
}

static int opal_recover_mce(struct pt_regs *regs,
					struct machine_check_event *evt)
{
	int recovered = 0;

	if (!(regs->msr & MSR_RI)) {
		/* If MSR_RI isn't set, we cannot recover */
		pr_err("Machine check interrupt unrecoverable: MSR(RI=0)\n");
		recovered = 0;
	} else if (evt->disposition == MCE_DISPOSITION_RECOVERED) {
		/* Platform corrected itself */
		recovered = 1;
	} else if (evt->severity == MCE_SEV_FATAL) {
		/* Fatal machine check */
		pr_err("Machine check interrupt is fatal\n");
		recovered = 0;
	}

	if (!recovered && evt->sync_error) {
		/*
		 * Try to kill processes if we get a synchronous machine check
		 * (e.g., one caused by execution of this instruction). This
		 * will devolve into a panic if we try to kill init or are in
		 * an interrupt etc.
		 *
		 * TODO: Queue up this address for hwpoisioning later.
		 * TODO: This is not quite right for d-side machine
		 *       checks ->nip is not necessarily the important
		 *       address.
		 */
		if ((user_mode(regs))) {
			_exception(SIGBUS, regs, BUS_MCEERR_AR, regs->nip);
			recovered = 1;
		} else if (die_will_crash()) {
			/*
			 * die() would kill the kernel, so better to go via
			 * the platform reboot code that will log the
			 * machine check.
			 */
			recovered = 0;
		} else {
			die("Machine check", regs, SIGBUS);
			recovered = 1;
		}
	}

	return recovered;
}

void __noreturn pnv_platform_error_reboot(struct pt_regs *regs, const char *msg)
{
	panic_flush_kmsg_start();

	pr_emerg("Hardware platform error: %s\n", msg);
	if (regs)
		show_regs(regs);
	smp_send_stop();

	panic_flush_kmsg_end();

	/*
	 * Don't bother to shut things down because this will
	 * xstop the system.
	 */
	if (opal_cec_reboot2(OPAL_REBOOT_PLATFORM_ERROR, msg)
						== OPAL_UNSUPPORTED) {
		pr_emerg("Reboot type %d not supported for %s\n",
				OPAL_REBOOT_PLATFORM_ERROR, msg);
	}

	/*
	 * We reached here. There can be three possibilities:
	 * 1. We are running on a firmware level that do not support
	 *    opal_cec_reboot2()
	 * 2. We are running on a firmware level that do not support
	 *    OPAL_REBOOT_PLATFORM_ERROR reboot type.
	 * 3. We are running on FSP based system that does not need
	 *    opal to trigger checkstop explicitly for error analysis.
	 *    The FSP PRD component would have already got notified
	 *    about this error through other channels.
	 * 4. We are running on a newer skiboot that by default does
	 *    not cause a checkstop, drops us back to the kernel to
	 *    extract context and state at the time of the error.
	 */

	panic(msg);
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
	machine_check_print_event_info(&evt, user_mode(regs), false);

	if (opal_recover_mce(regs, &evt))
		return 1;

	pnv_platform_error_reboot(regs, "Unrecoverable Machine Check exception");
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

int opal_hmi_exception_early2(struct pt_regs *regs)
{
	s64 rc;
	__be64 out_flags;

	/*
	 * call opal hmi handler.
	 * Check 64-bit flag mask to find out if an event was generated,
	 * and whether TB is still valid or not etc.
	 */
	rc = opal_handle_hmi2(&out_flags);
	if (rc != OPAL_SUCCESS)
		return 0;

	if (be64_to_cpu(out_flags) & OPAL_HMI_FLAGS_NEW_EVENT)
		local_paca->hmi_event_available = 1;
	if (be64_to_cpu(out_flags) & OPAL_HMI_FLAGS_TOD_TB_FAIL)
		tb_invalid = true;
	return 1;
}

/* HMI exception handler called in virtual mode during check_irq_replay. */
int opal_handle_hmi_exception(struct pt_regs *regs)
{
	/*
	 * Check if HMI event is available.
	 * if Yes, then wake kopald to process them.
	 */
	if (!local_paca->hmi_event_available)
		return 0;

	local_paca->hmi_event_available = 0;
	opal_wake_poller();

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
			(regs->nip < (opal.base + opal.size)))
		recover_addr = find_recovery_address(regs->nip);

	/*
	 * Setup regs->nip to rfi into fixup address.
	 */
	if (recover_addr)
		regs->nip = recover_addr;

out:
	return !!recover_addr;
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

static ssize_t symbol_map_read(struct file *fp, struct kobject *kobj,
			       struct bin_attribute *bin_attr,
			       char *buf, loff_t off, size_t count)
{
	return memory_read_from_buffer(buf, count, &off, bin_attr->private,
				       bin_attr->size);
}

static struct bin_attribute symbol_map_attr = {
	.attr = {.name = "symbol_map", .mode = 0400},
	.read = symbol_map_read
};

static void opal_export_symmap(void)
{
	const __be64 *syms;
	unsigned int size;
	struct device_node *fw;
	int rc;

	fw = of_find_node_by_path("/ibm,opal/firmware");
	if (!fw)
		return;
	syms = of_get_property(fw, "symbol-map", &size);
	if (!syms || size != 2 * sizeof(__be64))
		return;

	/* Setup attributes */
	symbol_map_attr.private = __va(be64_to_cpu(syms[0]));
	symbol_map_attr.size = be64_to_cpu(syms[1]);

	rc = sysfs_create_bin_file(opal_kobj, &symbol_map_attr);
	if (rc)
		pr_warn("Error %d creating OPAL symbols file\n", rc);
}

static ssize_t export_attr_read(struct file *fp, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *buf,
				loff_t off, size_t count)
{
	return memory_read_from_buffer(buf, count, &off, bin_attr->private,
				       bin_attr->size);
}

/*
 * opal_export_attrs: creates a sysfs node for each property listed in
 * the device-tree under /ibm,opal/firmware/exports/
 * All new sysfs nodes are created under /opal/exports/.
 * This allows for reserved memory regions (e.g. HDAT) to be read.
 * The new sysfs nodes are only readable by root.
 */
static void opal_export_attrs(void)
{
	struct bin_attribute *attr;
	struct device_node *np;
	struct property *prop;
	struct kobject *kobj;
	u64 vals[2];
	int rc;

	np = of_find_node_by_path("/ibm,opal/firmware/exports");
	if (!np)
		return;

	/* Create new 'exports' directory - /sys/firmware/opal/exports */
	kobj = kobject_create_and_add("exports", opal_kobj);
	if (!kobj) {
		pr_warn("kobject_create_and_add() of exports failed\n");
		return;
	}

	for_each_property_of_node(np, prop) {
		if (!strcmp(prop->name, "name") || !strcmp(prop->name, "phandle"))
			continue;

		if (of_property_read_u64_array(np, prop->name, &vals[0], 2))
			continue;

		attr = kzalloc(sizeof(*attr), GFP_KERNEL);

		if (attr == NULL) {
			pr_warn("Failed kmalloc for bin_attribute!");
			continue;
		}

		sysfs_bin_attr_init(attr);
		attr->attr.name = kstrdup(prop->name, GFP_KERNEL);
		attr->attr.mode = 0400;
		attr->read = export_attr_read;
		attr->private = __va(vals[0]);
		attr->size = vals[1];

		if (attr->attr.name == NULL) {
			pr_warn("Failed kstrdup for bin_attribute attr.name");
			kfree(attr);
			continue;
		}

		rc = sysfs_create_bin_file(kobj, attr);
		if (rc) {
			pr_warn("Error %d creating OPAL sysfs exports/%s file\n",
				 rc, prop->name);
			kfree(attr->attr.name);
			kfree(attr);
		}
	}

	of_node_put(np);
}

static void __init opal_dump_region_init(void)
{
	void *addr;
	uint64_t size;
	int rc;

	if (!opal_check_token(OPAL_REGISTER_DUMP_REGION))
		return;

	/* Register kernel log buffer */
	addr = log_buf_addr_get();
	if (addr == NULL)
		return;

	size = log_buf_len_get();
	if (size == 0)
		return;

	rc = opal_register_dump_region(OPAL_DUMP_REGION_LOG_BUF,
				       __pa(addr), size);
	/* Don't warn if this is just an older OPAL that doesn't
	 * know about that call
	 */
	if (rc && rc != OPAL_UNSUPPORTED)
		pr_warn("DUMP: Failed to register kernel log buffer. "
			"rc = %d\n", rc);
}

static void opal_pdev_init(const char *compatible)
{
	struct device_node *np;

	for_each_compatible_node(np, NULL, compatible)
		of_platform_device_create(np, NULL, NULL);
}

static void __init opal_imc_init_dev(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, IMC_DTB_COMPAT);
	if (np)
		of_platform_device_create(np, NULL, NULL);
}

static int kopald(void *unused)
{
	unsigned long timeout = msecs_to_jiffies(opal_heartbeat) + 1;

	set_freezable();
	do {
		try_to_freeze();

		opal_handle_events();

		set_current_state(TASK_INTERRUPTIBLE);
		if (opal_have_pending_events())
			__set_current_state(TASK_RUNNING);
		else
			schedule_timeout(timeout);

	} while (!kthread_should_stop());

	return 0;
}

void opal_wake_poller(void)
{
	if (kopald_tsk)
		wake_up_process(kopald_tsk);
}

static void opal_init_heartbeat(void)
{
	/* Old firwmware, we assume the HVC heartbeat is sufficient */
	if (of_property_read_u32(opal_node, "ibm,heartbeat-ms",
				 &opal_heartbeat) != 0)
		opal_heartbeat = 0;

	if (opal_heartbeat)
		kopald_tsk = kthread_run(kopald, NULL, "kopald");
}

static int __init opal_init(void)
{
	struct device_node *np, *consoles, *leds;
	int rc;

	opal_node = of_find_node_by_path("/ibm,opal");
	if (!opal_node) {
		pr_warn("Device node not found\n");
		return -ENODEV;
	}

	/* Register OPAL consoles if any ports */
	consoles = of_find_node_by_path("/ibm,opal/consoles");
	if (consoles) {
		for_each_child_of_node(consoles, np) {
			if (!of_node_name_eq(np, "serial"))
				continue;
			of_platform_device_create(np, NULL, NULL);
		}
		of_node_put(consoles);
	}

	/* Initialise OPAL messaging system */
	opal_message_init(opal_node);

	/* Initialise OPAL asynchronous completion interface */
	opal_async_comp_init();

	/* Initialise OPAL sensor interface */
	opal_sensor_init();

	/* Initialise OPAL hypervisor maintainence interrupt handling */
	opal_hmi_handler_init();

	/* Create i2c platform devices */
	opal_pdev_init("ibm,opal-i2c");

	/* Handle non-volatile memory devices */
	opal_pdev_init("pmem-region");

	/* Setup a heatbeat thread if requested by OPAL */
	opal_init_heartbeat();

	/* Detect In-Memory Collection counters and create devices*/
	opal_imc_init_dev();

	/* Create leds platform devices */
	leds = of_find_node_by_path("/ibm,opal/leds");
	if (leds) {
		of_platform_device_create(leds, "opal_leds", NULL);
		of_node_put(leds);
	}

	/* Initialise OPAL message log interface */
	opal_msglog_init();

	/* Create "opal" kobject under /sys/firmware */
	rc = opal_sysfs_init();
	if (rc == 0) {
		/* Export symbol map to userspace */
		opal_export_symmap();
		/* Setup dump region interface */
		opal_dump_region_init();
		/* Setup error log interface */
		rc = opal_elog_init();
		/* Setup code update interface */
		opal_flash_update_init();
		/* Setup platform dump extract interface */
		opal_platform_dump_init();
		/* Setup system parameters interface */
		opal_sys_param_init();
		/* Setup message log sysfs interface. */
		opal_msglog_sysfs_init();
	}

	/* Export all properties */
	opal_export_attrs();

	/* Initialize platform devices: IPMI backend, PRD & flash interface */
	opal_pdev_init("ibm,opal-ipmi");
	opal_pdev_init("ibm,opal-flash");
	opal_pdev_init("ibm,opal-prd");

	/* Initialise platform device: oppanel interface */
	opal_pdev_init("ibm,opal-oppanel");

	/* Initialise OPAL kmsg dumper for flushing console on panic */
	opal_kmsg_init();

	/* Initialise OPAL powercap interface */
	opal_powercap_init();

	/* Initialise OPAL Power-Shifting-Ratio interface */
	opal_psr_init();

	/* Initialise OPAL sensor groups */
	opal_sensor_groups_init();

	/* Initialise OPAL Power control interface */
	opal_power_control_init();

	/* Initialize OPAL secure variables */
	opal_pdev_init("ibm,secvar-backend");

	return 0;
}
machine_subsys_initcall(powernv, opal_init);

void opal_shutdown(void)
{
	long rc = OPAL_BUSY;

	opal_event_shutdown();

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
	if (opal_check_token(OPAL_UNREGISTER_DUMP_REGION))
		opal_unregister_dump_region(OPAL_DUMP_REGION_LOG_BUF);
}

/* Export this so that test modules can use it */
EXPORT_SYMBOL_GPL(opal_invalid_call);
EXPORT_SYMBOL_GPL(opal_xscom_read);
EXPORT_SYMBOL_GPL(opal_xscom_write);
EXPORT_SYMBOL_GPL(opal_ipmi_send);
EXPORT_SYMBOL_GPL(opal_ipmi_recv);
EXPORT_SYMBOL_GPL(opal_flash_read);
EXPORT_SYMBOL_GPL(opal_flash_write);
EXPORT_SYMBOL_GPL(opal_flash_erase);
EXPORT_SYMBOL_GPL(opal_prd_msg);
EXPORT_SYMBOL_GPL(opal_check_token);

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

int opal_error_code(int rc)
{
	switch (rc) {
	case OPAL_SUCCESS:		return 0;

	case OPAL_PARAMETER:		return -EINVAL;
	case OPAL_ASYNC_COMPLETION:	return -EINPROGRESS;
	case OPAL_BUSY:
	case OPAL_BUSY_EVENT:		return -EBUSY;
	case OPAL_NO_MEM:		return -ENOMEM;
	case OPAL_PERMISSION:		return -EPERM;

	case OPAL_UNSUPPORTED:		return -EIO;
	case OPAL_HARDWARE:		return -EIO;
	case OPAL_INTERNAL_ERROR:	return -EIO;
	case OPAL_TIMEOUT:		return -ETIMEDOUT;
	default:
		pr_err("%s: unexpected OPAL error %d\n", __func__, rc);
		return -EIO;
	}
}

void powernv_set_nmmu_ptcr(unsigned long ptcr)
{
	int rc;

	if (firmware_has_feature(FW_FEATURE_OPAL)) {
		rc = opal_nmmu_set_ptcr(-1UL, ptcr);
		if (rc != OPAL_SUCCESS && rc != OPAL_UNSUPPORTED)
			pr_warn("%s: Unable to set nest mmu ptcr\n", __func__);
	}
}

EXPORT_SYMBOL_GPL(opal_poll_events);
EXPORT_SYMBOL_GPL(opal_rtc_read);
EXPORT_SYMBOL_GPL(opal_rtc_write);
EXPORT_SYMBOL_GPL(opal_tpo_read);
EXPORT_SYMBOL_GPL(opal_tpo_write);
EXPORT_SYMBOL_GPL(opal_i2c_request);
/* Export these symbols for PowerNV LED class driver */
EXPORT_SYMBOL_GPL(opal_leds_get_ind);
EXPORT_SYMBOL_GPL(opal_leds_set_ind);
/* Export this symbol for PowerNV Operator Panel class driver */
EXPORT_SYMBOL_GPL(opal_write_oppanel_async);
/* Export this for KVM */
EXPORT_SYMBOL_GPL(opal_int_set_mfrr);
EXPORT_SYMBOL_GPL(opal_int_eoi);
EXPORT_SYMBOL_GPL(opal_error_code);
/* Export the below symbol for NX compression */
EXPORT_SYMBOL(opal_nx_coproc_init);
