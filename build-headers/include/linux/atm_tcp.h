/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* atm_tcp.h - Driver-specific declarations of the ATMTCP driver (for use by
	       driver-specific utilities) */

/* Written 1997-2000 by Werner Almesberger, EPFL LRC/ICA */


#ifndef LINUX_ATM_TCP_H
#define LINUX_ATM_TCP_H

#include <linux/atmapi.h>
#include <linux/atm.h>
#include <linux/atmioc.h>
#include <linux/types.h>


/*
 * All values in struct atmtcp_hdr are in network byte order
 */

struct atmtcp_hdr {
	__u16	vpi;
	__u16	vci;
	__u32	length;		/* ... of data part */
};

/*
 * All values in struct atmtcp_command are in host byte order
 */

#define ATMTCP_HDR_MAGIC	(~0)	/* this length indicates a command */
#define ATMTCP_CTRL_OPEN	1	/* request/reply */
#define ATMTCP_CTRL_CLOSE	2	/* request/reply */

struct atmtcp_control {
	struct atmtcp_hdr hdr;	/* must be first */
	int type;		/* message type; both directions */
	atm_kptr_t vcc;		/* both directions */
	struct sockaddr_atmpvc addr; /* suggested value from kernel */
	struct atm_qos	qos;	/* both directions */
	int result;		/* to kernel only */
} __ATM_API_ALIGN;

/*
 * Field usage:
 * Messge type	dir.	hdr.v?i	type	addr	qos	vcc	result
 * -----------  ----	------- ----	----	---	---	------
 * OPEN		K->D	Y	Y	Y	Y	Y	0
 * OPEN		D->K	-	Y	Y	Y	Y	Y
 * CLOSE	K->D	-	-	Y	-	Y	0
 * CLOSE	D->K	-	-	-	-	Y	Y
 */

#define SIOCSIFATMTCP	_IO('a',ATMIOC_ITF)	/* set ATMTCP mode */
#define ATMTCP_CREATE	_IO('a',ATMIOC_ITF+14)	/* create persistent ATMTCP
						   interface */
#define ATMTCP_REMOVE	_IO('a',ATMIOC_ITF+15)	/* destroy persistent ATMTCP
						   interface */



#endif /* LINUX_ATM_TCP_H */
