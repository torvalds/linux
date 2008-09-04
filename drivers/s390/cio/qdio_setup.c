/*
 * driver/s390/cio/qdio_setup.c
 *
 * qdio queue initialization
 *
 * Copyright (C) IBM Corp. 2008
 * Author(s): Jan Glauber <jang@linux.vnet.ibm.com>
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <asm/qdio.h>

#include "cio.h"
#include "css.h"
#include "device.h"
#include "ioasm.h"
#include "chsc.h"
#include "qdio.h"
#include "qdio_debug.h"

static struct kmem_cache *qdio_q_cache;

/*
 * qebsm is only available under 64bit but the adapter sets the feature
 * flag anyway, so we manually override it.
 */
static inline int qebsm_possible(void)
{
#ifdef CONFIG_64BIT
	return css_general_characteristics.qebsm;
#endif
	return 0;
}

/*
 * qib_param_field: pointer to 128 bytes or NULL, if no param field
 * nr_input_qs: pointer to nr_queues*128 words of data or NULL
 */
static void set_impl_params(struct qdio_irq *irq_ptr,
			    unsigned int qib_param_field_format,
			    unsigned char *qib_param_field,
			    unsigned long *input_slib_elements,
			    unsigned long *output_slib_elements)
{
	struct qdio_q *q;
	int i, j;

	if (!irq_ptr)
		return;

	WARN_ON((unsigned long)&irq_ptr->qib & 0xff);
	irq_ptr->qib.pfmt = qib_param_field_format;
	if (qib_param_field)
		memcpy(irq_ptr->qib.parm, qib_param_field,
		       QDIO_MAX_BUFFERS_PER_Q);

	if (!input_slib_elements)
		goto output;

	for_each_input_queue(irq_ptr, q, i) {
		for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; j++)
			q->slib->slibe[j].parms =
				input_slib_elements[i * QDIO_MAX_BUFFERS_PER_Q + j];
	}
output:
	if (!output_slib_elements)
		return;

	for_each_output_queue(irq_ptr, q, i) {
		for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; j++)
			q->slib->slibe[j].parms =
				output_slib_elements[i * QDIO_MAX_BUFFERS_PER_Q + j];
	}
}

static int __qdio_allocate_qs(struct qdio_q **irq_ptr_qs, int nr_queues)
{
	struct qdio_q *q;
	int i;

	for (i = 0; i < nr_queues; i++) {
		q = kmem_cache_alloc(qdio_q_cache, GFP_KERNEL);
		if (!q)
			return -ENOMEM;
		WARN_ON((unsigned long)q & 0xff);

		q->slib = (struct slib *) __get_free_page(GFP_KERNEL);
		if (!q->slib) {
			kmem_cache_free(qdio_q_cache, q);
			return -ENOMEM;
		}
		WARN_ON((unsigned long)q->slib & 0x7ff);
		irq_ptr_qs[i] = q;
	}
	return 0;
}

int qdio_allocate_qs(struct qdio_irq *irq_ptr, int nr_input_qs, int nr_output_qs)
{
	int rc;

	rc = __qdio_allocate_qs(irq_ptr->input_qs, nr_input_qs);
	if (rc)
		return rc;
	rc = __qdio_allocate_qs(irq_ptr->output_qs, nr_output_qs);
	return rc;
}

static void setup_queues_misc(struct qdio_q *q, struct qdio_irq *irq_ptr,
			      qdio_handler_t *handler, int i)
{
	/* must be cleared by every qdio_establish */
	memset(q, 0, ((char *)&q->slib) - ((char *)q));
	memset(q->slib, 0, PAGE_SIZE);

	q->irq_ptr = irq_ptr;
	q->mask = 1 << (31 - i);
	q->nr = i;
	q->handler = handler;
}

static void setup_storage_lists(struct qdio_q *q, struct qdio_irq *irq_ptr,
				void **sbals_array, char *dbf_text, int i)
{
	struct qdio_q *prev;
	int j;

	QDIO_DBF_TEXT0(0, setup, dbf_text);
	QDIO_DBF_HEX0(0, setup, &q, sizeof(void *));

	q->sl = (struct sl *)((char *)q->slib + PAGE_SIZE / 2);

	/* fill in sbal */
	for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; j++) {
		q->sbal[j] = *sbals_array++;
		WARN_ON((unsigned long)q->sbal[j] & 0xff);
	}

	/* fill in slib */
	if (i > 0) {
		prev = (q->is_input_q) ? irq_ptr->input_qs[i - 1]
			: irq_ptr->output_qs[i - 1];
		prev->slib->nsliba = (unsigned long)q->slib;
	}

	q->slib->sla = (unsigned long)q->sl;
	q->slib->slsba = (unsigned long)&q->slsb.val[0];

	/* fill in sl */
	for (j = 0; j < QDIO_MAX_BUFFERS_PER_Q; j++)
		q->sl->element[j].sbal = (unsigned long)q->sbal[j];

	QDIO_DBF_TEXT2(0, setup, "sl-sb-b0");
	QDIO_DBF_HEX2(0, setup, q->sl, sizeof(void *));
	QDIO_DBF_HEX2(0, setup, &q->slsb, sizeof(void *));
	QDIO_DBF_HEX2(0, setup, q->sbal, sizeof(void *));
}

static void setup_queues(struct qdio_irq *irq_ptr,
			 struct qdio_initialize *qdio_init)
{
	char dbf_text[20];
	struct qdio_q *q;
	void **input_sbal_array = qdio_init->input_sbal_addr_array;
	void **output_sbal_array = qdio_init->output_sbal_addr_array;
	int i;

	sprintf(dbf_text, "qset%4x", qdio_init->cdev->private->schid.sch_no);
	QDIO_DBF_TEXT0(0, setup, dbf_text);

	for_each_input_queue(irq_ptr, q, i) {
		sprintf(dbf_text, "in-q%4x", i);
		setup_queues_misc(q, irq_ptr, qdio_init->input_handler, i);

		q->is_input_q = 1;
		spin_lock_init(&q->u.in.lock);
		setup_storage_lists(q, irq_ptr, input_sbal_array, dbf_text, i);
		input_sbal_array += QDIO_MAX_BUFFERS_PER_Q;

		if (is_thinint_irq(irq_ptr))
			tasklet_init(&q->tasklet, tiqdio_inbound_processing,
				     (unsigned long) q);
		else
			tasklet_init(&q->tasklet, qdio_inbound_processing,
				     (unsigned long) q);
	}

	for_each_output_queue(irq_ptr, q, i) {
		sprintf(dbf_text, "outq%4x", i);
		setup_queues_misc(q, irq_ptr, qdio_init->output_handler, i);

		q->is_input_q = 0;
		setup_storage_lists(q, irq_ptr, output_sbal_array,
				    dbf_text, i);
		output_sbal_array += QDIO_MAX_BUFFERS_PER_Q;

		tasklet_init(&q->tasklet, qdio_outbound_processing,
			     (unsigned long) q);
		setup_timer(&q->u.out.timer, (void(*)(unsigned long))
			    &qdio_outbound_timer, (unsigned long)q);
	}
}

static void process_ac_flags(struct qdio_irq *irq_ptr, unsigned char qdioac)
{
	if (qdioac & AC1_SIGA_INPUT_NEEDED)
		irq_ptr->siga_flag.input = 1;
	if (qdioac & AC1_SIGA_OUTPUT_NEEDED)
		irq_ptr->siga_flag.output = 1;
	if (qdioac & AC1_SIGA_SYNC_NEEDED)
		irq_ptr->siga_flag.sync = 1;
	if (qdioac & AC1_AUTOMATIC_SYNC_ON_THININT)
		irq_ptr->siga_flag.no_sync_ti = 1;
	if (qdioac & AC1_AUTOMATIC_SYNC_ON_OUT_PCI)
		irq_ptr->siga_flag.no_sync_out_pci = 1;

	if (irq_ptr->siga_flag.no_sync_out_pci &&
	    irq_ptr->siga_flag.no_sync_ti)
		irq_ptr->siga_flag.no_sync_out_ti = 1;
}

static void check_and_setup_qebsm(struct qdio_irq *irq_ptr,
				  unsigned char qdioac, unsigned long token)
{
	char dbf_text[15];

	if (!(irq_ptr->qib.rflags & QIB_RFLAGS_ENABLE_QEBSM))
		goto no_qebsm;
	if (!(qdioac & AC1_SC_QEBSM_AVAILABLE) ||
	    (!(qdioac & AC1_SC_QEBSM_ENABLED)))
		goto no_qebsm;

	irq_ptr->sch_token = token;

	QDIO_DBF_TEXT0(0, setup, "V=V:1");
	sprintf(dbf_text, "%8lx", irq_ptr->sch_token);
	QDIO_DBF_TEXT0(0, setup, dbf_text);
	return;

no_qebsm:
	irq_ptr->sch_token = 0;
	irq_ptr->qib.rflags &= ~QIB_RFLAGS_ENABLE_QEBSM;
	QDIO_DBF_TEXT0(0, setup, "noV=V");
}

static int __get_ssqd_info(struct qdio_irq *irq_ptr)
{
	struct chsc_ssqd_area *ssqd;
	int rc;

	QDIO_DBF_TEXT0(0, setup, "getssqd");
	ssqd = (struct chsc_ssqd_area *)irq_ptr->chsc_page;
	memset(ssqd, 0, PAGE_SIZE);

	ssqd->request = (struct chsc_header) {
		.length = 0x0010,
		.code	= 0x0024,
	};
	ssqd->first_sch = irq_ptr->schid.sch_no;
	ssqd->last_sch = irq_ptr->schid.sch_no;
	ssqd->ssid = irq_ptr->schid.ssid;

	if (chsc(ssqd))
		return -EIO;
	rc = chsc_error_from_response(ssqd->response.code);
	if (rc)
		return rc;

	if (!(ssqd->qdio_ssqd.flags & CHSC_FLAG_QDIO_CAPABILITY) ||
	    !(ssqd->qdio_ssqd.flags & CHSC_FLAG_VALIDITY) ||
	    (ssqd->qdio_ssqd.sch != irq_ptr->schid.sch_no))
		return -EINVAL;

	memcpy(&irq_ptr->ssqd_desc, &ssqd->qdio_ssqd,
	       sizeof(struct qdio_ssqd_desc));
	return 0;
}

void qdio_setup_ssqd_info(struct qdio_irq *irq_ptr)
{
	unsigned char qdioac;
	char dbf_text[15];
	int rc;

	rc = __get_ssqd_info(irq_ptr);
	if (rc) {
		QDIO_DBF_TEXT2(0, setup, "ssqdasig");
		sprintf(dbf_text, "schn%4x", irq_ptr->schid.sch_no);
		QDIO_DBF_TEXT2(0, setup, dbf_text);
		sprintf(dbf_text, "rc:%d", rc);
		QDIO_DBF_TEXT2(0, setup, dbf_text);
		/* all flags set, worst case */
		qdioac = AC1_SIGA_INPUT_NEEDED | AC1_SIGA_OUTPUT_NEEDED |
			 AC1_SIGA_SYNC_NEEDED;
	} else
		qdioac = irq_ptr->ssqd_desc.qdioac1;

	check_and_setup_qebsm(irq_ptr, qdioac, irq_ptr->ssqd_desc.sch_token);
	process_ac_flags(irq_ptr, qdioac);

	sprintf(dbf_text, "qdioac%2x", qdioac);
	QDIO_DBF_TEXT2(0, setup, dbf_text);
}

void qdio_release_memory(struct qdio_irq *irq_ptr)
{
	struct qdio_q *q;
	int i;

	/*
	 * Must check queue array manually since irq_ptr->nr_input_queues /
	 * irq_ptr->nr_input_queues may not yet be set.
	 */
	for (i = 0; i < QDIO_MAX_QUEUES_PER_IRQ; i++) {
		q = irq_ptr->input_qs[i];
		if (q) {
			free_page((unsigned long) q->slib);
			kmem_cache_free(qdio_q_cache, q);
		}
	}
	for (i = 0; i < QDIO_MAX_QUEUES_PER_IRQ; i++) {
		q = irq_ptr->output_qs[i];
		if (q) {
			free_page((unsigned long) q->slib);
			kmem_cache_free(qdio_q_cache, q);
		}
	}
	free_page((unsigned long) irq_ptr->qdr);
	free_page(irq_ptr->chsc_page);
	free_page((unsigned long) irq_ptr);
}

static void __qdio_allocate_fill_qdr(struct qdio_irq *irq_ptr,
				     struct qdio_q **irq_ptr_qs,
				     int i, int nr)
{
	irq_ptr->qdr->qdf0[i + nr].sliba =
		(unsigned long)irq_ptr_qs[i]->slib;

	irq_ptr->qdr->qdf0[i + nr].sla =
		(unsigned long)irq_ptr_qs[i]->sl;

	irq_ptr->qdr->qdf0[i + nr].slsba =
		(unsigned long)&irq_ptr_qs[i]->slsb.val[0];

	irq_ptr->qdr->qdf0[i + nr].akey = PAGE_DEFAULT_KEY;
	irq_ptr->qdr->qdf0[i + nr].bkey = PAGE_DEFAULT_KEY;
	irq_ptr->qdr->qdf0[i + nr].ckey = PAGE_DEFAULT_KEY;
	irq_ptr->qdr->qdf0[i + nr].dkey = PAGE_DEFAULT_KEY;
}

static void setup_qdr(struct qdio_irq *irq_ptr,
		      struct qdio_initialize *qdio_init)
{
	int i;

	irq_ptr->qdr->qfmt = qdio_init->q_format;
	irq_ptr->qdr->iqdcnt = qdio_init->no_input_qs;
	irq_ptr->qdr->oqdcnt = qdio_init->no_output_qs;
	irq_ptr->qdr->iqdsz = sizeof(struct qdesfmt0) / 4; /* size in words */
	irq_ptr->qdr->oqdsz = sizeof(struct qdesfmt0) / 4;
	irq_ptr->qdr->qiba = (unsigned long)&irq_ptr->qib;
	irq_ptr->qdr->qkey = PAGE_DEFAULT_KEY;

	for (i = 0; i < qdio_init->no_input_qs; i++)
		__qdio_allocate_fill_qdr(irq_ptr, irq_ptr->input_qs, i, 0);

	for (i = 0; i < qdio_init->no_output_qs; i++)
		__qdio_allocate_fill_qdr(irq_ptr, irq_ptr->output_qs, i,
					 qdio_init->no_input_qs);
}

static void setup_qib(struct qdio_irq *irq_ptr,
		      struct qdio_initialize *init_data)
{
	if (qebsm_possible())
		irq_ptr->qib.rflags |= QIB_RFLAGS_ENABLE_QEBSM;

	irq_ptr->qib.qfmt = init_data->q_format;
	if (init_data->no_input_qs)
		irq_ptr->qib.isliba =
			(unsigned long)(irq_ptr->input_qs[0]->slib);
	if (init_data->no_output_qs)
		irq_ptr->qib.osliba =
			(unsigned long)(irq_ptr->output_qs[0]->slib);
	memcpy(irq_ptr->qib.ebcnam, init_data->adapter_name, 8);
}

int qdio_setup_irq(struct qdio_initialize *init_data)
{
	struct ciw *ciw;
	struct qdio_irq *irq_ptr = init_data->cdev->private->qdio_data;
	int rc;

	memset(irq_ptr, 0, ((char *)&irq_ptr->qdr) - ((char *)irq_ptr));
	/* wipes qib.ac, required by ar7063 */
	memset(irq_ptr->qdr, 0, sizeof(struct qdr));

	irq_ptr->int_parm = init_data->int_parm;
	irq_ptr->nr_input_qs = init_data->no_input_qs;
	irq_ptr->nr_output_qs = init_data->no_output_qs;

	irq_ptr->schid = ccw_device_get_subchannel_id(init_data->cdev);
	irq_ptr->cdev = init_data->cdev;
	setup_queues(irq_ptr, init_data);

	setup_qib(irq_ptr, init_data);
	qdio_setup_thinint(irq_ptr);
	set_impl_params(irq_ptr, init_data->qib_param_field_format,
			init_data->qib_param_field,
			init_data->input_slib_elements,
			init_data->output_slib_elements);

	/* fill input and output descriptors */
	setup_qdr(irq_ptr, init_data);

	/* qdr, qib, sls, slsbs, slibs, sbales are filled now */

	/* get qdio commands */
	ciw = ccw_device_get_ciw(init_data->cdev, CIW_TYPE_EQUEUE);
	if (!ciw) {
		QDIO_DBF_TEXT2(1, setup, "no eq");
		rc = -EINVAL;
		goto out_err;
	}
	irq_ptr->equeue = *ciw;

	ciw = ccw_device_get_ciw(init_data->cdev, CIW_TYPE_AQUEUE);
	if (!ciw) {
		QDIO_DBF_TEXT2(1, setup, "no aq");
		rc = -EINVAL;
		goto out_err;
	}
	irq_ptr->aqueue = *ciw;

	/* set new interrupt handler */
	irq_ptr->orig_handler = init_data->cdev->handler;
	init_data->cdev->handler = qdio_int_handler;
	return 0;
out_err:
	qdio_release_memory(irq_ptr);
	return rc;
}

void qdio_print_subchannel_info(struct qdio_irq *irq_ptr,
				struct ccw_device *cdev)
{
	char s[80];

	sprintf(s, "%s sc:%x ", cdev->dev.bus_id, irq_ptr->schid.sch_no);

	switch (irq_ptr->qib.qfmt) {
	case QDIO_QETH_QFMT:
		sprintf(s + strlen(s), "OSADE ");
		break;
	case QDIO_ZFCP_QFMT:
		sprintf(s + strlen(s), "ZFCP ");
		break;
	case QDIO_IQDIO_QFMT:
		sprintf(s + strlen(s), "HiperSockets ");
		break;
	}
	sprintf(s + strlen(s), "using: ");

	if (!is_thinint_irq(irq_ptr))
		sprintf(s + strlen(s), "no");
	sprintf(s + strlen(s), "AdapterInterrupts ");
	if (!(irq_ptr->sch_token != 0))
		sprintf(s + strlen(s), "no");
	sprintf(s + strlen(s), "QEBSM ");
	if (!(irq_ptr->qib.ac & QIB_AC_OUTBOUND_PCI_SUPPORTED))
		sprintf(s + strlen(s), "no");
	sprintf(s + strlen(s), "OutboundPCI ");
	if (!css_general_characteristics.aif_tdd)
		sprintf(s + strlen(s), "no");
	sprintf(s + strlen(s), "TDD\n");
	printk(KERN_INFO "qdio: %s", s);

	memset(s, 0, sizeof(s));
	sprintf(s, "%s SIGA required: ", cdev->dev.bus_id);
	if (irq_ptr->siga_flag.input)
		sprintf(s + strlen(s), "Read ");
	if (irq_ptr->siga_flag.output)
		sprintf(s + strlen(s), "Write ");
	if (irq_ptr->siga_flag.sync)
		sprintf(s + strlen(s), "Sync ");
	if (!irq_ptr->siga_flag.no_sync_ti)
		sprintf(s + strlen(s), "SyncAI ");
	if (!irq_ptr->siga_flag.no_sync_out_ti)
		sprintf(s + strlen(s), "SyncOutAI ");
	if (!irq_ptr->siga_flag.no_sync_out_pci)
		sprintf(s + strlen(s), "SyncOutPCI");
	sprintf(s + strlen(s), "\n");
	printk(KERN_INFO "qdio: %s", s);
}

int __init qdio_setup_init(void)
{
	char dbf_text[15];

	qdio_q_cache = kmem_cache_create("qdio_q", sizeof(struct qdio_q),
					 256, 0, NULL);
	if (!qdio_q_cache)
		return -ENOMEM;

	/* Check for OSA/FCP thin interrupts (bit 67). */
	sprintf(dbf_text, "thini%1x",
		(css_general_characteristics.aif_osa) ? 1 : 0);
	QDIO_DBF_TEXT0(0, setup, dbf_text);

	/* Check for QEBSM support in general (bit 58). */
	sprintf(dbf_text, "cssQBS:%1x",
		(qebsm_possible()) ? 1 : 0);
	QDIO_DBF_TEXT0(0, setup, dbf_text);
	return 0;
}

void qdio_setup_exit(void)
{
	kmem_cache_destroy(qdio_q_cache);
}
