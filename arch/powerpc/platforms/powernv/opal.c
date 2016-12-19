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

#define pr_fmt(fmt)	"opal: " fmt

#include <linux/printk.h>
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
#include <linux/kthread.h>
#include <linux/freezer.h>

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
static struct atomic_notifier_head opal_msg_notifier_head[OPAL_MSG_TYPE_MAX];
static uint32_t opal_heartbeat;
static struct task_struct *kopald_tsk;

void opal_configure_cores(void)
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
		pr_info("OPAL detected !\n");
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

	/*
	 * Check if we are running on newer firmware that exports
	 * OPAL_HANDLE_HMI token. If yes, then don't ask OPAL to patch
	 * the HMI interrupt and we catch it directly in Linux.
	 *
	 * For older firmware (i.e currently released POWER8 System Firmware
	 * as of today <= SV810_087), we fallback to old behavior and let OPAL
	 * patch the HMI vector and handle it inside OPAL firmware.
	 *
	 * For newer firmware (in development/yet to be released) we will
	 * start catching/handling HMI directly in Linux.
	 */
	if (!opal_check_token(OPAL_HANDLE_HMI)) {
		pr_info("Old firmware detected, OPAL handles HMIs.\n");
		opal_register_exception_handler(
				OPAL_HYPERVISOR_MAINTENANCE_HANDLER,
				0, glue);
		glue += 128;
	}

	opal_register_exception_handler(OPAL_SOFTPATCH_HANDLER, 0, glue);
#endif

	return 0;
}
machine_early_initcall(powernv, opal_register_exception_handlers);

/*
 * Opal message notifier based on message type. Allow subscribers to get
 * notified for specific messgae type.
 */
int opal_message_notifier_register(enum opal_msg_type msg_type,
					struct notifier_block *nb)
{
	if (!nb || msg_type >= OPAL_MSG_TYPE_MAX) {
		pr_warning("%s: Invalid arguments, msg_type:%d\n",
			   __func__, msg_type);
		return -EINVAL;
	}

	return atomic_notifier_chain_register(
				&opal_msg_notifier_head[msg_type], nb);
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
	if (type >= OPAL_MSG_TYPE_MAX) {
		pr_warn_once("%s: Unknown message type: %u\n", __func__, type);
		return;
	}
	opal_message_do_notify(type, (void *)&msg);
}

static irqreturn_t opal_message_notify(int irq, void *data)
{
	opal_handle_message();
	return IRQ_HANDLED;
}

static int __init opal_message_init(void)
{
	int ret, i, irq;

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
		pr_err("Machine check interrupt unrecoverable: MSR(RI=0)\n");
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
	int ret;

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

	/*
	 * Unrecovered machine check, we are heading to panic path.
	 *
	 * We may have hit this MCE in very early stage of kernel
	 * initialization even before opal-prd has started running. If
	 * this is the case then this MCE error may go un-noticed or
	 * un-analyzed if we go down panic path. We need to inform
	 * BMC/OCC about this error so that they can collect relevant
	 * data for error analysis before rebooting.
	 * Use opal_cec_reboot2(OPAL_REBOOT_PLATFORM_ERROR) to do so.
	 * This function may not return on BMC based system.
	 */
	ret = opal_cec_reboot2(OPAL_REBOOT_PLATFORM_ERROR,
			"Unrecoverable Machine Check exception");
	if (ret == OPAL_UNSUPPORTED) {
		pr_emerg("Reboot type %d not supported\n",
					OPAL_REBOOT_PLATFORM_ERROR);
	}

	/*
	 * We reached here. There can be three possibilities:
	 * 1. We are running on a firmware level that do not support
	 *    opal_cec_reboot2()
	 * 2. We are running on a firmware level that do not support
	 *    OPAL_REBOOT_PLATFORM_ERROR reboot type.
	 * 3. We are running on FSP based system that does not need opal
	 *    to trigger checkstop explicitly for error analysis. The FSP
	 *    PRD component would have already got notified about this
	 *    error through other channels.
	 *
	 * If hardware marked this as an unrecoverable MCE, we are
	 * going to panic anyway. Even if it didn't, it's not safe to
	 * continue at this point, so we should explicitly panic.
	 */

	panic("PowerNV Unrecovered Machine Check");
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
		opal_handle_events(be64_to_cpu(evt));

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

static BIN_ATTR_RO(symbol_map, 0);

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
	bin_attr_symbol_map.private = __va(be64_to_cpu(syms[0]));
	bin_attr_symbol_map.size = be64_to_cpu(syms[1]);

	rc = sysfs_create_bin_file(opal_kobj, &bin_attr_symbol_map);
	if (rc)
		pr_warn("Error %d creating OPAL symbols file\n", rc);
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

static int kopald(void *unused)
{
	unsigned long timeout = msecs_to_jiffies(opal_heartbeat) + 1;
	__be64 events;

	set_freezable();
	do {
		try_to_freeze();
		opal_poll_events(&events);
		opal_handle_events(be64_to_cpu(events));
		schedule_timeout_interruptible(timeout);
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
			if (strcmp(np->name, "serial"))
				continue;
			of_platform_device_create(np, NULL, NULL);
		}
		of_node_put(consoles);
	}

	/* Initialise OPAL messaging system */
	opal_message_init();

	/* Initialise OPAL asynchronous completion interface */
	opal_async_comp_init();

	/* Initialise OPAL sensor interface */
	opal_sensor_init();

	/* Initialise OPAL hypervisor maintainence interrupt handling */
	opal_hmi_handler_init();

	/* Create i2c platform devices */
	opal_pdev_init("ibm,opal-i2c");

	/* Setup a heatbeat thread if requested by OPAL */
	opal_init_heartbeat();

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

	/* Initialize platform devices: IPMI backend, PRD & flash interface */
	opal_pdev_init("ibm,opal-ipmi");
	opal_pdev_init("ibm,opal-flash");
	opal_pdev_init("ibm,opal-prd");

	/* Initialise platform device: oppanel interface */
	opal_pdev_init("ibm,opal-oppanel");

	/* Initialise OPAL kmsg dumper for flushing console on panic */
	opal_kmsg_init();

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
	case OPAL_BUSY_EVENT:		return -EBUSY;
	case OPAL_NO_MEM:		return -ENOMEM;
	case OPAL_PERMISSION:		return -EPERM;

	case OPAL_UNSUPPORTED:		return -EIO;
	case OPAL_HARDWARE:		return -EIO;
	case OPAL_INTERNAL_ERROR:	return -EIO;
	default:
		pr_err("%s: unexpected OPAL error %d\n", __func__, rc);
		return -EIO;
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
