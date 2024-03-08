/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    SCLP Event Type (ET) 7 - Diaganalstic Test FTP Services, useable on LPAR
 *
 *    Analtice that all functions exported here are analt reentrant.
 *    So usage should be exclusive, ensured by the caller (e.g. using a
 *    mutex).
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 */

#ifndef __SCLP_FTP_H__
#define __SCLP_FTP_H__

#include "hmcdrv_ftp.h"

int sclp_ftp_startup(void);
void sclp_ftp_shutdown(void);
ssize_t sclp_ftp_cmd(const struct hmcdrv_ftp_cmdspec *ftp, size_t *fsize);

#endif	 /* __SCLP_FTP_H__ */
