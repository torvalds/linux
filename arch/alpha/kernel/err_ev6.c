/*
 *	linux/arch/alpha/kernel/err_ev6.c
 *
 *	Copyright (C) 2000 Jeff Wiedemeier (Compaq Computer Corporation)
 *
 *	Error handling code supporting Alpha systems
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/irq_regs.h>
#include <asm/hwrpb.h>
#include <asm/smp.h>
#include <asm/err_common.h>
#include <asm/err_ev6.h>

#include "err_impl.h"
#include "proto.h"

static int
ev6_parse_ibox(u64 i_stat, int print)
{
	int status = MCHK_DISPOSITION_REPORT;

#define EV6__I_STAT__PAR	(1UL << 29)
#define EV6__I_STAT__ERRMASK	(EV6__I_STAT__PAR)

	if (!(i_stat & EV6__I_STAT__ERRMASK))
		return MCHK_DISPOSITION_UNKNOWN_ERROR;

	if (!print)
		return status;

	if (i_stat & EV6__I_STAT__PAR)
		printk("%s    Icache parity error\n", err_print_prefix);

	return status;
}

static int
ev6_parse_mbox(u64 mm_stat, u64 d_stat, u64 c_stat, int print)
{
	int status = MCHK_DISPOSITION_REPORT;

#define EV6__MM_STAT__DC_TAG_PERR	(1UL << 10)
#define EV6__MM_STAT__ERRMASK		(EV6__MM_STAT__DC_TAG_PERR)
#define EV6__D_STAT__TPERR_P0		(1UL << 0)
#define EV6__D_STAT__TPERR_P1		(1UL << 1)
#define EV6__D_STAT__ECC_ERR_ST		(1UL << 2)
#define EV6__D_STAT__ECC_ERR_LD		(1UL << 3)
#define EV6__D_STAT__SEO		(1UL << 4)
#define EV6__D_STAT__ERRMASK		(EV6__D_STAT__TPERR_P0 |	\
                                         EV6__D_STAT__TPERR_P1 | 	\
                                         EV6__D_STAT__ECC_ERR_ST | 	\
                                         EV6__D_STAT__ECC_ERR_LD | 	\
                                         EV6__D_STAT__SEO)

	if (!(d_stat & EV6__D_STAT__ERRMASK) && 
	    !(mm_stat & EV6__MM_STAT__ERRMASK))
		return MCHK_DISPOSITION_UNKNOWN_ERROR;

	if (!print)
		return status;

	if (mm_stat & EV6__MM_STAT__DC_TAG_PERR)
		printk("%s    Dcache tag parity error on probe\n",
		       err_print_prefix);
	if (d_stat & EV6__D_STAT__TPERR_P0)
		printk("%s    Dcache tag parity error - pipe 0\n",
		       err_print_prefix);
	if (d_stat & EV6__D_STAT__TPERR_P1)
		printk("%s    Dcache tag parity error - pipe 1\n",
		       err_print_prefix);
	if (d_stat & EV6__D_STAT__ECC_ERR_ST)
		printk("%s    ECC error occurred on a store\n", 
		       err_print_prefix);
	if (d_stat & EV6__D_STAT__ECC_ERR_LD)
		printk("%s    ECC error occurred on a %s load\n",
		       err_print_prefix,
		       c_stat ? "" : "speculative ");
	if (d_stat & EV6__D_STAT__SEO)
		printk("%s    Dcache second error\n", err_print_prefix);

	return status;
}

static int
ev6_parse_cbox(u64 c_addr, u64 c1_syn, u64 c2_syn, 
	       u64 c_stat, u64 c_sts, int print)
{
	char *sourcename[] = { "UNKNOWN", "UNKNOWN", "UNKNOWN",
			       "MEMORY", "BCACHE", "DCACHE", 
			       "BCACHE PROBE", "BCACHE PROBE" };
	char *streamname[] = { "D", "I" };
	char *bitsname[] = { "SINGLE", "DOUBLE" };
	int status = MCHK_DISPOSITION_REPORT;
	int source = -1, stream = -1, bits = -1;

#define EV6__C_STAT__BC_PERR		(0x01)
#define EV6__C_STAT__DC_PERR		(0x02)
#define EV6__C_STAT__DSTREAM_MEM_ERR	(0x03)
#define EV6__C_STAT__DSTREAM_BC_ERR	(0x04)
#define EV6__C_STAT__DSTREAM_DC_ERR	(0x05)
#define EV6__C_STAT__PROBE_BC_ERR0	(0x06)	/* both 6 and 7 indicate... */
#define EV6__C_STAT__PROBE_BC_ERR1	(0x07)	/* ...probe bc error.       */
#define EV6__C_STAT__ISTREAM_MEM_ERR	(0x0B)
#define EV6__C_STAT__ISTREAM_BC_ERR	(0x0C)
#define EV6__C_STAT__DSTREAM_MEM_DBL	(0x13)
#define EV6__C_STAT__DSTREAM_BC_DBL	(0x14)
#define EV6__C_STAT__ISTREAM_MEM_DBL	(0x1B)
#define EV6__C_STAT__ISTREAM_BC_DBL	(0x1C)
#define EV6__C_STAT__SOURCE_MEMORY	(0x03)
#define EV6__C_STAT__SOURCE_BCACHE	(0x04)
#define EV6__C_STAT__SOURCE__S		(0)
#define EV6__C_STAT__SOURCE__M 		(0x07)
#define EV6__C_STAT__ISTREAM__S		(3)
#define EV6__C_STAT__ISTREAM__M		(0x01)
#define EV6__C_STAT__DOUBLE__S		(4)
#define EV6__C_STAT__DOUBLE__M		(0x01)
#define EV6__C_STAT__ERRMASK		(0x1F)
#define EV6__C_STS__SHARED		(1 << 0)
#define EV6__C_STS__DIRTY		(1 << 1)
#define EV6__C_STS__VALID		(1 << 2)
#define EV6__C_STS__PARITY		(1 << 3)

	if (!(c_stat & EV6__C_STAT__ERRMASK))
		return MCHK_DISPOSITION_UNKNOWN_ERROR;

	if (!print)
		return status;

	source = EXTRACT(c_stat, EV6__C_STAT__SOURCE);
	stream = EXTRACT(c_stat, EV6__C_STAT__ISTREAM);
	bits = EXTRACT(c_stat, EV6__C_STAT__DOUBLE);

	if (c_stat & EV6__C_STAT__BC_PERR) {
		printk("%s    Bcache tag parity error\n", err_print_prefix);
		source = -1;
	}

	if (c_stat & EV6__C_STAT__DC_PERR) {
		printk("%s    Dcache tag parity error\n", err_print_prefix);
		source = -1;
	}

	if (c_stat == EV6__C_STAT__PROBE_BC_ERR0 ||
	    c_stat == EV6__C_STAT__PROBE_BC_ERR1) {
		printk("%s    Bcache single-bit error on a probe hit\n",
		       err_print_prefix);
		source = -1;
	}

	if (source != -1) 
		printk("%s    %s-STREAM %s-BIT ECC error from %s\n",
		       err_print_prefix,
		       streamname[stream], bitsname[bits], sourcename[source]);

	printk("%s    Address: 0x%016lx\n"
	         "    Syndrome[upper.lower]: %02lx.%02lx\n", 
	       err_print_prefix,
	       c_addr,
	       c2_syn, c1_syn);

	if (source == EV6__C_STAT__SOURCE_MEMORY ||
	    source == EV6__C_STAT__SOURCE_BCACHE) 
		printk("%s    Block status: %s%s%s%s\n",
		       err_print_prefix,
		       (c_sts & EV6__C_STS__SHARED) ? "SHARED " : "",
		       (c_sts & EV6__C_STS__DIRTY)  ? "DIRTY "  : "",
		       (c_sts & EV6__C_STS__VALID)  ? "VALID "  : "",
		       (c_sts & EV6__C_STS__PARITY) ? "PARITY " : "");
		
	return status;
}

void
ev6_register_error_handlers(void)
{
	/* None right now. */
}

int
ev6_process_logout_frame(struct el_common *mchk_header, int print)
{
	struct el_common_EV6_mcheck *ev6mchk = 
		(struct el_common_EV6_mcheck *)mchk_header;
	int status = MCHK_DISPOSITION_UNKNOWN_ERROR;

	status |= ev6_parse_ibox(ev6mchk->I_STAT, print);
	status |= ev6_parse_mbox(ev6mchk->MM_STAT, ev6mchk->DC_STAT, 
				 ev6mchk->C_STAT, print);
	status |= ev6_parse_cbox(ev6mchk->C_ADDR, ev6mchk->DC1_SYNDROME,
				 ev6mchk->DC0_SYNDROME, ev6mchk->C_STAT,
				 ev6mchk->C_STS, print);

	if (!print)
		return status;

	if (status != MCHK_DISPOSITION_DISMISS) {
		char *saved_err_prefix = err_print_prefix;

		/*
		 * Dump some additional information from the frame
		 */
		printk("%s    EXC_ADDR: 0x%016lx   IER_CM: 0x%016lx"
		            "   ISUM: 0x%016lx\n"
		         "    PAL_BASE: 0x%016lx   I_CTL:  0x%016lx"
		            "   PCTX: 0x%016lx\n",
		       err_print_prefix,
		       ev6mchk->EXC_ADDR, ev6mchk->IER_CM, ev6mchk->ISUM,
		       ev6mchk->PAL_BASE, ev6mchk->I_CTL, ev6mchk->PCTX);

		if (status == MCHK_DISPOSITION_UNKNOWN_ERROR) {
			printk("%s    UNKNOWN error, frame follows:\n",
			       err_print_prefix);
		} else {
			/* had decode -- downgrade print level for frame */
			err_print_prefix = KERN_NOTICE;
		}

		mchk_dump_logout_frame(mchk_header);

		err_print_prefix = saved_err_prefix;
	}

	return status;
}

void
ev6_machine_check(u64 vector, u64 la_ptr)
{
	struct el_common *mchk_header = (struct el_common *)la_ptr;

	/*
	 * Sync the processor
	 */
	mb();
	draina();

	/*
	 * Parse the logout frame without printing first. If the only error(s)
	 * found are have a disposition of "dismiss", then just dismiss them
	 * and don't print any message
	 */
	if (ev6_process_logout_frame(mchk_header, 0) != 
	    MCHK_DISPOSITION_DISMISS) {
		char *saved_err_prefix = err_print_prefix;
		err_print_prefix = KERN_CRIT;

		/*
		 * Either a nondismissable error was detected or no
		 * recognized error was detected  in the logout frame 
		 * -- report the error in either case
		 */
		printk("%s*CPU %s Error (Vector 0x%x) reported on CPU %d:\n", 
		       err_print_prefix,
		       (vector == SCB_Q_PROCERR)?"Correctable":"Uncorrectable",
		       (unsigned int)vector, (int)smp_processor_id());
		
		ev6_process_logout_frame(mchk_header, 1);
		dik_show_regs(get_irq_regs(), NULL);

		err_print_prefix = saved_err_prefix;
	}

	/* 
	 * Release the logout frame 
	 */
	wrmces(0x7);
	mb();
}

