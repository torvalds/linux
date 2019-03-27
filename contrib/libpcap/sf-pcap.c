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
 * sf-pcap.c - libpcap-file-format-specific code from savefile.c
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

#include "pcap-common.h"

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#include "sf-pcap.h"

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

/*
 * Standard libpcap format.
 */
#define TCPDUMP_MAGIC		0xa1b2c3d4

/*
 * Alexey Kuznetzov's modified libpcap format.
 */
#define KUZNETZOV_TCPDUMP_MAGIC	0xa1b2cd34

/*
 * Reserved for Francisco Mesquita <francisco.mesquita@radiomovel.pt>
 * for another modified format.
 */
#define FMESQUITA_TCPDUMP_MAGIC	0xa1b234cd

/*
 * Navtel Communcations' format, with nanosecond timestamps,
 * as per a request from Dumas Hwang <dumas.hwang@navtelcom.com>.
 */
#define NAVTEL_TCPDUMP_MAGIC	0xa12b3c4d

/*
 * Normal libpcap format, except for seconds/nanoseconds timestamps,
 * as per a request by Ulf Lamping <ulf.lamping@web.de>
 */
#define NSEC_TCPDUMP_MAGIC	0xa1b23c4d

/*
 * Mechanism for storing information about a capture in the upper
 * 6 bits of a linktype value in a capture file.
 *
 * LT_LINKTYPE_EXT(x) extracts the additional information.
 *
 * The rest of the bits are for a value describing the link-layer
 * value.  LT_LINKTYPE(x) extracts that value.
 */
#define LT_LINKTYPE(x)		((x) & 0x03FFFFFF)
#define LT_LINKTYPE_EXT(x)	((x) & 0xFC000000)

static int pcap_next_packet(pcap_t *p, struct pcap_pkthdr *hdr, u_char **datap);

/*
 * Private data for reading pcap savefiles.
 */
typedef enum {
	NOT_SWAPPED,
	SWAPPED,
	MAYBE_SWAPPED
} swapped_type_t;

typedef enum {
	PASS_THROUGH,
	SCALE_UP,
	SCALE_DOWN
} tstamp_scale_type_t;

struct pcap_sf {
	size_t hdrsize;
	swapped_type_t lengths_swapped;
	tstamp_scale_type_t scale_type;
};

/*
 * Check whether this is a pcap savefile and, if it is, extract the
 * relevant information from the header.
 */
pcap_t *
pcap_check_header(bpf_u_int32 magic, FILE *fp, u_int precision, char *errbuf,
		  int *err)
{
	struct pcap_file_header hdr;
	size_t amt_read;
	pcap_t *p;
	int swapped = 0;
	struct pcap_sf *ps;

	/*
	 * Assume no read errors.
	 */
	*err = 0;

	/*
	 * Check whether the first 4 bytes of the file are the magic
	 * number for a pcap savefile, or for a byte-swapped pcap
	 * savefile.
	 */
	if (magic != TCPDUMP_MAGIC && magic != KUZNETZOV_TCPDUMP_MAGIC &&
	    magic != NSEC_TCPDUMP_MAGIC) {
		magic = SWAPLONG(magic);
		if (magic != TCPDUMP_MAGIC && magic != KUZNETZOV_TCPDUMP_MAGIC &&
		    magic != NSEC_TCPDUMP_MAGIC)
			return (NULL);	/* nope */
		swapped = 1;
	}

	/*
	 * They are.  Put the magic number in the header, and read
	 * the rest of the header.
	 */
	hdr.magic = magic;
	amt_read = fread(((char *)&hdr) + sizeof hdr.magic, 1,
	    sizeof(hdr) - sizeof(hdr.magic), fp);
	if (amt_read != sizeof(hdr) - sizeof(hdr.magic)) {
		if (ferror(fp)) {
			pcap_fmt_errmsg_for_errno(errbuf, PCAP_ERRBUF_SIZE,
			    errno, "error reading dump file");
		} else {
			pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
			    "truncated dump file; tried to read %lu file header bytes, only got %lu",
			    (unsigned long)sizeof(hdr),
			    (unsigned long)amt_read);
		}
		*err = 1;
		return (NULL);
	}

	/*
	 * If it's a byte-swapped capture file, byte-swap the header.
	 */
	if (swapped) {
		hdr.version_major = SWAPSHORT(hdr.version_major);
		hdr.version_minor = SWAPSHORT(hdr.version_minor);
		hdr.thiszone = SWAPLONG(hdr.thiszone);
		hdr.sigfigs = SWAPLONG(hdr.sigfigs);
		hdr.snaplen = SWAPLONG(hdr.snaplen);
		hdr.linktype = SWAPLONG(hdr.linktype);
	}

	if (hdr.version_major < PCAP_VERSION_MAJOR) {
		pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
		    "archaic pcap savefile format");
		*err = 1;
		return (NULL);
	}

	/*
	 * currently only versions 2.[0-4] are supported with
	 * the exception of 543.0 for DG/UX tcpdump.
	 */
	if (! ((hdr.version_major == PCAP_VERSION_MAJOR &&
		hdr.version_minor <= PCAP_VERSION_MINOR) ||
	       (hdr.version_major == 543 &&
		hdr.version_minor == 0))) {
		pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
			 "unsupported pcap savefile version %u.%u",
			 hdr.version_major, hdr.version_minor);
		*err = 1;
		return NULL;
	}

	/*
	 * OK, this is a good pcap file.
	 * Allocate a pcap_t for it.
	 */
	p = pcap_open_offline_common(errbuf, sizeof (struct pcap_sf));
	if (p == NULL) {
		/* Allocation failed. */
		*err = 1;
		return (NULL);
	}
	p->swapped = swapped;
	p->version_major = hdr.version_major;
	p->version_minor = hdr.version_minor;
	p->tzoff = hdr.thiszone;
	p->snapshot = hdr.snaplen;
	if (p->snapshot <= 0) {
		/*
		 * Bogus snapshot length; use the maximum for this
		 * link-layer type as a fallback.
		 *
		 * XXX - the only reason why snapshot is signed is
		 * that pcap_snapshot() returns an int, not an
		 * unsigned int.
		 */
		p->snapshot = max_snaplen_for_dlt(hdr.linktype);
	}
	p->linktype = linktype_to_dlt(LT_LINKTYPE(hdr.linktype));
	p->linktype_ext = LT_LINKTYPE_EXT(hdr.linktype);

	p->next_packet_op = pcap_next_packet;

	ps = p->priv;

	p->opt.tstamp_precision = precision;

	/*
	 * Will we need to scale the timestamps to match what the
	 * user wants?
	 */
	switch (precision) {

	case PCAP_TSTAMP_PRECISION_MICRO:
		if (magic == NSEC_TCPDUMP_MAGIC) {
			/*
			 * The file has nanoseconds, the user
			 * wants microseconds; scale the
			 * precision down.
			 */
			ps->scale_type = SCALE_DOWN;
		} else {
			/*
			 * The file has microseconds, the
			 * user wants microseconds; nothing to do.
			 */
			ps->scale_type = PASS_THROUGH;
		}
		break;

	case PCAP_TSTAMP_PRECISION_NANO:
		if (magic == NSEC_TCPDUMP_MAGIC) {
			/*
			 * The file has nanoseconds, the
			 * user wants nanoseconds; nothing to do.
			 */
			ps->scale_type = PASS_THROUGH;
		} else {
			/*
			 * The file has microoseconds, the user
			 * wants nanoseconds; scale the
			 * precision up.
			 */
			ps->scale_type = SCALE_UP;
		}
		break;

	default:
		pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE,
		    "unknown time stamp resolution %u", precision);
		free(p);
		*err = 1;
		return (NULL);
	}

	/*
	 * We interchanged the caplen and len fields at version 2.3,
	 * in order to match the bpf header layout.  But unfortunately
	 * some files were written with version 2.3 in their headers
	 * but without the interchanged fields.
	 *
	 * In addition, DG/UX tcpdump writes out files with a version
	 * number of 543.0, and with the caplen and len fields in the
	 * pre-2.3 order.
	 */
	switch (hdr.version_major) {

	case 2:
		if (hdr.version_minor < 3)
			ps->lengths_swapped = SWAPPED;
		else if (hdr.version_minor == 3)
			ps->lengths_swapped = MAYBE_SWAPPED;
		else
			ps->lengths_swapped = NOT_SWAPPED;
		break;

	case 543:
		ps->lengths_swapped = SWAPPED;
		break;

	default:
		ps->lengths_swapped = NOT_SWAPPED;
		break;
	}

	if (magic == KUZNETZOV_TCPDUMP_MAGIC) {
		/*
		 * XXX - the patch that's in some versions of libpcap
		 * changes the packet header but not the magic number,
		 * and some other versions with this magic number have
		 * some extra debugging information in the packet header;
		 * we'd have to use some hacks^H^H^H^H^Hheuristics to
		 * detect those variants.
		 *
		 * Ethereal does that, but it does so by trying to read
		 * the first two packets of the file with each of the
		 * record header formats.  That currently means it seeks
		 * backwards and retries the reads, which doesn't work
		 * on pipes.  We want to be able to read from a pipe, so
		 * that strategy won't work; we'd have to buffer some
		 * data ourselves and read from that buffer in order to
		 * make that work.
		 */
		ps->hdrsize = sizeof(struct pcap_sf_patched_pkthdr);

		if (p->linktype == DLT_EN10MB) {
			/*
			 * This capture might have been done in raw mode
			 * or cooked mode.
			 *
			 * If it was done in cooked mode, p->snapshot was
			 * passed to recvfrom() as the buffer size, meaning
			 * that the most packet data that would be copied
			 * would be p->snapshot.  However, a faked Ethernet
			 * header would then have been added to it, so the
			 * most data that would be in a packet in the file
			 * would be p->snapshot + 14.
			 *
			 * We can't easily tell whether the capture was done
			 * in raw mode or cooked mode, so we'll assume it was
			 * cooked mode, and add 14 to the snapshot length.
			 * That means that, for a raw capture, the snapshot
			 * length will be misleading if you use it to figure
			 * out why a capture doesn't have all the packet data,
			 * but there's not much we can do to avoid that.
			 */
			p->snapshot += 14;
		}
	} else
		ps->hdrsize = sizeof(struct pcap_sf_pkthdr);

	/*
	 * Allocate a buffer for the packet data.
	 * Choose the minimum of the file's snapshot length and 2K bytes;
	 * that should be enough for most network packets - we'll grow it
	 * if necessary.  That way, we don't allocate a huge chunk of
	 * memory just because there's a huge snapshot length, as the
	 * snapshot length might be larger than the size of the largest
	 * packet.
	 */
	p->bufsize = p->snapshot;
	if (p->bufsize > 2048)
		p->bufsize = 2048;
	p->buffer = malloc(p->bufsize);
	if (p->buffer == NULL) {
		pcap_snprintf(errbuf, PCAP_ERRBUF_SIZE, "out of memory");
		free(p);
		*err = 1;
		return (NULL);
	}

	p->cleanup_op = sf_cleanup;

	return (p);
}

/*
 * Grow the packet buffer to the specified size.
 */
static int
grow_buffer(pcap_t *p, u_int bufsize)
{
	void *bigger_buffer;

	bigger_buffer = realloc(p->buffer, bufsize);
	if (bigger_buffer == NULL) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "out of memory");
		return (0);
	}
	p->buffer = bigger_buffer;
	p->bufsize = bufsize;
	return (1);
}

/*
 * Read and return the next packet from the savefile.  Return the header
 * in hdr and a pointer to the contents in data.  Return 0 on success, 1
 * if there were no more packets, and -1 on an error.
 */
static int
pcap_next_packet(pcap_t *p, struct pcap_pkthdr *hdr, u_char **data)
{
	struct pcap_sf *ps = p->priv;
	struct pcap_sf_patched_pkthdr sf_hdr;
	FILE *fp = p->rfile;
	size_t amt_read;
	bpf_u_int32 t;

	/*
	 * Read the packet header; the structure we use as a buffer
	 * is the longer structure for files generated by the patched
	 * libpcap, but if the file has the magic number for an
	 * unpatched libpcap we only read as many bytes as the regular
	 * header has.
	 */
	amt_read = fread(&sf_hdr, 1, ps->hdrsize, fp);
	if (amt_read != ps->hdrsize) {
		if (ferror(fp)) {
			pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
			    errno, "error reading dump file");
			return (-1);
		} else {
			if (amt_read != 0) {
				pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
				    "truncated dump file; tried to read %lu header bytes, only got %lu",
				    (unsigned long)ps->hdrsize,
				    (unsigned long)amt_read);
				return (-1);
			}
			/* EOF */
			return (1);
		}
	}

	if (p->swapped) {
		/* these were written in opposite byte order */
		hdr->caplen = SWAPLONG(sf_hdr.caplen);
		hdr->len = SWAPLONG(sf_hdr.len);
		hdr->ts.tv_sec = SWAPLONG(sf_hdr.ts.tv_sec);
		hdr->ts.tv_usec = SWAPLONG(sf_hdr.ts.tv_usec);
	} else {
		hdr->caplen = sf_hdr.caplen;
		hdr->len = sf_hdr.len;
		hdr->ts.tv_sec = sf_hdr.ts.tv_sec;
		hdr->ts.tv_usec = sf_hdr.ts.tv_usec;
	}

	switch (ps->scale_type) {

	case PASS_THROUGH:
		/*
		 * Just pass the time stamp through.
		 */
		break;

	case SCALE_UP:
		/*
		 * File has microseconds, user wants nanoseconds; convert
		 * it.
		 */
		hdr->ts.tv_usec = hdr->ts.tv_usec * 1000;
		break;

	case SCALE_DOWN:
		/*
		 * File has nanoseconds, user wants microseconds; convert
		 * it.
		 */
		hdr->ts.tv_usec = hdr->ts.tv_usec / 1000;
		break;
	}

	/* Swap the caplen and len fields, if necessary. */
	switch (ps->lengths_swapped) {

	case NOT_SWAPPED:
		break;

	case MAYBE_SWAPPED:
		if (hdr->caplen <= hdr->len) {
			/*
			 * The captured length is <= the actual length,
			 * so presumably they weren't swapped.
			 */
			break;
		}
		/* FALLTHROUGH */

	case SWAPPED:
		t = hdr->caplen;
		hdr->caplen = hdr->len;
		hdr->len = t;
		break;
	}

	/*
	 * Is the packet bigger than we consider sane?
	 */
	if (hdr->caplen > max_snaplen_for_dlt(p->linktype)) {
		/*
		 * Yes.  This may be a damaged or fuzzed file.
		 *
		 * Is it bigger than the snapshot length?
		 * (We don't treat that as an error if it's not
		 * bigger than the maximum we consider sane; see
		 * below.)
		 */
		if (hdr->caplen > (bpf_u_int32)p->snapshot) {
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "invalid packet capture length %u, bigger than "
			    "snaplen of %d", hdr->caplen, p->snapshot);
		} else {
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "invalid packet capture length %u, bigger than "
			    "maximum of %u", hdr->caplen,
			    max_snaplen_for_dlt(p->linktype));
		}
		return (-1);
	}

	if (hdr->caplen > (bpf_u_int32)p->snapshot) {
		/*
		 * The packet is bigger than the snapshot length
		 * for this file.
		 *
		 * This can happen due to Solaris 2.3 systems tripping
		 * over the BUFMOD problem and not setting the snapshot
		 * length correctly in the savefile header.
		 *
		 * libpcap 0.4 and later on Solaris 2.3 should set the
		 * snapshot length correctly in the pcap file header,
		 * even though they don't set a snapshot length in bufmod
		 * (the buggy bufmod chops off the *beginning* of the
		 * packet if a snapshot length is specified); they should
		 * also reduce the captured length, as supplied to the
		 * per-packet callback, to the snapshot length if it's
		 * greater than the snapshot length, so the code using
		 * libpcap should see the packet cut off at the snapshot
		 * length, even though the full packet is copied up to
		 * userland.
		 *
		 * However, perhaps some versions of libpcap failed to
		 * set the snapshot length currectly in the file header
		 * or the per-packet header, or perhaps this is a
		 * corrupted safefile or a savefile built/modified by a
		 * fuzz tester, so we check anyway.  We grow the buffer
		 * to be big enough for the snapshot length, read up
		 * to the snapshot length, discard the rest of the
		 * packet, and report the snapshot length as the captured
		 * length; we don't want to hand our caller a packet
		 * bigger than the snapshot length, because they might
		 * be assuming they'll never be handed such a packet,
		 * and might copy the packet into a snapshot-length-
		 * sized buffer, assuming it'll fit.
		 */
		size_t bytes_to_discard;
		size_t bytes_to_read, bytes_read;
		char discard_buf[4096];

		if (hdr->caplen > p->bufsize) {
			/*
			 * Grow the buffer to the snapshot length.
			 */
			if (!grow_buffer(p, p->snapshot))
				return (-1);
		}

		/*
		 * Read the first p->snapshot bytes into the buffer.
		 */
		amt_read = fread(p->buffer, 1, p->snapshot, fp);
		if (amt_read != (bpf_u_int32)p->snapshot) {
			if (ferror(fp)) {
				pcap_fmt_errmsg_for_errno(p->errbuf,
				     PCAP_ERRBUF_SIZE, errno,
				    "error reading dump file");
			} else {
				/*
				 * Yes, this uses hdr->caplen; technically,
				 * it's true, because we would try to read
				 * and discard the rest of those bytes, and
				 * that would fail because we got EOF before
				 * the read finished.
				 */
				pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
				    "truncated dump file; tried to read %u captured bytes, only got %lu",
				    p->snapshot, (unsigned long)amt_read);
			}
			return (-1);
		}

		/*
		 * Now read and discard what's left.
		 */
		bytes_to_discard = hdr->caplen - p->snapshot;
		bytes_read = amt_read;
		while (bytes_to_discard != 0) {
			bytes_to_read = bytes_to_discard;
			if (bytes_to_read > sizeof (discard_buf))
				bytes_to_read = sizeof (discard_buf);
			amt_read = fread(discard_buf, 1, bytes_to_read, fp);
			bytes_read += amt_read;
			if (amt_read != bytes_to_read) {
				if (ferror(fp)) {
					pcap_fmt_errmsg_for_errno(p->errbuf,
					    PCAP_ERRBUF_SIZE, errno,
					    "error reading dump file");
				} else {
					pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
					    "truncated dump file; tried to read %u captured bytes, only got %lu",
					    hdr->caplen, (unsigned long)bytes_read);
				}
				return (-1);
			}
			bytes_to_discard -= amt_read;
		}

		/*
		 * Adjust caplen accordingly, so we don't get confused later
		 * as to how many bytes we have to play with.
		 */
		hdr->caplen = p->snapshot;
	} else {
		if (hdr->caplen > p->bufsize) {
			/*
			 * Grow the buffer to the next power of 2, or
			 * the snaplen, whichever is lower.
			 */
			u_int new_bufsize;

			new_bufsize = hdr->caplen;
			/*
			 * http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
			 */
			new_bufsize--;
			new_bufsize |= new_bufsize >> 1;
			new_bufsize |= new_bufsize >> 2;
			new_bufsize |= new_bufsize >> 4;
			new_bufsize |= new_bufsize >> 8;
			new_bufsize |= new_bufsize >> 16;
			new_bufsize++;

			if (new_bufsize > (u_int)p->snapshot)
				new_bufsize = p->snapshot;

			if (!grow_buffer(p, new_bufsize))
				return (-1);
		}

		/* read the packet itself */
		amt_read = fread(p->buffer, 1, hdr->caplen, fp);
		if (amt_read != hdr->caplen) {
			if (ferror(fp)) {
				pcap_fmt_errmsg_for_errno(p->errbuf,
				    PCAP_ERRBUF_SIZE, errno,
				    "error reading dump file");
			} else {
				pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
				    "truncated dump file; tried to read %u captured bytes, only got %lu",
				    hdr->caplen, (unsigned long)amt_read);
			}
			return (-1);
		}
	}
	*data = p->buffer;

	if (p->swapped)
		swap_pseudo_headers(p->linktype, hdr, *data);

	return (0);
}

static int
sf_write_header(pcap_t *p, FILE *fp, int linktype, int thiszone, int snaplen)
{
	struct pcap_file_header hdr;

	hdr.magic = p->opt.tstamp_precision == PCAP_TSTAMP_PRECISION_NANO ? NSEC_TCPDUMP_MAGIC : TCPDUMP_MAGIC;
	hdr.version_major = PCAP_VERSION_MAJOR;
	hdr.version_minor = PCAP_VERSION_MINOR;

	hdr.thiszone = thiszone;
	hdr.snaplen = snaplen;
	hdr.sigfigs = 0;
	hdr.linktype = linktype;

	if (fwrite((char *)&hdr, sizeof(hdr), 1, fp) != 1)
		return (-1);

	return (0);
}

/*
 * Output a packet to the initialized dump file.
 */
void
pcap_dump(u_char *user, const struct pcap_pkthdr *h, const u_char *sp)
{
	register FILE *f;
	struct pcap_sf_pkthdr sf_hdr;

	f = (FILE *)user;
	sf_hdr.ts.tv_sec  = h->ts.tv_sec;
	sf_hdr.ts.tv_usec = h->ts.tv_usec;
	sf_hdr.caplen     = h->caplen;
	sf_hdr.len        = h->len;
	/* XXX we should check the return status */
	(void)fwrite(&sf_hdr, sizeof(sf_hdr), 1, f);
	(void)fwrite(sp, h->caplen, 1, f);
}

static pcap_dumper_t *
pcap_setup_dump(pcap_t *p, int linktype, FILE *f, const char *fname)
{

#if defined(_WIN32) || defined(MSDOS)
	/*
	 * If we're writing to the standard output, put it in binary
	 * mode, as savefiles are binary files.
	 *
	 * Otherwise, we turn off buffering.
	 * XXX - why?  And why not on the standard output?
	 */
	if (f == stdout)
		SET_BINMODE(f);
	else
		setvbuf(f, NULL, _IONBF, 0);
#endif
	if (sf_write_header(p, f, linktype, p->tzoff, p->snapshot) == -1) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "Can't write to %s", fname);
		if (f != stdout)
			(void)fclose(f);
		return (NULL);
	}
	return ((pcap_dumper_t *)f);
}

/*
 * Initialize so that sf_write() will output to the file named 'fname'.
 */
pcap_dumper_t *
pcap_dump_open(pcap_t *p, const char *fname)
{
	FILE *f;
	int linktype;

	/*
	 * If this pcap_t hasn't been activated, it doesn't have a
	 * link-layer type, so we can't use it.
	 */
	if (!p->activated) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: not-yet-activated pcap_t passed to pcap_dump_open",
		    fname);
		return (NULL);
	}
	linktype = dlt_to_linktype(p->linktype);
	if (linktype == -1) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: link-layer type %d isn't supported in savefiles",
		    fname, p->linktype);
		return (NULL);
	}
	linktype |= p->linktype_ext;

	if (fname == NULL) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "A null pointer was supplied as the file name");
		return NULL;
	}
	if (fname[0] == '-' && fname[1] == '\0') {
		f = stdout;
		fname = "standard output";
	} else {
		/*
		 * "b" is supported as of C90, so *all* UN*Xes should
		 * support it, even though it does nothing.  It's
		 * required on Windows, as the file is a binary file
		 * and must be written in binary mode.
		 */
		f = fopen(fname, "wb");
		if (f == NULL) {
			pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
			    errno, "%s", fname);
			return (NULL);
		}
	}
	return (pcap_setup_dump(p, linktype, f, fname));
}

/*
 * Initialize so that sf_write() will output to the given stream.
 */
pcap_dumper_t *
pcap_dump_fopen(pcap_t *p, FILE *f)
{
	int linktype;

	linktype = dlt_to_linktype(p->linktype);
	if (linktype == -1) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "stream: link-layer type %d isn't supported in savefiles",
		    p->linktype);
		return (NULL);
	}
	linktype |= p->linktype_ext;

	return (pcap_setup_dump(p, linktype, f, "stream"));
}

pcap_dumper_t *
pcap_dump_open_append(pcap_t *p, const char *fname)
{
	FILE *f;
	int linktype;
	size_t amt_read;
	struct pcap_file_header ph;

	linktype = dlt_to_linktype(p->linktype);
	if (linktype == -1) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "%s: link-layer type %d isn't supported in savefiles",
		    fname, linktype);
		return (NULL);
	}

	if (fname == NULL) {
		pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "A null pointer was supplied as the file name");
		return NULL;
	}
	if (fname[0] == '-' && fname[1] == '\0')
		return (pcap_setup_dump(p, linktype, stdout, "standard output"));

	/*
	 * "b" is supported as of C90, so *all* UN*Xes should support it,
	 * even though it does nothing.  It's required on Windows, as the
	 * file is a binary file and must be read in binary mode.
	 */
	f = fopen(fname, "rb+");
	if (f == NULL) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "%s", fname);
		return (NULL);
	}

	/*
	 * Try to read a pcap header.
	 */
	amt_read = fread(&ph, 1, sizeof (ph), f);
	if (amt_read != sizeof (ph)) {
		if (ferror(f)) {
			pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
			    errno, "%s", fname);
			fclose(f);
			return (NULL);
		} else if (feof(f) && amt_read > 0) {
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "%s: truncated pcap file header", fname);
			fclose(f);
			return (NULL);
		}
	}

#if defined(_WIN32) || defined(MSDOS)
	/*
	 * We turn off buffering.
	 * XXX - why?  And why not on the standard output?
	 */
	setvbuf(f, NULL, _IONBF, 0);
#endif

	/*
	 * If a header is already present and:
	 *
	 *	it's not for a pcap file of the appropriate resolution
	 *	and the right byte order for this machine;
	 *
	 *	the link-layer header types don't match;
	 *
	 *	the snapshot lengths don't match;
	 *
	 * return an error.
	 */
	if (amt_read > 0) {
		/*
		 * A header is already present.
		 * Do the checks.
		 */
		switch (ph.magic) {

		case TCPDUMP_MAGIC:
			if (p->opt.tstamp_precision != PCAP_TSTAMP_PRECISION_MICRO) {
				pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
				    "%s: different time stamp precision, cannot append to file", fname);
				fclose(f);
				return (NULL);
			}
			break;

		case NSEC_TCPDUMP_MAGIC:
			if (p->opt.tstamp_precision != PCAP_TSTAMP_PRECISION_NANO) {
				pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
				    "%s: different time stamp precision, cannot append to file", fname);
				fclose(f);
				return (NULL);
			}
			break;

		case SWAPLONG(TCPDUMP_MAGIC):
		case SWAPLONG(NSEC_TCPDUMP_MAGIC):
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "%s: different byte order, cannot append to file", fname);
			fclose(f);
			return (NULL);

		case KUZNETZOV_TCPDUMP_MAGIC:
		case SWAPLONG(KUZNETZOV_TCPDUMP_MAGIC):
		case NAVTEL_TCPDUMP_MAGIC:
		case SWAPLONG(NAVTEL_TCPDUMP_MAGIC):
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "%s: not a pcap file to which we can append", fname);
			fclose(f);
			return (NULL);

		default:
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "%s: not a pcap file", fname);
			fclose(f);
			return (NULL);
		}

		/*
		 * Good version?
		 */
		if (ph.version_major != PCAP_VERSION_MAJOR ||
		    ph.version_minor != PCAP_VERSION_MINOR) {
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "%s: version is %u.%u, cannot append to file", fname,
			    ph.version_major, ph.version_minor);
			fclose(f);
			return (NULL);
		}
		if ((bpf_u_int32)linktype != ph.linktype) {
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "%s: different linktype, cannot append to file", fname);
			fclose(f);
			return (NULL);
		}
		if ((bpf_u_int32)p->snapshot != ph.snaplen) {
			pcap_snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
			    "%s: different snaplen, cannot append to file", fname);
			fclose(f);
			return (NULL);
		}
	} else {
		/*
		 * A header isn't present; attempt to write it.
		 */
		if (sf_write_header(p, f, linktype, p->tzoff, p->snapshot) == -1) {
			pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
			    errno, "Can't write to %s", fname);
			(void)fclose(f);
			return (NULL);
		}
	}

	/*
	 * Start writing at the end of the file.
	 */
	if (fseek(f, 0, SEEK_END) == -1) {
		pcap_fmt_errmsg_for_errno(p->errbuf, PCAP_ERRBUF_SIZE,
		    errno, "Can't seek to end of %s", fname);
		(void)fclose(f);
		return (NULL);
	}
	return ((pcap_dumper_t *)f);
}

FILE *
pcap_dump_file(pcap_dumper_t *p)
{
	return ((FILE *)p);
}

long
pcap_dump_ftell(pcap_dumper_t *p)
{
	return (ftell((FILE *)p));
}

#if defined(HAVE_FSEEKO)
/*
 * We have fseeko(), so we have ftello().
 * If we have large file support (files larger than 2^31-1 bytes),
 * ftello() will give us a current file position with more than 32
 * bits.
 */
int64_t
pcap_dump_ftell64(pcap_dumper_t *p)
{
	return (ftello((FILE *)p));
}
#elif defined(_MSC_VER)
/*
 * We have Visual Studio; we support only 2005 and later, so we have
 * _ftelli64().
 */
int64_t
pcap_dump_ftell64(pcap_dumper_t *p)
{
	return (_ftelli64((FILE *)p));
}
#else
/*
 * We don't have ftello() or _ftelli64(), so fall back on ftell().
 * Either long is 64 bits, in which case ftell() should suffice,
 * or this is probably an older 32-bit UN*X without large file
 * support, which means you'll probably get errors trying to
 * write files > 2^31-1, so it won't matter anyway.
 *
 * XXX - what about MinGW?
 */
int64_t
pcap_dump_ftell64(pcap_dumper_t *p)
{
	return (ftell((FILE *)p));
}
#endif

int
pcap_dump_flush(pcap_dumper_t *p)
{

	if (fflush((FILE *)p) == EOF)
		return (-1);
	else
		return (0);
}

void
pcap_dump_close(pcap_dumper_t *p)
{

#ifdef notyet
	if (ferror((FILE *)p))
		return-an-error;
	/* XXX should check return from fclose() too */
#endif
	(void)fclose((FILE *)p);
}
