/*
 *	linux/arch/alpha/kernel/err_ev7.c
 *
 *	Copyright (C) 2000 Jeff Wiedemeier (Compaq Computer Corporation)
 *
 *	Error handling code supporting Alpha systems
 */

#include <linux/init.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/hwrpb.h>
#include <asm/smp.h>
#include <asm/err_common.h>
#include <asm/err_ev7.h>

#include "err_impl.h"
#include "proto.h"

struct ev7_lf_subpackets *
ev7_collect_logout_frame_subpackets(struct el_subpacket *el_ptr,
				    struct ev7_lf_subpackets *lf_subpackets)
{
	struct el_subpacket *subpacket;
	int i;

	/*
	 * A Marvel machine check frame is always packaged in an
	 * el_subpacket of class HEADER, type LOGOUT_FRAME.
	 */
	if (el_ptr->class != EL_CLASS__HEADER || 
	    el_ptr->type != EL_TYPE__HEADER__LOGOUT_FRAME)
		return NULL;

	/*
	 * It is a logout frame header. Look at the one subpacket.
	 */
	el_ptr = (struct el_subpacket *)
		((unsigned long)el_ptr + el_ptr->length);

	/*
	 * It has to be class PAL, type LOGOUT_FRAME.
	 */
	if (el_ptr->class != EL_CLASS__PAL ||
	    el_ptr->type != EL_TYPE__PAL__LOGOUT_FRAME)
		return NULL;

	lf_subpackets->logout = (struct ev7_pal_logout_subpacket *)
		el_ptr->by_type.raw.data_start;

	/*
	 * Process the subpackets.
	 */
	subpacket = (struct el_subpacket *)
		((unsigned long)el_ptr + el_ptr->length);
	for (i = 0;
	     subpacket && i < lf_subpackets->logout->subpacket_count;
	     subpacket = (struct el_subpacket *)
		     ((unsigned long)subpacket + subpacket->length), i++) {
		/*
		 * All subpackets should be class PAL.
		 */
		if (subpacket->class != EL_CLASS__PAL) {
			printk("%s**UNEXPECTED SUBPACKET CLASS %d "
			       "IN LOGOUT FRAME (packet %d\n",
			       err_print_prefix, subpacket->class, i);
			return NULL;
		}

		/*
		 * Remember the subpacket.
		 */
		switch(subpacket->type) {
		case EL_TYPE__PAL__EV7_PROCESSOR:
			lf_subpackets->ev7 =
				(struct ev7_pal_processor_subpacket *)
				subpacket->by_type.raw.data_start;
			break;

		case EL_TYPE__PAL__EV7_RBOX:
			lf_subpackets->rbox = (struct ev7_pal_rbox_subpacket *)
				subpacket->by_type.raw.data_start;
			break;

		case EL_TYPE__PAL__EV7_ZBOX:
			lf_subpackets->zbox = (struct ev7_pal_zbox_subpacket *)
				subpacket->by_type.raw.data_start;
			break;

		case EL_TYPE__PAL__EV7_IO:
			lf_subpackets->io = (struct ev7_pal_io_subpacket *)
				subpacket->by_type.raw.data_start;
			break;

		case EL_TYPE__PAL__ENV__AMBIENT_TEMPERATURE:
		case EL_TYPE__PAL__ENV__AIRMOVER_FAN:
		case EL_TYPE__PAL__ENV__VOLTAGE:
		case EL_TYPE__PAL__ENV__INTRUSION:
		case EL_TYPE__PAL__ENV__POWER_SUPPLY:
		case EL_TYPE__PAL__ENV__LAN:
		case EL_TYPE__PAL__ENV__HOT_PLUG:
			lf_subpackets->env[ev7_lf_env_index(subpacket->type)] =
 				(struct ev7_pal_environmental_subpacket *)
				subpacket->by_type.raw.data_start;
			break;
				
		default:
			/*
			 * Don't know what kind of frame this is.
			 */
			return NULL;
		}
	}

	return lf_subpackets;
}

void
ev7_machine_check(u64 vector, u64 la_ptr)
{
	struct el_subpacket *el_ptr = (struct el_subpacket *)la_ptr;
	char *saved_err_prefix = err_print_prefix;

	/*
	 * Sync the processor
	 */
	mb();
	draina();

	err_print_prefix = KERN_CRIT;
	printk("%s*CPU %s Error (Vector 0x%x) reported on CPU %d\n",
	       err_print_prefix, 
	       (vector == SCB_Q_PROCERR) ? "Correctable" : "Uncorrectable",
	       (unsigned int)vector, (int)smp_processor_id());
	el_process_subpacket(el_ptr);
	err_print_prefix = saved_err_prefix;

	/* 
	 * Release the logout frame 
	 */
	wrmces(0x7);
	mb();
}

static char *el_ev7_processor_subpacket_annotation[] = {
	"Subpacket Header",	"I_STAT",	"DC_STAT",
	"C_ADDR",		"C_SYNDROME_1",	"C_SYNDROME_0",
	"C_STAT",		"C_STS",	"MM_STAT",
	"EXC_ADDR",		"IER_CM",	"ISUM",
	"PAL_BASE",		"I_CTL",	"PROCESS_CONTEXT",
	"CBOX_CTL",		"CBOX_STP_CTL",	"CBOX_ACC_CTL",
	"CBOX_LCL_SET",		"CBOX_GLB_SET",	"BBOX_CTL",
	"BBOX_ERR_STS",		"BBOX_ERR_IDX",	"CBOX_DDP_ERR_STS",
	"BBOX_DAT_RMP",		NULL
};

static char *el_ev7_zbox_subpacket_annotation[] = {
	"Subpacket Header", 	
	"ZBOX(0): DRAM_ERR_STATUS_2 / DRAM_ERR_STATUS_1",
	"ZBOX(0): DRAM_ERROR_CTL    / DRAM_ERR_STATUS_3",
	"ZBOX(0): DIFT_TIMEOUT      / DRAM_ERR_ADR",
	"ZBOX(0): FRC_ERR_ADR       / DRAM_MAPPER_CTL",
	"ZBOX(0): reserved          / DIFT_ERR_STATUS",
	"ZBOX(1): DRAM_ERR_STATUS_2 / DRAM_ERR_STATUS_1",
	"ZBOX(1): DRAM_ERROR_CTL    / DRAM_ERR_STATUS_3",
	"ZBOX(1): DIFT_TIMEOUT      / DRAM_ERR_ADR",
	"ZBOX(1): FRC_ERR_ADR       / DRAM_MAPPER_CTL",
	"ZBOX(1): reserved          / DIFT_ERR_STATUS",
	"CBOX_CTL",		"CBOX_STP_CTL",
	"ZBOX(0)_ERROR_PA",	"ZBOX(1)_ERROR_PA",
	"ZBOX(0)_ORED_SYNDROME","ZBOX(1)_ORED_SYNDROME",
	NULL
};

static char *el_ev7_rbox_subpacket_annotation[] = {
	"Subpacket Header",	"RBOX_CFG",	"RBOX_N_CFG",
	"RBOX_S_CFG",		"RBOX_E_CFG",	"RBOX_W_CFG",
	"RBOX_N_ERR",		"RBOX_S_ERR",	"RBOX_E_ERR",
	"RBOX_W_ERR",		"RBOX_IO_CFG",	"RBOX_IO_ERR",
	"RBOX_L_ERR",		"RBOX_WHOAMI",	"RBOX_IMASL",
	"RBOX_INTQ",		"RBOX_INT",	NULL
};

static char *el_ev7_io_subpacket_annotation[] = {
	"Subpacket Header",	"IO_ASIC_REV",	"IO_SYS_REV",
	"IO7_UPH",		"HPI_CTL",	"CRD_CTL",
	"HEI_CTL",		"PO7_ERROR_SUM","PO7_UNCRR_SYM",
	"PO7_CRRCT_SYM",	"PO7_UGBGE_SYM","PO7_ERR_PKT0",
	"PO7_ERR_PKT1",		"reserved",	"reserved",
	"PO0_ERR_SUM",		"PO0_TLB_ERR",	"PO0_SPL_COMPLT",
	"PO0_TRANS_SUM",	"PO0_FIRST_ERR","PO0_MULT_ERR",
	"DM CSR PH",		"DM CSR PH",	"DM CSR PH",
	"DM CSR PH",		"reserved",
	"PO1_ERR_SUM",		"PO1_TLB_ERR",	"PO1_SPL_COMPLT",
	"PO1_TRANS_SUM",	"PO1_FIRST_ERR","PO1_MULT_ERR",
	"DM CSR PH",		"DM CSR PH",	"DM CSR PH",
	"DM CSR PH",		"reserved",
	"PO2_ERR_SUM",		"PO2_TLB_ERR",	"PO2_SPL_COMPLT",
	"PO2_TRANS_SUM",	"PO2_FIRST_ERR","PO2_MULT_ERR",
	"DM CSR PH",		"DM CSR PH",	"DM CSR PH",
	"DM CSR PH",		"reserved",
	"PO3_ERR_SUM",		"PO3_TLB_ERR",	"PO3_SPL_COMPLT",
	"PO3_TRANS_SUM",	"PO3_FIRST_ERR","PO3_MULT_ERR",
	"DM CSR PH",		"DM CSR PH",	"DM CSR PH",
	"DM CSR PH",		"reserved",	
	NULL
};
	
static struct el_subpacket_annotation el_ev7_pal_annotations[] = {
	SUBPACKET_ANNOTATION(EL_CLASS__PAL,
			     EL_TYPE__PAL__EV7_PROCESSOR,
			     1,
			     "EV7 Processor Subpacket",
			     el_ev7_processor_subpacket_annotation),
	SUBPACKET_ANNOTATION(EL_CLASS__PAL,
			     EL_TYPE__PAL__EV7_ZBOX,
			     1,
			     "EV7 ZBOX Subpacket",
			     el_ev7_zbox_subpacket_annotation),
	SUBPACKET_ANNOTATION(EL_CLASS__PAL,
			     EL_TYPE__PAL__EV7_RBOX,
			     1,
			     "EV7 RBOX Subpacket",
			     el_ev7_rbox_subpacket_annotation),
	SUBPACKET_ANNOTATION(EL_CLASS__PAL,
			     EL_TYPE__PAL__EV7_IO,
			     1,
			     "EV7 IO Subpacket",
			     el_ev7_io_subpacket_annotation)
};

static struct el_subpacket *
ev7_process_pal_subpacket(struct el_subpacket *header)
{
	struct ev7_pal_subpacket *packet;

	if (header->class != EL_CLASS__PAL) {
		printk("%s  ** Unexpected header CLASS %d TYPE %d, aborting\n",
		       err_print_prefix,
		       header->class, header->type);
		return NULL;
	}

	packet = (struct ev7_pal_subpacket *)header->by_type.raw.data_start;

	switch(header->type) {
	case EL_TYPE__PAL__LOGOUT_FRAME:
		printk("%s*** MCHK occurred on LPID %ld (RBOX %lx)\n",
		       err_print_prefix,
		       packet->by_type.logout.whami, 
		       packet->by_type.logout.rbox_whami);
		el_print_timestamp(&packet->by_type.logout.timestamp);
		printk("%s  EXC_ADDR: %016lx\n"
		         "  HALT_CODE: %lx\n", 
		       err_print_prefix,
		       packet->by_type.logout.exc_addr,
		       packet->by_type.logout.halt_code);
		el_process_subpackets(header,
                                      packet->by_type.logout.subpacket_count);
		break;
	default:
		printk("%s  ** PAL TYPE %d SUBPACKET\n", 
		       err_print_prefix,
		       header->type);
		el_annotate_subpacket(header);
		break;
	}
	
	return (struct el_subpacket *)((unsigned long)header + header->length);
}

struct el_subpacket_handler ev7_pal_subpacket_handler =
	SUBPACKET_HANDLER_INIT(EL_CLASS__PAL, ev7_process_pal_subpacket);

void
ev7_register_error_handlers(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(el_ev7_pal_annotations); i++)
		cdl_register_subpacket_annotation(&el_ev7_pal_annotations[i]);

	cdl_register_subpacket_handler(&ev7_pal_subpacket_handler);
}

