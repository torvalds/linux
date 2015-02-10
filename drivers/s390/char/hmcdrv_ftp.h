/*
 *    SE/HMC Drive FTP Services
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 */

#ifndef __HMCDRV_FTP_H__
#define __HMCDRV_FTP_H__

#include <linux/types.h> /* size_t, loff_t */

/*
 * HMC drive FTP Service max. length of path (w/ EOS)
 */
#define HMCDRV_FTP_FIDENT_MAX 192

/**
 * enum hmcdrv_ftp_cmdid - HMC drive FTP commands
 * @HMCDRV_FTP_NOOP: do nothing (only for probing)
 * @HMCDRV_FTP_GET: read a file
 * @HMCDRV_FTP_PUT: (over-) write a file
 * @HMCDRV_FTP_APPEND: append to a file
 * @HMCDRV_FTP_DIR: list directory long (ls -l)
 * @HMCDRV_FTP_NLIST: list files, no directories (name list)
 * @HMCDRV_FTP_DELETE: delete a file
 * @HMCDRV_FTP_CANCEL: cancel operation (SCLP/LPAR only)
 */
enum hmcdrv_ftp_cmdid {
	HMCDRV_FTP_NOOP = 0,
	HMCDRV_FTP_GET = 1,
	HMCDRV_FTP_PUT = 2,
	HMCDRV_FTP_APPEND = 3,
	HMCDRV_FTP_DIR = 4,
	HMCDRV_FTP_NLIST = 5,
	HMCDRV_FTP_DELETE = 6,
	HMCDRV_FTP_CANCEL = 7
};

/**
 * struct hmcdrv_ftp_cmdspec - FTP command specification
 * @id: FTP command ID
 * @ofs: offset in file
 * @fname: filename (ASCII), null-terminated
 * @buf: kernel-space transfer data buffer, 4k aligned
 * @len: (max) number of bytes to transfer from/to @buf
 */
struct hmcdrv_ftp_cmdspec {
	enum hmcdrv_ftp_cmdid id;
	loff_t ofs;
	const char *fname;
	void __kernel *buf;
	size_t len;
};

int hmcdrv_ftp_startup(void);
void hmcdrv_ftp_shutdown(void);
int hmcdrv_ftp_probe(void);
ssize_t hmcdrv_ftp_do(const struct hmcdrv_ftp_cmdspec *ftp);
ssize_t hmcdrv_ftp_cmd(char __kernel *cmd, loff_t offset,
		       char __user *buf, size_t len);

#endif	 /* __HMCDRV_FTP_H__ */
