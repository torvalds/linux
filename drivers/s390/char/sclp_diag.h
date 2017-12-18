/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 */

#ifndef _SCLP_DIAG_H
#define _SCLP_DIAG_H

#include <linux/types.h>

/* return codes for Diagnostic Test FTP Service, as indicated in member
 * sclp_diag_ftp::ldflg
 */
#define SCLP_DIAG_FTP_OK	0x80U /* success */
#define SCLP_DIAG_FTP_LDFAIL	0x01U /* load failed */
#define SCLP_DIAG_FTP_LDNPERM	0x02U /* not allowed */
#define SCLP_DIAG_FTP_LDRUNS	0x03U /* LD runs */
#define SCLP_DIAG_FTP_LDNRUNS	0x04U /* LD does not run */

#define SCLP_DIAG_FTP_XPCX	0x80 /* PCX communication code */
#define SCLP_DIAG_FTP_ROUTE	4 /* routing code for new FTP service */

/*
 * length of Diagnostic Test FTP Service event buffer
 */
#define SCLP_DIAG_FTP_EVBUF_LEN				\
	(offsetof(struct sclp_diag_evbuf, mdd) +	\
	 sizeof(struct sclp_diag_ftp))

/**
 * struct sclp_diag_ftp - Diagnostic Test FTP Service model-dependent data
 * @pcx: code for PCX communication (should be 0x80)
 * @ldflg: load flag (see defines above)
 * @cmd: FTP command
 * @pgsize: page size (0 = 4kB, 1 = large page size)
 * @srcflg: source flag
 * @spare: reserved (zeroes)
 * @offset: file offset
 * @fsize: file size
 * @length: buffer size resp. bytes transferred
 * @failaddr: failing address
 * @bufaddr: buffer address, virtual
 * @asce: region or segment table designation
 * @fident: file name (ASCII, zero-terminated)
 */
struct sclp_diag_ftp {
	u8 pcx;
	u8 ldflg;
	u8 cmd;
	u8 pgsize;
	u8 srcflg;
	u8 spare;
	u64 offset;
	u64 fsize;
	u64 length;
	u64 failaddr;
	u64 bufaddr;
	u64 asce;

	u8 fident[256];
} __packed;

/**
 * struct sclp_diag_evbuf - Diagnostic Test (ET7) Event Buffer
 * @hdr: event buffer header
 * @route: diagnostic route
 * @mdd: model-dependent data (@route dependent)
 */
struct sclp_diag_evbuf {
	struct evbuf_header hdr;
	u16 route;

	union {
		struct sclp_diag_ftp ftp;
	} mdd;
} __packed;

/**
 * struct sclp_diag_sccb - Diagnostic Test (ET7) SCCB
 * @hdr: SCCB header
 * @evbuf: event buffer
 */
struct sclp_diag_sccb {

	struct sccb_header hdr;
	struct sclp_diag_evbuf evbuf;
} __packed;

#endif /* _SCLP_DIAG_H */
