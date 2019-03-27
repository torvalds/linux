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
 * savefile.c - supports offline use of tcpdump
 *	Extraction/creation by Jeffrey Mogul, DECWRL
 *	Modified by Steve McCanne, LBL.
 *
 * Used to save the received packet headers, after filtering, to
 * a file, and then read them later.
 * The first record in the file contains saved values for the machine
 * dependent values so we can print the dump file on any architecture.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pcap-types.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif /* _WIN32 */

#include <errno.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcap-int.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#include "sf-pcap.h"
#include "sf-pcapng.h"

#ifdef _WIN32
/*
 * These aren't exported on Windows, because they would only work if both
 * WinPcap and the code using it were to use the Universal CRT; otherwise,
 * a FILE structure in WinPcap and a FILE structure in the code using it
 * could be different if they're using different versions of the C runtime.
 *
 * Instead, pcap/pcap.h defines them as macros that wrap the hopen versions,
 * with the wrappers calling _fileno() and _get_osfhandle() themselves,
 * so that they convert the appropriate CRT version's FILE structure to
 * a HANDLE (which is OS-defined, not CRT-defined, and is part of the Win32
 * and Win64 ABIs).
 */
static pcap_t *pcap_fopen_offline_with_tstamp_precision(FILE *, u_int, char *);
static pcap_t *pcap_fopen_offline(FILE *, char *);
#endif

/*
 * Setting O_BINARY on DOS/Windows is a bit tricky
 */
#if defined(_WIN32)
  #define SET_BINMODE(f)  _setmode(_fileno(f), _O_BINARY)
#elif defined(MSDOS)
  #if defined(__HIGHC__)
  #define SET_BINMODE(f)  setmode(f, O_BINARY)
  #else
  #define SET_BINMODE(f)  setmode(fileno(f), O_BINARY)
  #endif
#endif

static int
sf_getnonblock(pcap_t *p _U_)
{
	/*
	 * This is a savefile, not a live capture file, so never say
	 * it's in non-blocking mode.
	 */
	return (0);
}

static int
sf_setnonblock(pcap_t *p, int nonblock _U_)
{
	/*
	 * This is a savefile, not a live capture file, so reject
	 * requests to put it in non-blocking mode.  (If it's a
	 * pipe, it could be put in non-blocking mode, but that
	 * would significantly complicate the code to read packets,
	 * as it would have to handle reading partial packets and
	 * keeping the state of the read.)
	 */
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "Savefiles cannot be put into non-blocking mode");
	return (-1);
}

static int
sf_stats(pcap_t *p, struct pcap_stat *ps _U_)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "Statistics aren't available from savefiles");
	return (-1);
}

#ifdef _WIN32
static struct pcap_stat *
sf_stats_ex(pcap_t *p, int *size)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "Statistics aren't available from savefiles");
	return (NULL);
}

static int
sf_setbuff(pcap_t *p, int dim)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "The kernel buffer size cannot be set while reading from a file");
	return (-1);
}

static int
sf_setmode(pcap_t *p, int mode)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "impossible to set mode while reading from a file");
	return (-1);
}

static int
sf_setmintocopy(pcap_t *p, int size)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "The mintocopy parameter cannot be set while reading from a file");
	return (-1);
}

static HANDLE
sf_getevent(pcap_t *pcap)
{
	(void)pcap_snprintf(pcap->errbuf, sizeof(pcap->errbuf),
	    "The read event cannot be retrieved while reading from a file");
	return (INVALID_HANDLE_VALUE);
}

static int
sf_oid_get_request(pcap_t *p, bpf_u_int32 oid _U_, void *data _U_,
    size_t *lenp _U_)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "An OID get request cannot be performed on a file");
	return (PCAP_ERROR);
}

static int
sf_oid_set_request(pcap_t *p, bpf_u_int32 oid _U_, const void *data _U_,
    size_t *lenp _U_)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "An OID set request cannot be performed on a file");
	return (PCAP_ERROR);
}

static u_int
sf_sendqueue_transmit(pcap_t *p, pcap_send_queue *queue, int sync)
{
	strlcpy(p->errbuf, "Sending packets isn't supported on savefiles",
	    PCAP_ERRBUF_SIZE);
	return (0);
}

static int
sf_setuserbuffer(pcap_t *p, int size)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "The user buffer cannot be set when reading from a file");
	return (-1);
}

static int
sf_live_dump(pcap_t *p, char *filename, int maxsize, int maxpacks)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "Live packet dumping cannot be performed when reading from a file");
	return (-1);
}

static int
sf_live_dump_ended(pcap_t *p, int sync)
{
	pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "Live packet dumping cannot be performed on a pcap_open_dead pcap_t");
	return (-1);
}

static PAirpcapHandle
sf_get_airpcap_handle(pcap_t *pcap)
{
	return (NULL);
}
#endif

static int
sf_inject(pcap_t *p, const void *buf _U_, size_t size _U_)
{
	strlcpy(p->errbuf, "Sending packets isn't supported on savefiles",
	    PCAP_ERRBUF_SIZE);
	return (-1);
}

/*
 * Set direction flag: Which packets do we accept on a forwarding
 * single device? IN, OUT or both?
 */
static int
sf_setdirection(pcap_t *p, pcap_direction_t d _U_)
{
	pcap_snprintf(p->errbuf, sizeof(p->errbuf),
	    "Setting direction is not supported on savefiles");
	return (-1);
}

void
sf_cleanup(pcap_t *p)
{
	if (p->rfile != stdin)
		(void)fclose(p->rfile);
	if (p->buffer != NULL)
		free(p->buffer);
	pcap_freecode(&p->fcode);
}

pcap_t *
pcap_open_offline_with_tstamp_precision(const char *fname, u_int precision,
					char *errbuf)
{
	FILE *fp;
	pcap_t *p;

	if (fname == NULL) {
		pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
		    "A null pointer was supplied as the file name");
		return (NULL);
	}
	if (fname[0] == '-' && fname[1] == '\0')
	{
		fp = stdin;
#if defined(_WIN32) || defined(MSDOS)
		/*
		 * We're reading from the standard input, so put it in binary
		 * mode, as savefiles are binary files.
		 */
		SET_BINMODE(fp);
#endif
	}
	else {
		/*
		 * "b" is supported as of C90, so *all* UN*Xes should
		 * support it, even though it does nothing.  It's
		 * required on Windows, as the file is a binary file
		 * and must be read in binary mode.
		 */
		fp = fopen(fname, "rb");
		if (fp == NULL) {
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    errno, "%s", fname);
			return (NULL);
		}
	}
	p = pcap_fopen_offline_with_tstamp_precision(fp, precision, errbuf);
	if (p == NULL) {
		if (fp != stdin)
			fclose(fp);
	}
	return (p);
}

pcap_t *
pcap_open_offline(const char *fname, char *errbuf)
{
	return (pcap_open_offline_with_tstamp_precision(fname,
	    PCAP_TSTAMP_PRECISION_MICRO, errbuf));
}

#ifdef _WIN32
pcap_t* pcap_hopen_offline_with_tstamp_precision(intptr_t osfd, u_int precision,
    char *errbuf)
{
	int fd;
	FILE *file;

	fd = _open_osfhandle(osfd, _O_RDONLY);
	if ( fd < 0 )
	{
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "_open_osfhandle");
		return NULL;
	}

	file = _fdopen(fd, "rb");
	if ( file == NULL )
	{
		pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
		    errno, "_fdopen");
		return NULL;
	}

	return pcap_fopen_offline_with_tstamp_precision(file, precision,
	    errbuf);
}

pcap_t* pcap_hopen_offline(intptr_t osfd, char *errbuf)
{
	return pcap_hopen_offline_with_tstamp_precision(osfd,
	    PCAP_TSTAMP_PRECISION_MICRO, errbuf);
}
#endif

static pcap_t *(*check_headers[])(bpf_u_int32, FILE *, u_int, char *, int *) = {
	pcap_check_header,
	pcap_ng_check_header
};

#define	N_FILE_TYPES	(sizeof check_headers / sizeof check_headers[0])

#ifdef _WIN32
static
#endif
pcap_t *
pcap_fopen_offline_with_tstamp_precision(FILE *fp, u_int precision,
    char *errbuf)
{
	register pcap_t *p;
	bpf_u_int32 magic;
	size_t amt_read;
	u_int i;
	int err;

	/*
	 * Read the first 4 bytes of the file; the network analyzer dump
	 * file formats we support (pcap and pcapng), and several other
	 * formats we might support in the future (such as snoop, DOS and
	 * Windows Sniffer, and Microsoft Network Monitor) all have magic
	 * numbers that are unique in their first 4 bytes.
	 */
	amt_read = fread((char *)&magic, 1, sizeof(magic), fp);
	if (amt_read != sizeof(magic)) {
		if (ferror(fp)) {
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    errno, "error reading dump file");
		} else {
			pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "truncated dump file; tried to read %lu file header bytes, only got %lu",
			    (unsigned long)sizeof(magic),
			    (unsigned long)amt_read);
		}
		return (NULL);
	}

	/*
	 * Try all file types.
	 */
	for (i = 0; i < N_FILE_TYPES; i++) {
		p = (*check_headers[i])(magic, fp, precision, errbuf, &err);
		if (p != NULL) {
			/* Yup, that's it. */
			goto found;
		}
		if (err) {
			/*
			 * Error trying to read the header.
			 */
			return (NULL);
		}
	}

	/*
	 * Well, who knows what this mess is....
	 */
	pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE, "unknown file format");
	return (NULL);

found:
	p->rfile = fp;

	/* Padding only needed for live capture fcode */
	p->fddipad = 0;

#if !defined(_WIN32) && !defined(MSDOS)
	/*
	 * You can do "select()" and "poll()" on plain files on most
	 * platforms, and should be able to do so on pipes.
	 *
	 * You can't do "select()" on anything other than sockets in
	 * Windows, so, on Win32 systems, we don't have "selectable_fd".
	 */
	p->selectable_fd = fileno(fp);
#endif

	p->read_op = pcap_offline_read;
	p->inject_op = sf_inject;
	p->setfilter_op = install_bpf_program;
	p->setdirection_op = sf_setdirection;
	p->set_datalink_op = NULL;	/* we don't support munging link-layer headers */
	p->getnonblock_op = sf_getnonblock;
	p->setnonblock_op = sf_setnonblock;
	p->stats_op = sf_stats;
#ifdef _WIN32
	p->stats_ex_op = sf_stats_ex;
	p->setbuff_op = sf_setbuff;
	p->setmode_op = sf_setmode;
	p->setmintocopy_op = sf_setmintocopy;
	p->getevent_op = sf_getevent;
	p->oid_get_request_op = sf_oid_get_request;
	p->oid_set_request_op = sf_oid_set_request;
	p->sendqueue_transmit_op = sf_sendqueue_transmit;
	p->setuserbuffer_op = sf_setuserbuffer;
	p->live_dump_op = sf_live_dump;
	p->live_dump_ended_op = sf_live_dump_ended;
	p->get_airpcap_handle_op = sf_get_airpcap_handle;
#endif

	/*
	 * For offline captures, the standard one-shot callback can
	 * be used for pcap_next()/pcap_next_ex().
	 */
	p->oneshot_callback = pcap_oneshot;

	/*
	 * Savefiles never require special BPF code generation.
	 */
	p->bpf_codegen_flags = 0;

	p->activated = 1;

	return (p);
}

#ifdef _WIN32
static
#endif
pcap_t *
pcap_fopen_offline(FILE *fp, char *errbuf)
{
	return (pcap_fopen_offline_with_tstamp_precision(fp,
	    PCAP_TSTAMP_PRECISION_MICRO, errbuf));
}

/*
 * Read packets from a capture file, and call the callback for each
 * packet.
 * If cnt > 0, return after 'cnt' packets, otherwise continue until eof.
 */
int
pcap_offline_read(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
	struct bpf_insn *fcode;
	int status = 0;
	int n = 0;
	u_char *data;

	while (status == 0) {
		struct pcap_pkthdr h;

		/*
		 * Has "pcap_breakloop()" been called?
		 * If so, return immediately - if we haven't read any
		 * packets, clear the flag and return -2 to indicate
		 * that we were told to break out of the loop, otherwise
		 * leave the flag set, so that the *next* call will break
		 * out of the loop without having read any packets, and
		 * return the number of packets we've processed so far.
		 */
		if (p->break_loop) {
			if (n == 0) {
				p->break_loop = 0;
				return (-2);
			} else
				return (n);
		}

		status = p->next_packet_op(p, &h, &data);
		if (status) {
			if (status == 1)
				return (0);
			return (status);
		}

		if ((fcode = p->fcode.bf_insns) == NULL ||
		    bpf_filter(fcode, data, h.len, h.caplen)) {
			(*callback)(user, &h, data);
			if (++n >= cnt && cnt > 0)
				break;
		}
	}
	/*XXX this breaks semantics tcpslice expects */
	return (n);
}
