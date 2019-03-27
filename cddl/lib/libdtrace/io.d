/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Portions Copyright 2018 Devin Teske dteske@freebsd.org
 *
 * $FreeBSD$
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma D depends_on module kernel
#pragma D depends_on provider io

typedef struct devinfo {
	int dev_major;			/* major number */
	int dev_minor;			/* minor number */
	int dev_instance;		/* instance number */
	int dev_type;			/* type of device */
	string dev_name;		/* name of device */
	string dev_statname;		/* name of device + instance/minor */
	string dev_pathname;		/* pathname of device */
} devinfo_t;

#pragma D binding "1.0" translator
translator devinfo_t < struct devstat *D > {
	dev_major = D->device_number;
	dev_minor = D->unit_number;
	dev_instance = 0;
	dev_type = D->device_type;
	dev_name = stringof(D->device_name);
	dev_statname = stringof(D->device_name);
	dev_pathname = stringof(D->device_name);
};

typedef struct bufinfo {
	int b_cmd;			/* I/O operation */
	int b_flags;			/* flags */
	long b_bcount;			/* number of bytes */
	caddr_t b_addr;			/* buffer address */
	uint64_t b_blkno;		/* expanded block # on device */
	uint64_t b_lblkno;		/* block # on device */
	size_t b_resid;			/* # of bytes not transferred */
	size_t b_bufsize;		/* size of allocated buffer */
/*	caddr_t b_iodone;		I/O completion routine */
	int b_error;			/* expanded error field */
/*	dev_t b_edev;			extended device */
} bufinfo_t;

#pragma D binding "1.0" translator
translator bufinfo_t < struct bio *B > {
	b_cmd = B->bio_cmd;
	b_flags = B->bio_flags;
	b_bcount = B->bio_bcount;
	b_addr = B->bio_data;
	b_blkno = 0;
	b_lblkno = 0;
	b_resid = B->bio_resid;
	b_bufsize = 0; /* XXX gnn */
	b_error = B->bio_error;
};

/*
 * The following inline constants can be used to examine fi_oflags when using
 * the fds[] array or a translated fileinfo_t.  Note that the various open
 * flags behave as a bit-field *except* for O_RDONLY, O_WRONLY, and O_RDWR.
 * To test the open mode, you write code similar to that used with the fcntl(2)
 * F_GET[X]FL command, such as: if ((fi_oflags & O_ACCMODE) == O_WRONLY).
 */
inline int O_ACCMODE = 0x0003;
#pragma D binding "1.1" O_ACCMODE

inline int O_RDONLY = 0x0000;
#pragma D binding "1.1" O_RDONLY
inline int O_WRONLY = 0x0001;
#pragma D binding "1.1" O_WRONLY
inline int O_RDWR = 0x0002;
#pragma D binding "1.1" O_RDWR

inline int O_APPEND = 0x0008;
#pragma D binding "1.1" O_APPEND
inline int O_CREAT = 0x0200;
#pragma D binding "1.1" O_CREAT
inline int O_EXCL = 0x0800;
#pragma D binding "1.1" O_EXCL
inline int O_NOCTTY = 0x8000;
#pragma D binding "1.1" O_NOCTTY
inline int O_NONBLOCK = 0x0004;
#pragma D binding "1.1" O_NONBLOCK
inline int O_NDELAY = 0x0004;
#pragma D binding "1.1" O_NDELAY
inline int O_SYNC = 0x0080;
#pragma D binding "1.1" O_SYNC
inline int O_TRUNC = 0x0400;
#pragma D binding "1.1" O_TRUNC

/*
 * The following inline constants can be used to examine bio_cmd of struct bio
 * or a translated bufinfo_t.
 */
inline int BIO_READ =		0x01;
#pragma D binding "1.13" BIO_READ
inline int BIO_WRITE =		0x02;
#pragma D binding "1.13" BIO_WRITE
inline int BIO_DELETE =		0x03;
#pragma D binding "1.13" BIO_DELETE
inline int BIO_GETATTR =	0x04;
#pragma D binding "1.13" BIO_GETATTR
inline int BIO_FLUSH =		0x05;
#pragma D binding "1.13" BIO_FLUSH
inline int BIO_CMD0 =		0x06;
#pragma D binding "1.13" BIO_CMD0
inline int BIO_CMD1 =		0x07;
#pragma D binding "1.13" BIO_CMD1
inline int BIO_CMD2 =		0x08;
#pragma D binding "1.13" BIO_CMD2
inline int BIO_ZONE =		0x09;
#pragma D binding "1.13" BIO_ZONE

/*
 * The following inline constants can be used to examine bio_flags of struct
 * bio or a translated bufinfo_t.
 */
inline int BIO_ERROR =			0x01;
#pragma D binding "1.13" BIO_ERROR
inline int BIO_DONE =			0x02;
#pragma D binding "1.13" BIO_DONE
inline int BIO_ONQUEUE =		0x04;
#pragma D binding "1.13" BIO_ONQUEUE
inline int BIO_ORDERED =		0x08;
#pragma D binding "1.13" BIO_ORDERED
inline int BIO_UNMAPPED =		0x10;
#pragma D binding "1.13" BIO_UNMAPPED
inline int BIO_TRANSIENT_MAPPING =	0x20;
#pragma D binding "1.13" BIO_TRANSIENT_MAPPING
inline int BIO_VLIST =			0x40;
#pragma D binding "1.13" BIO_VLIST

/*
 * The following inline constants can be used to examine device_type of struct
 * devstat or a translated devinfo_t.
 */
inline int DEVSTAT_TYPE_DIRECT =	0x000;
#pragma D binding "1.13" DEVSTAT_TYPE_DIRECT
inline int DEVSTAT_TYPE_SEQUENTIAL =	0x001;
#pragma D binding "1.13" DEVSTAT_TYPE_SEQUENTIAL
inline int DEVSTAT_TYPE_PRINTER =	0x002;
#pragma D binding "1.13" DEVSTAT_TYPE_PRINTER
inline int DEVSTAT_TYPE_PROCESSOR =	0x003;
#pragma D binding "1.13" DEVSTAT_TYPE_PROCESSOR
inline int DEVSTAT_TYPE_WORM =		0x004;
#pragma D binding "1.13" DEVSTAT_TYPE_WORM
inline int DEVSTAT_TYPE_CDROM =		0x005;
#pragma D binding "1.13" DEVSTAT_TYPE_CDROM
inline int DEVSTAT_TYPE_SCANNER =	0x006;
#pragma D binding "1.13" DEVSTAT_TYPE_SCANNER
inline int DEVSTAT_TYPE_OPTICAL =	0x007;
#pragma D binding "1.13" DEVSTAT_TYPE_OPTICAL
inline int DEVSTAT_TYPE_CHANGER =	0x008;
#pragma D binding "1.13" DEVSTAT_TYPE_CHANGER
inline int DEVSTAT_TYPE_COMM =		0x009;
#pragma D binding "1.13" DEVSTAT_TYPE_COMM
inline int DEVSTAT_TYPE_ASC0 =		0x00a;
#pragma D binding "1.13" DEVSTAT_TYPE_ASC0
inline int DEVSTAT_TYPE_ASC1 =		0x00b;
#pragma D binding "1.13" DEVSTAT_TYPE_ASC1
inline int DEVSTAT_TYPE_STORARRAY =	0x00c;
#pragma D binding "1.13" DEVSTAT_TYPE_STORARRAY
inline int DEVSTAT_TYPE_ENCLOSURE =	0x00d;
#pragma D binding "1.13" DEVSTAT_TYPE_ENCLOSURE
inline int DEVSTAT_TYPE_FLOPPY =	0x00e;
#pragma D binding "1.13" DEVSTAT_TYPE_FLOPPY
inline int DEVSTAT_TYPE_MASK =		0x00f;
#pragma D binding "1.13" DEVSTAT_TYPE_MASK
inline int DEVSTAT_TYPE_IF_SCSI =	0x010;
#pragma D binding "1.13" DEVSTAT_TYPE_IF_SCSI
inline int DEVSTAT_TYPE_IF_IDE =	0x020;
#pragma D binding "1.13" DEVSTAT_TYPE_IF_IDE
inline int DEVSTAT_TYPE_IF_OTHER =	0x030;
#pragma D binding "1.13" DEVSTAT_TYPE_IF_OTHER
inline int DEVSTAT_TYPE_IF_MASK =	0x0f0;
#pragma D binding "1.13" DEVSTAT_TYPE_IF_MASK
inline int DEVSTAT_TYPE_PASS =		0x100;
#pragma D binding "1.13" DEVSTAT_TYPE_PASS

#pragma D binding "1.13" device_type_string
inline string device_type_string[int type] =
	type == DEVSTAT_TYPE_DIRECT ?		"DIRECT" :
	type == DEVSTAT_TYPE_SEQUENTIAL ?	"SEQUENTIAL" :
	type == DEVSTAT_TYPE_PRINTER ?		"PRINTER" :
	type == DEVSTAT_TYPE_PROCESSOR ?	"PROCESSOR" :
	type == DEVSTAT_TYPE_WORM ?		"WORM" :
	type == DEVSTAT_TYPE_CDROM ?		"CDROM" :
	type == DEVSTAT_TYPE_SCANNER ?		"SCANNER" :
	type == DEVSTAT_TYPE_OPTICAL ?		"OPTICAL" :
	type == DEVSTAT_TYPE_CHANGER ?		"CHANGER" :
	type == DEVSTAT_TYPE_COMM ?		"COMM" :
	type == DEVSTAT_TYPE_ASC0 ?		"ASC0" :
	type == DEVSTAT_TYPE_ASC1 ?		"ASC1" :
	type == DEVSTAT_TYPE_STORARRAY ?	"STORARRAY" :
	type == DEVSTAT_TYPE_ENCLOSURE ?	"ENCLOSURE" :
	type == DEVSTAT_TYPE_FLOPPY ?		"FLOPPY" :
	strjoin("UNKNOWN(", strjoin(lltostr(type), ")"));

#pragma D binding "1.13" device_type
inline string device_type[int type] =
	device_type_string[type & DEVSTAT_TYPE_MASK];

#pragma D binding "1.13" device_if_string
inline string device_if_string[int type] =
	type == 0 ?			"ACCESS" :
	type == DEVSTAT_TYPE_IF_SCSI ?	"SCSI" :
	type == DEVSTAT_TYPE_IF_IDE ?	"IDE" :
	type == DEVSTAT_TYPE_IF_OTHER ?	"OTHER" :
	strjoin("UNKNOWN(", strjoin(lltostr(type), ")"));

#pragma D binding "1.13" device_if
inline string device_if[int type] =
	device_if_string[type & DEVSTAT_TYPE_IF_MASK];

#pragma D binding "1.13" bio_cmd_string
inline string bio_cmd_string[int cmd] =
	cmd == BIO_READ ?	"READ" :
	cmd == BIO_WRITE ?	"WRITE" :
	cmd == BIO_DELETE ?	"DELETE" :
	cmd == BIO_GETATTR ?	"GETATTR" :
	cmd == BIO_FLUSH ?	"FLUSH" :
	cmd == BIO_CMD0 ?	"CMD0" :
	cmd == BIO_CMD1 ?	"CMD1" :
	cmd == BIO_CMD2 ?	"CMD2" :
	cmd == BIO_ZONE ?	"ZONE" :
	strjoin("UNKNOWN(", strjoin(lltostr(cmd), ")"));

#pragma D binding "1.13" bio_flag_string
inline string bio_flag_string[int flag] =
	flag == BIO_ERROR ?		"ERROR" :
	flag == BIO_DONE ?		"DONE" :
	flag == BIO_ONQUEUE ?		"ONQUEUE" :
	flag == BIO_ORDERED ?		"ORDERED" :
	flag == BIO_UNMAPPED ?		"UNMAPPED" :
	flag == BIO_TRANSIENT_MAPPING ?	"TRANSIENT_MAPPING" :
	flag == BIO_VLIST ?		"VLIST" :
	"";
