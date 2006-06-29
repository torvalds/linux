/*
 * File...........: linux/drivers/s390/block/dasd_9345_erp.c
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 2000
 *
 */

#define PRINTK_HEADER "dasd_erp(9343)"

#include "dasd_int.h"

dasd_era_t
dasd_9343_erp_examine(struct dasd_ccw_req * cqr, struct irb * irb)
{
	if (irb->scsw.cstat == 0x00 &&
	    irb->scsw.dstat == (DEV_STAT_CHN_END | DEV_STAT_DEV_END))
		return dasd_era_none;

	return dasd_era_recover;
}
