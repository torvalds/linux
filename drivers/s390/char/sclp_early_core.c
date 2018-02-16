// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 2015
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kernel.h>
#include <asm/processor.h>
#include <asm/lowcore.h>
#include <asm/ebcdic.h>
#include <asm/irq.h>
#include "sclp.h"
#include "sclp_rw.h"

char sclp_early_sccb[PAGE_SIZE] __aligned(PAGE_SIZE) __section(.data);
int sclp_init_state __section(.data) = sclp_init_state_uninitialized;

void sclp_early_wait_irq(void)
{
	unsigned long psw_mask, addr;
	psw_t psw_ext_save, psw_wait;
	union ctlreg0 cr0, cr0_new;

	__ctl_store(cr0.val, 0, 0);
	cr0_new.val = cr0.val & ~CR0_IRQ_SUBCLASS_MASK;
	cr0_new.lap = 0;
	cr0_new.sssm = 1;
	__ctl_load(cr0_new.val, 0, 0);

	psw_ext_save = S390_lowcore.external_new_psw;
	psw_mask = __extract_psw();
	S390_lowcore.external_new_psw.mask = psw_mask;
	psw_wait.mask = psw_mask | PSW_MASK_EXT | PSW_MASK_WAIT;
	S390_lowcore.ext_int_code = 0;

	do {
		asm volatile(
			"	larl	%[addr],0f\n"
			"	stg	%[addr],%[psw_wait_addr]\n"
			"	stg	%[addr],%[psw_ext_addr]\n"
			"	lpswe	%[psw_wait]\n"
			"0:\n"
			: [addr] "=&d" (addr),
			  [psw_wait_addr] "=Q" (psw_wait.addr),
			  [psw_ext_addr] "=Q" (S390_lowcore.external_new_psw.addr)
			: [psw_wait] "Q" (psw_wait)
			: "cc", "memory");
	} while (S390_lowcore.ext_int_code != EXT_IRQ_SERVICE_SIG);

	S390_lowcore.external_new_psw = psw_ext_save;
	__ctl_load(cr0.val, 0, 0);
}

int sclp_early_cmd(sclp_cmdw_t cmd, void *sccb)
{
	unsigned long flags;
	int rc;

	raw_local_irq_save(flags);
	rc = sclp_service_call(cmd, sccb);
	if (rc)
		goto out;
	sclp_early_wait_irq();
out:
	raw_local_irq_restore(flags);
	return rc;
}

struct write_sccb {
	struct sccb_header header;
	struct msg_buf msg;
} __packed;

/* Output multi-line text using SCLP Message interface. */
static void sclp_early_print_lm(const char *str, unsigned int len)
{
	unsigned char *ptr, *end, ch;
	unsigned int count, offset;
	struct write_sccb *sccb;
	struct msg_buf *msg;
	struct mdb *mdb;
	struct mto *mto;
	struct go *go;

	sccb = (struct write_sccb *) &sclp_early_sccb;
	end = (unsigned char *) sccb + sizeof(sclp_early_sccb) - 1;
	memset(sccb, 0, sizeof(*sccb));
	ptr = (unsigned char *) &sccb->msg.mdb.mto;
	offset = 0;
	do {
		for (count = sizeof(*mto); offset < len; count++) {
			ch = str[offset++];
			if ((ch == 0x0a) || (ptr + count > end))
				break;
			ptr[count] = _ascebc[ch];
		}
		mto = (struct mto *) ptr;
		memset(mto, 0, sizeof(*mto));
		mto->length = count;
		mto->type = 4;
		mto->line_type_flags = LNTPFLGS_ENDTEXT;
		ptr += count;
	} while ((offset < len) && (ptr + sizeof(*mto) <= end));
	len = ptr - (unsigned char *) sccb;
	sccb->header.length = len - offsetof(struct write_sccb, header);
	msg = &sccb->msg;
	msg->header.type = EVTYP_MSG;
	msg->header.length = len - offsetof(struct write_sccb, msg.header);
	mdb = &msg->mdb;
	mdb->header.type = 1;
	mdb->header.tag = 0xD4C4C240;
	mdb->header.revision_code = 1;
	mdb->header.length = len - offsetof(struct write_sccb, msg.mdb.header);
	go = &mdb->go;
	go->length = sizeof(*go);
	go->type = 1;
	sclp_early_cmd(SCLP_CMDW_WRITE_EVENT_DATA, sccb);
}

struct vt220_sccb {
	struct sccb_header header;
	struct {
		struct evbuf_header header;
		char data[];
	} msg;
} __packed;

/* Output multi-line text using SCLP VT220 interface. */
static void sclp_early_print_vt220(const char *str, unsigned int len)
{
	struct vt220_sccb *sccb;

	sccb = (struct vt220_sccb *) &sclp_early_sccb;
	if (sizeof(*sccb) + len >= sizeof(sclp_early_sccb))
		len = sizeof(sclp_early_sccb) - sizeof(*sccb);
	memset(sccb, 0, sizeof(*sccb));
	memcpy(&sccb->msg.data, str, len);
	sccb->header.length = sizeof(*sccb) + len;
	sccb->msg.header.length = sizeof(sccb->msg) + len;
	sccb->msg.header.type = EVTYP_VT220MSG;
	sclp_early_cmd(SCLP_CMDW_WRITE_EVENT_DATA, sccb);
}

int sclp_early_set_event_mask(struct init_sccb *sccb,
			      unsigned long receive_mask,
			      unsigned long send_mask)
{
	memset(sccb, 0, sizeof(*sccb));
	sccb->header.length = sizeof(*sccb);
	sccb->mask_length = sizeof(sccb_mask_t);
	sccb->receive_mask = receive_mask;
	sccb->send_mask = send_mask;
	if (sclp_early_cmd(SCLP_CMDW_WRITE_EVENT_MASK, sccb))
		return -EIO;
	if (sccb->header.response_code != 0x20)
		return -EIO;
	return 0;
}

unsigned int sclp_early_con_check_linemode(struct init_sccb *sccb)
{
	if (!(sccb->sclp_send_mask & EVTYP_OPCMD_MASK))
		return 0;
	if (!(sccb->sclp_receive_mask & (EVTYP_MSG_MASK | EVTYP_PMSGCMD_MASK)))
		return 0;
	return 1;
}

static int sclp_early_setup(int disable, int *have_linemode, int *have_vt220)
{
	unsigned long receive_mask, send_mask;
	struct init_sccb *sccb;
	int rc;

	*have_linemode = *have_vt220 = 0;
	sccb = (struct init_sccb *) &sclp_early_sccb;
	receive_mask = disable ? 0 : EVTYP_OPCMD_MASK;
	send_mask = disable ? 0 : EVTYP_VT220MSG_MASK | EVTYP_MSG_MASK;
	rc = sclp_early_set_event_mask(sccb, receive_mask, send_mask);
	if (rc)
		return rc;
	*have_linemode = sclp_early_con_check_linemode(sccb);
	*have_vt220 = sccb->send_mask & EVTYP_VT220MSG_MASK;
	return rc;
}

/*
 * Output one or more lines of text on the SCLP console (VT220 and /
 * or line-mode).
 */
void __sclp_early_printk(const char *str, unsigned int len)
{
	int have_linemode, have_vt220;

	if (sclp_init_state != sclp_init_state_uninitialized)
		return;
	if (sclp_early_setup(0, &have_linemode, &have_vt220) != 0)
		return;
	if (have_linemode)
		sclp_early_print_lm(str, len);
	if (have_vt220)
		sclp_early_print_vt220(str, len);
	sclp_early_setup(1, &have_linemode, &have_vt220);
}

void sclp_early_printk(const char *str)
{
	__sclp_early_printk(str, strlen(str));
}
