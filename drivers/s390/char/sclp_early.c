/*
 * SCLP early driver
 *
 * Copyright IBM Corp. 2013
 */

#define KMSG_COMPONENT "sclp_early"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <asm/sclp.h>
#include <asm/ipl.h>
#include "sclp_sdias.h"
#include "sclp.h"

static __initdata char sccb_early[PAGE_SIZE] __aligned(PAGE_SIZE);
static unsigned long sclp_hsa_size;

static int __init sclp_cmd_early(sclp_cmdw_t cmd, void *sccb)
{
	int rc;

	do {
		rc = sclp_cmd_sync_early(cmd, sccb);
	} while (rc == -EBUSY);

	if (rc)
		return -EIO;
	if (((struct sccb_header *) sccb)->response_code != 0x0020)
		return -EIO;
	return 0;
}

static void __init sccb_init_eq_size(struct sdias_sccb *sccb)
{
	memset(sccb, 0, sizeof(*sccb));

	sccb->hdr.length = sizeof(*sccb);
	sccb->evbuf.hdr.length = sizeof(struct sdias_evbuf);
	sccb->evbuf.hdr.type = EVTYP_SDIAS;
	sccb->evbuf.event_qual = SDIAS_EQ_SIZE;
	sccb->evbuf.data_id = SDIAS_DI_FCP_DUMP;
	sccb->evbuf.event_id = 4712;
	sccb->evbuf.dbs = 1;
}

static int __init sclp_set_event_mask(unsigned long receive_mask,
				      unsigned long send_mask)
{
	struct init_sccb *sccb = (void *) &sccb_early;

	memset(sccb, 0, sizeof(*sccb));
	sccb->header.length = sizeof(*sccb);
	sccb->mask_length = sizeof(sccb_mask_t);
	sccb->receive_mask = receive_mask;
	sccb->send_mask = send_mask;
	return sclp_cmd_early(SCLP_CMDW_WRITE_EVENT_MASK, sccb);
}

static long __init sclp_hsa_size_init(void)
{
	struct sdias_sccb *sccb = (void *) &sccb_early;

	sccb_init_eq_size(sccb);
	if (sclp_cmd_early(SCLP_CMDW_WRITE_EVENT_DATA, sccb))
		return -EIO;
	if (sccb->evbuf.blk_cnt != 0)
		return (sccb->evbuf.blk_cnt - 1) * PAGE_SIZE;
	return 0;
}

static long __init sclp_hsa_copy_wait(void)
{
	struct sccb_header *sccb = (void *) &sccb_early;

	memset(sccb, 0, PAGE_SIZE);
	sccb->length = PAGE_SIZE;
	if (sclp_cmd_early(SCLP_CMDW_READ_EVENT_DATA, sccb))
		return -EIO;
	return (((struct sdias_sccb *) sccb)->evbuf.blk_cnt - 1) * PAGE_SIZE;
}

unsigned long sclp_get_hsa_size(void)
{
	return sclp_hsa_size;
}

void __init sclp_hsa_size_detect(void)
{
	long size;

	/* First try synchronous interface (LPAR) */
	if (sclp_set_event_mask(0, 0x40000010))
		return;
	size = sclp_hsa_size_init();
	if (size < 0)
		return;
	if (size != 0)
		goto out;
	/* Then try asynchronous interface (z/VM) */
	if (sclp_set_event_mask(0x00000010, 0x40000010))
		return;
	size = sclp_hsa_size_init();
	if (size < 0)
		return;
	size = sclp_hsa_copy_wait();
	if (size < 0)
		return;
out:
	sclp_set_event_mask(0, 0);
	sclp_hsa_size = size;
}
