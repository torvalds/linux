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

struct sclp_readinfo_sccb s390_readinfo_sccb;

void __init sclp_readinfo_early(void)
{
	sclp_cmdw_t command;
	struct sccb_header *sccb;
	int ret;

	__ctl_set_bit(0, 9); /* enable service signal subclass mask */

	sccb = &s390_readinfo_sccb.header;
	command = SCLP_CMDW_READ_SCP_INFO_FORCED;
	while (1) {
		u16 response;

		memset(&s390_readinfo_sccb, 0, sizeof(s390_readinfo_sccb));
		sccb->length = sizeof(s390_readinfo_sccb);
		sccb->control_mask[2] = 0x80;

		ret = sclp_service_call(command, &s390_readinfo_sccb);

		if (ret == -EIO)
			goto out;
		if (ret == -EBUSY)
			continue;

		__load_psw_mask(PSW_BASE_BITS | PSW_MASK_EXT |
				PSW_MASK_WAIT | PSW_DEFAULT_KEY);
		local_irq_disable();
		barrier();

		response = sccb->response_code;

		if (response == 0x10)
			break;

		if (response != 0x1f0 || command == SCLP_CMDW_READ_SCP_INFO)
			break;

		command = SCLP_CMDW_READ_SCP_INFO;
	}
out:
	__ctl_clear_bit(0, 9); /* disable service signal subclass mask */
}
