/*
 *  drivers/s390/char/sclp_info.c
 *
 *    Copyright IBM Corp. 2007
 *    Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <asm/sclp.h>
#include "sclp.h"

struct sclp_readinfo_sccb {
	struct	sccb_header header;	/* 0-7 */
	u16	rnmax;			/* 8-9 */
	u8	rnsize;			/* 10 */
	u8	_reserved0[24 - 11];	/* 11-23 */
	u8	loadparm[8];		/* 24-31 */
	u8	_reserved1[48 - 32];	/* 32-47 */
	u64	facilities;		/* 48-55 */
	u8	_reserved2[91 - 56];	/* 56-90 */
	u8	flags;			/* 91 */
	u8	_reserved3[100 - 92];	/* 92-99 */
	u32	rnsize2;		/* 100-103 */
	u64	rnmax2;			/* 104-111 */
	u8	_reserved4[4096 - 112];	/* 112-4095 */
} __attribute__((packed, aligned(4096)));

static struct sclp_readinfo_sccb __initdata early_readinfo_sccb;
static int __initdata early_readinfo_sccb_valid;

u64 sclp_facilities;

void __init sclp_readinfo_early(void)
{
	int ret;
	int i;
	struct sclp_readinfo_sccb *sccb;
	sclp_cmdw_t commands[] = {SCLP_CMDW_READ_SCP_INFO_FORCED,
				  SCLP_CMDW_READ_SCP_INFO};

	/* Enable service signal subclass mask. */
	__ctl_set_bit(0, 9);
	sccb = &early_readinfo_sccb;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		do {
			memset(sccb, 0, sizeof(*sccb));
			sccb->header.length = sizeof(*sccb);
			sccb->header.control_mask[2] = 0x80;
			ret = sclp_service_call(commands[i], sccb);
		} while (ret == -EBUSY);

		if (ret)
			break;
		__load_psw_mask(PSW_BASE_BITS | PSW_MASK_EXT |
				PSW_MASK_WAIT | PSW_DEFAULT_KEY);
		local_irq_disable();
		/*
		 * Contents of the sccb might have changed
		 * therefore a barrier is needed.
		 */
		barrier();
		if (sccb->header.response_code == 0x10) {
			early_readinfo_sccb_valid = 1;
			break;
		}
		if (sccb->header.response_code != 0x1f0)
			break;
	}
	/* Disable service signal subclass mask again. */
	__ctl_clear_bit(0, 9);
}

void __init sclp_facilities_detect(void)
{
	if (!early_readinfo_sccb_valid)
		return;
	sclp_facilities = early_readinfo_sccb.facilities;
}

unsigned long long __init sclp_memory_detect(void)
{
	unsigned long long memsize;
	struct sclp_readinfo_sccb *sccb;

	if (!early_readinfo_sccb_valid)
		return 0;
	sccb = &early_readinfo_sccb;
	if (sccb->rnsize)
		memsize = sccb->rnsize << 20;
	else
		memsize = sccb->rnsize2 << 20;
	if (sccb->rnmax)
		memsize *= sccb->rnmax;
	else
		memsize *= sccb->rnmax2;
	return memsize;
}

/*
 * This function will be called after sclp_memory_detect(), which gets called
 * early from early.c code. Therefore the sccb should have valid contents.
 */
void __init sclp_get_ipl_info(struct sclp_ipl_info *info)
{
	struct sclp_readinfo_sccb *sccb;

	if (!early_readinfo_sccb_valid)
		return;
	sccb = &early_readinfo_sccb;
	info->is_valid = 1;
	if (sccb->flags & 0x2)
		info->has_dump = 1;
	memcpy(&info->loadparm, &sccb->loadparm, LOADPARM_LEN);
}
