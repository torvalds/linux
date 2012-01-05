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
#include <linux/of_platform.h>
#include <linux/interrupt.h>
#include <asm/opal.h>
#include <asm/firmware.h>

#include "powernv.h"

struct opal {
	u64 base;
	u64 entry;
} opal;

static struct device_node *opal_node;
static DEFINE_SPINLOCK(opal_write_lock);
extern u64 opal_mc_secondary_handler[];

int __init early_init_dt_scan_opal(unsigned long node,
				   const char *uname, int depth, void *data)
{
	const void *basep, *entryp;
	unsigned long basesz, entrysz;
	u64 glue;

	if (depth != 1 || strcmp(uname, "ibm,opal") != 0)
		return 0;

	basep  = of_get_flat_dt_prop(node, "opal-base-address", &basesz);
	entryp = of_get_flat_dt_prop(node, "opal-entry-address", &entrysz);

	if (!basep || !entryp)
		return 1;

	opal.base = of_read_number(basep, basesz/4);
	opal.entry = of_read_number(entryp, entrysz/4);

	pr_debug("OPAL Base  = 0x%llx (basep=%p basesz=%ld)\n",
		 opal.base, basep, basesz);
	pr_debug("OPAL Entry = 0x%llx (entryp=%p basesz=%ld)\n",
		 opal.entry, entryp, entrysz);

	powerpc_firmware_features |= FW_FEATURE_OPAL;
	if (of_flat_dt_is_compatible(node, "ibm,opal-v2")) {
		powerpc_firmware_features |= FW_FEATURE_OPALv2;
		printk("OPAL V2 detected !\n");
	} else {
		printk("OPAL V1 detected !\n");
	}

	/* Hookup some exception handlers. We use the fwnmi area at 0x7000
	 * to provide the glue space to OPAL
	 */
	glue = 0x7000;
	opal_register_exception_handler(OPAL_MACHINE_CHECK_HANDLER,
					__pa(opal_mc_secondary_handler[0]),
					glue);
	glue += 128;
	opal_register_exception_handler(OPAL_HYPERVISOR_MAINTENANCE_HANDLER,
					0, glue);
	glue += 128;
	opal_register_exception_handler(OPAL_SOFTPATCH_HANDLER, 0, glue);

	return 1;
}

int opal_get_chars(uint32_t vtermno, char *buf, int count)
{
	s64 len, rc;
	u64 evt;

	if (!opal.entry)
		return -ENODEV;
	opal_poll_events(&evt);
	if ((evt & OPAL_EVENT_CONSOLE_INPUT) == 0)
		return 0;
	len = count;
	rc = opal_console_read(vtermno, &len, buf);
	if (rc == OPAL_SUCCESS)
		return len;
	return 0;
}

int opal_put_chars(uint32_t vtermno, const char *data, int total_len)
{
	int written = 0;
	s64 len, rc;
	unsigned long flags;
	u64 evt;

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
		rc = opal_console_write_buffer_space(vtermno, &len);
		if (rc || len < total_len) {
			spin_unlock_irqrestore(&opal_write_lock, flags);
			/* Closed -> drop characters */
			if (rc)
				return total_len;
			opal_poll_events(&evt);
			return -EAGAIN;
		}
	}

	/* We still try to handle partial completions, though they
	 * should no longer happen.
	 */
	rc = OPAL_BUSY;
	while(total_len > 0 && (rc == OPAL_BUSY ||
				rc == OPAL_BUSY_EVENT || rc == OPAL_SUCCESS)) {
		len = total_len;
		rc = opal_console_write(vtermno, &len, data);
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
		while(rc == OPAL_SUCCESS && (evt & OPAL_EVENT_CONSOLE_OUTPUT));
	}
	spin_unlock_irqrestore(&opal_write_lock, flags);
	return written;
}

int opal_machine_check(struct pt_regs *regs)
{
	struct opal_machine_check_event *opal_evt = get_paca()->opal_mc_evt;
	struct opal_machine_check_event evt;
	const char *level, *sevstr, *subtype;
	static const char *opal_mc_ue_types[] = {
		"Indeterminate",
		"Instruction fetch",
		"Page table walk ifetch",
		"Load/Store",
		"Page table walk Load/Store",
	};
	static const char *opal_mc_slb_types[] = {
		"Indeterminate",
		"Parity",
		"Multihit",
	};
	static const char *opal_mc_erat_types[] = {
		"Indeterminate",
		"Parity",
		"Multihit",
	};
	static const char *opal_mc_tlb_types[] = {
		"Indeterminate",
		"Parity",
		"Multihit",
	};

	/* Copy the event structure and release the original */
	evt = *opal_evt;
	opal_evt->in_use = 0;

	/* Print things out */
	if (evt.version != OpalMCE_V1) {
		pr_err("Machine Check Exception, Unknown event version %d !\n",
		       evt.version);
		return 0;
	}
	switch(evt.severity) {
	case OpalMCE_SEV_NO_ERROR:
		level = KERN_INFO;
		sevstr = "Harmless";
		break;
	case OpalMCE_SEV_WARNING:
		level = KERN_WARNING;
		sevstr = "";
		break;
	case OpalMCE_SEV_ERROR_SYNC:
		level = KERN_ERR;
		sevstr = "Severe";
		break;
	case OpalMCE_SEV_FATAL:
	default:
		level = KERN_ERR;
		sevstr = "Fatal";
		break;
	}

	printk("%s%s Machine check interrupt [%s]\n", level, sevstr,
	       evt.disposition == OpalMCE_DISPOSITION_RECOVERED ?
	       "Recovered" : "[Not recovered");
	printk("%s  Initiator: %s\n", level,
	       evt.initiator == OpalMCE_INITIATOR_CPU ? "CPU" : "Unknown");
	switch(evt.error_type) {
	case OpalMCE_ERROR_TYPE_UE:
		subtype = evt.u.ue_error.ue_error_type <
			ARRAY_SIZE(opal_mc_ue_types) ?
			opal_mc_ue_types[evt.u.ue_error.ue_error_type]
			: "Unknown";
		printk("%s  Error type: UE [%s]\n", level, subtype);
		if (evt.u.ue_error.effective_address_provided)
			printk("%s    Effective address: %016llx\n",
			       level, evt.u.ue_error.effective_address);
		if (evt.u.ue_error.physical_address_provided)
			printk("%s      Physial address: %016llx\n",
			       level, evt.u.ue_error.physical_address);
		break;
	case OpalMCE_ERROR_TYPE_SLB:
		subtype = evt.u.slb_error.slb_error_type <
			ARRAY_SIZE(opal_mc_slb_types) ?
			opal_mc_slb_types[evt.u.slb_error.slb_error_type]
			: "Unknown";
		printk("%s  Error type: SLB [%s]\n", level, subtype);
		if (evt.u.slb_error.effective_address_provided)
			printk("%s    Effective address: %016llx\n",
			       level, evt.u.slb_error.effective_address);
		break;
	case OpalMCE_ERROR_TYPE_ERAT:
		subtype = evt.u.erat_error.erat_error_type <
			ARRAY_SIZE(opal_mc_erat_types) ?
			opal_mc_erat_types[evt.u.erat_error.erat_error_type]
			: "Unknown";
		printk("%s  Error type: ERAT [%s]\n", level, subtype);
		if (evt.u.erat_error.effective_address_provided)
			printk("%s    Effective address: %016llx\n",
			       level, evt.u.erat_error.effective_address);
		break;
	case OpalMCE_ERROR_TYPE_TLB:
		subtype = evt.u.tlb_error.tlb_error_type <
			ARRAY_SIZE(opal_mc_tlb_types) ?
			opal_mc_tlb_types[evt.u.tlb_error.tlb_error_type]
			: "Unknown";
		printk("%s  Error type: TLB [%s]\n", level, subtype);
		if (evt.u.tlb_error.effective_address_provided)
			printk("%s    Effective address: %016llx\n",
			       level, evt.u.tlb_error.effective_address);
		break;
	default:
	case OpalMCE_ERROR_TYPE_UNKNOWN:
		printk("%s  Error type: Unknown\n", level);
		break;
	}
	return evt.severity == OpalMCE_SEV_FATAL ? 0 : 1;
}

static irqreturn_t opal_interrupt(int irq, void *data)
{
	uint64_t events;

	opal_handle_interrupt(virq_to_hw(irq), &events);

	/* XXX TODO: Do something with the events */

	return IRQ_HANDLED;
}

static int __init opal_init(void)
{
	struct device_node *np, *consoles;
	const u32 *irqs;
	int rc, i, irqlen;

	opal_node = of_find_node_by_path("/ibm,opal");
	if (!opal_node) {
		pr_warn("opal: Node not found\n");
		return -ENODEV;
	}
	if (firmware_has_feature(FW_FEATURE_OPALv2))
		consoles = of_find_node_by_path("/ibm,opal/consoles");
	else
		consoles = of_node_get(opal_node);

	/* Register serial ports */
	for_each_child_of_node(consoles, np) {
		if (strcmp(np->name, "serial"))
			continue;
		of_platform_device_create(np, NULL, NULL);
	}
	of_node_put(consoles);

	/* Find all OPAL interrupts and request them */
	irqs = of_get_property(opal_node, "opal-interrupts", &irqlen);
	pr_debug("opal: Found %d interrupts reserved for OPAL\n",
		 irqs ? (irqlen / 4) : 0);
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
	}
	return 0;
}
subsys_initcall(opal_init);
