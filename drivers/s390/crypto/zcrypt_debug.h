/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright IBM Corp. 2016
 *  Author(s): Holger Dengler (hd@linux.vnet.ibm.com)
 *	       Harald Freudenberger <freude@de.ibm.com>
 */
#ifndef ZCRYPT_DE_H
#define ZCRYPT_DE_H

#include <asm/de.h>

#define DBF_ERR		3	/* error conditions   */
#define DBF_WARN	4	/* warning conditions */
#define DBF_INFO	5	/* informational      */
#define DBF_DE	6	/* for deging only */

#define RC2ERR(rc) ((rc) ? DBF_ERR : DBF_INFO)
#define RC2WARN(rc) ((rc) ? DBF_WARN : DBF_INFO)

#define DBF_MAX_SPRINTF_ARGS 5

#define ZCRYPT_DBF(...)					\
	de_sprintf_event(zcrypt_dbf_info, ##__VA_ARGS__)

extern de_info_t *zcrypt_dbf_info;

int zcrypt_de_init(void);
void zcrypt_de_exit(void);

#endif /* ZCRYPT_DE_H */
