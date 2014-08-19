/* bnx2fc_debug.h: QLogic NetXtreme II Linux FCoE offload driver.
 * Handles operations such as session offload/upload etc, and manages
 * session resources such as connection id and qp resources.
 *
 * Copyright (c) 2008 - 2013 Broadcom Corporation
 * Copyright (c) 2014, QLogic Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 */

#ifndef __BNX2FC_DEBUG__
#define __BNX2FC_DEBUG__

/* Log level bit mask */
#define LOG_IO		0x01	/* scsi cmd error, cleanup */
#define LOG_TGT		0x02	/* Session setup, cleanup, etc' */
#define LOG_HBA		0x04	/* lport events, link, mtu, etc' */
#define LOG_ELS		0x08	/* ELS logs */
#define LOG_MISC	0x10	/* fcoe L2 frame related logs*/
#define LOG_ALL		0xff	/* LOG all messages */

extern unsigned int bnx2fc_debug_level;

#define BNX2FC_ELS_DBG(fmt, ...)				\
do {								\
	if (unlikely(bnx2fc_debug_level & LOG_ELS))		\
		pr_info(fmt, ##__VA_ARGS__);			\
} while (0)

#define BNX2FC_MISC_DBG(fmt, ...)				\
do {								\
	if (unlikely(bnx2fc_debug_level & LOG_MISC))		\
		pr_info(fmt, ##__VA_ARGS__);			\
} while (0)

__printf(2, 3)
void BNX2FC_IO_DBG(const struct bnx2fc_cmd *io_req, const char *fmt, ...);
__printf(2, 3)
void BNX2FC_TGT_DBG(const struct bnx2fc_rport *tgt, const char *fmt, ...);
__printf(2, 3)
void BNX2FC_HBA_DBG(const struct fc_lport *lport, const char *fmt, ...);

#endif
