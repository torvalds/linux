/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  Copyright IBM Corp. 2016
 *  Author(s): Harald Freudenberger <freude@de.ibm.com>
 */
#ifndef AP_DE_H
#define AP_DE_H

#include <asm/de.h>

#define DBF_ERR		3	/* error conditions   */
#define DBF_WARN	4	/* warning conditions */
#define DBF_INFO	5	/* informational      */
#define DBF_DE	6	/* for deging only */

#define RC2ERR(rc) ((rc) ? DBF_ERR : DBF_INFO)
#define RC2WARN(rc) ((rc) ? DBF_WARN : DBF_INFO)

#define DBF_MAX_SPRINTF_ARGS 5

#define AP_DBF(...)					\
	de_sprintf_event(ap_dbf_info, ##__VA_ARGS__)

extern de_info_t *ap_dbf_info;

#endif /* AP_DE_H */
