// SPDX-License-Identifier: GPL-2.0
/*
 * Machine check handling for Tsunami/Typhoon systems.
 * Author: Magnus Lindholm <linmag7@gmail.com>
 * Based on Titan error handling by Jeff Wiedemeier (Compaq).
 * Register definitions: 21272 Hardware Reference Manual, revision 4.0.
 */
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <asm/io.h>
#include <asm/hwrpb.h>
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

static const char * const CLIPPER_EnvQW1SMIR[] = {
	"", /* SMIR<0>: retain as raw context until its semantics are known. */
	"OCP or RMC halt detected",
	"Sys_DC_Notok failure detected",
	"",
	"System temperature over 50 degrees C failure",
	"PCI Bus #0 is in reset",
	"PCI Bus #1 is in reset",
	"System is being reset"
};

static const char * const CLIPPER_EnvQW2CPUIR[] = {
	"CPU0 regulator is enabled",
	"CPU1 regulator is enabled",
	"CPU2 regulator is enabled",
	"CPU3 regulator is enabled",
	"CPU0 regulator or configuration sequence fail",
	"CPU1 regulator or configuration sequence fail",
	"CPU2 regulator or configuration sequence fail",
	"CPU3 regulator or configuration sequence fail"
};

static const char * const CLIPPER_EnvQW3PSIR[] = {
	"Power Supply 0 is enabled",
	"Power Supply 1 is enabled",
	"Power Supply 2 is enabled",
	"",
	"Power Supply 0 was enabled but failed",
	"Power Supply 1 was enabled but failed",
	"Power Supply 2 was enabled but failed"
};

static const char * const CLIPPER_EnvQW4LM78ISR[] = {
	"PS +3.3V out of tolerance",
	"PS +5V out of tolerance",
	"PS +12V out of tolerance",
	"VTERM out of tolerance",
	"Temperature zone 0 (PCI Backplane slots 1-3 area) over limit failure",
	"LM75 CPU0-3 Temperature over limit failure",
	"System Fan 1 failure",
	"System Fan 2 failure",
	"CTERM out of tolerance",
	"",
	"-12V out of tolerance",
	"",
	"",
	"",
	"",
	"",
	"CPU0_VCORE +2V out of tolerance",
	"CPU0_VIO +1.5V out of tolerance",
	"CPU1_VCORE +2V out of tolerance",
	"CPU1_VIO +1.5V out of tolerance",
	"Temperature zone 1 (PCI Backplane slots 7-10 area) over limit failure",
	"",
	"System Fan 4 failure",
	"System Fan 5 failure",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"CPU2_VCORE +2V out of tolerance",
	"CPU2_VIO +1.5V out of tolerance",
	"CPU3_VCORE +2V out of tolerance",
	"CPU3_VIO +1.5V out of tolerance",
	"Temperature zone 2 (PCI Backplane slots 4-6 area) over limit failure",
	"",
	"System Fan 3 failure",
	"System Fan 6 failure",
	"",
	"",
	"Power supply 3.3V rail above high amperage warning",
	"Power supply 5.0V rail above high amperage warning",
	"Power supply 12V rail above high amperage warning",
	"Power supply high temperature warning",
	"Power supply AC input low limit warning",
	"Power supply AC input high limit warning"
};

static const char * const CLIPPER_EnvQW5DOOR[] = {
	"",
	"System CPU door is open",
	"System Fan door is open",
	"System PCI door is open",
	"",
	"System CPU door is closed",
	"System Fan door is closed",
	"System PCI door is closed"
};

static const char * const CLIPPER_EnvQW6TEMP[] = {
	"CPU0 temperature warning fault has occurred",
	"CPU1 temperature warning fault has occurred",
	"CPU2 temperature warning fault has occurred",
	"CPU3 temperature warning fault has occurred",
	"System temperature zone 0 warning fault has occurred",
	"System temperature zone 1 warning fault has occurred",
	"System temperature zone 2 warning fault has occurred"
};

static const char * const CLIPPER_EnvQW7FAN[] = {
	"System Fan 1 is not responding to RMC Commands",
	"System Fan 2 is not responding to RMC Commands",
	"System Fan 3 is not responding to RMC Commands",
	"System Fan 4 is not responding to RMC Commands",
	"System Fan 5 is not responding to RMC Commands",
	"System Fan 6 is not responding to RMC Commands",
	"",
	"",
	"CPU fans 5/6 at maximum speed",
	"CPU fans 5/6 reduced speed from maximum",
	"PCI fans 1-4 at maximum speed",
	"PCI fans 1-4 reduced speed from maximum"
};

#define CLIPPER_ENV_LM78ISR_MASK	0xfcdf00df05ffUL

/* ES40 Service Guide EK-ES240-SV A01, live frame: Tables D-20 and D-21. */
struct el_clipper_envdata {
	u64 summary;
	u64 dir;
	u64 smir;
	u64 cpuir;
	u64 psir;
	u64 fault;
	u64 doors;
	u64 temp_warn;
	u64 fan_ctrl;
	u64 fatal_power_down;
	u64 reserved;
};

static bool
clipper_print_bits(const char * const *messages, size_t count,
		   u64 value, u64 mask)
{
	size_t i;

	if (!(value & mask))
		return false;
	for (i = 0; i < count; i++)
		if (value & mask & (1UL << i))
			printk("%s    %s\n", err_print_prefix, messages[i]);
	return true;
}

#define CLIPPER_BITS(table, value, mask) \
	clipper_print_bits(table, ARRAY_SIZE(table), value, mask)

static void
clipper_process_environment(struct el_common *header)
{
	struct el_clipper_envdata *frame;
	bool fault = false;

	if (!tsunami_frame_valid(header, sizeof(*frame))) {
		printk("%s  Truncated Clipper environmental frame\n", err_print_prefix);
		mchk_dump_logout_frame_raw(header);
		return;
	}
	frame = (void *)header + header->sys_offset;
	/* Only QW1 through QW7 carry error state in the live 680 frame. */
	/* PSU summary and inverted halt/reset signals remain raw context. */
	fault |= CLIPPER_BITS(CLIPPER_EnvQW1SMIR, frame->smir, 0x14);
	fault |= CLIPPER_BITS(CLIPPER_EnvQW2CPUIR, frame->cpuir, 0xf0);
	fault |= CLIPPER_BITS(CLIPPER_EnvQW3PSIR, frame->psir, 0x70);
	fault |= CLIPPER_BITS(CLIPPER_EnvQW4LM78ISR,
			       frame->fault, CLIPPER_ENV_LM78ISR_MASK);
	fault |= CLIPPER_BITS(CLIPPER_EnvQW5DOOR, frame->doors, 0x0e);
	fault |= CLIPPER_BITS(CLIPPER_EnvQW6TEMP, frame->temp_warn, 0x7f);
	fault |= CLIPPER_BITS(CLIPPER_EnvQW7FAN, frame->fan_ctrl, 0x3f);
	/* A notification with no recognized fault can describe recovery. */
	if (!fault)
		printk("%s    No recognized environmental fault bits set\n",
		       err_print_prefix);
	printk("%s  Environmental summary: %016llx DIR: %016llx\n",
	       err_print_prefix, frame->summary, frame->dir);
	printk("%s  SMIR: %016llx (PSU summary/halt/reset bits undecoded)\n",
	       err_print_prefix, frame->smir);
	CLIPPER_BITS(CLIPPER_EnvQW2CPUIR, frame->cpuir, 0x0f);
	CLIPPER_BITS(CLIPPER_EnvQW3PSIR, frame->psir, 0x07);
	CLIPPER_BITS(CLIPPER_EnvQW5DOOR, frame->doors, 0xe0);
	CLIPPER_BITS(CLIPPER_EnvQW7FAN, frame->fan_ctrl, 0xf00);
	if (frame->fault & (0x3fUL << 42))
		printk("%s  Power supply warning source: %llu (3 = reserved)\n",
		       err_print_prefix, (frame->fault >> 40) & 3);
}

void
clipper_machine_check(unsigned long vector, unsigned long la_ptr)
{
	struct el_common *header = (void *)la_ptr;
	char *saved_err_prefix = err_print_prefix;

	if (vector != SCB_Q_SYSEVENT) {
		tsunami_machine_check(vector, la_ptr);
		return;
	}
	/* Drain pending accesses before inspecting the environmental frame. */
	mb();
	draina();
	err_print_prefix = KERN_CRIT;
	printk("%sSystem correctable environmental event (code %x) on CPU %d:\n",
	       err_print_prefix, header->code, smp_processor_id());
	if (header->code == 0x206)
		clipper_process_environment(header);
	else {
		printk("%s  Unrecognized environmental event; raw frame follows\n",
		       err_print_prefix);
		mchk_dump_logout_frame_raw(header);
	}
	err_print_prefix = saved_err_prefix;
	wrmces(0x7);
	/* Complete acknowledgment before returning from the event. */
	mb();
}

static struct el_subpacket * __init
tsunami_process_subpacket(struct el_subpacket *packet)
{
	struct el_common *frame;
	size_t offset = offsetof(struct el_subpacket, by_type.regatta_frame.data_start);
	int status;

	/* Raw dumps consume whole quadwords, and the next packet must advance. */
	if (packet->length < offsetof(struct el_subpacket, by_type) ||
	    (packet->length & 7))
		return NULL;
	if (packet->revision != 1) {
		el_annotate_subpacket(packet);
		return (void *)packet + packet->length;
	}

	switch (packet->type) {
	case EL_TYPE__REGATTA__PROCESSOR_ERROR_FRAME:
	case EL_TYPE__REGATTA__PROCESSOR_DBL_ERROR_HALT:
	case EL_TYPE__REGATTA__SYSTEM_ERROR_FRAME:
	case EL_TYPE__REGATTA__SYSTEM_DBL_ERROR_HALT:
		if (packet->length < offset + sizeof(*frame))
			return NULL;
		frame = (void *)packet + offset;
		if (frame->size > packet->length - offset || frame->size < sizeof(*frame))
			return NULL;
		if (packet->type == EL_TYPE__REGATTA__PROCESSOR_ERROR_FRAME ||
		    packet->type == EL_TYPE__REGATTA__PROCESSOR_DBL_ERROR_HALT) {
			printk("%s  ** Occurred on CPU %d:\n", err_print_prefix,
			       (int)packet->by_type.regatta_frame.cpuid);
			status = ev6_process_logout_frame(frame, 0);
			if (status != MCHK_DISPOSITION_DISMISS)
				ev6_process_logout_frame(frame, 1);
		} else {
			tsunami_process_logout_frame(frame, 1);
		}
		break;
	case EL_TYPE__REGATTA__ENVIRONMENTAL_FRAME:
		/*
		 * ES40 Table D-18 gives sys_offset=0x20, but places the
		 * summary at frame+0x18. Preserve the complete packet until
		 * the console layout is verified; do not apply the live layout.
		 */
		printk("%s  Console environmental layout unverified; raw packet follows\n",
		       err_print_prefix);
		el_annotate_subpacket(packet);
		break;
	default:
		el_annotate_subpacket(packet);
		break;
	}
	return (void *)packet + packet->length;
}

/*
 * Clipper console logs have an HWRPB byte count. Do not use the generic
 * unbounded dispatcher: a rejected record must not acknowledge the log.
 */
static bool __init
tsunami_console_log(void *data, size_t remaining)
{
	struct el_subpacket *packet;
	unsigned char *pos = data;
	size_t size, length;
	u32 count;
	union el_timestamp timestamp;
	const char *name;

	while (remaining >= offsetof(struct el_subpacket, by_type)) {
		packet = (void *)pos;
		size = packet->length;
		if (size < offsetof(struct el_subpacket, by_type) ||
		    size > remaining || (size & 7))
			return false;
		if (packet->class == EL_CLASS__TERMINATION)
			return packet->type == EL_TYPE__TERMINATION__TERMINATION;
		if (packet->class != EL_CLASS__HEADER || packet->revision != 1)
			return false;

		timestamp.as_int = 0;
		/* Check each header's extent before accessing its union member. */
#define CONSOLE_HEADER_SIZE(member) \
	(offsetof(struct el_subpacket, by_type.member) + \
	 sizeof(packet->by_type.member))
		switch (packet->type) {
		case EL_TYPE__HEADER__SYSTEM_ERROR_FRAME:
			if (size < CONSOLE_HEADER_SIZE(sys_err))
				return false;
			name = "SYSTEM ERROR";
			length = packet->by_type.sys_err.frame_length;
			count = packet->by_type.sys_err.frame_packet_count;
			break;
		case EL_TYPE__HEADER__SYSTEM_EVENT_FRAME:
			if (size < CONSOLE_HEADER_SIZE(sys_event))
				return false;
			name = "SYSTEM EVENT";
			timestamp = packet->by_type.sys_event.timestamp;
			length = packet->by_type.sys_event.frame_length;
			count = packet->by_type.sys_event.frame_packet_count;
			break;
		case EL_TYPE__HEADER__HALT_FRAME:
			if (size < CONSOLE_HEADER_SIZE(err_halt))
				return false;
			name = "ERROR HALT";
			timestamp = packet->by_type.err_halt.timestamp;
			length = packet->by_type.err_halt.frame_length;
			count = packet->by_type.err_halt.frame_packet_count;
			break;
		case EL_TYPE__HEADER__LOGOUT_FRAME:
			if (size < CONSOLE_HEADER_SIZE(logout_header))
				return false;
			name = "LOGOUT FRAME";
			length = packet->by_type.logout_header.frame_length;
			count = 1;
			break;
		default:
			return false;
		}
#undef CONSOLE_HEADER_SIZE
		if (length > remaining - size || (length & 7) ||
		    count > length / offsetof(struct el_subpacket, by_type))
			return false;
		printk("%s*** %s: CLASS %u, TYPE %u\n", err_print_prefix,
		       name, packet->class, packet->type);
		el_print_timestamp(&timestamp);
		if (packet->type == EL_TYPE__HEADER__HALT_FRAME)
			printk("%s  HALT CODE: %04x\n", err_print_prefix,
			       packet->by_type.err_halt.halt_code);
		pos += size;
		remaining -= size + length;
		while (count--) {
			if (length < offsetof(struct el_subpacket, by_type))
				return false;
			packet = (void *)pos;
			size = packet->length;
			if (size < offsetof(struct el_subpacket, by_type) ||
			    size > length || (size & 7) ||
			    packet->class != EL_CLASS__REGATTA_FAMILY)
				return false;
			/* This handler checks embedded logout-frame bounds. */
			if (tsunami_process_subpacket(packet) != (void *)pos + size)
				return false;
			pos += size;
			length -= size;
		}
		if (length)
			return false;
	}
	/* Exhaustion without a complete termination packet is incomplete. */
	return false;
}

static void __init
tsunami_console_data_log(int cpu, struct percpu_struct *pcpu)
{
	unsigned char *data = (void *)(IDENT_ADDR | pcpu->console_data_log_pa);
	size_t length = pcpu->console_data_log_length;
	size_t dump = length < 1024 ? length : 1024;

	printk("%sConsole data log for CPU %d (%zu bytes):\n",
	       err_print_prefix, cpu, length);
	if (!(pcpu->console_data_log_pa & 7) &&
	    length <= ~0UL - (unsigned long)data &&
	    tsunami_console_log(data, length)) {
		/* Acknowledge only after reaching a valid termination packet. */
		pcpu->console_data_log_pa = 0;
		return;
	}
	printk("%s  Incomplete console log; retaining firmware pointer\n",
	       err_print_prefix);
	/* Byte reads preserve partial headers without rounding beyond length. */
	if (dump > ~0UL - (unsigned long)data)
		return;
	print_hex_dump(err_print_prefix, "  ", DUMP_PREFIX_OFFSET, 16, 1,
		       data, dump, false);
	if (dump < length)
		printk("%s  Raw console log truncated to %zu bytes\n",
		       err_print_prefix, dump);
}

void __init
tsunami_check_console_data_log(void)
{
	struct percpu_struct *pcpu;
	unsigned long cpu;

	for (cpu = 0; cpu < hwrpb->nr_processors; cpu++) {
		pcpu = (void *)hwrpb + hwrpb->processor_offset +
			cpu * hwrpb->processor_size;
		if (pcpu->console_data_log_pa)
			tsunami_console_data_log(cpu, pcpu);
	}
}
