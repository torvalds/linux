/*
 *    DIAGNOSE X'2C4' instruction based SE/HMC FTP Services, useable on z/VM
 *
 *    Notice that all functions exported here are not reentrant.
 *    So usage should be exclusive, ensured by the caller (e.g. using a
 *    mutex).
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 */

#ifndef __DIAG_FTP_H__
#define __DIAG_FTP_H__

#include "hmcdrv_ftp.h"

int diag_ftp_startup(void);
void diag_ftp_shutdown(void);
ssize_t diag_ftp_cmd(const struct hmcdrv_ftp_cmdspec *ftp, size_t *fsize);

#endif	 /* __DIAG_FTP_H__ */
