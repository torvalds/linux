/*
 * Author: Martin Peschke <mpeschke@de.ibm.com>
 * Copyright (C) 2001 IBM Entwicklung GmbH, IBM Corporation
 *
 * SCLP Control-Program Identification.
 */

#include <linux/config.h>
#include <linux/version.h>
#include <linux/kmod.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <asm/ebcdic.h>
#include <asm/semaphore.h>

#include "sclp.h"
#include "sclp_rw.h"

#define CPI_LENGTH_SYSTEM_TYPE	8
#define CPI_LENGTH_SYSTEM_NAME	8
#define CPI_LENGTH_SYSPLEX_NAME	8

struct cpi_evbuf {
	struct evbuf_header header;
	u8	id_format;
	u8	reserved0;
	u8	system_type[CPI_LENGTH_SYSTEM_TYPE];
	u64	reserved1;
	u8	system_name[CPI_LENGTH_SYSTEM_NAME];
	u64	reserved2;
	u64	system_level;
	u64	reserved3;
	u8	sysplex_name[CPI_LENGTH_SYSPLEX_NAME];
	u8	reserved4[16];
} __attribute__((packed));

struct cpi_sccb {
	struct sccb_header header;
	struct cpi_evbuf cpi_evbuf;
} __attribute__((packed));

/* Event type structure for write message and write priority message */
static struct sclp_register sclp_cpi_event =
{
	.send_mask = EvTyp_CtlProgIdent_Mask
};

MODULE_AUTHOR(
	"Martin Peschke, IBM Deutschland Entwicklung GmbH "
	"<mpeschke@de.ibm.com>");

MODULE_DESCRIPTION(
	"identify this operating system instance to the S/390 "
	"or zSeries hardware");

static char *system_name = NULL;
module_param(system_name, charp, 0);
MODULE_PARM_DESC(system_name, "e.g. hostname - max. 8 characters");

static char *sysplex_name = NULL;
#ifdef ALLOW_SYSPLEX_NAME
module_param(sysplex_name, charp, 0);
MODULE_PARM_DESC(sysplex_name, "if applicable - max. 8 characters");
#endif

/* use default value for this field (as well as for system level) */
static char *system_type = "LINUX";

static int
cpi_check_parms(void)
{
	/* reject if no system type specified */
	if (!system_type) {
		printk("cpi: bug: no system type specified\n");
		return -EINVAL;
	}

	/* reject if system type larger than 8 characters */
	if (strlen(system_type) > CPI_LENGTH_SYSTEM_NAME) {
		printk("cpi: bug: system type has length of %li characters - "
		       "only %i characters supported\n",
		       strlen(system_type), CPI_LENGTH_SYSTEM_TYPE);
		return -EINVAL;
	}

	/* reject if no system name specified */
	if (!system_name) {
		printk("cpi: no system name specified\n");
		return -EINVAL;
	}

	/* reject if system name larger than 8 characters */
	if (strlen(system_name) > CPI_LENGTH_SYSTEM_NAME) {
		printk("cpi: system name has length of %li characters - "
		       "only %i characters supported\n",
		       strlen(system_name), CPI_LENGTH_SYSTEM_NAME);
		return -EINVAL;
	}

	/* reject if specified sysplex name larger than 8 characters */
	if (sysplex_name && strlen(sysplex_name) > CPI_LENGTH_SYSPLEX_NAME) {
		printk("cpi: sysplex name has length of %li characters"
		       " - only %i characters supported\n",
		       strlen(sysplex_name), CPI_LENGTH_SYSPLEX_NAME);
		return -EINVAL;
	}
	return 0;
}

static void
cpi_callback(struct sclp_req *req, void *data)
{
	struct semaphore *sem;

	sem = (struct semaphore *) data;
	up(sem);
}

static struct sclp_req *
cpi_prepare_req(void)
{
	struct sclp_req *req;
	struct cpi_sccb *sccb;
	struct cpi_evbuf *evb;

	req = (struct sclp_req *) kmalloc(sizeof(struct sclp_req), GFP_KERNEL);
	if (req == NULL)
		return ERR_PTR(-ENOMEM);
	sccb = (struct cpi_sccb *) __get_free_page(GFP_KERNEL | GFP_DMA);
	if (sccb == NULL) {
		kfree(req);
		return ERR_PTR(-ENOMEM);
	}
	memset(sccb, 0, sizeof(struct cpi_sccb));

	/* setup SCCB for Control-Program Identification */
	sccb->header.length = sizeof(struct cpi_sccb);
	sccb->cpi_evbuf.header.length = sizeof(struct cpi_evbuf);
	sccb->cpi_evbuf.header.type = 0x0B;
	evb = &sccb->cpi_evbuf;

	/* set system type */
	memset(evb->system_type, ' ', CPI_LENGTH_SYSTEM_TYPE);
	memcpy(evb->system_type, system_type, strlen(system_type));
	sclp_ascebc_str(evb->system_type, CPI_LENGTH_SYSTEM_TYPE);
	EBC_TOUPPER(evb->system_type, CPI_LENGTH_SYSTEM_TYPE);

	/* set system name */
	memset(evb->system_name, ' ', CPI_LENGTH_SYSTEM_NAME);
	memcpy(evb->system_name, system_name, strlen(system_name));
	sclp_ascebc_str(evb->system_name, CPI_LENGTH_SYSTEM_NAME);
	EBC_TOUPPER(evb->system_name, CPI_LENGTH_SYSTEM_NAME);

	/* set sytem level */
	evb->system_level = LINUX_VERSION_CODE;

	/* set sysplex name */
	if (sysplex_name) {
		memset(evb->sysplex_name, ' ', CPI_LENGTH_SYSPLEX_NAME);
		memcpy(evb->sysplex_name, sysplex_name, strlen(sysplex_name));
		sclp_ascebc_str(evb->sysplex_name, CPI_LENGTH_SYSPLEX_NAME);
		EBC_TOUPPER(evb->sysplex_name, CPI_LENGTH_SYSPLEX_NAME);
	}

	/* prepare request data structure presented to SCLP driver */
	req->command = SCLP_CMDW_WRITEDATA;
	req->sccb = sccb;
	req->status = SCLP_REQ_FILLED;
	req->callback = cpi_callback;
	return req;
}

static void
cpi_free_req(struct sclp_req *req)
{
	free_page((unsigned long) req->sccb);
	kfree(req);
}

static int __init
cpi_module_init(void)
{
	struct semaphore sem;
	struct sclp_req *req;
	int rc;

	rc = cpi_check_parms();
	if (rc)
		return rc;

	rc = sclp_register(&sclp_cpi_event);
	if (rc) {
		/* could not register sclp event. Die. */
		printk(KERN_WARNING "cpi: could not register to hardware "
		       "console.\n");
		return -EINVAL;
	}
	if (!(sclp_cpi_event.sclp_send_mask & EvTyp_CtlProgIdent_Mask)) {
		printk(KERN_WARNING "cpi: no control program identification "
		       "support\n");
		sclp_unregister(&sclp_cpi_event);
		return -ENOTSUPP;
	}

	req = cpi_prepare_req();
	if (IS_ERR(req)) {
		printk(KERN_WARNING "cpi: couldn't allocate request\n");
		sclp_unregister(&sclp_cpi_event);
		return PTR_ERR(req);
	}

	/* Prepare semaphore */
	sema_init(&sem, 0);
	req->callback_data = &sem;
	/* Add request to sclp queue */
	rc = sclp_add_request(req);
	if (rc) {
		printk(KERN_WARNING "cpi: could not start request\n");
		cpi_free_req(req);
		sclp_unregister(&sclp_cpi_event);
		return rc;
	}
	/* make "insmod" sleep until callback arrives */
	down(&sem);

	rc = ((struct cpi_sccb *) req->sccb)->header.response_code;
	if (rc != 0x0020) {
		printk(KERN_WARNING "cpi: failed with response code 0x%x\n",
		       rc);
		rc = -ECOMM;
	} else
		rc = 0;

	cpi_free_req(req);
	sclp_unregister(&sclp_cpi_event);

	return rc;
}


static void __exit cpi_module_exit(void)
{
}


/* declare driver module init/cleanup functions */
module_init(cpi_module_init);
module_exit(cpi_module_exit);

