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
#include <asm/sections.h>
#include <asm/mem_detect.h>
#include <asm/facility.h>
#include "sclp.h"
#include "sclp_rw.h"

static struct read_info_sccb __bootdata(sclp_info_sccb);
static int __bootdata(sclp_info_sccb_valid);
char *sclp_early_sccb = (char *) EARLY_SCCB_OFFSET;
int sclp_init_state = sclp_init_state_uninitialized;
/*
 * Used to keep track of the size of the event masks. Qemu until version 2.11
 * only supports 4 and needs a workaround.
 */
bool sclp_mask_compat_mode;

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

	flags = arch_local_irq_save();
	rc = sclp_service_call(cmd, sccb);
	if (rc)
		goto out;
	sclp_early_wait_irq();
out:
	arch_local_irq_restore(flags);
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

	sccb = (struct write_sccb *) sclp_early_sccb;
	end = (unsigned char *) sccb + EARLY_SCCB_SIZE - 1;
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

	sccb = (struct vt220_sccb *) sclp_early_sccb;
	if (sizeof(*sccb) + len >= EARLY_SCCB_SIZE)
		len = EARLY_SCCB_SIZE - sizeof(*sccb);
	memset(sccb, 0, sizeof(*sccb));
	memcpy(&sccb->msg.data, str, len);
	sccb->header.length = sizeof(*sccb) + len;
	sccb->msg.header.length = sizeof(sccb->msg) + len;
	sccb->msg.header.type = EVTYP_VT220MSG;
	sclp_early_cmd(SCLP_CMDW_WRITE_EVENT_DATA, sccb);
}

int sclp_early_set_event_mask(struct init_sccb *sccb,
			      sccb_mask_t receive_mask,
			      sccb_mask_t send_mask)
{
retry:
	memset(sccb, 0, sizeof(*sccb));
	sccb->header.length = sizeof(*sccb);
	if (sclp_mask_compat_mode)
		sccb->mask_length = SCLP_MASK_SIZE_COMPAT;
	else
		sccb->mask_length = sizeof(sccb_mask_t);
	sccb_set_recv_mask(sccb, receive_mask);
	sccb_set_send_mask(sccb, send_mask);
	if (sclp_early_cmd(SCLP_CMDW_WRITE_EVENT_MASK, sccb))
		return -EIO;
	if ((sccb->header.response_code == 0x74f0) && !sclp_mask_compat_mode) {
		sclp_mask_compat_mode = true;
		goto retry;
	}
	if (sccb->header.response_code != 0x20)
		return -EIO;
	return 0;
}

unsigned int sclp_early_con_check_linemode(struct init_sccb *sccb)
{
	if (!(sccb_get_sclp_send_mask(sccb) & EVTYP_OPCMD_MASK))
		return 0;
	if (!(sccb_get_sclp_recv_mask(sccb) & (EVTYP_MSG_MASK | EVTYP_PMSGCMD_MASK)))
		return 0;
	return 1;
}

unsigned int sclp_early_con_check_vt220(struct init_sccb *sccb)
{
	if (sccb_get_sclp_send_mask(sccb) & EVTYP_VT220MSG_MASK)
		return 1;
	return 0;
}

static int sclp_early_setup(int disable, int *have_linemode, int *have_vt220)
{
	unsigned long receive_mask, send_mask;
	struct init_sccb *sccb;
	int rc;

	BUILD_BUG_ON(sizeof(struct init_sccb) > PAGE_SIZE);

	*have_linemode = *have_vt220 = 0;
	sccb = (struct init_sccb *) sclp_early_sccb;
	receive_mask = disable ? 0 : EVTYP_OPCMD_MASK;
	send_mask = disable ? 0 : EVTYP_VT220MSG_MASK | EVTYP_MSG_MASK;
	rc = sclp_early_set_event_mask(sccb, receive_mask, send_mask);
	if (rc)
		return rc;
	*have_linemode = sclp_early_con_check_linemode(sccb);
	*have_vt220 = !!(sccb_get_send_mask(sccb) & EVTYP_VT220MSG_MASK);
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

int __init sclp_early_read_info(void)
{
	int i;
	int length = test_facility(140) ? EXT_SCCB_READ_SCP : PAGE_SIZE;
	struct read_info_sccb *sccb = &sclp_info_sccb;
	sclp_cmdw_t commands[] = {SCLP_CMDW_READ_SCP_INFO_FORCED,
				  SCLP_CMDW_READ_SCP_INFO};

	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		memset(sccb, 0, length);
		sccb->header.length = length;
		sccb->header.function_code = 0x80;
		sccb->header.control_mask[2] = 0x80;
		if (sclp_early_cmd(commands[i], sccb))
			break;
		if (sccb->header.response_code == 0x10) {
			sclp_info_sccb_valid = 1;
			return 0;
		}
		if (sccb->header.response_code != 0x1f0)
			break;
	}
	return -EIO;
}

struct read_info_sccb * __init sclp_early_get_info(void)
{
	if (!sclp_info_sccb_valid)
		return NULL;

	return &sclp_info_sccb;
}

int __init sclp_early_get_memsize(unsigned long *mem)
{
	unsigned long rnmax;
	unsigned long rnsize;
	struct read_info_sccb *sccb = &sclp_info_sccb;

	if (!sclp_info_sccb_valid)
		return -EIO;

	rnmax = sccb->rnmax ? sccb->rnmax : sccb->rnmax2;
	rnsize = sccb->rnsize ? sccb->rnsize : sccb->rnsize2;
	rnsize <<= 20;
	*mem = rnsize * rnmax;
	return 0;
}

int __init sclp_early_get_hsa_size(unsigned long *hsa_size)
{
	if (!sclp_info_sccb_valid)
		return -EIO;

	*hsa_size = 0;
	if (sclp_info_sccb.hsa_size)
		*hsa_size = (sclp_info_sccb.hsa_size - 1) * PAGE_SIZE;
	return 0;
}

#define SCLP_STORAGE_INFO_FACILITY     0x0000400000000000UL

void __weak __init add_mem_detect_block(u64 start, u64 end) {}
int __init sclp_early_read_storage_info(void)
{
	struct read_storage_sccb *sccb = (struct read_storage_sccb *)sclp_early_sccb;
	int rc, id, max_id = 0;
	unsigned long rn, rzm;
	sclp_cmdw_t command;
	u16 sn;

	if (!sclp_info_sccb_valid)
		return -EIO;

	if (!(sclp_info_sccb.facilities & SCLP_STORAGE_INFO_FACILITY))
		return -EOPNOTSUPP;

	rzm = sclp_info_sccb.rnsize ?: sclp_info_sccb.rnsize2;
	rzm <<= 20;

	for (id = 0; id <= max_id; id++) {
		memset(sclp_early_sccb, 0, EARLY_SCCB_SIZE);
		sccb->header.length = EARLY_SCCB_SIZE;
		command = SCLP_CMDW_READ_STORAGE_INFO | (id << 8);
		rc = sclp_early_cmd(command, sccb);
		if (rc)
			goto fail;

		max_id = sccb->max_id;
		switch (sccb->header.response_code) {
		case 0x0010:
			for (sn = 0; sn < sccb->assigned; sn++) {
				if (!sccb->entries[sn])
					continue;
				rn = sccb->entries[sn] >> 16;
				add_mem_detect_block((rn - 1) * rzm, rn * rzm);
			}
			break;
		case 0x0310:
		case 0x0410:
			break;
		default:
			goto fail;
		}
	}

	return 0;
fail:
	mem_detect.count = 0;
	return -EIO;
}
