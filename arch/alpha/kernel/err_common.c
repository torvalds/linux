/*
 *	linux/arch/alpha/kernel/err_common.c
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

#include "err_impl.h"
#include "proto.h"

/*
 * err_print_prefix -- error handling print routines should prefix
 * all prints with this
 */
char *err_print_prefix = KERN_NOTICE;


/*
 * Generic
 */
void
mchk_dump_mem(void *data, size_t length, char **annotation)
{
	unsigned long *ldata = data;
	size_t i;
	
	for (i = 0; (i * sizeof(*ldata)) < length; i++) {
		if (annotation && !annotation[i]) 
			annotation = NULL;
		printk("%s    %08x: %016lx    %s\n",
		       err_print_prefix,
		       (unsigned)(i * sizeof(*ldata)), ldata[i],
		       annotation ? annotation[i] : "");
	}
}

void
mchk_dump_logout_frame(struct el_common *mchk_header)
{
	printk("%s  -- Frame Header --\n"
	         "    Frame Size:   %d (0x%x) bytes\n"
	         "    Flags:        %s%s\n"
	         "    MCHK Code:    0x%x\n"
	         "    Frame Rev:    %d\n"
	         "    Proc Offset:  0x%08x\n"
	         "    Sys Offset:   0x%08x\n"
  	         "  -- Processor Region --\n",
	       err_print_prefix, 
	       mchk_header->size, mchk_header->size,
	       mchk_header->retry ? "RETRY " : "", 
  	         mchk_header->err2 ? "SECOND_ERR " : "",
	       mchk_header->code,
	       mchk_header->frame_rev,
	       mchk_header->proc_offset,
	       mchk_header->sys_offset);

	mchk_dump_mem((void *)
		      ((unsigned long)mchk_header + mchk_header->proc_offset),
		      mchk_header->sys_offset - mchk_header->proc_offset,
		      NULL);
	
	printk("%s  -- System Region --\n", err_print_prefix);
	mchk_dump_mem((void *)
		      ((unsigned long)mchk_header + mchk_header->sys_offset),
		      mchk_header->size - mchk_header->sys_offset,
		      NULL);
	printk("%s  -- End of Frame --\n", err_print_prefix);
}


/*
 * Console Data Log
 */
/* Data */
static struct el_subpacket_handler *subpacket_handler_list = NULL;
static struct el_subpacket_annotation *subpacket_annotation_list = NULL;

static struct el_subpacket *
el_process_header_subpacket(struct el_subpacket *header)
{
	union el_timestamp timestamp;
	char *name = "UNKNOWN EVENT";
	int packet_count = 0;
	int length = 0;

	if (header->class != EL_CLASS__HEADER) {
		printk("%s** Unexpected header CLASS %d TYPE %d, aborting\n",
		       err_print_prefix,
		       header->class, header->type);
		return NULL;
	}

	switch(header->type) {
	case EL_TYPE__HEADER__SYSTEM_ERROR_FRAME:
		name = "SYSTEM ERROR";
		length = header->by_type.sys_err.frame_length;
		packet_count = 
			header->by_type.sys_err.frame_packet_count;
		timestamp.as_int = 0;
		break;
	case EL_TYPE__HEADER__SYSTEM_EVENT_FRAME:
		name = "SYSTEM EVENT";
		length = header->by_type.sys_event.frame_length;
		packet_count = 
			header->by_type.sys_event.frame_packet_count;
		timestamp = header->by_type.sys_event.timestamp;
		break;
	case EL_TYPE__HEADER__HALT_FRAME:
		name = "ERROR HALT";
		length = header->by_type.err_halt.frame_length;
		packet_count = 
			header->by_type.err_halt.frame_packet_count;
		timestamp = header->by_type.err_halt.timestamp;
		break;
	case EL_TYPE__HEADER__LOGOUT_FRAME:
		name = "LOGOUT FRAME";
		length = header->by_type.logout_header.frame_length;
		packet_count = 1;
		timestamp.as_int = 0;
		break;
	default: /* Unknown */
		printk("%s** Unknown header - CLASS %d TYPE %d, aborting\n",
		       err_print_prefix,
		       header->class, header->type);
		return NULL;		
	}

	printk("%s*** %s:\n"
	         "  CLASS %d, TYPE %d\n", 
	       err_print_prefix,
	       name,
	       header->class, header->type);
	el_print_timestamp(&timestamp);
	
	/*
	 * Process the subpackets
	 */
	el_process_subpackets(header, packet_count);

	/* return the next header */
	header = (struct el_subpacket *)
		((unsigned long)header + header->length + length);
	return header;
}

static struct el_subpacket *
el_process_subpacket_reg(struct el_subpacket *header)
{
	struct el_subpacket *next = NULL;
	struct el_subpacket_handler *h = subpacket_handler_list;

	for (; h && h->class != header->class; h = h->next);
	if (h) next = h->handler(header);

	return next;
}

void
el_print_timestamp(union el_timestamp *timestamp)
{
	if (timestamp->as_int)
		printk("%s  TIMESTAMP: %d/%d/%02d %d:%02d:%0d\n", 
		       err_print_prefix,
		       timestamp->b.month, timestamp->b.day,
		       timestamp->b.year, timestamp->b.hour,
		       timestamp->b.minute, timestamp->b.second);
}

void
el_process_subpackets(struct el_subpacket *header, int packet_count)
{
	struct el_subpacket *subpacket;
	int i;

	subpacket = (struct el_subpacket *)
		((unsigned long)header + header->length);

	for (i = 0; subpacket && i < packet_count; i++) {
		printk("%sPROCESSING SUBPACKET %d\n", err_print_prefix, i);
		subpacket = el_process_subpacket(subpacket);
	}
}

struct el_subpacket *
el_process_subpacket(struct el_subpacket *header)
{
	struct el_subpacket *next = NULL;

	switch(header->class) {
	case EL_CLASS__TERMINATION:
		/* Termination packet, there are no more */
		break;
	case EL_CLASS__HEADER: 
		next = el_process_header_subpacket(header);
		break;
	default:
		if (NULL == (next = el_process_subpacket_reg(header))) {
			printk("%s** Unexpected header CLASS %d TYPE %d"
			       " -- aborting.\n",
			       err_print_prefix,
			       header->class, header->type);
		}
		break;
	}

	return next;
}

void 
el_annotate_subpacket(struct el_subpacket *header)
{
	struct el_subpacket_annotation *a;
	char **annotation = NULL;

	for (a = subpacket_annotation_list; a; a = a->next) {
		if (a->class == header->class &&
		    a->type == header->type &&
		    a->revision == header->revision) {
			/*
			 * We found the annotation
			 */
			annotation = a->annotation;
			printk("%s  %s\n", err_print_prefix, a->description);
			break;
		}
	}

	mchk_dump_mem(header, header->length, annotation);
}

static void __init
cdl_process_console_data_log(int cpu, struct percpu_struct *pcpu)
{
	struct el_subpacket *header = (struct el_subpacket *)
		(IDENT_ADDR | pcpu->console_data_log_pa);
	int err;

	printk("%s******* CONSOLE DATA LOG FOR CPU %d. *******\n"
	         "*** Error(s) were logged on a previous boot\n",
	       err_print_prefix, cpu);
	
	for (err = 0; header && (header->class != EL_CLASS__TERMINATION); err++)
		header = el_process_subpacket(header);

	/* let the console know it's ok to clear the error(s) at restart */
	pcpu->console_data_log_pa = 0;

	printk("%s*** %d total error(s) logged\n"
	         "**** END OF CONSOLE DATA LOG FOR CPU %d ****\n", 
	       err_print_prefix, err, cpu);
}

void __init
cdl_check_console_data_log(void)
{
	struct percpu_struct *pcpu;
	unsigned long cpu;

	for (cpu = 0; cpu < hwrpb->nr_processors; cpu++) {
		pcpu = (struct percpu_struct *)
			((unsigned long)hwrpb + hwrpb->processor_offset 
			 + cpu * hwrpb->processor_size);
		if (pcpu->console_data_log_pa)
			cdl_process_console_data_log(cpu, pcpu);
	}

}

int __init
cdl_register_subpacket_annotation(struct el_subpacket_annotation *new)
{
	struct el_subpacket_annotation *a = subpacket_annotation_list;

	if (a == NULL) subpacket_annotation_list = new;
	else {
		for (; a->next != NULL; a = a->next) {
			if ((a->class == new->class && a->type == new->type) ||
			    a == new) {
				printk("Attempted to re-register "
				       "subpacket annotation\n");
				return -EINVAL;
			}
		}
		a->next = new;
	}
	new->next = NULL;

	return 0;
}

int __init
cdl_register_subpacket_handler(struct el_subpacket_handler *new)
{
	struct el_subpacket_handler *h = subpacket_handler_list;

	if (h == NULL) subpacket_handler_list = new;
	else {
		for (; h->next != NULL; h = h->next) {
			if (h->class == new->class || h == new) {
				printk("Attempted to re-register "
				       "subpacket handler\n");
				return -EINVAL;
			}
		}
		h->next = new;
	}
	new->next = NULL;

	return 0;
}

