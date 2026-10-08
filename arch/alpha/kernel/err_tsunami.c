// SPDX-License-Identifier: GPL-2.0
/*
 * Machine check handling for Tsunami/Typhoon systems.
 * Author: Magnus Lindholm <linmag7@gmail.com>
 * Based on Titan error handling by Jeff Wiedemeier (Compaq).
 * Register definitions: 21272 Hardware Reference Manual, revision 4.0.
 */
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/core_tsunami.h>
#include <asm/smp.h>
#include <asm/err_common.h>
#include <asm/irq_regs.h>
#include "err_impl.h"
#include "proto.h"
#include "pci_impl.h"

#define TSUNAMI_PERROR_ERRMASK	0xdffUL
#define TSUNAMI_PERROR_ECCMASK	(perror_m_uecc | perror_m_cre)
#define TSUNAMI_PERROR_INV	(1UL << 51)
#define TSUNAMI_MISC_NXM		(1UL << 28)

static int
tsunami_frame_valid(struct el_common *header, size_t size)
{
	return !((header->sys_offset | header->size) & 7) &&
	       header->sys_offset >= sizeof(*header) &&
	       header->sys_offset <= header->size &&
	       size <= header->size - header->sys_offset;
}

static int
tsunami_parse_p_perror(int which, u64 perror, int print)
{
	static const char * const errors[] = {
		"Lost error", "SERR asserted", "PERR asserted",
		"Delayed-completion retry timeout", "Invalid scatter-gather PTE",
		"Address parity error", "Target abort", "Read data parity error",
		"No DEVSEL (master abort)", NULL,
		"Uncorrectable ECC error", "Correctable ECC error"
	};
	static const char * const pci_cmd[] = {
		"Interrupt acknowledge", "Special cycle", "I/O read", "I/O write",
		"Reserved", "Reserved", "Memory read", "Memory write",
		"Reserved", "Reserved", "Configuration read", "Configuration write",
		"Memory read multiple", "Dual address cycle",
		"Memory read line", "Memory write and invalidate"
	};
	unsigned long addr;
	unsigned int cmd;
	int i;

	if (!(perror & TSUNAMI_PERROR_ERRMASK))
		return MCHK_DISPOSITION_UNKNOWN_ERROR;
	/* Do not import Titan's BIOS master-abort dismissal policy. */
	if (!print)
		return MCHK_DISPOSITION_REPORT;

	printk("%s  Pchip %d PERROR: %016llx\n", err_print_prefix, which, perror);
	for (i = 0; i < ARRAY_SIZE(errors); i++)
		if ((perror & TSUNAMI_PERROR_ERRMASK & (1UL << i)) && errors[i])
			printk("%s    %s\n", err_print_prefix, errors[i]);

	/*
	 * INV invalidates SYN, CMD and ADDR. HRM 8.8.2.6 leaves command/address
	 * undefined whenever SERR is set, including with other error bits.
	 */
	if (perror & (TSUNAMI_PERROR_INV | perror_m_serr)) {
		printk("%s    Transaction information unavailable\n", err_print_prefix);
		return MCHK_DISPOSITION_REPORT;
	}
	cmd = (perror & perror_m_cmd) >> 52;
	/*
	 * Table 10-42 selects the ECC fields whenever CRE or UECC is set.
	 * The first error freezes PERROR (10.2.5.6); mixed error bits were
	 * detected together. Only LOST can be set afterwards.
	 */
	if (perror & TSUNAMI_PERROR_ECCMASK) {
		const char *command = "Reserved";

		if (cmd == 0)
			command = "DMA read";
		else if (cmd == 1)
			command = "DMA RMW";
		else if (cmd == 3)
			command = "SGTE read";
		addr = ((perror >> 19) & 0xffffffffUL) << 3;
		printk("%s    Command: %s (%x), syndrome: %02llx\n",
		       err_print_prefix, command, cmd, (perror & perror_m_syn) >> 56);
		printk("%s    System address: %lx\n", err_print_prefix, addr);
	} else {
		addr = ((perror >> 18) & 0x3fffffffUL) << 2;
		/* SGE captures only PCI address bits <31:13> (8KB page). */
		if (perror & perror_m_sge)
			addr &= ~0x1fffUL;
		printk("%s    Command: %s (%x), PCI address: %lx%s\n",
		       err_print_prefix, pci_cmd[cmd], cmd, addr,
		       (perror & perror_m_sge) ? " (8KB page)" : "");
		if (perror & (1UL << 17))
			printk("%s    Monster window\n", err_print_prefix);
		else if (perror & (1UL << 16))
			printk("%s    DAC scatter-gather window 3\n", err_print_prefix);
	}
	return MCHK_DISPOSITION_REPORT;
}

/* SESF bits 0/1 identify PCI errors, bit 2 covers ECC on either Pchip/CPU. */
static u64
tsunami_pchip_error(struct el_TSUNAMI_sysdata_mcheck *frame, int which)
{
	u64 perror = which ? frame->p1_perror : frame->p0_perror;

	if (frame->sesf & (1UL << which))
		return perror;
	if ((frame->sesf & 4) && (perror & TSUNAMI_PERROR_ECCMASK))
		return perror;
	return 0;
}

static struct el_TSUNAMI_sysdata_mcheck *
tsunami_system_frame(struct el_common *header)
{
	if (!tsunami_frame_valid(header, sizeof(struct el_TSUNAMI_sysdata_mcheck)))
		return NULL;
	return (void *)header + header->sys_offset;
}

static int
tsunami_process_logout_frame(struct el_common *header, int print)
{
	struct el_TSUNAMI_sysdata_mcheck *frame = tsunami_system_frame(header);
	int status = MCHK_DISPOSITION_UNKNOWN_ERROR;
	int i;

	if (!frame) {
		if (print) {
			printk("%s  Invalid Tsunami system frame; raw frame follows\n",
			       err_print_prefix);
			mchk_dump_logout_frame_raw(header);
		}
		return MCHK_DISPOSITION_UNKNOWN_ERROR;
	}
	if (print)
		printk("%s  SESF: %016llx DIR: %016llx MISC: %016llx\n",
		       err_print_prefix, frame->sesf, frame->dir, frame->misc);
	if (frame->misc & TSUNAMI_MISC_NXM) {
		status = MCHK_DISPOSITION_REPORT;
		if (print)
			printk("%s  Non-existent memory, NXS: %llu\n",
			       err_print_prefix, (frame->misc >> 29) & 7);
	}
	for (i = 0; i < 2; i++)
		status |= tsunami_parse_p_perror(i, tsunami_pchip_error(frame, i), print);
	return status;
}

struct tsunami_error_state {
	u64 perror[2];
	u64 misc;
};

static void
tsunami_ack_pchip(tsunami_pchip *pchip, u64 perror)
{
	if (!(perror & TSUNAMI_PERROR_ERRMASK))
		return;
	pchip->perror.csr = perror & TSUNAMI_PERROR_ERRMASK;
	/* Complete the W1C write before releasing the logout frame. */
	mb();
	pchip->perror.csr;
}

static void
tsunami_clear_errors(struct tsunami_error_state *state)
{
	bool pchip1 = TSUNAMI_cchip->csc.csr & (1UL << 14);

	/* Preserve each chip's transaction and the NXM source before clearing. */
	state->perror[0] = TSUNAMI_pchip0->perror.csr;
	state->perror[1] = pchip1 ? TSUNAMI_pchip1->perror.csr : 0;
	state->misc = TSUNAMI_cchip->misc.csr;

	/*
	 * Acknowledge both present Pchips: the saved frame may predate
	 * live errors. This can consume another chip's pending error
	 * with INV set before it reaches DRIR, losing its transaction
	 * details and subsequent system-error notification.
	 */
	tsunami_ack_pchip(TSUNAMI_pchip0, state->perror[0]);
	/* Do not access the second Pchip on single-Pchip systems. */
	if (pchip1)
		tsunami_ack_pchip(TSUNAMI_pchip1, state->perror[1]);
	if (state->misc & TSUNAMI_MISC_NXM) {
		/* Zero writes leave the other MISC fields unchanged. */
		TSUNAMI_cchip->misc.csr = TSUNAMI_MISC_NXM;
		/* Complete the NXM acknowledgment before releasing the frame. */
		mb();
		TSUNAMI_cchip->misc.csr;
	}
}

static void
tsunami_report_live_errors(struct tsunami_error_state *state,
			  struct el_TSUNAMI_sysdata_mcheck *frame)
{
	int i;

	/* PALcode's frame may predate these errors, or describe another chip. */
	for (i = 0; i < 2; i++) {
		if (!(state->perror[i] & TSUNAMI_PERROR_ERRMASK) ||
		    (frame && state->perror[i] == tsunami_pchip_error(frame, i)))
			continue;
		printk("%s  Live Pchip status before acknowledgment:\n", err_print_prefix);
		tsunami_parse_p_perror(i, state->perror[i], 1);
	}
	if ((state->misc & TSUNAMI_MISC_NXM) &&
	    (!frame || ((state->misc ^ frame->misc) & (0xfUL << 28))))
		printk("%s  Live MISC: %016llx, non-existent memory, NXS: %llu\n",
		       err_print_prefix, state->misc, (state->misc >> 29) & 7);
}

void
tsunami_machine_check(unsigned long vector, unsigned long la_ptr)
{
	struct el_common *header = (void *)la_ptr;
	struct el_TSUNAMI_sysdata_mcheck *frame = NULL;
	struct tsunami_error_state live = {};
	char *saved_err_prefix = err_print_prefix;
	bool show_regs = vector == SCB_Q_SYSMCHK;

	/* Synchronize pending accesses before inspecting the chipset. */
	mb();
	mb();
	draina();
	/* CPU/event vectors must not consume a pending Pchip system error. */
	if (vector == SCB_Q_SYSMCHK || vector == SCB_Q_SYSERR) {
		tsunami_clear_errors(&live);
		frame = tsunami_system_frame(header);
	}

#ifdef CONFIG_VERBOSE_MCHECK
	show_regs |= alpha_verbose_mcheck != 0;
#endif
	err_print_prefix = KERN_CRIT;
	if (vector == SCB_Q_SYSMCHK || vector == SCB_Q_SYSERR) {
		printk("%sSystem %s error (vector %lx, code %x) on CPU %d:\n",
		       err_print_prefix, vector == SCB_Q_SYSERR ? "correctable" :
		       "uncorrectable", vector, header->code, smp_processor_id());
		tsunami_process_logout_frame(header, 1);
		if (show_regs)
			dik_show_regs(get_irq_regs(), NULL);
		tsunami_report_live_errors(&live, frame);
	} else if (vector == SCB_Q_SYSEVENT) {
		printk("%sTsunami environmental event (code %x): layout unknown\n",
		       err_print_prefix, header->code);
		mchk_dump_logout_frame_raw(header);
	}
	err_print_prefix = saved_err_prefix;

	if (vector != SCB_Q_SYSMCHK && vector != SCB_Q_SYSERR &&
	    vector != SCB_Q_SYSEVENT) {
		/* EV6 decoding also releases the logout frame. */
		ev6_machine_check(vector, la_ptr);
	} else {
		wrmces(0x7);
		/* Complete acknowledgment before resuming. */
		mb();
	}
}
