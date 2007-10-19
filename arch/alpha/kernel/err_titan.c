/*
 *	linux/arch/alpha/kernel/err_titan.c
 *
 *	Copyright (C) 2000 Jeff Wiedemeier (Compaq Computer Corporation)
 *
 *	Error handling code supporting TITAN systems
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/core_titan.h>
#include <asm/hwrpb.h>
#include <asm/smp.h>
#include <asm/err_common.h>
#include <asm/err_ev6.h>
#include <asm/irq_regs.h>

#include "err_impl.h"
#include "proto.h"


static int
titan_parse_c_misc(u64 c_misc, int print)
{
#ifdef CONFIG_VERBOSE_MCHECK
	char *src;
	int nxs = 0;
#endif
	int status = MCHK_DISPOSITION_REPORT;

#define TITAN__CCHIP_MISC__NXM		(1UL << 28)
#define TITAN__CCHIP_MISC__NXS__S	(29)
#define TITAN__CCHIP_MISC__NXS__M	(0x7)

	if (!(c_misc & TITAN__CCHIP_MISC__NXM))
		return MCHK_DISPOSITION_UNKNOWN_ERROR;

#ifdef CONFIG_VERBOSE_MCHECK
	if (!print)
		return status;

	nxs = EXTRACT(c_misc, TITAN__CCHIP_MISC__NXS);
	switch(nxs) {
	case 0:	/* CPU 0 */
	case 1:	/* CPU 1 */
	case 2:	/* CPU 2 */
	case 3:	/* CPU 3 */
		src = "CPU";
		/* num is already the CPU number */
		break;
	case 4:	/* Pchip 0 */
	case 5:	/* Pchip 1 */
		src = "Pchip";
		nxs -= 4;
		break;
	default:/* reserved */
		src = "Unknown, NXS =";
		/* leave num untouched */
		break;
	}

	printk("%s    Non-existent memory access from: %s %d\n", 
	       err_print_prefix, src, nxs);
#endif /* CONFIG_VERBOSE_MCHECK */

	return status;
}

static int
titan_parse_p_serror(int which, u64 serror, int print)
{
	int status = MCHK_DISPOSITION_REPORT;

#ifdef CONFIG_VERBOSE_MCHECK
	char *serror_src[] = {"GPCI", "APCI", "AGP HP", "AGP LP"};
	char *serror_cmd[] = {"DMA Read", "DMA RMW", "SGTE Read", "Reserved"};
#endif /* CONFIG_VERBOSE_MCHECK */

#define TITAN__PCHIP_SERROR__LOST_UECC	(1UL << 0)
#define TITAN__PCHIP_SERROR__UECC	(1UL << 1)
#define TITAN__PCHIP_SERROR__CRE	(1UL << 2)
#define TITAN__PCHIP_SERROR__NXIO	(1UL << 3)
#define TITAN__PCHIP_SERROR__LOST_CRE	(1UL << 4)
#define TITAN__PCHIP_SERROR__ECCMASK	(TITAN__PCHIP_SERROR__UECC |	  \
					 TITAN__PCHIP_SERROR__CRE)
#define TITAN__PCHIP_SERROR__ERRMASK	(TITAN__PCHIP_SERROR__LOST_UECC | \
					 TITAN__PCHIP_SERROR__UECC |	  \
					 TITAN__PCHIP_SERROR__CRE |	  \
					 TITAN__PCHIP_SERROR__NXIO |	  \
					 TITAN__PCHIP_SERROR__LOST_CRE)
#define TITAN__PCHIP_SERROR__SRC__S	(52)
#define TITAN__PCHIP_SERROR__SRC__M	(0x3)
#define TITAN__PCHIP_SERROR__CMD__S	(54)
#define TITAN__PCHIP_SERROR__CMD__M	(0x3)
#define TITAN__PCHIP_SERROR__SYN__S	(56)
#define TITAN__PCHIP_SERROR__SYN__M	(0xff)
#define TITAN__PCHIP_SERROR__ADDR__S	(15)
#define TITAN__PCHIP_SERROR__ADDR__M	(0xffffffffUL)

	if (!(serror & TITAN__PCHIP_SERROR__ERRMASK))
		return MCHK_DISPOSITION_UNKNOWN_ERROR;

#ifdef CONFIG_VERBOSE_MCHECK
	if (!print)
		return status;

	printk("%s  PChip %d SERROR: %016lx\n", 
	       err_print_prefix, which, serror);
	if (serror & TITAN__PCHIP_SERROR__ECCMASK) {
		printk("%s    %sorrectable ECC Error:\n"
		       "      Source: %-6s  Command: %-8s  Syndrome: 0x%08x\n"
		       "      Address: 0x%lx\n", 
		       err_print_prefix,
		       (serror & TITAN__PCHIP_SERROR__UECC) ? "Unc" : "C",
		       serror_src[EXTRACT(serror, TITAN__PCHIP_SERROR__SRC)],
		       serror_cmd[EXTRACT(serror, TITAN__PCHIP_SERROR__CMD)],
		       (unsigned)EXTRACT(serror, TITAN__PCHIP_SERROR__SYN),
		       EXTRACT(serror, TITAN__PCHIP_SERROR__ADDR));
	}
	if (serror & TITAN__PCHIP_SERROR__NXIO)
		printk("%s    Non Existent I/O Error\n", err_print_prefix);
	if (serror & TITAN__PCHIP_SERROR__LOST_UECC)
		printk("%s    Lost Uncorrectable ECC Error\n", 
		       err_print_prefix);
	if (serror & TITAN__PCHIP_SERROR__LOST_CRE)
		printk("%s    Lost Correctable ECC Error\n", err_print_prefix);
#endif /* CONFIG_VERBOSE_MCHECK */

	return status;
}

static int 
titan_parse_p_perror(int which, int port, u64 perror, int print)
{
	int cmd;
	unsigned long addr;
	int status = MCHK_DISPOSITION_REPORT;

#ifdef CONFIG_VERBOSE_MCHECK
	char *perror_cmd[] = { "Interrupt Acknowledge", "Special Cycle",
			       "I/O Read",	       	"I/O Write",
			       "Reserved",	       	"Reserved",
			       "Memory Read",		"Memory Write",
			       "Reserved",		"Reserved",
			       "Configuration Read",	"Configuration Write",
			       "Memory Read Multiple",	"Dual Address Cycle",
			       "Memory Read Line","Memory Write and Invalidate"
	};
#endif /* CONFIG_VERBOSE_MCHECK */

#define TITAN__PCHIP_PERROR__LOST	(1UL << 0)
#define TITAN__PCHIP_PERROR__SERR	(1UL << 1)
#define TITAN__PCHIP_PERROR__PERR	(1UL << 2)
#define TITAN__PCHIP_PERROR__DCRTO	(1UL << 3)
#define TITAN__PCHIP_PERROR__SGE	(1UL << 4)
#define TITAN__PCHIP_PERROR__APE	(1UL << 5)
#define TITAN__PCHIP_PERROR__TA		(1UL << 6)
#define TITAN__PCHIP_PERROR__DPE	(1UL << 7)
#define TITAN__PCHIP_PERROR__NDS	(1UL << 8)
#define TITAN__PCHIP_PERROR__IPTPR	(1UL << 9)
#define TITAN__PCHIP_PERROR__IPTPW	(1UL << 10)
#define TITAN__PCHIP_PERROR__ERRMASK	(TITAN__PCHIP_PERROR__LOST |	\
					 TITAN__PCHIP_PERROR__SERR |	\
					 TITAN__PCHIP_PERROR__PERR |	\
					 TITAN__PCHIP_PERROR__DCRTO |	\
					 TITAN__PCHIP_PERROR__SGE |	\
					 TITAN__PCHIP_PERROR__APE |	\
					 TITAN__PCHIP_PERROR__TA |	\
					 TITAN__PCHIP_PERROR__DPE |	\
					 TITAN__PCHIP_PERROR__NDS |	\
					 TITAN__PCHIP_PERROR__IPTPR |	\
					 TITAN__PCHIP_PERROR__IPTPW)
#define TITAN__PCHIP_PERROR__DAC	(1UL << 47)
#define TITAN__PCHIP_PERROR__MWIN	(1UL << 48)
#define TITAN__PCHIP_PERROR__CMD__S	(52)
#define TITAN__PCHIP_PERROR__CMD__M	(0x0f)
#define TITAN__PCHIP_PERROR__ADDR__S	(14)
#define TITAN__PCHIP_PERROR__ADDR__M	(0x1fffffffful)

	if (!(perror & TITAN__PCHIP_PERROR__ERRMASK))
		return MCHK_DISPOSITION_UNKNOWN_ERROR;

	cmd = EXTRACT(perror, TITAN__PCHIP_PERROR__CMD);
	addr = EXTRACT(perror, TITAN__PCHIP_PERROR__ADDR) << 2;

	/*
	 * Initializing the BIOS on a video card on a bus without
	 * a south bridge (subtractive decode agent) can result in 
	 * master aborts as the BIOS probes the capabilities of the
	 * card. XFree86 does such initialization. If the error
	 * is a master abort (No DevSel as PCI Master) and the command
	 * is an I/O read or write below the address where we start
	 * assigning PCI I/O spaces (SRM uses 0x1000), then mark the
	 * error as dismissable so starting XFree86 doesn't result
	 * in a series of uncorrectable errors being reported. Also
	 * dismiss master aborts to VGA frame buffer space
	 * (0xA0000 - 0xC0000) and legacy BIOS space (0xC0000 - 0x100000)
	 * for the same reason.
	 *
	 * Also mark the error dismissible if it looks like the right
	 * error but only the Lost bit is set. Since the BIOS initialization
	 * can cause multiple master aborts and the error interrupt can
	 * be handled on a different CPU than the BIOS code is run on,
	 * it is possible for a second master abort to occur between the
	 * time the PALcode reads PERROR and the time it writes PERROR
	 * to acknowledge the error. If this timing happens, a second
	 * error will be signalled after the first, and if no additional
	 * errors occur, will look like a Lost error with no additional 
	 * errors on the same transaction as the previous error.
	 */
	if (((perror & TITAN__PCHIP_PERROR__NDS) || 
	     ((perror & TITAN__PCHIP_PERROR__ERRMASK) == 
	      TITAN__PCHIP_PERROR__LOST)) &&
	    ((((cmd & 0xE) == 2) && (addr < 0x1000)) ||
	     (((cmd & 0xE) == 6) && (addr >= 0xA0000) && (addr < 0x100000)))) {
		status = MCHK_DISPOSITION_DISMISS;
	}

#ifdef CONFIG_VERBOSE_MCHECK
	if (!print) 
		return status;

	printk("%s  PChip %d %cPERROR: %016lx\n", 
	       err_print_prefix, which, 
	       port ? 'A' : 'G', perror);
	if (perror & TITAN__PCHIP_PERROR__IPTPW)
		printk("%s    Invalid Peer-to-Peer Write\n", err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__IPTPR)
		printk("%s    Invalid Peer-to-Peer Read\n", err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__NDS)
		printk("%s    No DEVSEL as PCI Master [Master Abort]\n",
		       err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__DPE)
		printk("%s    Data Parity Error\n", err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__TA)
		printk("%s    Target Abort\n", err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__APE)
		printk("%s    Address Parity Error\n", err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__SGE)
		printk("%s    Scatter-Gather Error, Invalid PTE\n", 
		       err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__DCRTO)
		printk("%s    Delayed-Completion Retry Timeout\n", 
		       err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__PERR)
		printk("%s    PERR Asserted\n", err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__SERR)
		printk("%s    SERR Asserted\n", err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__LOST)
		printk("%s    Lost Error\n", err_print_prefix);
	printk("%s      Command: 0x%x - %s\n"
		 "      Address: 0x%lx\n",
	       err_print_prefix,
	       cmd, perror_cmd[cmd],
	       addr);
	if (perror & TITAN__PCHIP_PERROR__DAC)
		printk("%s      Dual Address Cycle\n", err_print_prefix);
	if (perror & TITAN__PCHIP_PERROR__MWIN)
		printk("%s      Hit in Monster Window\n", err_print_prefix);
#endif /* CONFIG_VERBOSE_MCHECK */

	return status;
}

static int
titan_parse_p_agperror(int which, u64 agperror, int print)
{
	int status = MCHK_DISPOSITION_REPORT;
#ifdef CONFIG_VERBOSE_MCHECK
	int cmd, len;
	unsigned long addr;

	char *agperror_cmd[] = { "Read (low-priority)",	"Read (high-priority)",
				 "Write (low-priority)",
				 "Write (high-priority)",
				 "Reserved",		"Reserved",
				 "Flush",		"Fence"
	};
#endif /* CONFIG_VERBOSE_MCHECK */

#define TITAN__PCHIP_AGPERROR__LOST	(1UL << 0)
#define TITAN__PCHIP_AGPERROR__LPQFULL	(1UL << 1)
#define TITAN__PCHIP_AGPERROR__HPQFULL	(1UL << 2)
#define TITAN__PCHIP_AGPERROR__RESCMD	(1UL << 3)
#define TITAN__PCHIP_AGPERROR__IPTE	(1UL << 4)
#define TITAN__PCHIP_AGPERROR__PTP	(1UL << 5)
#define TITAN__PCHIP_AGPERROR__NOWINDOW	(1UL << 6)
#define TITAN__PCHIP_AGPERROR__ERRMASK	(TITAN__PCHIP_AGPERROR__LOST |    \
					 TITAN__PCHIP_AGPERROR__LPQFULL | \
					 TITAN__PCHIP_AGPERROR__HPQFULL | \
					 TITAN__PCHIP_AGPERROR__RESCMD |  \
					 TITAN__PCHIP_AGPERROR__IPTE |    \
					 TITAN__PCHIP_AGPERROR__PTP |     \
					 TITAN__PCHIP_AGPERROR__NOWINDOW)
#define TITAN__PCHIP_AGPERROR__DAC	(1UL << 48)
#define TITAN__PCHIP_AGPERROR__MWIN	(1UL << 49)
#define TITAN__PCHIP_AGPERROR__FENCE	(1UL << 59)
#define TITAN__PCHIP_AGPERROR__CMD__S	(50)
#define TITAN__PCHIP_AGPERROR__CMD__M	(0x07)
#define TITAN__PCHIP_AGPERROR__ADDR__S	(15)
#define TITAN__PCHIP_AGPERROR__ADDR__M  (0xffffffffUL)
#define TITAN__PCHIP_AGPERROR__LEN__S	(53)
#define TITAN__PCHIP_AGPERROR__LEN__M	(0x3f)

	if (!(agperror & TITAN__PCHIP_AGPERROR__ERRMASK))
		return MCHK_DISPOSITION_UNKNOWN_ERROR;

#ifdef CONFIG_VERBOSE_MCHECK
	if (!print)
		return status;

	cmd = EXTRACT(agperror, TITAN__PCHIP_AGPERROR__CMD);
	addr = EXTRACT(agperror, TITAN__PCHIP_AGPERROR__ADDR) << 3;
	len = EXTRACT(agperror, TITAN__PCHIP_AGPERROR__LEN);

	printk("%s  PChip %d AGPERROR: %016lx\n", err_print_prefix,
	       which, agperror);
	if (agperror & TITAN__PCHIP_AGPERROR__NOWINDOW)
		printk("%s    No Window\n", err_print_prefix);
	if (agperror & TITAN__PCHIP_AGPERROR__PTP)
		printk("%s    Peer-to-Peer set\n", err_print_prefix);
	if (agperror & TITAN__PCHIP_AGPERROR__IPTE)
		printk("%s    Invalid PTE\n", err_print_prefix);
	if (agperror & TITAN__PCHIP_AGPERROR__RESCMD)
		printk("%s    Reserved Command\n", err_print_prefix);
	if (agperror & TITAN__PCHIP_AGPERROR__HPQFULL)
		printk("%s    HP Transaction Received while Queue Full\n", 
		       err_print_prefix);
	if (agperror & TITAN__PCHIP_AGPERROR__LPQFULL)
		printk("%s    LP Transaction Received while Queue Full\n", 
		       err_print_prefix);
	if (agperror & TITAN__PCHIP_AGPERROR__LOST)
		printk("%s    Lost Error\n", err_print_prefix);
	printk("%s      Command: 0x%x - %s, %d Quadwords%s\n"
		 "      Address: 0x%lx\n",
	       err_print_prefix, cmd, agperror_cmd[cmd], len,
	       (agperror & TITAN__PCHIP_AGPERROR__FENCE) ? ", FENCE" : "",
	       addr);
	if (agperror & TITAN__PCHIP_AGPERROR__DAC)
		printk("%s      Dual Address Cycle\n", err_print_prefix);
	if (agperror & TITAN__PCHIP_AGPERROR__MWIN)
		printk("%s      Hit in Monster Window\n", err_print_prefix);
#endif /* CONFIG_VERBOSE_MCHECK */

	return status;
}	

static int
titan_parse_p_chip(int which, u64 serror, u64 gperror, 
		   u64 aperror, u64 agperror, int print)
{
	int status = MCHK_DISPOSITION_UNKNOWN_ERROR;
	status |= titan_parse_p_serror(which, serror, print);
	status |= titan_parse_p_perror(which, 0, gperror, print);
	status |= titan_parse_p_perror(which, 1, aperror, print);
	status |= titan_parse_p_agperror(which, agperror, print);
	return status;
}

int
titan_process_logout_frame(struct el_common *mchk_header, int print)
{
	struct el_TITAN_sysdata_mcheck *tmchk =
		(struct el_TITAN_sysdata_mcheck *)
		((unsigned long)mchk_header + mchk_header->sys_offset);
	int status = MCHK_DISPOSITION_UNKNOWN_ERROR;

	status |= titan_parse_c_misc(tmchk->c_misc, print);
	status |= titan_parse_p_chip(0, tmchk->p0_serror, tmchk->p0_gperror,
				     tmchk->p0_aperror, tmchk->p0_agperror, 
				     print);
	status |= titan_parse_p_chip(1, tmchk->p1_serror, tmchk->p1_gperror,
				     tmchk->p1_aperror, tmchk->p1_agperror, 
				     print);

	return status;
}

void
titan_machine_check(u64 vector, u64 la_ptr)
{
	struct el_common *mchk_header = (struct el_common *)la_ptr;
	struct el_TITAN_sysdata_mcheck *tmchk =
		(struct el_TITAN_sysdata_mcheck *)
		((unsigned long)mchk_header + mchk_header->sys_offset);
	u64 irqmask;

	/*
	 * Mask of Titan interrupt sources which are reported as machine checks
	 *
	 * 63 - CChip Error
	 * 62 - PChip 0 H_Error
	 * 61 - PChip 1 H_Error
	 * 60 - PChip 0 C_Error
	 * 59 - PChip 1 C_Error
	 */
#define TITAN_MCHECK_INTERRUPT_MASK	0xF800000000000000UL

	/*
	 * Sync the processor
	 */
	mb();
	draina();
	
	/*
	 * Only handle system errors here 
	 */
	if ((vector != SCB_Q_SYSMCHK) && (vector != SCB_Q_SYSERR)) {
		ev6_machine_check(vector, la_ptr);
		return;
	}

	/* 
	 * It's a system error, handle it here
	 *
	 * The PALcode has already cleared the error, so just parse it
	 */
	
	/* 
	 * Parse the logout frame without printing first. If the only error(s)
	 * found are classified as "dismissable", then just dismiss them and
	 * don't print any message
	 */
	if (titan_process_logout_frame(mchk_header, 0) != 
	    MCHK_DISPOSITION_DISMISS) {
		char *saved_err_prefix = err_print_prefix;
		err_print_prefix = KERN_CRIT;

		/*
		 * Either a nondismissable error was detected or no
		 * recognized error was detected  in the logout frame 
		 * -- report the error in either case
		 */
		printk("%s"
		       "*System %s Error (Vector 0x%x) reported on CPU %d:\n", 
		       err_print_prefix,
		       (vector == SCB_Q_SYSERR)?"Correctable":"Uncorrectable",
		       (unsigned int)vector, (int)smp_processor_id());
		
#ifdef CONFIG_VERBOSE_MCHECK
		titan_process_logout_frame(mchk_header, alpha_verbose_mcheck);
		if (alpha_verbose_mcheck)
			dik_show_regs(get_irq_regs(), NULL);
#endif /* CONFIG_VERBOSE_MCHECK */

		err_print_prefix = saved_err_prefix;

		/*
		 * Convert any pending interrupts which report as system
		 * machine checks to interrupts
		 */
		irqmask = tmchk->c_dirx & TITAN_MCHECK_INTERRUPT_MASK;
		titan_dispatch_irqs(irqmask);
	}	


	/* 
	 * Release the logout frame 
	 */
	wrmces(0x7);
	mb();
}

/*
 * Subpacket Annotations
 */
static char *el_titan_pchip0_extended_annotation[] = {
	"Subpacket Header", 	"P0_SCTL",	"P0_SERREN",
	"P0_APCTL",		"P0_APERREN",	"P0_AGPERREN",
	"P0_ASPRST",		"P0_AWSBA0",	"P0_AWSBA1",
	"P0_AWSBA2",		"P0_AWSBA3",	"P0_AWSM0",
	"P0_AWSM1",		"P0_AWSM2",	"P0_AWSM3",
	"P0_ATBA0",		"P0_ATBA1",	"P0_ATBA2",
	"P0_ATBA3",		"P0_GPCTL",	"P0_GPERREN",
	"P0_GSPRST",		"P0_GWSBA0",	"P0_GWSBA1",
	"P0_GWSBA2",		"P0_GWSBA3",	"P0_GWSM0",
	"P0_GWSM1",		"P0_GWSM2",	"P0_GWSM3",
	"P0_GTBA0",		"P0_GTBA1",	"P0_GTBA2",
	"P0_GTBA3",		NULL 
};
static char *el_titan_pchip1_extended_annotation[] = {
	"Subpacket Header", 	"P1_SCTL",	"P1_SERREN",
	"P1_APCTL",		"P1_APERREN",	"P1_AGPERREN",
	"P1_ASPRST",		"P1_AWSBA0",	"P1_AWSBA1",
	"P1_AWSBA2",		"P1_AWSBA3",	"P1_AWSM0",
	"P1_AWSM1",		"P1_AWSM2",	"P1_AWSM3",
	"P1_ATBA0",		"P1_ATBA1",	"P1_ATBA2",
	"P1_ATBA3",		"P1_GPCTL",	"P1_GPERREN",
	"P1_GSPRST",		"P1_GWSBA0",	"P1_GWSBA1",
	"P1_GWSBA2",		"P1_GWSBA3",	"P1_GWSM0",
	"P1_GWSM1",		"P1_GWSM2",	"P1_GWSM3",
	"P1_GTBA0",		"P1_GTBA1",	"P1_GTBA2",
	"P1_GTBA3",		NULL 
};
static char *el_titan_memory_extended_annotation[] = {
	"Subpacket Header", 	"AAR0",		"AAR1",
	"AAR2",			"AAR3",		"P0_SCTL",
	"P0_GPCTL",		"P0_APCTL",	"P1_SCTL",
	"P1_GPCTL",		"P1_SCTL",	NULL 
};

static struct el_subpacket_annotation el_titan_annotations[] = {
	SUBPACKET_ANNOTATION(EL_CLASS__REGATTA_FAMILY,
			     EL_TYPE__REGATTA__TITAN_PCHIP0_EXTENDED,
			     1,
			     "Titan PChip 0 Extended Frame",
			     el_titan_pchip0_extended_annotation),
	SUBPACKET_ANNOTATION(EL_CLASS__REGATTA_FAMILY,
			     EL_TYPE__REGATTA__TITAN_PCHIP1_EXTENDED,
			     1,
			     "Titan PChip 1 Extended Frame",
			     el_titan_pchip1_extended_annotation),
	SUBPACKET_ANNOTATION(EL_CLASS__REGATTA_FAMILY,
			     EL_TYPE__REGATTA__TITAN_MEMORY_EXTENDED,
			     1,
			     "Titan Memory Extended Frame",
			     el_titan_memory_extended_annotation),
	SUBPACKET_ANNOTATION(EL_CLASS__REGATTA_FAMILY,
			     EL_TYPE__TERMINATION__TERMINATION,
			     1,
			     "Termination Subpacket",
			     NULL)
};

static struct el_subpacket *
el_process_regatta_subpacket(struct el_subpacket *header)
{
	int status;

	if (header->class != EL_CLASS__REGATTA_FAMILY) {
		printk("%s  ** Unexpected header CLASS %d TYPE %d, aborting\n",
		       err_print_prefix,
		       header->class, header->type);
		return NULL;
	}

	switch(header->type) {
	case EL_TYPE__REGATTA__PROCESSOR_ERROR_FRAME:
	case EL_TYPE__REGATTA__SYSTEM_ERROR_FRAME:
	case EL_TYPE__REGATTA__ENVIRONMENTAL_FRAME:
	case EL_TYPE__REGATTA__PROCESSOR_DBL_ERROR_HALT:
	case EL_TYPE__REGATTA__SYSTEM_DBL_ERROR_HALT:
		printk("%s  ** Occurred on CPU %d:\n", 
		       err_print_prefix,
		       (int)header->by_type.regatta_frame.cpuid);
		status = privateer_process_logout_frame((struct el_common *)
			header->by_type.regatta_frame.data_start, 1);
		break;
	default:
		printk("%s  ** REGATTA TYPE %d SUBPACKET\n", 
		       err_print_prefix, header->type);
		el_annotate_subpacket(header);
		break;
	}


	return (struct el_subpacket *)((unsigned long)header + header->length);
} 

static struct el_subpacket_handler titan_subpacket_handler = 
	SUBPACKET_HANDLER_INIT(EL_CLASS__REGATTA_FAMILY, 
			       el_process_regatta_subpacket);

void
titan_register_error_handlers(void)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE (el_titan_annotations); i++)
		cdl_register_subpacket_annotation(&el_titan_annotations[i]);

	cdl_register_subpacket_handler(&titan_subpacket_handler);

	ev6_register_error_handlers();
}


/*
 * Privateer
 */

static int
privateer_process_680_frame(struct el_common *mchk_header, int print)
{
	int status = MCHK_DISPOSITION_UNKNOWN_ERROR;
#ifdef CONFIG_VERBOSE_MCHECK
	struct el_PRIVATEER_envdata_mcheck *emchk =
		(struct el_PRIVATEER_envdata_mcheck *)
		((unsigned long)mchk_header + mchk_header->sys_offset);

	/* TODO - categorize errors, for now, no error */

	if (!print)
		return status;

	/* TODO - decode instead of just dumping... */
	printk("%s  Summary Flags:         %016lx\n"
 	         "  CChip DIRx:            %016lx\n"
		 "  System Management IR:  %016lx\n"
		 "  CPU IR:                %016lx\n"
		 "  Power Supply IR:       %016lx\n"
		 "  LM78 Fault Status:     %016lx\n"
		 "  System Doors:          %016lx\n"
		 "  Temperature Warning:   %016lx\n"
		 "  Fan Control:           %016lx\n"
		 "  Fatal Power Down Code: %016lx\n",
	       err_print_prefix,
	       emchk->summary,
	       emchk->c_dirx,
	       emchk->smir,
	       emchk->cpuir,
	       emchk->psir,
	       emchk->fault,
	       emchk->sys_doors,
	       emchk->temp_warn,
	       emchk->fan_ctrl,
	       emchk->code);
#endif /* CONFIG_VERBOSE_MCHECK */

	return status;
}

int
privateer_process_logout_frame(struct el_common *mchk_header, int print)
{
	struct el_common_EV6_mcheck *ev6mchk = 
		(struct el_common_EV6_mcheck *)mchk_header;
	int status = MCHK_DISPOSITION_UNKNOWN_ERROR;

	/*
	 * Machine check codes
	 */
#define PRIVATEER_MCHK__CORR_ECC		0x86	/* 630 */
#define PRIVATEER_MCHK__DC_TAG_PERR		0x9E	/* 630 */
#define PRIVATEER_MCHK__PAL_BUGCHECK		0x8E	/* 670 */
#define PRIVATEER_MCHK__OS_BUGCHECK		0x90	/* 670 */
#define PRIVATEER_MCHK__PROC_HRD_ERR		0x98	/* 670 */
#define PRIVATEER_MCHK__ISTREAM_CMOV_PRX	0xA0	/* 670 */
#define PRIVATEER_MCHK__ISTREAM_CMOV_FLT	0xA2	/* 670 */
#define PRIVATEER_MCHK__SYS_HRD_ERR		0x202	/* 660 */
#define PRIVATEER_MCHK__SYS_CORR_ERR		0x204	/* 620 */
#define PRIVATEER_MCHK__SYS_ENVIRON		0x206	/* 680 */

	switch(ev6mchk->MCHK_Code) {
	/*
	 * Vector 630 - Processor, Correctable
	 */
	case PRIVATEER_MCHK__CORR_ECC:
	case PRIVATEER_MCHK__DC_TAG_PERR:
		/*
		 * Fall through to vector 670 for processing...
		 */
	/*
	 * Vector 670 - Processor, Uncorrectable
	 */
	case PRIVATEER_MCHK__PAL_BUGCHECK:
	case PRIVATEER_MCHK__OS_BUGCHECK:
	case PRIVATEER_MCHK__PROC_HRD_ERR:
	case PRIVATEER_MCHK__ISTREAM_CMOV_PRX:
	case PRIVATEER_MCHK__ISTREAM_CMOV_FLT:
		status |= ev6_process_logout_frame(mchk_header, print);
		break;

	/*
	 * Vector 620 - System, Correctable
	 */
	case PRIVATEER_MCHK__SYS_CORR_ERR:
		/*
		 * Fall through to vector 660 for processing...
		 */
	/*
	 * Vector 660 - System, Uncorrectable
	 */
	case PRIVATEER_MCHK__SYS_HRD_ERR:
		status |= titan_process_logout_frame(mchk_header, print);
		break;

	/* 
	 * Vector 680 - System, Environmental
	 */
	case PRIVATEER_MCHK__SYS_ENVIRON:	/* System, Environmental */
		status |= privateer_process_680_frame(mchk_header, print);
		break;

	/* 
	 * Unknown
	 */
	default:
		status |= MCHK_DISPOSITION_REPORT;
		if (print) {
			printk("%s** Unknown Error, frame follows\n", 
			       err_print_prefix);
			mchk_dump_logout_frame(mchk_header);
		}

	}

	return status;
}

void
privateer_machine_check(u64 vector, u64 la_ptr)
{
	struct el_common *mchk_header = (struct el_common *)la_ptr;
	struct el_TITAN_sysdata_mcheck *tmchk =
		(struct el_TITAN_sysdata_mcheck *)
		(la_ptr + mchk_header->sys_offset);
	u64 irqmask;
	char *saved_err_prefix = err_print_prefix;

#define PRIVATEER_680_INTERRUPT_MASK		(0xE00UL)
#define PRIVATEER_HOTPLUG_INTERRUPT_MASK	(0xE00UL)

	/*
	 * Sync the processor.
	 */
	mb();
	draina();

	/* 
	 * Only handle system events here.
	 */
	if (vector != SCB_Q_SYSEVENT) 
		return titan_machine_check(vector, la_ptr);

	/*
	 * Report the event - System Events should be reported even if no
	 * error is indicated since the event could indicate the return
	 * to normal status.
	 */
	err_print_prefix = KERN_CRIT;
	printk("%s*System Event (Vector 0x%x) reported on CPU %d:\n", 
	       err_print_prefix,
	       (unsigned int)vector, (int)smp_processor_id());
	privateer_process_680_frame(mchk_header, 1);
	err_print_prefix = saved_err_prefix;
	
	/* 
	 * Convert any pending interrupts which report as 680 machine
	 * checks to interrupts.
	 */
	irqmask = tmchk->c_dirx & PRIVATEER_680_INTERRUPT_MASK;

	/*
	 * Dispatch the interrupt(s).
	 */
	titan_dispatch_irqs(irqmask);

	/* 
	 * Release the logout frame.
	 */
	wrmces(0x7);
	mb();
}
