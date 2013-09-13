/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/lustre/libiam.h
 *
 * iam user level library
 *
 * Author: Wang Di <wangdi@clusterfs.com>
 * Author: Nikita Danilov <nikita@clusterfs.com>
 * Author: Fan Yong <fanyong@clusterfs.com>
 */

/*
 *  lustre/libiam.h
 */

#ifndef __IAM_ULIB_H__
#define __IAM_ULIB_H__

/** \defgroup libiam libiam
 *
 * @{
 */


#define DX_FMT_NAME_LEN 16

enum iam_fmt_t {
	FMT_LFIX,
	FMT_LVAR
};

struct iam_uapi_info {
	__u16 iui_keysize;
	__u16 iui_recsize;
	__u16 iui_ptrsize;
	__u16 iui_height;
	char  iui_fmt_name[DX_FMT_NAME_LEN];
};

/*
 * Creat an iam file, but do NOT open it.
 * Return 0 if success, else -1.
 */
int iam_creat(char *filename, enum iam_fmt_t fmt,
	      int blocksize, int keysize, int recsize, int ptrsize);

/*
 * Open an iam file, but do NOT creat it if the file doesn't exist.
 * Please use iam_creat for creating the file before use iam_open.
 * Return file id (fd) if success, else -1.
 */
int iam_open(char *filename, struct iam_uapi_info *ua);

/*
 * Close file opened by iam_open.
 */
int iam_close(int fd);

/*
 * Please use iam_open before use this function.
 */
int iam_insert(int fd, struct iam_uapi_info *ua,
	       int key_need_convert, char *keybuf,
	       int rec_need_convert, char *recbuf);

/*
 * Please use iam_open before use this function.
 */
int iam_lookup(int fd, struct iam_uapi_info *ua,
	       int key_need_convert, char *key_buf,
	       int *keysize, char *save_key,
	       int rec_need_convert, char *rec_buf,
	       int *recsize, char *save_rec);

/*
 * Please use iam_open before use this function.
 */
int iam_delete(int fd, struct iam_uapi_info *ua,
	       int key_need_convert, char *keybuf,
	       int rec_need_convert, char *recbuf);

/*
 * Please use iam_open before use this function.
 */
int iam_it_start(int fd, struct iam_uapi_info *ua,
		 int key_need_convert, char *key_buf,
		 int *keysize, char *save_key,
		 int rec_need_convert, char *rec_buf,
		 int *recsize, char *save_rec);

/*
 * Please use iam_open before use this function.
 */
int iam_it_next(int fd, struct iam_uapi_info *ua,
		int key_need_convert, char *key_buf,
		int *keysize, char *save_key,
		int rec_need_convert, char *rec_buf,
		int *recsize, char *save_rec);

/*
 * Please use iam_open before use this function.
 */
int iam_it_stop(int fd, struct iam_uapi_info *ua,
		int key_need_convert, char *keybuf,
		int rec_need_convert, char *recbuf);

/*
 * Change iam file mode.
 */
int iam_polymorph(char *filename, unsigned long mode);

/** @} libiam */

#endif
