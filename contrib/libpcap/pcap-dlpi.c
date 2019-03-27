/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that: (1) source code distributions
 * retain the above copyright notice and this paragraph in its entirety, (2)
 * distributions including binary code include the above copyright notice and
 * this paragraph in its entirety in the documentation or other materials
 * provided with the distribution, and (3) all advertising materials mentioning
 * features or use of this software display the following acknowledgement:
 * ``This product includes software developed by the University of California,
 * Lawrence Berkeley Laboratory and its contributors.'' Neither the name of
 * the University nor the names of its contributors may be used to endorse
 * or promote products derived from this software without specific prior
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * This code contributed by Atanu Ghosh (atanu@cs.ucl.ac.uk),
 * University College London, and subsequently modified by
 * Guy Harris (guy@alum.mit.edu), Mark Pizzolato
 * <List-tcpdump-workers@subscriptions.pizzolato.net>,
 * Mark C. Brown (mbrown@hp.com), and Sagun Shakya <Sagun.Shakya@Sun.COM>.
 */

/*
 * Packet capture routine for DLPI under SunOS 5, HP-UX 9/10/11, and AIX.
 *
 * Notes:
 *
 *    - The DLIOCRAW ioctl() is specific to SunOS.
 *
 *    - There is a bug in bufmod(7) such that setting the snapshot
 *      length results in data being left of the front of the packet.
 *
 *    - It might be desirable to use pfmod(7) to filter packets in the
 *      kernel when possible.
 *
 *    - An older version of the HP-UX DLPI Programmer's Guide, which
 *      I think was advertised as the 10.20 version, used to be available
 *      at
 *
 *            http://docs.hp.com/hpux/onlinedocs/B2355-90093/B2355-90093.html
 *
 *      but is no longer available; it can still be found at
 *
 *            http://h21007.www2.hp.com/dspp/files/unprotected/Drivers/Docs/Refs/B2355-90093.pdf
 *
 *      in PDF form.
 *
 *    - The HP-UX 10.x, 11.0, and 11i v1.6 version of the HP-UX DLPI
 *      Programmer's Guide, which I think was once advertised as the
 *      11.00 version is available at
 *
 *            http://docs.hp.com/en/B2355-90139/index.html
 *
 *    - The HP-UX 11i v2 version of the HP-UX DLPI Programmer's Guide
 *      is available at
 *
 *            http://docs.hp.com/en/B2355-90871/index.html
 *
 *    - All of the HP documents describe raw-mode services, which are
 *      what we use if DL_HP_RAWDLS is defined.  XXX - we use __hpux
 *      in some places to test for HP-UX, but use DL_HP_RAWDLS in
 *      other places; do we support any versions of HP-UX without
 *      DL_HP_RAWDLS?
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <sys/time.h>
#ifdef HAVE_SYS_BUFMOD_H
#include <sys/bufmod.h>
#endif
#include <sys/dlpi.h>
#ifdef HAVE_SYS_DLPI_EXT_H
#include <sys/dlpi_ext.h>
#endif
#ifdef HAVE_HPUX9
#include <sys/socket.h>
#endif
#ifdef DL_HP_PPA_REQ
#include <sys/stat.h>
#endif
#include <sys/stream.h>
#if defined(HAVE_SOLARIS) && defined(HAVE_SYS_BUFMOD_H)
#include <sys/systeminfo.h>
#endif

#ifdef HAVE_HPUX9
#include <net/if.h>
#endif

#include <ctype.h>
#ifdef HAVE_HPUX9
#include <nlist.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stropts.h>
#include <unistd.h>

#ifdef HAVE_LIMITS_H
#include <limits.h>
#else
#define INT_MAX		2147483647
#endif

#include "pcap-int.h"
#include "dlpisubs.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#if defined(__hpux)
  /*
   * HP-UX has a /dev/dlpi device; you open it and set the PPA of the actual
   * network device you want.
   */
  #define HAVE_DEV_DLPI
#elif defined(_AIX)
  /*
   * AIX has a /dev/dlpi directory, with devices named after the interfaces
   * underneath it.
   */
  #define PCAP_DEV_PREFIX "/dev/dlpi"
#elif defined(HAVE_SOLARIS)
  /*
   * Solaris has devices named after the interfaces underneath /dev.
   */
  #define PCAP_DEV_PREFIX "/dev"
#endif

#define	MAXDLBUF	8192

/* Forwards */
static char *split_dname(char *, u_int *, char *);
static int dl_doattach(int, int, char *);
#ifdef DL_HP_RAWDLS
static int dl_dohpuxbind(int, char *);
#endif
static int dlpromiscon(pcap_t *, bpf_u_int32);
static int dlbindreq(int, bpf_u_int32, char *);
static int dlbindack(int, char *, char *, int *);
static int dlokack(int, const char *, char *, char *);
static int dlinforeq(int, char *);
static int dlinfoack(int, char *, char *);

#ifdef HAVE_DL_PASSIVE_REQ_T
static void dlpassive(int, char *);
#endif

#ifdef DL_HP_RAWDLS
static int dlrawdatareq(int, const u_char *, int);
#endif
static int recv_ack(int, int, const char *, char *, char *, int *);
static char *dlstrerror(char *, size_t, bpf_u_int32);
static char *dlprim(char *, size_t, bpf_u_int32);
#if defined(HAVE_SOLARIS) && defined(HAVE_SYS_BUFMOD_H)
#define GET_RELEASE_BUFSIZE	32
static void get_release(char *, size_t, bpf_u_int32 *, bpf_u_int32 *,
    bpf_u_int32 *);
#endif
static int send_request(int, char *, int, char *, char *);
#ifdef HAVE_HPUX9
static int dlpi_kread(int, off_t, void *, u_int, char *);
#endif
#ifdef HAVE_DEV_DLPI
static int get_dlpi_ppa(int, const char *, u_int, u_int *, char *);
#endif

/*
 * Cast a buffer to "union DL_primitives" without provoking warnings
 * from the compiler.
 */
#define MAKE_DL_PRIMITIVES(ptr)	((union DL_primitives *)(void *)(ptr))

static int
pcap_read_dlpi(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
	int cc;
	u_char *bp;
	int flags;
	bpf_u_int32 ctlbuf[MAXDLBUF];
	struct strbuf ctl = {
		MAXDLBUF,
		0,
		(char *)ctlbuf
	};
	struct strbuf data;

	flags = 0;
	cc = p->cc;
	if (cc == 0) {
		data.buf = (char *)p->buffer + p->offset;
		data.maxlen = p->bufsize;
		data.len = 0;
		do {
			/*
			 * Has "pcap_breakloop()" been called?
			 */
			if (p->break_loop) {
				/*
				 * Yes - clear the flag that indicates
				 * that it has, and return -2 to
				 * indicate that we were told to
				 * break out of the loop.
				 */
				p->break_loop = 0;
				return (-2);
			}
			/*
			 * XXX - check for the DLPI primitive, which
			 * would be DL_HP_RAWDATA_IND on HP-UX
			 * if we're in raw mode?
			 */
			ctl.buf = (char *)ctlbuf;
			ctl.maxlen = MAXDLBUF;
			ctl.len = 0;
			if (getmsg(p->fd, &ctl, &data, &flags) < 0) {
				/* Don't choke when we get ptraced */
				switch (errno) {

				case EINTR:
					cc = 0;
					continue;

				case EAGAIN:
					return (0);
				}
				pcap_fmt_errmsg_for_errno(p->errbuf,
				    sizeof(p->errbuf), errno, "getmsg");
				return (-1);
			}
			cc = data.len;
		} while (cc == 0);
		bp = (u_char *)p->buffer + p->offset;
	} else
		bp = p->bp;

	return (pcap_process_pkts(p, callback, user, cnt, bp, cc));
}

static int
pcap_inject_dlpi(pcap_t *p, const void *buf, size_t size)
{
#ifdef DL_HP_RAWDLS
	struct pcap_dlpi *pd = p->priv;
#endif
	int ret;

#if defined(DLIOCRAW)
	ret = write(p->fd, buf, size);
	if (ret == -1) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "send");
		return (-1);
	}
#elif defined(DL_HP_RAWDLS)
	if (pd->send_fd < 0) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "send: Output FD couldn't be opened");
		return (-1);
	}
	ret = dlrawdatareq(pd->send_fd, buf, size);
	if (ret == -1) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "send");
		return (-1);
	}
	/*
	 * putmsg() returns either 0 or -1; it doesn't indicate how
	 * many bytes were written (presumably they were all written
	 * or none of them were written).  OpenBSD's pcap_inject()
	 * returns the number of bytes written, so, for API compatibility,
	 * we return the number of bytes we were told to write.
	 */
	ret = size;
#else /* no raw mode */
	/*
	 * XXX - this is a pain, because you might have to extract
	 * the address from the packet and use it in a DL_UNITDATA_REQ
	 * request.  That would be dependent on the link-layer type.
	 *
	 * I also don't know what SAP you'd have to bind the descriptor
	 * to, or whether you'd need separate "receive" and "send" FDs,
	 * nor do I know whether you'd need different bindings for
	 * D/I/X Ethernet and 802.3, or for {FDDI,Token Ring} plus
	 * 802.2 and {FDDI,Token Ring} plus 802.2 plus SNAP.
	 *
	 * So, for now, we just return a "you can't send" indication,
	 * and leave it up to somebody with a DLPI-based system lacking
	 * both DLIOCRAW and DL_HP_RAWDLS to supply code to implement
	 * packet transmission on that system.  If they do, they should
	 * send it to us - but should not send us code that assumes
	 * Ethernet; if the code doesn't work on non-Ethernet interfaces,
	 * it should check "p->linktype" and reject the send request if
	 * it's anything other than DLT_EN10MB.
	 */
	strlcpy(p->errbuf, "send: Not supported on this version of this OS",
	    PCAP_ERRBUF_SIZE);
	ret = -1;
#endif /* raw mode */
	return (ret);
}

#ifndef DL_IPATM
#define DL_IPATM	0x12	/* ATM Classical IP interface */
#endif

#ifdef HAVE_SOLARIS
/*
 * For SunATM.
 */
#ifndef A_GET_UNITS
#define A_GET_UNITS	(('A'<<8)|118)
#endif /* A_GET_UNITS */
#ifndef A_PROMISCON_REQ
#define A_PROMISCON_REQ	(('A'<<8)|121)
#endif /* A_PROMISCON_REQ */
#endif /* HAVE_SOLARIS */

static void
pcap_cleanup_dlpi(pcap_t *p)
{
#ifdef DL_HP_RAWDLS
	struct pcap_dlpi *pd = p->priv;

	if (pd->send_fd >= 0) {
		close(pd->send_fd);
		pd->send_fd = -1;
	}
#endif
	pcap_cleanup_live_common(p);
}

static int
open_dlpi_device(const char *name, u_int *ppa, char *errbuf)
{
	int status;
	char dname[100];
	char *cp;
	int fd;
#ifdef HAVE_DEV_DLPI
	u_int unit;
#else
	char dname2[100];
#endif

#ifdef HAVE_DEV_DLPI
	/*
	** Remove any "/dev/" on the front of the device.
	*/
	cp = strrchr(name, '/');
	if (cp == NULL)
		strlcpy(dname, name, sizeof(dname));
	else
		strlcpy(dname, cp + 1, sizeof(dname));

	/*
	 * Split the device name into a device type name and a unit number;
	 * chop off the unit number, so "dname" is just a device type name.
	 */
	cp = split_dname(dname, &unit, errbuf);
	if (cp == NULL)
		return (PCAP_ERROR_NO_SUCH_DEVICE);
	*cp = '\0';

	/*
	 * Use "/dev/dlpi" as the device.
	 *
	 * XXX - HP's DLPI Programmer's Guide for HP-UX 11.00 says that
	 * the "dl_mjr_num" field is for the "major number of interface
	 * driver"; that's the major of "/dev/dlpi" on the system on
	 * which I tried this, but there may be DLPI devices that
	 * use a different driver, in which case we may need to
	 * search "/dev" for the appropriate device with that major
	 * device number, rather than hardwiring "/dev/dlpi".
	 */
	cp = "/dev/dlpi";
	if ((fd = open(cp, O_RDWR)) < 0) {
		if (errno == EPERM || errno == EACCES)
			status = PCAP_ERROR_PERM_DENIED;
		else
			status = PCAP_ERROR;
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "%s", cp);
		return (status);
	}

	/*
	 * Get a table of all PPAs for that device, and search that
	 * table for the specified device type name and unit number.
	 */
	status = get_dlpi_ppa(fd, dname, unit, ppa, errbuf);
	if (status < 0) {
		close(fd);
		return (status);
	}
#else
	/*
	 * If the device name begins with "/", assume it begins with
	 * the pathname of the directory containing the device to open;
	 * otherwise, concatenate the device directory name and the
	 * device name.
	 */
	if (*name == '/')
		strlcpy(dname, name, sizeof(dname));
	else
		pcap_snprintf(dname, sizeof(dname), "%s/%s", PCAP_DEV_PREFIX,
		    name);

	/*
	 * Get the unit number, and a pointer to the end of the device
	 * type name.
	 */
	cp = split_dname(dname, ppa, errbuf);
	if (cp == NULL)
		return (PCAP_ERROR_NO_SUCH_DEVICE);

	/*
	 * Make a copy of the device pathname, and then remove the unit
	 * number from the device pathname.
	 */
	strlcpy(dname2, dname, sizeof(dname));
	*cp = '\0';

	/* Try device without unit number */
	if ((fd = open(dname, O_RDWR)) < 0) {
		if (errno != ENOENT) {
			if (errno == EPERM || errno == EACCES)
				status = PCAP_ERROR_PERM_DENIED;
			else
				status = PCAP_ERROR;
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    errno, "%s", dname);
			return (status);
		}

		/* Try again with unit number */
		if ((fd = open(dname2, O_RDWR)) < 0) {
			if (errno == ENOENT) {
				status = PCAP_ERROR_NO_SUCH_DEVICE;

				/*
				 * We provide an error message even
				 * for this error, for diagnostic
				 * purposes (so that, for example,
				 * the app can show the message if the
				 * user requests it).
				 *
				 * In it, we just report "No DLPI device
				 * found" with the device name, so people
				 * don't get confused and think, for example,
				 * that if they can't capture on "lo0"
				 * on Solaris prior to Solaris 11 the fix
				 * is to change libpcap (or the application
				 * that uses it) to look for something other
				 * than "/dev/lo0", as the fix is to use
				 * Solaris 11 or some operating system
				 * other than Solaris - you just *can't*
				 * capture on a loopback interface
				 * on Solaris prior to Solaris 11, the lack
				 * of a DLPI device for the loopback
				 * interface is just a symptom of that
				 * inability.
				 */
				pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
				    "%s: No DLPI device found", name);
			} else {
				if (errno == EPERM || errno == EACCES)
					status = PCAP_ERROR_PERM_DENIED;
				else
					status = PCAP_ERROR;
				pcap_fmt_errmsg_for_errno(errbuf,
				    PCAP_ERRBUF_SIZE, errno, "%s", dname2);
			}
			return (status);
		}
		/* XXX Assume unit zero */
		*ppa = 0;
	}
#endif
	return (fd);
}

static int
pcap_activate_dlpi(pcap_t *p)
{
#ifdef DL_HP_RAWDLS
	struct pcap_dlpi *pd = p->priv;
#endif
	int status = 0;
	int retv;
	u_int ppa;
#ifdef HAVE_SOLARIS
	int isatm = 0;
#endif
	register dl_info_ack_t *infop;
#ifdef HAVE_SYS_BUFMOD_H
	bpf_u_int32 ss;
#ifdef HAVE_SOLARIS
	char release[GET_RELEASE_BUFSIZE];
	bpf_u_int32 osmajor, osminor, osmicro;
#endif
#endif
	bpf_u_int32 buf[MAXDLBUF];

	p->fd = open_dlpi_device(p->opt.device, &ppa, p->errbuf);
	if (p->fd < 0) {
		status = p->fd;
		goto bad;
	}

#ifdef DL_HP_RAWDLS
	/*
	 * XXX - HP-UX 10.20 and 11.xx don't appear to support sending and
	 * receiving packets on the same descriptor - you need separate
	 * descriptors for sending and receiving, bound to different SAPs.
	 *
	 * If the open fails, we just leave -1 in "pd->send_fd" and reject
	 * attempts to send packets, just as if, in pcap-bpf.c, we fail
	 * to open the BPF device for reading and writing, we just try
	 * to open it for reading only and, if that succeeds, just let
	 * the send attempts fail.
	 */
	pd->send_fd = open("/dev/dlpi", O_RDWR);
#endif

	/*
	** Attach if "style 2" provider
	*/
	if (dlinforeq(p->fd, p->errbuf) < 0 ||
	    dlinfoack(p->fd, (char *)buf, p->errbuf) < 0) {
		status = PCAP_ERROR;
		goto bad;
	}
	infop = &(MAKE_DL_PRIMITIVES(buf))->info_ack;
#ifdef HAVE_SOLARIS
	if (infop->dl_mac_type == DL_IPATM)
		isatm = 1;
#endif
	if (infop->dl_provider_style == DL_STYLE2) {
		retv = dl_doattach(p->fd, ppa, p->errbuf);
		if (retv < 0) {
			status = retv;
			goto bad;
		}
#ifdef DL_HP_RAWDLS
		if (pd->send_fd >= 0) {
			retv = dl_doattach(pd->send_fd, ppa, p->errbuf);
			if (retv < 0) {
				status = retv;
				goto bad;
			}
		}
#endif
	}

	if (p->opt.rfmon) {
		/*
		 * This device exists, but we don't support monitor mode
		 * any platforms that support DLPI.
		 */
		status = PCAP_ERROR_RFMON_NOTSUP;
		goto bad;
	}

#ifdef HAVE_DL_PASSIVE_REQ_T
	/*
	 * Enable Passive mode to be able to capture on aggregated link.
	 * Not supported in all Solaris versions.
	 */
	dlpassive(p->fd, p->errbuf);
#endif
	/*
	** Bind (defer if using HP-UX 9 or HP-UX 10.20 or later, totally
	** skip if using SINIX)
	*/
#if !defined(HAVE_HPUX9) && !defined(HAVE_HPUX10_20_OR_LATER) && !defined(sinix)
#ifdef _AIX
	/*
	** AIX.
	** According to IBM's AIX Support Line, the dl_sap value
	** should not be less than 0x600 (1536) for standard Ethernet.
	** However, we seem to get DL_BADADDR - "DLSAP addr in improper
	** format or invalid" - errors if we use 1537 on the "tr0"
	** device, which, given that its name starts with "tr" and that
	** it's IBM, probably means a Token Ring device.  (Perhaps we
	** need to use 1537 on "/dev/dlpi/en" because that device is for
	** D/I/X Ethernet, the "SAP" is actually an Ethernet type, and
	** it rejects invalid Ethernet types.)
	**
	** So if 1537 fails, we try 2, as Hyung Sik Yoon of IBM Korea
	** says that works on Token Ring (he says that 0 does *not*
	** work; perhaps that's considered an invalid LLC SAP value - I
	** assume the SAP value in a DLPI bind is an LLC SAP for network
	** types that use 802.2 LLC).
	*/
	if ((dlbindreq(p->fd, 1537, p->errbuf) < 0 &&
	     dlbindreq(p->fd, 2, p->errbuf) < 0) ||
	     dlbindack(p->fd, (char *)buf, p->errbuf, NULL) < 0) {
		status = PCAP_ERROR;
		goto bad;
	}
#elif defined(DL_HP_RAWDLS)
	/*
	** HP-UX 10.0x and 10.1x.
	*/
	if (dl_dohpuxbind(p->fd, p->errbuf) < 0) {
		status = PCAP_ERROR;
		goto bad;
	}
	if (pd->send_fd >= 0) {
		/*
		** XXX - if this fails, just close send_fd and
		** set it to -1, so that you can't send but can
		** still receive?
		*/
		if (dl_dohpuxbind(pd->send_fd, p->errbuf) < 0) {
			status = PCAP_ERROR;
			goto bad;
		}
	}
#else /* neither AIX nor HP-UX */
	/*
	** Not Sinix, and neither AIX nor HP-UX - Solaris, and any other
	** OS using DLPI.
	**/
	if (dlbindreq(p->fd, 0, p->errbuf) < 0 ||
	    dlbindack(p->fd, (char *)buf, p->errbuf, NULL) < 0) {
	    	status = PCAP_ERROR;
		goto bad;
	}
#endif /* AIX vs. HP-UX vs. other */
#endif /* !HP-UX 9 and !HP-UX 10.20 or later and !SINIX */

	/*
	 * Turn a negative snapshot value (invalid), a snapshot value of
	 * 0 (unspecified), or a value bigger than the normal maximum
	 * value, into the maximum allowed value.
	 *
	 * If some application really *needs* a bigger snapshot
	 * length, we should just increase MAXIMUM_SNAPLEN.
	 */
	if (p->snapshot <= 0 || p->snapshot > MAXIMUM_SNAPLEN)
		p->snapshot = MAXIMUM_SNAPLEN;

#ifdef HAVE_SOLARIS
	if (isatm) {
		/*
		** Have to turn on some special ATM promiscuous mode
		** for SunATM.
		** Do *NOT* turn regular promiscuous mode on; it doesn't
		** help, and may break things.
		*/
		if (strioctl(p->fd, A_PROMISCON_REQ, 0, NULL) < 0) {
			status = PCAP_ERROR;
			pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
			    errno, "A_PROMISCON_REQ");
			goto bad;
		}
	} else
#endif
	if (p->opt.promisc) {
		/*
		** Enable promiscuous (not necessary on send FD)
		*/
		retv = dlpromiscon(p, DL_PROMISC_PHYS);
		if (retv < 0) {
			if (retv == PCAP_ERROR_PERM_DENIED)
				status = PCAP_ERROR_PROMISC_PERM_DENIED;
			else
				status = retv;
			goto bad;
		}

		/*
		** Try to enable multicast (you would have thought
		** promiscuous would be sufficient). (Skip if using
		** HP-UX or SINIX) (Not necessary on send FD)
		*/
#if !defined(__hpux) && !defined(sinix)
		retv = dlpromiscon(p, DL_PROMISC_MULTI);
		if (retv < 0)
			status = PCAP_WARNING;
#endif
	}
	/*
	** Try to enable SAP promiscuity (when not in promiscuous mode
	** when using HP-UX, when not doing SunATM on Solaris, and never
	** under SINIX) (Not necessary on send FD)
	*/
#ifndef sinix
#if defined(__hpux)
	/* HP-UX - only do this when not in promiscuous mode */
	if (!p->opt.promisc) {
#elif defined(HAVE_SOLARIS)
	/* Solaris - don't do this on SunATM devices */
	if (!isatm) {
#else
	/* Everything else (except for SINIX) - always do this */
	{
#endif
		retv = dlpromiscon(p, DL_PROMISC_SAP);
		if (retv < 0) {
			if (p->opt.promisc) {
				/*
				 * Not fatal, since the DL_PROMISC_PHYS mode
				 * worked.
				 *
				 * Report it as a warning, however.
				 */
				status = PCAP_WARNING;
			} else {
				/*
				 * Fatal.
				 */
				status = retv;
				goto bad;
			}
		}
	}
#endif /* sinix */

	/*
	** HP-UX 9, and HP-UX 10.20 or later, must bind after setting
	** promiscuous options.
	*/
#if defined(HAVE_HPUX9) || defined(HAVE_HPUX10_20_OR_LATER)
	if (dl_dohpuxbind(p->fd, p->errbuf) < 0) {
		status = PCAP_ERROR;
		goto bad;
	}
	/*
	** We don't set promiscuous mode on the send FD, but we'll defer
	** binding it anyway, just to keep the HP-UX 9/10.20 or later
	** code together.
	*/
	if (pd->send_fd >= 0) {
		/*
		** XXX - if this fails, just close send_fd and
		** set it to -1, so that you can't send but can
		** still receive?
		*/
		if (dl_dohpuxbind(pd->send_fd, p->errbuf) < 0) {
			status = PCAP_ERROR;
			goto bad;
		}
	}
#endif

	/*
	** Determine link type
	** XXX - get SAP length and address length as well, for use
	** when sending packets.
	*/
	if (dlinforeq(p->fd, p->errbuf) < 0 ||
	    dlinfoack(p->fd, (char *)buf, p->errbuf) < 0) {
	    	status = PCAP_ERROR;
		goto bad;
	}

	infop = &(MAKE_DL_PRIMITIVES(buf))->info_ack;
	if (pcap_process_mactype(p, infop->dl_mac_type) != 0) {
		status = PCAP_ERROR;
		goto bad;
	}

#ifdef	DLIOCRAW
	/*
	** This is a non standard SunOS hack to get the full raw link-layer
	** header.
	*/
	if (strioctl(p->fd, DLIOCRAW, 0, NULL) < 0) {
		status = PCAP_ERROR;
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "DLIOCRAW");
		goto bad;
	}
#endif

#ifdef HAVE_SYS_BUFMOD_H
	ss = p->snapshot;

	/*
	** There is a bug in bufmod(7). When dealing with messages of
	** less than snaplen size it strips data from the beginning not
	** the end.
	**
	** This bug is fixed in 5.3.2. Also, there is a patch available.
	** Ask for bugid 1149065.
	*/
#ifdef HAVE_SOLARIS
	get_release(release, sizeof (release), &osmajor, &osminor, &osmicro);
	if (osmajor == 5 && (osminor <= 2 || (osminor == 3 && osmicro < 2)) &&
	    getenv("BUFMOD_FIXED") == NULL) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		"WARNING: bufmod is broken in SunOS %s; ignoring snaplen.",
		    release);
		ss = 0;
		status = PCAP_WARNING;
	}
#endif

	/* Push and configure bufmod. */
	if (pcap_conf_bufmod(p, ss) != 0) {
		status = PCAP_ERROR;
		goto bad;
	}
#endif

	/*
	** As the last operation flush the read side.
	*/
	if (ioctl(p->fd, I_FLUSH, FLUSHR) != 0) {
		status = PCAP_ERROR;
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "FLUSHR");
		goto bad;
	}

	/* Allocate data buffer. */
	if (pcap_alloc_databuf(p) != 0) {
		status = PCAP_ERROR;
		goto bad;
	}

	/*
	 * Success.
	 *
	 * "p->fd" is an FD for a STREAMS device, so "select()" and
	 * "poll()" should work on it.
	 */
	p->selectable_fd = p->fd;

	p->read_op = pcap_read_dlpi;
	p->inject_op = pcap_inject_dlpi;
	p->setfilter_op = install_bpf_program;	/* no kernel filtering */
	p->setdirection_op = NULL;	/* Not implemented.*/
	p->set_datalink_op = NULL;	/* can't change data link type */
	p->getnonblock_op = pcap_getnonblock_fd;
	p->setnonblock_op = pcap_setnonblock_fd;
	p->stats_op = pcap_stats_dlpi;
	p->cleanup_op = pcap_cleanup_dlpi;

	return (status);
bad:
	pcap_cleanup_dlpi(p);
	return (status);
}

/*
 * Split a device name into a device type name and a unit number;
 * return the a pointer to the beginning of the unit number, which
 * is the end of the device type name, and set "*unitp" to the unit
 * number.
 *
 * Returns NULL on error, and fills "ebuf" with an error message.
 */
static char *
split_dname(char *device, u_int *unitp, char *ebuf)
{
	char *cp;
	char *eos;
	long unit;

	/*
	 * Look for a number at the end of the device name string.
	 */
	cp = device + strlen(device) - 1;
	if (*cp < '0' || *cp > '9') {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE, "%s missing unit number",
		    device);
		return (NULL);
	}

	/* Digits at end of string are unit number */
	while (cp-1 >= device && *(cp-1) >= '0' && *(cp-1) <= '9')
		cp--;

	errno = 0;
	unit = strtol(cp, &eos, 10);
	if (*eos != '\0') {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE, "%s bad unit number", device);
		return (NULL);
	}
	if (errno == ERANGE || unit > INT_MAX) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE, "%s unit number too large",
		    device);
		return (NULL);
	}
	if (unit < 0) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE, "%s unit number is negative",
		    device);
		return (NULL);
	}
	*unitp = (u_int)unit;
	return (cp);
}

static int
dl_doattach(int fd, int ppa, char *ebuf)
{
	dl_attach_req_t	req;
	bpf_u_int32 buf[MAXDLBUF];
	int err;

	req.dl_primitive = DL_ATTACH_REQ;
	req.dl_ppa = ppa;
	if (send_request(fd, (char *)&req, sizeof(req), "attach", ebuf) < 0)
		return (PCAP_ERROR);

	err = dlokack(fd, "attach", (char *)buf, ebuf);
	if (err < 0)
		return (err);
	return (0);
}

#ifdef DL_HP_RAWDLS
static int
dl_dohpuxbind(int fd, char *ebuf)
{
	int hpsap;
	int uerror;
	bpf_u_int32 buf[MAXDLBUF];

	/*
	 * XXX - we start at 22 because we used to use only 22, but
	 * that was just because that was the value used in some
	 * sample code from HP.  With what value *should* we start?
	 * Does it matter, given that we're enabling SAP promiscuity
	 * on the input FD?
	 */
	hpsap = 22;
	for (;;) {
		if (dlbindreq(fd, hpsap, ebuf) < 0)
			return (-1);
		if (dlbindack(fd, (char *)buf, ebuf, &uerror) >= 0)
			break;
		/*
		 * For any error other than a UNIX EBUSY, give up.
		 */
		if (uerror != EBUSY) {
			/*
			 * dlbindack() has already filled in ebuf for
			 * this error.
			 */
			return (-1);
		}

		/*
		 * For EBUSY, try the next SAP value; that means that
		 * somebody else is using that SAP.  Clear ebuf so
		 * that application doesn't report the "Device busy"
		 * error as a warning.
		 */
		*ebuf = '\0';
		hpsap++;
		if (hpsap > 100) {
			strlcpy(ebuf,
			    "All SAPs from 22 through 100 are in use",
			    PCAP_ERRBUF_SIZE);
			return (-1);
		}
	}
	return (0);
}
#endif

#define STRINGIFY(n)	#n

static int
dlpromiscon(pcap_t *p, bpf_u_int32 level)
{
	dl_promiscon_req_t req;
	bpf_u_int32 buf[MAXDLBUF];
	int err;

	req.dl_primitive = DL_PROMISCON_REQ;
	req.dl_level = level;
	if (send_request(p->fd, (char *)&req, sizeof(req), "promiscon",
	    p->errbuf) < 0)
		return (PCAP_ERROR);
	err = dlokack(p->fd, "promiscon" STRINGIFY(level), (char *)buf,
	    p->errbuf);
	if (err < 0)
		return (err);
	return (0);
}

/*
 * Not all interfaces are DLPI interfaces, and thus not all interfaces
 * can be opened with DLPI (for example, the loopback interface is not
 * a DLPI interface on Solaris prior to Solaris 11), so try to open
 * the specified interface; return 0 if we fail with PCAP_ERROR_NO_SUCH_DEVICE
 * and 1 otherwise.
 */
static int
is_dlpi_interface(const char *name)
{
	int fd;
	u_int ppa;
	char errbuf[PCAP_ERRBUF_SIZE];

	fd = open_dlpi_device(name, &ppa, errbuf);
	if (fd < 0) {
		/*
		 * Error - was it PCAP_ERROR_NO_SUCH_DEVICE?
		 */
		if (fd == PCAP_ERROR_NO_SUCH_DEVICE) {
			/*
			 * Yes, so we can't open this because it's
			 * not a DLPI interface.
			 */
			return (0);
		}
		/*
		 * No, so, in the case where there's a single DLPI
		 * device for all interfaces of this type ("style
		 * 2" providers?), we don't know whether it's a DLPI
		 * interface or not, as we didn't try an attach.
		 * Say it is a DLPI device, so that the user can at
		 * least try to open it and report the error (which
		 * is probably "you don't have permission to open that
		 * DLPI device"; reporting those interfaces means
		 * users will ask "why am I getting a permissions error
		 * when I try to capture" rather than "why am I not
		 * seeing any interfaces", making the underlying problem
		 * clearer).
		 */
		return (1);
	}

	/*
	 * Success.
	 */
	close(fd);
	return (1);
}

static int
get_if_flags(const char *name _U_, bpf_u_int32 *flags _U_, char *errbuf _U_)
{
	/*
	 * Nothing we can do other than mark loopback devices as "the
	 * connected/disconnected status doesn't apply".
	 *
	 * XXX - on Solaris, can we do what the dladm command does,
	 * i.e. get a connected/disconnected indication from a kstat?
	 * (Note that you can also get the link speed, and possibly
	 * other information, from a kstat as well.)
	 */
	if (*flags & PCAP_IF_LOOPBACK) {
		/*
		 * Loopback devices aren't wireless, and "connected"/
		 * "disconnected" doesn't apply to them.
		 */
		*flags |= PCAP_IF_CONNECTION_STATUS_NOT_APPLICABLE;
		return (0);
	}
	return (0);
}

int
pcap_platform_finddevs(pcap_if_list_t *devlistp, char *errbuf)
{
#ifdef HAVE_SOLARIS
	int fd;
	union {
		u_int nunits;
		char pad[516];	/* XXX - must be at least 513; is 516
				   in "atmgetunits" */
	} buf;
	char baname[2+1+1];
	u_int i;
#endif

	/*
	 * Get the list of regular interfaces first.
	 */
	if (pcap_findalldevs_interfaces(devlistp, errbuf, is_dlpi_interface,
	    get_if_flags) == -1)
		return (-1);	/* failure */

#ifdef HAVE_SOLARIS
	/*
	 * We may have to do special magic to get ATM devices.
	 */
	if ((fd = open("/dev/ba", O_RDWR)) < 0) {
		/*
		 * We couldn't open the "ba" device.
		 * For now, just give up; perhaps we should
		 * return an error if the problem is neither
		 * a "that device doesn't exist" error (ENOENT,
		 * ENXIO, etc.) or a "you're not allowed to do
		 * that" error (EPERM, EACCES).
		 */
		return (0);
	}

	if (strioctl(fd, A_GET_UNITS, sizeof(buf), (char *)&buf) < 0) {
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "A_GET_UNITS");
		return (-1);
	}
	for (i = 0; i < buf.nunits; i++) {
		pcap_snprintf(baname, sizeof baname, "ba%u", i);
		/*
		 * XXX - is there a notion of "up" and "running"?
		 * And is there a way to determine whether the
		 * interface is plugged into a network?
		 */
		if (add_dev(devlistp, baname, 0, NULL, errbuf) == NULL)
			return (-1);
	}
#endif

	return (0);
}

static int
send_request(int fd, char *ptr, int len, char *what, char *ebuf)
{
	struct	strbuf	ctl;
	int	flags;

	ctl.maxlen = 0;
	ctl.len = len;
	ctl.buf = ptr;

	flags = 0;
	if (putmsg(fd, &ctl, (struct strbuf *) NULL, flags) < 0) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "send_request: putmsg \"%s\"", what);
		return (-1);
	}
	return (0);
}

static int
recv_ack(int fd, int size, const char *what, char *bufp, char *ebuf, int *uerror)
{
	union	DL_primitives	*dlp;
	struct	strbuf	ctl;
	int	flags;
	char	errmsgbuf[PCAP_ERRBUF_SIZE];
	char	dlprimbuf[64];

	/*
	 * Clear out "*uerror", so it's only set for DL_ERROR_ACK/DL_SYSERR,
	 * making that the only place where EBUSY is treated specially.
	 */
	if (uerror != NULL)
		*uerror = 0;

	ctl.maxlen = MAXDLBUF;
	ctl.len = 0;
	ctl.buf = bufp;

	flags = 0;
	if (getmsg(fd, &ctl, (struct strbuf*)NULL, &flags) < 0) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "recv_ack: %s getmsg", what);
		return (PCAP_ERROR);
	}

	dlp = MAKE_DL_PRIMITIVES(ctl.buf);
	switch (dlp->dl_primitive) {

	case DL_INFO_ACK:
	case DL_BIND_ACK:
	case DL_OK_ACK:
#ifdef DL_HP_PPA_ACK
	case DL_HP_PPA_ACK:
#endif
		/* These are OK */
		break;

	case DL_ERROR_ACK:
		switch (dlp->error_ack.dl_errno) {

		case DL_SYSERR:
			if (uerror != NULL)
				*uerror = dlp->error_ack.dl_unix_errno;
			pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
			    dlp->error_ack.dl_unix_errno,
			    "recv_ack: %s: UNIX error", what);
			if (dlp->error_ack.dl_unix_errno == EPERM ||
			    dlp->error_ack.dl_unix_errno == EACCES)
				return (PCAP_ERROR_PERM_DENIED);
			break;

		default:
			pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
			    "recv_ack: %s: %s", what,
			    dlstrerror(errmsgbuf, sizeof (errmsgbuf), dlp->error_ack.dl_errno));
			if (dlp->error_ack.dl_errno == DL_BADPPA)
				return (PCAP_ERROR_NO_SUCH_DEVICE);
			else if (dlp->error_ack.dl_errno == DL_ACCESS)
				return (PCAP_ERROR_PERM_DENIED);
			break;
		}
		return (PCAP_ERROR);

	default:
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "recv_ack: %s: Unexpected primitive ack %s",
		    what, dlprim(dlprimbuf, sizeof (dlprimbuf), dlp->dl_primitive));
		return (PCAP_ERROR);
	}

	if (ctl.len < size) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "recv_ack: %s: Ack too small (%d < %d)",
		    what, ctl.len, size);
		return (PCAP_ERROR);
	}
	return (ctl.len);
}

static char *
dlstrerror(char *errbuf, size_t errbufsize, bpf_u_int32 dl_errno)
{
	switch (dl_errno) {

	case DL_ACCESS:
		return ("Improper permissions for request");

	case DL_BADADDR:
		return ("DLSAP addr in improper format or invalid");

	case DL_BADCORR:
		return ("Seq number not from outstand DL_CONN_IND");

	case DL_BADDATA:
		return ("User data exceeded provider limit");

	case DL_BADPPA:
#ifdef HAVE_DEV_DLPI
		/*
		 * With a single "/dev/dlpi" device used for all
		 * DLPI providers, PPAs have nothing to do with
		 * unit numbers.
		 */
		return ("Specified PPA was invalid");
#else
		/*
		 * We have separate devices for separate devices;
		 * the PPA is just the unit number.
		 */
		return ("Specified PPA (device unit) was invalid");
#endif

	case DL_BADPRIM:
		return ("Primitive received not known by provider");

	case DL_BADQOSPARAM:
		return ("QOS parameters contained invalid values");

	case DL_BADQOSTYPE:
		return ("QOS structure type is unknown/unsupported");

	case DL_BADSAP:
		return ("Bad LSAP selector");

	case DL_BADTOKEN:
		return ("Token used not an active stream");

	case DL_BOUND:
		return ("Attempted second bind with dl_max_conind");

	case DL_INITFAILED:
		return ("Physical link initialization failed");

	case DL_NOADDR:
		return ("Provider couldn't allocate alternate address");

	case DL_NOTINIT:
		return ("Physical link not initialized");

	case DL_OUTSTATE:
		return ("Primitive issued in improper state");

	case DL_SYSERR:
		return ("UNIX system error occurred");

	case DL_UNSUPPORTED:
		return ("Requested service not supplied by provider");

	case DL_UNDELIVERABLE:
		return ("Previous data unit could not be delivered");

	case DL_NOTSUPPORTED:
		return ("Primitive is known but not supported");

	case DL_TOOMANY:
		return ("Limit exceeded");

	case DL_NOTENAB:
		return ("Promiscuous mode not enabled");

	case DL_BUSY:
		return ("Other streams for PPA in post-attached");

	case DL_NOAUTO:
		return ("Automatic handling XID&TEST not supported");

	case DL_NOXIDAUTO:
		return ("Automatic handling of XID not supported");

	case DL_NOTESTAUTO:
		return ("Automatic handling of TEST not supported");

	case DL_XIDAUTO:
		return ("Automatic handling of XID response");

	case DL_TESTAUTO:
		return ("Automatic handling of TEST response");

	case DL_PENDING:
		return ("Pending outstanding connect indications");

	default:
		pcap_snprintf(errbuf, errbufsize, "Error %02x", dl_errno);
		return (errbuf);
	}
}

static char *
dlprim(char *primbuf, size_t primbufsize, bpf_u_int32 prim)
{
	switch (prim) {

	case DL_INFO_REQ:
		return ("DL_INFO_REQ");

	case DL_INFO_ACK:
		return ("DL_INFO_ACK");

	case DL_ATTACH_REQ:
		return ("DL_ATTACH_REQ");

	case DL_DETACH_REQ:
		return ("DL_DETACH_REQ");

	case DL_BIND_REQ:
		return ("DL_BIND_REQ");

	case DL_BIND_ACK:
		return ("DL_BIND_ACK");

	case DL_UNBIND_REQ:
		return ("DL_UNBIND_REQ");

	case DL_OK_ACK:
		return ("DL_OK_ACK");

	case DL_ERROR_ACK:
		return ("DL_ERROR_ACK");

	case DL_SUBS_BIND_REQ:
		return ("DL_SUBS_BIND_REQ");

	case DL_SUBS_BIND_ACK:
		return ("DL_SUBS_BIND_ACK");

	case DL_UNITDATA_REQ:
		return ("DL_UNITDATA_REQ");

	case DL_UNITDATA_IND:
		return ("DL_UNITDATA_IND");

	case DL_UDERROR_IND:
		return ("DL_UDERROR_IND");

	case DL_UDQOS_REQ:
		return ("DL_UDQOS_REQ");

	case DL_CONNECT_REQ:
		return ("DL_CONNECT_REQ");

	case DL_CONNECT_IND:
		return ("DL_CONNECT_IND");

	case DL_CONNECT_RES:
		return ("DL_CONNECT_RES");

	case DL_CONNECT_CON:
		return ("DL_CONNECT_CON");

	case DL_TOKEN_REQ:
		return ("DL_TOKEN_REQ");

	case DL_TOKEN_ACK:
		return ("DL_TOKEN_ACK");

	case DL_DISCONNECT_REQ:
		return ("DL_DISCONNECT_REQ");

	case DL_DISCONNECT_IND:
		return ("DL_DISCONNECT_IND");

	case DL_RESET_REQ:
		return ("DL_RESET_REQ");

	case DL_RESET_IND:
		return ("DL_RESET_IND");

	case DL_RESET_RES:
		return ("DL_RESET_RES");

	case DL_RESET_CON:
		return ("DL_RESET_CON");

	default:
		pcap_snprintf(primbuf, primbufsize, "unknown primitive 0x%x",
		    prim);
		return (primbuf);
	}
}

static int
dlbindreq(int fd, bpf_u_int32 sap, char *ebuf)
{

	dl_bind_req_t	req;

	memset((char *)&req, 0, sizeof(req));
	req.dl_primitive = DL_BIND_REQ;
	/* XXX - what if neither of these are defined? */
#if defined(DL_HP_RAWDLS)
	req.dl_max_conind = 1;			/* XXX magic number */
	req.dl_service_mode = DL_HP_RAWDLS;
#elif defined(DL_CLDLS)
	req.dl_service_mode = DL_CLDLS;
#endif
	req.dl_sap = sap;

	return (send_request(fd, (char *)&req, sizeof(req), "bind", ebuf));
}

static int
dlbindack(int fd, char *bufp, char *ebuf, int *uerror)
{

	return (recv_ack(fd, DL_BIND_ACK_SIZE, "bind", bufp, ebuf, uerror));
}

static int
dlokack(int fd, const char *what, char *bufp, char *ebuf)
{

	return (recv_ack(fd, DL_OK_ACK_SIZE, what, bufp, ebuf, NULL));
}


static int
dlinforeq(int fd, char *ebuf)
{
	dl_info_req_t req;

	req.dl_primitive = DL_INFO_REQ;

	return (send_request(fd, (char *)&req, sizeof(req), "info", ebuf));
}

static int
dlinfoack(int fd, char *bufp, char *ebuf)
{

	return (recv_ack(fd, DL_INFO_ACK_SIZE, "info", bufp, ebuf, NULL));
}

#ifdef HAVE_DL_PASSIVE_REQ_T
/*
 * Enable DLPI passive mode. We do not care if this request fails, as this
 * indicates the underlying DLPI device does not support link aggregation.
 */
static void
dlpassive(int fd, char *ebuf)
{
	dl_passive_req_t req;
	bpf_u_int32 buf[MAXDLBUF];

	req.dl_primitive = DL_PASSIVE_REQ;

	if (send_request(fd, (char *)&req, sizeof(req), "dlpassive", ebuf) == 0)
	    (void) dlokack(fd, "dlpassive", (char *)buf, ebuf);
}
#endif

#ifdef DL_HP_RAWDLS
/*
 * There's an ack *if* there's an error.
 */
static int
dlrawdatareq(int fd, const u_char *datap, int datalen)
{
	struct strbuf ctl, data;
	long buf[MAXDLBUF];	/* XXX - char? */
	union DL_primitives *dlp;
	int dlen;

	dlp = MAKE_DL_PRIMITIVES(buf);

	dlp->dl_primitive = DL_HP_RAWDATA_REQ;
	dlen = DL_HP_RAWDATA_REQ_SIZE;

	/*
	 * HP's documentation doesn't appear to show us supplying any
	 * address pointed to by the control part of the message.
	 * I think that's what raw mode means - you just send the raw
	 * packet, you don't specify where to send it to, as that's
	 * implied by the destination address.
	 */
	ctl.maxlen = 0;
	ctl.len = dlen;
	ctl.buf = (void *)buf;

	data.maxlen = 0;
	data.len = datalen;
	data.buf = (void *)datap;

	return (putmsg(fd, &ctl, &data, 0));
}
#endif /* DL_HP_RAWDLS */

#if defined(HAVE_SOLARIS) && defined(HAVE_SYS_BUFMOD_H)
static void
get_release(char *buf, size_t bufsize, bpf_u_int32 *majorp,
    bpf_u_int32 *minorp, bpf_u_int32 *microp)
{
	char *cp;

	*majorp = 0;
	*minorp = 0;
	*microp = 0;
	if (sysinfo(SI_RELEASE, buf, bufsize) < 0) {
		strlcpy(buf, "?", bufsize);
		return;
	}
	cp = buf;
	if (!isdigit((unsigned char)*cp))
		return;
	*majorp = strtol(cp, &cp, 10);
	if (*cp++ != '.')
		return;
	*minorp =  strtol(cp, &cp, 10);
	if (*cp++ != '.')
		return;
	*microp =  strtol(cp, &cp, 10);
}
#endif

#ifdef DL_HP_PPA_REQ
/*
 * Under HP-UX 10 and HP-UX 11, we can ask for the ppa
 */


/*
 * Determine ppa number that specifies ifname.
 *
 * If the "dl_hp_ppa_info_t" doesn't have a "dl_module_id_1" member,
 * the code that's used here is the old code for HP-UX 10.x.
 *
 * However, HP-UX 10.20, at least, appears to have such a member
 * in its "dl_hp_ppa_info_t" structure, so the new code is used.
 * The new code didn't work on an old 10.20 system on which Rick
 * Jones of HP tried it, but with later patches installed, it
 * worked - it appears that the older system had those members but
 * didn't put anything in them, so, if the search by name fails, we
 * do the old search.
 *
 * Rick suggests that making sure your system is "up on the latest
 * lancommon/DLPI/driver patches" is probably a good idea; it'd fix
 * that problem, as well as allowing libpcap to see packets sent
 * from the system on which the libpcap application is being run.
 * (On 10.20, in addition to getting the latest patches, you need
 * to turn the kernel "lanc_outbound_promisc_flag" flag on with ADB;
 * a posting to "comp.sys.hp.hpux" at
 *
 *	http://www.deja.com/[ST_rn=ps]/getdoc.xp?AN=558092266
 *
 * says that, to see the machine's outgoing traffic, you'd need to
 * apply the right patches to your system, and also set that variable
 * with:

echo 'lanc_outbound_promisc_flag/W1' | /usr/bin/adb -w /stand/vmunix /dev/kmem

 * which could be put in, for example, "/sbin/init.d/lan".
 *
 * Setting the variable is not necessary on HP-UX 11.x.
 */
static int
get_dlpi_ppa(register int fd, register const char *device, register u_int unit,
    u_int *ppa, register char *ebuf)
{
	register dl_hp_ppa_ack_t *ap;
	register dl_hp_ppa_info_t *ipstart, *ip;
	register u_int i;
	char dname[100];
	register u_long majdev;
	struct stat statbuf;
	dl_hp_ppa_req_t	req;
	char buf[MAXDLBUF];
	char *ppa_data_buf;
	dl_hp_ppa_ack_t	*dlp;
	struct strbuf ctl;
	int flags;

	memset((char *)&req, 0, sizeof(req));
	req.dl_primitive = DL_HP_PPA_REQ;

	memset((char *)buf, 0, sizeof(buf));
	if (send_request(fd, (char *)&req, sizeof(req), "hpppa", ebuf) < 0)
		return (PCAP_ERROR);

	ctl.maxlen = DL_HP_PPA_ACK_SIZE;
	ctl.len = 0;
	ctl.buf = (char *)buf;

	flags = 0;
	/*
	 * DLPI may return a big chunk of data for a DL_HP_PPA_REQ. The normal
	 * recv_ack will fail because it set the maxlen to MAXDLBUF (8192)
	 * which is NOT big enough for a DL_HP_PPA_REQ.
	 *
	 * This causes libpcap applications to fail on a system with HP-APA
	 * installed.
	 *
	 * To figure out how big the returned data is, we first call getmsg
	 * to get the small head and peek at the head to get the actual data
	 * length, and  then issue another getmsg to get the actual PPA data.
	 */
	/* get the head first */
	if (getmsg(fd, &ctl, (struct strbuf *)NULL, &flags) < 0) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "get_dlpi_ppa: hpppa getmsg");
		return (PCAP_ERROR);
	}
	if (ctl.len == -1) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "get_dlpi_ppa: hpppa getmsg: control buffer has no data");
		return (PCAP_ERROR);
	}

	dlp = (dl_hp_ppa_ack_t *)ctl.buf;
	if (dlp->dl_primitive != DL_HP_PPA_ACK) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "get_dlpi_ppa: hpppa unexpected primitive ack 0x%x",
		    (bpf_u_int32)dlp->dl_primitive);
		return (PCAP_ERROR);
	}

	if ((size_t)ctl.len < DL_HP_PPA_ACK_SIZE) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "get_dlpi_ppa: hpppa ack too small (%d < %lu)",
		     ctl.len, (unsigned long)DL_HP_PPA_ACK_SIZE);
		return (PCAP_ERROR);
	}

	/* allocate buffer */
	if ((ppa_data_buf = (char *)malloc(dlp->dl_length)) == NULL) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "get_dlpi_ppa: hpppa malloc");
		return (PCAP_ERROR);
	}
	ctl.maxlen = dlp->dl_length;
	ctl.len = 0;
	ctl.buf = (char *)ppa_data_buf;
	/* get the data */
	if (getmsg(fd, &ctl, (struct strbuf *)NULL, &flags) < 0) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "get_dlpi_ppa: hpppa getmsg");
		free(ppa_data_buf);
		return (PCAP_ERROR);
	}
	if (ctl.len == -1) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "get_dlpi_ppa: hpppa getmsg: control buffer has no data");
		return (PCAP_ERROR);
	}
	if ((u_int)ctl.len < dlp->dl_length) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "get_dlpi_ppa: hpppa ack too small (%d < %lu)",
		    ctl.len, (unsigned long)dlp->dl_length);
		free(ppa_data_buf);
		return (PCAP_ERROR);
	}

	ap = (dl_hp_ppa_ack_t *)buf;
	ipstart = (dl_hp_ppa_info_t *)ppa_data_buf;
	ip = ipstart;

#ifdef HAVE_DL_HP_PPA_INFO_T_DL_MODULE_ID_1
	/*
	 * The "dl_hp_ppa_info_t" structure has a "dl_module_id_1"
	 * member that should, in theory, contain the part of the
	 * name for the device that comes before the unit number,
	 * and should also have a "dl_module_id_2" member that may
	 * contain an alternate name (e.g., I think Ethernet devices
	 * have both "lan", for "lanN", and "snap", for "snapN", with
	 * the former being for Ethernet packets and the latter being
	 * for 802.3/802.2 packets).
	 *
	 * Search for the device that has the specified name and
	 * instance number.
	 */
	for (i = 0; i < ap->dl_count; i++) {
		if ((strcmp((const char *)ip->dl_module_id_1, device) == 0 ||
		     strcmp((const char *)ip->dl_module_id_2, device) == 0) &&
		    ip->dl_instance_num == unit)
			break;

		ip = (dl_hp_ppa_info_t *)((u_char *)ipstart + ip->dl_next_offset);
	}
#else
	/*
	 * We don't have that member, so the search is impossible; make it
	 * look as if the search failed.
	 */
	i = ap->dl_count;
#endif

	if (i == ap->dl_count) {
		/*
		 * Well, we didn't, or can't, find the device by name.
		 *
		 * HP-UX 10.20, whilst it has "dl_module_id_1" and
		 * "dl_module_id_2" fields in the "dl_hp_ppa_info_t",
		 * doesn't seem to fill them in unless the system is
		 * at a reasonably up-to-date patch level.
		 *
		 * Older HP-UX 10.x systems might not have those fields
		 * at all.
		 *
		 * Therefore, we'll search for the entry with the major
		 * device number of a device with the name "/dev/<dev><unit>",
		 * if such a device exists, as the old code did.
		 */
		pcap_snprintf(dname, sizeof(dname), "/dev/%s%u", device, unit);
		if (stat(dname, &statbuf) < 0) {
			pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
			    errno, "stat: %s", dname);
			return (PCAP_ERROR);
		}
		majdev = major(statbuf.st_rdev);

		ip = ipstart;

		for (i = 0; i < ap->dl_count; i++) {
			if (ip->dl_mjr_num == majdev &&
			    ip->dl_instance_num == unit)
				break;

			ip = (dl_hp_ppa_info_t *)((u_char *)ipstart + ip->dl_next_offset);
		}
	}
	if (i == ap->dl_count) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "can't find /dev/dlpi PPA for %s%u", device, unit);
		return (PCAP_ERROR_NO_SUCH_DEVICE);
	}
	if (ip->dl_hdw_state == HDW_DEAD) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "%s%d: hardware state: DOWN\n", device, unit);
		free(ppa_data_buf);
		return (PCAP_ERROR);
	}
	*ppa = ip->dl_ppa;
	free(ppa_data_buf);
	return (0);
}
#endif

#ifdef HAVE_HPUX9
/*
 * Under HP-UX 9, there is no good way to determine the ppa.
 * So punt and read it from /dev/kmem.
 */
static struct nlist nl[] = {
#define NL_IFNET 0
	{ "ifnet" },
	{ "" }
};

static char path_vmunix[] = "/hp-ux";

/* Determine ppa number that specifies ifname */
static int
get_dlpi_ppa(register int fd, register const char *ifname, register u_int unit,
    u_int *ppa, register char *ebuf)
{
	register const char *cp;
	register int kd;
	void *addr;
	struct ifnet ifnet;
	char if_name[sizeof(ifnet.if_name) + 1];

	cp = strrchr(ifname, '/');
	if (cp != NULL)
		ifname = cp + 1;
	if (nlist(path_vmunix, &nl) < 0) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE, "nlist %s failed",
		    path_vmunix);
		return (PCAP_ERROR);
	}
	if (nl[NL_IFNET].n_value == 0) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE,
		    "could't find %s kernel symbol",
		    nl[NL_IFNET].n_name);
		return (PCAP_ERROR);
	}
	kd = open("/dev/kmem", O_RDONLY);
	if (kd < 0) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "kmem open");
		return (PCAP_ERROR);
	}
	if (dlpi_kread(kd, nl[NL_IFNET].n_value,
	    &addr, sizeof(addr), ebuf) < 0) {
		close(kd);
		return (PCAP_ERROR);
	}
	for (; addr != NULL; addr = ifnet.if_next) {
		if (dlpi_kread(kd, (off_t)addr,
		    &ifnet, sizeof(ifnet), ebuf) < 0 ||
		    dlpi_kread(kd, (off_t)ifnet.if_name,
		    if_name, sizeof(ifnet.if_name), ebuf) < 0) {
			(void)close(kd);
			return (PCAP_ERROR);
		}
		if_name[sizeof(ifnet.if_name)] = '\0';
		if (strcmp(if_name, ifname) == 0 && ifnet.if_unit == unit) {
			*ppa = ifnet.if_index;
			return (0);
		}
	}

	pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE, "Can't find %s", ifname);
	return (PCAP_ERROR_NO_SUCH_DEVICE);
}

static int
dlpi_kread(register int fd, register off_t addr,
    register void *buf, register u_int len, register char *ebuf)
{
	register int cc;

	if (lseek(fd, addr, SEEK_SET) < 0) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "lseek");
		return (-1);
	}
	cc = read(fd, buf, len);
	if (cc < 0) {
		pcap_fmt_errmsg_for_errno(ebuf, PCAP_ERRBUF_SIZE,
		    errno, "read");
		return (-1);
	} else if (cc != len) {
		pcap_snprintf(ebuf, PCAP_ERRBUF_SIZE, "short read (%d != %d)", cc,
		    len);
		return (-1);
	}
	return (cc);
}
#endif

pcap_t *
pcap_create_interface(const char *device _U_, char *ebuf)
{
	pcap_t *p;
#ifdef DL_HP_RAWDLS
	struct pcap_dlpi *pd;
#endif

	p = pcap_create_common(ebuf, sizeof (struct pcap_dlpi));
	if (p == NULL)
		return (NULL);

#ifdef DL_HP_RAWDLS
	pd = p->priv;
	pd->send_fd = -1;	/* it hasn't been opened yet */
#endif

	p->activate_op = pcap_activate_dlpi;
	return (p);
}

/*
 * Libpcap version string.
 */
const char *
pcap_lib_version(void)
{
	return (PCAP_VERSION_STRING);
}
