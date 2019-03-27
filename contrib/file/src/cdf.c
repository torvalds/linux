/*-
 * Copyright (c) 2008 Christos Zoulas
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/*
 * Parse Composite Document Files, the format used in Microsoft Office
 * document files before they switched to zipped XML.
 * Info from: http://sc.openoffice.org/compdocfileformat.pdf
 *
 * N.B. This is the "Composite Document File" format, and not the
 * "Compound Document Format", nor the "Channel Definition Format".
 */

#include "file.h"

#ifndef lint
FILE_RCSID("@(#)$File: cdf.c,v 1.110 2017/12/19 00:21:21 christos Exp $")
#endif

#include <assert.h>
#ifdef CDF_DEBUG
#include <err.h>
#endif
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifndef EFTYPE
#define EFTYPE EINVAL
#endif

#include "cdf.h"

#ifdef CDF_DEBUG
#define DPRINTF(a) printf a, fflush(stdout)
#else
#define DPRINTF(a)
#endif

static union {
	char s[4];
	uint32_t u;
} cdf_bo;

#define NEED_SWAP	(cdf_bo.u == (uint32_t)0x01020304)

#define CDF_TOLE8(x)	((uint64_t)(NEED_SWAP ? _cdf_tole8(x) : (uint64_t)(x)))
#define CDF_TOLE4(x)	((uint32_t)(NEED_SWAP ? _cdf_tole4(x) : (uint32_t)(x)))
#define CDF_TOLE2(x)	((uint16_t)(NEED_SWAP ? _cdf_tole2(x) : (uint16_t)(x)))
#define CDF_TOLE(x)	(/*CONSTCOND*/sizeof(x) == 2 ? \
			    CDF_TOLE2(CAST(uint16_t, x)) : \
			(/*CONSTCOND*/sizeof(x) == 4 ? \
			    CDF_TOLE4(CAST(uint32_t, x)) : \
			    CDF_TOLE8(CAST(uint64_t, x))))
#define CDF_GETUINT32(x, y)	cdf_getuint32(x, y)

#define CDF_MALLOC(n) cdf_malloc(__FILE__, __LINE__, (n))
#define CDF_REALLOC(p, n) cdf_realloc(__FILE__, __LINE__, (p), (n))
#define CDF_CALLOC(n, u) cdf_calloc(__FILE__, __LINE__, (n), (u))


/*ARGSUSED*/
static void *
cdf_malloc(const char *file __attribute__((__unused__)),
    size_t line __attribute__((__unused__)), size_t n)
{
	DPRINTF(("%s,%zu: %s %zu\n", file, line, __func__, n));
	return malloc(n);
}

/*ARGSUSED*/
static void *
cdf_realloc(const char *file __attribute__((__unused__)),
    size_t line __attribute__((__unused__)), void *p, size_t n)
{
	DPRINTF(("%s,%zu: %s %zu\n", file, line, __func__, n));
	return realloc(p, n);
}

/*ARGSUSED*/
static void *
cdf_calloc(const char *file __attribute__((__unused__)),
    size_t line __attribute__((__unused__)), size_t n, size_t u)
{
	DPRINTF(("%s,%zu: %s %zu %zu\n", file, line, __func__, n, u));
	return calloc(n, u);
}

/*
 * swap a short
 */
static uint16_t
_cdf_tole2(uint16_t sv)
{
	uint16_t rv;
	uint8_t *s = (uint8_t *)(void *)&sv;
	uint8_t *d = (uint8_t *)(void *)&rv;
	d[0] = s[1];
	d[1] = s[0];
	return rv;
}

/*
 * swap an int
 */
static uint32_t
_cdf_tole4(uint32_t sv)
{
	uint32_t rv;
	uint8_t *s = (uint8_t *)(void *)&sv;
	uint8_t *d = (uint8_t *)(void *)&rv;
	d[0] = s[3];
	d[1] = s[2];
	d[2] = s[1];
	d[3] = s[0];
	return rv;
}

/*
 * swap a quad
 */
static uint64_t
_cdf_tole8(uint64_t sv)
{
	uint64_t rv;
	uint8_t *s = (uint8_t *)(void *)&sv;
	uint8_t *d = (uint8_t *)(void *)&rv;
	d[0] = s[7];
	d[1] = s[6];
	d[2] = s[5];
	d[3] = s[4];
	d[4] = s[3];
	d[5] = s[2];
	d[6] = s[1];
	d[7] = s[0];
	return rv;
}

/*
 * grab a uint32_t from a possibly unaligned address, and return it in
 * the native host order.
 */
static uint32_t
cdf_getuint32(const uint8_t *p, size_t offs)
{
	uint32_t rv;
	(void)memcpy(&rv, p + offs * sizeof(uint32_t), sizeof(rv));
	return CDF_TOLE4(rv);
}

#define CDF_UNPACK(a)	\
    (void)memcpy(&(a), &buf[len], sizeof(a)), len += sizeof(a)
#define CDF_UNPACKA(a)	\
    (void)memcpy((a), &buf[len], sizeof(a)), len += sizeof(a)

uint16_t
cdf_tole2(uint16_t sv)
{
	return CDF_TOLE2(sv);
}

uint32_t
cdf_tole4(uint32_t sv)
{
	return CDF_TOLE4(sv);
}

uint64_t
cdf_tole8(uint64_t sv)
{
	return CDF_TOLE8(sv);
}

void
cdf_swap_header(cdf_header_t *h)
{
	size_t i;

	h->h_magic = CDF_TOLE8(h->h_magic);
	h->h_uuid[0] = CDF_TOLE8(h->h_uuid[0]);
	h->h_uuid[1] = CDF_TOLE8(h->h_uuid[1]);
	h->h_revision = CDF_TOLE2(h->h_revision);
	h->h_version = CDF_TOLE2(h->h_version);
	h->h_byte_order = CDF_TOLE2(h->h_byte_order);
	h->h_sec_size_p2 = CDF_TOLE2(h->h_sec_size_p2);
	h->h_short_sec_size_p2 = CDF_TOLE2(h->h_short_sec_size_p2);
	h->h_num_sectors_in_sat = CDF_TOLE4(h->h_num_sectors_in_sat);
	h->h_secid_first_directory = CDF_TOLE4(h->h_secid_first_directory);
	h->h_min_size_standard_stream =
	    CDF_TOLE4(h->h_min_size_standard_stream);
	h->h_secid_first_sector_in_short_sat =
	    CDF_TOLE4((uint32_t)h->h_secid_first_sector_in_short_sat);
	h->h_num_sectors_in_short_sat =
	    CDF_TOLE4(h->h_num_sectors_in_short_sat);
	h->h_secid_first_sector_in_master_sat =
	    CDF_TOLE4((uint32_t)h->h_secid_first_sector_in_master_sat);
	h->h_num_sectors_in_master_sat =
	    CDF_TOLE4(h->h_num_sectors_in_master_sat);
	for (i = 0; i < __arraycount(h->h_master_sat); i++)
		h->h_master_sat[i] = CDF_TOLE4((uint32_t)h->h_master_sat[i]);
}

void
cdf_unpack_header(cdf_header_t *h, char *buf)
{
	size_t i;
	size_t len = 0;

	CDF_UNPACK(h->h_magic);
	CDF_UNPACKA(h->h_uuid);
	CDF_UNPACK(h->h_revision);
	CDF_UNPACK(h->h_version);
	CDF_UNPACK(h->h_byte_order);
	CDF_UNPACK(h->h_sec_size_p2);
	CDF_UNPACK(h->h_short_sec_size_p2);
	CDF_UNPACKA(h->h_unused0);
	CDF_UNPACK(h->h_num_sectors_in_sat);
	CDF_UNPACK(h->h_secid_first_directory);
	CDF_UNPACKA(h->h_unused1);
	CDF_UNPACK(h->h_min_size_standard_stream);
	CDF_UNPACK(h->h_secid_first_sector_in_short_sat);
	CDF_UNPACK(h->h_num_sectors_in_short_sat);
	CDF_UNPACK(h->h_secid_first_sector_in_master_sat);
	CDF_UNPACK(h->h_num_sectors_in_master_sat);
	for (i = 0; i < __arraycount(h->h_master_sat); i++)
		CDF_UNPACK(h->h_master_sat[i]);
}

void
cdf_swap_dir(cdf_directory_t *d)
{
	d->d_namelen = CDF_TOLE2(d->d_namelen);
	d->d_left_child = CDF_TOLE4((uint32_t)d->d_left_child);
	d->d_right_child = CDF_TOLE4((uint32_t)d->d_right_child);
	d->d_storage = CDF_TOLE4((uint32_t)d->d_storage);
	d->d_storage_uuid[0] = CDF_TOLE8(d->d_storage_uuid[0]);
	d->d_storage_uuid[1] = CDF_TOLE8(d->d_storage_uuid[1]);
	d->d_flags = CDF_TOLE4(d->d_flags);
	d->d_created = CDF_TOLE8((uint64_t)d->d_created);
	d->d_modified = CDF_TOLE8((uint64_t)d->d_modified);
	d->d_stream_first_sector = CDF_TOLE4((uint32_t)d->d_stream_first_sector);
	d->d_size = CDF_TOLE4(d->d_size);
}

void
cdf_swap_class(cdf_classid_t *d)
{
	d->cl_dword = CDF_TOLE4(d->cl_dword);
	d->cl_word[0] = CDF_TOLE2(d->cl_word[0]);
	d->cl_word[1] = CDF_TOLE2(d->cl_word[1]);
}

void
cdf_unpack_dir(cdf_directory_t *d, char *buf)
{
	size_t len = 0;

	CDF_UNPACKA(d->d_name);
	CDF_UNPACK(d->d_namelen);
	CDF_UNPACK(d->d_type);
	CDF_UNPACK(d->d_color);
	CDF_UNPACK(d->d_left_child);
	CDF_UNPACK(d->d_right_child);
	CDF_UNPACK(d->d_storage);
	CDF_UNPACKA(d->d_storage_uuid);
	CDF_UNPACK(d->d_flags);
	CDF_UNPACK(d->d_created);
	CDF_UNPACK(d->d_modified);
	CDF_UNPACK(d->d_stream_first_sector);
	CDF_UNPACK(d->d_size);
	CDF_UNPACK(d->d_unused0);
}

int
cdf_zero_stream(cdf_stream_t *scn)
{
	scn->sst_len = 0;
	scn->sst_dirlen = 0;
	scn->sst_ss = 0;
	free(scn->sst_tab);
	scn->sst_tab = NULL;
	return -1;
}

static size_t
cdf_check_stream(const cdf_stream_t *sst, const cdf_header_t *h)
{
	size_t ss = sst->sst_dirlen < h->h_min_size_standard_stream ?
	    CDF_SHORT_SEC_SIZE(h) : CDF_SEC_SIZE(h);
	assert(ss == sst->sst_ss);
	return sst->sst_ss;
}

static int
cdf_check_stream_offset(const cdf_stream_t *sst, const cdf_header_t *h,
    const void *p, size_t tail, int line)
{
	const char *b = (const char *)sst->sst_tab;
	const char *e = ((const char *)p) + tail;
	size_t ss = cdf_check_stream(sst, h);
	/*LINTED*/(void)&line;
	if (e >= b && (size_t)(e - b) <= ss * sst->sst_len)
		return 0;
	DPRINTF(("%d: offset begin %p < end %p || %" SIZE_T_FORMAT "u"
	    " > %" SIZE_T_FORMAT "u [%" SIZE_T_FORMAT "u %"
	    SIZE_T_FORMAT "u]\n", line, b, e, (size_t)(e - b),
	    ss * sst->sst_len, ss, sst->sst_len));
	errno = EFTYPE;
	return -1;
}

static ssize_t
cdf_read(const cdf_info_t *info, off_t off, void *buf, size_t len)
{
	size_t siz = (size_t)off + len;

	if ((off_t)(off + len) != (off_t)siz)
		goto out;

	if (info->i_buf != NULL && info->i_len >= siz) {
		(void)memcpy(buf, &info->i_buf[off], len);
		return (ssize_t)len;
	}

	if (info->i_fd == -1)
		goto out;

	if (pread(info->i_fd, buf, len, off) != (ssize_t)len)
		return -1;

	return (ssize_t)len;
out:
	errno = EINVAL;
	return -1;
}

int
cdf_read_header(const cdf_info_t *info, cdf_header_t *h)
{
	char buf[512];

	(void)memcpy(cdf_bo.s, "\01\02\03\04", 4);
	if (cdf_read(info, (off_t)0, buf, sizeof(buf)) == -1)
		return -1;
	cdf_unpack_header(h, buf);
	cdf_swap_header(h);
	if (h->h_magic != CDF_MAGIC) {
		DPRINTF(("Bad magic %#" INT64_T_FORMAT "x != %#"
		    INT64_T_FORMAT "x\n",
		    (unsigned long long)h->h_magic,
		    (unsigned long long)CDF_MAGIC));
		goto out;
	}
	if (h->h_sec_size_p2 > 20) {
		DPRINTF(("Bad sector size %hu\n", h->h_sec_size_p2));
		goto out;
	}
	if (h->h_short_sec_size_p2 > 20) {
		DPRINTF(("Bad short sector size %hu\n",
		    h->h_short_sec_size_p2));
		goto out;
	}
	return 0;
out:
	errno = EFTYPE;
	return -1;
}


ssize_t
cdf_read_sector(const cdf_info_t *info, void *buf, size_t offs, size_t len,
    const cdf_header_t *h, cdf_secid_t id)
{
	size_t ss = CDF_SEC_SIZE(h);
	size_t pos = CDF_SEC_POS(h, id);
	assert(ss == len);
	return cdf_read(info, (off_t)pos, ((char *)buf) + offs, len);
}

ssize_t
cdf_read_short_sector(const cdf_stream_t *sst, void *buf, size_t offs,
    size_t len, const cdf_header_t *h, cdf_secid_t id)
{
	size_t ss = CDF_SHORT_SEC_SIZE(h);
	size_t pos = CDF_SHORT_SEC_POS(h, id);
	assert(ss == len);
	if (pos + len > CDF_SEC_SIZE(h) * sst->sst_len) {
		DPRINTF(("Out of bounds read %" SIZE_T_FORMAT "u > %"
		    SIZE_T_FORMAT "u\n",
		    pos + len, CDF_SEC_SIZE(h) * sst->sst_len));
		goto out;
	}
	(void)memcpy(((char *)buf) + offs,
	    ((const char *)sst->sst_tab) + pos, len);
	return len;
out:
	errno = EFTYPE;
	return -1;
}

/*
 * Read the sector allocation table.
 */
int
cdf_read_sat(const cdf_info_t *info, cdf_header_t *h, cdf_sat_t *sat)
{
	size_t i, j, k;
	size_t ss = CDF_SEC_SIZE(h);
	cdf_secid_t *msa, mid, sec;
	size_t nsatpersec = (ss / sizeof(mid)) - 1;

	for (i = 0; i < __arraycount(h->h_master_sat); i++)
		if (h->h_master_sat[i] == CDF_SECID_FREE)
			break;

#define CDF_SEC_LIMIT (UINT32_MAX / (64 * ss))
	if ((nsatpersec > 0 &&
	    h->h_num_sectors_in_master_sat > CDF_SEC_LIMIT / nsatpersec) ||
	    i > CDF_SEC_LIMIT) {
		DPRINTF(("Number of sectors in master SAT too big %u %"
		    SIZE_T_FORMAT "u\n", h->h_num_sectors_in_master_sat, i));
		errno = EFTYPE;
		return -1;
	}

	sat->sat_len = h->h_num_sectors_in_master_sat * nsatpersec + i;
	DPRINTF(("sat_len = %" SIZE_T_FORMAT "u ss = %" SIZE_T_FORMAT "u\n",
	    sat->sat_len, ss));
	if ((sat->sat_tab = CAST(cdf_secid_t *, CDF_CALLOC(sat->sat_len, ss)))
	    == NULL)
		return -1;

	for (i = 0; i < __arraycount(h->h_master_sat); i++) {
		if (h->h_master_sat[i] < 0)
			break;
		if (cdf_read_sector(info, sat->sat_tab, ss * i, ss, h,
		    h->h_master_sat[i]) != (ssize_t)ss) {
			DPRINTF(("Reading sector %d", h->h_master_sat[i]));
			goto out1;
		}
	}

	if ((msa = CAST(cdf_secid_t *, CDF_CALLOC(1, ss))) == NULL)
		goto out1;

	mid = h->h_secid_first_sector_in_master_sat;
	for (j = 0; j < h->h_num_sectors_in_master_sat; j++) {
		if (mid < 0)
			goto out;
		if (j >= CDF_LOOP_LIMIT) {
			DPRINTF(("Reading master sector loop limit"));
			goto out3;
		}
		if (cdf_read_sector(info, msa, 0, ss, h, mid) != (ssize_t)ss) {
			DPRINTF(("Reading master sector %d", mid));
			goto out2;
		}
		for (k = 0; k < nsatpersec; k++, i++) {
			sec = CDF_TOLE4((uint32_t)msa[k]);
			if (sec < 0)
				goto out;
			if (i >= sat->sat_len) {
			    DPRINTF(("Out of bounds reading MSA %" SIZE_T_FORMAT
				"u >= %" SIZE_T_FORMAT "u", i, sat->sat_len));
			    goto out3;
			}
			if (cdf_read_sector(info, sat->sat_tab, ss * i, ss, h,
			    sec) != (ssize_t)ss) {
				DPRINTF(("Reading sector %d",
				    CDF_TOLE4(msa[k])));
				goto out2;
			}
		}
		mid = CDF_TOLE4((uint32_t)msa[nsatpersec]);
	}
out:
	sat->sat_len = i;
	free(msa);
	return 0;
out3:
	errno = EFTYPE;
out2:
	free(msa);
out1:
	free(sat->sat_tab);
	return -1;
}

size_t
cdf_count_chain(const cdf_sat_t *sat, cdf_secid_t sid, size_t size)
{
	size_t i, j;
	cdf_secid_t maxsector = (cdf_secid_t)((sat->sat_len * size)
	    / sizeof(maxsector));

	DPRINTF(("Chain:"));
	if (sid == CDF_SECID_END_OF_CHAIN) {
		/* 0-length chain. */
		DPRINTF((" empty\n"));
		return 0;
	}

	for (j = i = 0; sid >= 0; i++, j++) {
		DPRINTF((" %d", sid));
		if (j >= CDF_LOOP_LIMIT) {
			DPRINTF(("Counting chain loop limit"));
			goto out;
		}
		if (sid >= maxsector) {
			DPRINTF(("Sector %d >= %d\n", sid, maxsector));
			goto out;
		}
		sid = CDF_TOLE4((uint32_t)sat->sat_tab[sid]);
	}
	if (i == 0) {
		DPRINTF((" none, sid: %d\n", sid));
		goto out;

	}
	DPRINTF(("\n"));
	return i;
out:
	errno = EFTYPE;
	return (size_t)-1;
}

int
cdf_read_long_sector_chain(const cdf_info_t *info, const cdf_header_t *h,
    const cdf_sat_t *sat, cdf_secid_t sid, size_t len, cdf_stream_t *scn)
{
	size_t ss = CDF_SEC_SIZE(h), i, j;
	ssize_t nr;
	scn->sst_tab = NULL;
	scn->sst_len = cdf_count_chain(sat, sid, ss);
	scn->sst_dirlen = MAX(h->h_min_size_standard_stream, len);
	scn->sst_ss = ss;

	if (sid == CDF_SECID_END_OF_CHAIN || len == 0)
		return cdf_zero_stream(scn);

	if (scn->sst_len == (size_t)-1)
		goto out;

	scn->sst_tab = CDF_CALLOC(scn->sst_len, ss);
	if (scn->sst_tab == NULL)
		return cdf_zero_stream(scn);

	for (j = i = 0; sid >= 0; i++, j++) {
		if (j >= CDF_LOOP_LIMIT) {
			DPRINTF(("Read long sector chain loop limit"));
			goto out;
		}
		if (i >= scn->sst_len) {
			DPRINTF(("Out of bounds reading long sector chain "
			    "%" SIZE_T_FORMAT "u > %" SIZE_T_FORMAT "u\n", i,
			    scn->sst_len));
			goto out;
		}
		if ((nr = cdf_read_sector(info, scn->sst_tab, i * ss, ss, h,
		    sid)) != (ssize_t)ss) {
			if (i == scn->sst_len - 1 && nr > 0) {
				/* Last sector might be truncated */
				return 0;
			}
			DPRINTF(("Reading long sector chain %d", sid));
			goto out;
		}
		sid = CDF_TOLE4((uint32_t)sat->sat_tab[sid]);
	}
	return 0;
out:
	errno = EFTYPE;
	return cdf_zero_stream(scn);
}

int
cdf_read_short_sector_chain(const cdf_header_t *h,
    const cdf_sat_t *ssat, const cdf_stream_t *sst,
    cdf_secid_t sid, size_t len, cdf_stream_t *scn)
{
	size_t ss = CDF_SHORT_SEC_SIZE(h), i, j;
	scn->sst_tab = NULL;
	scn->sst_len = cdf_count_chain(ssat, sid, CDF_SEC_SIZE(h));
	scn->sst_dirlen = len;
	scn->sst_ss = ss;

	if (scn->sst_len == (size_t)-1)
		goto out;

	scn->sst_tab = CDF_CALLOC(scn->sst_len, ss);
	if (scn->sst_tab == NULL)
		return cdf_zero_stream(scn);

	for (j = i = 0; sid >= 0; i++, j++) {
		if (j >= CDF_LOOP_LIMIT) {
			DPRINTF(("Read short sector chain loop limit"));
			goto out;
		}
		if (i >= scn->sst_len) {
			DPRINTF(("Out of bounds reading short sector chain "
			    "%" SIZE_T_FORMAT "u > %" SIZE_T_FORMAT "u\n",
			    i, scn->sst_len));
			goto out;
		}
		if (cdf_read_short_sector(sst, scn->sst_tab, i * ss, ss, h,
		    sid) != (ssize_t)ss) {
			DPRINTF(("Reading short sector chain %d", sid));
			goto out;
		}
		sid = CDF_TOLE4((uint32_t)ssat->sat_tab[sid]);
	}
	return 0;
out:
	errno = EFTYPE;
	return cdf_zero_stream(scn);
}

int
cdf_read_sector_chain(const cdf_info_t *info, const cdf_header_t *h,
    const cdf_sat_t *sat, const cdf_sat_t *ssat, const cdf_stream_t *sst,
    cdf_secid_t sid, size_t len, cdf_stream_t *scn)
{

	if (len < h->h_min_size_standard_stream && sst->sst_tab != NULL)
		return cdf_read_short_sector_chain(h, ssat, sst, sid, len,
		    scn);
	else
		return cdf_read_long_sector_chain(info, h, sat, sid, len, scn);
}

int
cdf_read_dir(const cdf_info_t *info, const cdf_header_t *h,
    const cdf_sat_t *sat, cdf_dir_t *dir)
{
	size_t i, j;
	size_t ss = CDF_SEC_SIZE(h), ns, nd;
	char *buf;
	cdf_secid_t sid = h->h_secid_first_directory;

	ns = cdf_count_chain(sat, sid, ss);
	if (ns == (size_t)-1)
		return -1;

	nd = ss / CDF_DIRECTORY_SIZE;

	dir->dir_len = ns * nd;
	dir->dir_tab = CAST(cdf_directory_t *,
	    CDF_CALLOC(dir->dir_len, sizeof(dir->dir_tab[0])));
	if (dir->dir_tab == NULL)
		return -1;

	if ((buf = CAST(char *, CDF_MALLOC(ss))) == NULL) {
		free(dir->dir_tab);
		return -1;
	}

	for (j = i = 0; i < ns; i++, j++) {
		if (j >= CDF_LOOP_LIMIT) {
			DPRINTF(("Read dir loop limit"));
			goto out;
		}
		if (cdf_read_sector(info, buf, 0, ss, h, sid) != (ssize_t)ss) {
			DPRINTF(("Reading directory sector %d", sid));
			goto out;
		}
		for (j = 0; j < nd; j++) {
			cdf_unpack_dir(&dir->dir_tab[i * nd + j],
			    &buf[j * CDF_DIRECTORY_SIZE]);
		}
		sid = CDF_TOLE4((uint32_t)sat->sat_tab[sid]);
	}
	if (NEED_SWAP)
		for (i = 0; i < dir->dir_len; i++)
			cdf_swap_dir(&dir->dir_tab[i]);
	free(buf);
	return 0;
out:
	free(dir->dir_tab);
	free(buf);
	errno = EFTYPE;
	return -1;
}


int
cdf_read_ssat(const cdf_info_t *info, const cdf_header_t *h,
    const cdf_sat_t *sat, cdf_sat_t *ssat)
{
	size_t i, j;
	size_t ss = CDF_SEC_SIZE(h);
	cdf_secid_t sid = h->h_secid_first_sector_in_short_sat;

	ssat->sat_tab = NULL;
	ssat->sat_len = cdf_count_chain(sat, sid, ss);
	if (ssat->sat_len == (size_t)-1)
		goto out;

	ssat->sat_tab = CAST(cdf_secid_t *, CDF_CALLOC(ssat->sat_len, ss));
	if (ssat->sat_tab == NULL)
		goto out1;

	for (j = i = 0; sid >= 0; i++, j++) {
		if (j >= CDF_LOOP_LIMIT) {
			DPRINTF(("Read short sat sector loop limit"));
			goto out;
		}
		if (i >= ssat->sat_len) {
			DPRINTF(("Out of bounds reading short sector chain "
			    "%" SIZE_T_FORMAT "u > %" SIZE_T_FORMAT "u\n", i,
			    ssat->sat_len));
			goto out;
		}
		if (cdf_read_sector(info, ssat->sat_tab, i * ss, ss, h, sid) !=
		    (ssize_t)ss) {
			DPRINTF(("Reading short sat sector %d", sid));
			goto out1;
		}
		sid = CDF_TOLE4((uint32_t)sat->sat_tab[sid]);
	}
	return 0;
out:
	errno = EFTYPE;
out1:
	free(ssat->sat_tab);
	return -1;
}

int
cdf_read_short_stream(const cdf_info_t *info, const cdf_header_t *h,
    const cdf_sat_t *sat, const cdf_dir_t *dir, cdf_stream_t *scn,
    const cdf_directory_t **root)
{
	size_t i;
	const cdf_directory_t *d;

	*root = NULL;
	for (i = 0; i < dir->dir_len; i++)
		if (dir->dir_tab[i].d_type == CDF_DIR_TYPE_ROOT_STORAGE)
			break;

	/* If the it is not there, just fake it; some docs don't have it */
	if (i == dir->dir_len) {
		DPRINTF(("Cannot find root storage dir\n"));
		goto out;
	}
	d = &dir->dir_tab[i];
	*root = d;

	/* If the it is not there, just fake it; some docs don't have it */
	if (d->d_stream_first_sector < 0) {
		DPRINTF(("No first secror in dir\n"));
		goto out;
	}

	return cdf_read_long_sector_chain(info, h, sat,
	    d->d_stream_first_sector, d->d_size, scn);
out:
	scn->sst_tab = NULL;
	(void)cdf_zero_stream(scn);
	return 0;
}

static int
cdf_namecmp(const char *d, const uint16_t *s, size_t l)
{
	for (; l--; d++, s++)
		if (*d != CDF_TOLE2(*s))
			return (unsigned char)*d - CDF_TOLE2(*s);
	return 0;
}

int
cdf_read_doc_summary_info(const cdf_info_t *info, const cdf_header_t *h,
    const cdf_sat_t *sat, const cdf_sat_t *ssat, const cdf_stream_t *sst,
    const cdf_dir_t *dir, cdf_stream_t *scn)
{
	return cdf_read_user_stream(info, h, sat, ssat, sst, dir,
	    "\05DocumentSummaryInformation", scn);
}

int
cdf_read_summary_info(const cdf_info_t *info, const cdf_header_t *h,
    const cdf_sat_t *sat, const cdf_sat_t *ssat, const cdf_stream_t *sst,
    const cdf_dir_t *dir, cdf_stream_t *scn)
{
	return cdf_read_user_stream(info, h, sat, ssat, sst, dir,
	    "\05SummaryInformation", scn);
}

int
cdf_read_user_stream(const cdf_info_t *info, const cdf_header_t *h,
    const cdf_sat_t *sat, const cdf_sat_t *ssat, const cdf_stream_t *sst,
    const cdf_dir_t *dir, const char *name, cdf_stream_t *scn)
{
	const cdf_directory_t *d;
	int i = cdf_find_stream(dir, name, CDF_DIR_TYPE_USER_STREAM);

	if (i <= 0) {
		memset(scn, 0, sizeof(*scn));
		return -1;
	}

	d = &dir->dir_tab[i - 1];
	return cdf_read_sector_chain(info, h, sat, ssat, sst,
	    d->d_stream_first_sector, d->d_size, scn);
}

int
cdf_find_stream(const cdf_dir_t *dir, const char *name, int type)
{
	size_t i, name_len = strlen(name) + 1;

	for (i = dir->dir_len; i > 0; i--)
		if (dir->dir_tab[i - 1].d_type == type &&
		    cdf_namecmp(name, dir->dir_tab[i - 1].d_name, name_len)
		    == 0)
			break;
	if (i > 0)
		return CAST(int, i);

	DPRINTF(("Cannot find type %d `%s'\n", type, name));
	errno = ESRCH;
	return 0;
}

#define CDF_SHLEN_LIMIT (UINT32_MAX / 64)
#define CDF_PROP_LIMIT (UINT32_MAX / (64 * sizeof(cdf_property_info_t)))

static const void *
cdf_offset(const void *p, size_t l)
{
	return CAST(const void *, CAST(const uint8_t *, p) + l);
}

static const uint8_t *
cdf_get_property_info_pos(const cdf_stream_t *sst, const cdf_header_t *h, 
    const uint8_t *p, const uint8_t *e, size_t i)
{
	size_t tail = (i << 1) + 1;
	size_t ofs;
	const uint8_t *q;

	if (p >= e) {
		DPRINTF(("Past end %p < %p\n", e, p));
		return NULL;
	}
	if (cdf_check_stream_offset(sst, h, p, (tail + 1) * sizeof(uint32_t),
	    __LINE__) == -1)
		return NULL;
	ofs = CDF_GETUINT32(p, tail);
	q = CAST(const uint8_t *, cdf_offset(CAST(const void *, p), 
	    ofs - 2 * sizeof(uint32_t)));

	if (q < p) {
		DPRINTF(("Wrapped around %p < %p\n", q, p));
		return NULL;
	}

	if (q >= e) {
		DPRINTF(("Ran off the end %p >= %p\n", q, e));
		return NULL;
	}
	return q;
}

static cdf_property_info_t *
cdf_grow_info(cdf_property_info_t **info, size_t *maxcount, size_t incr)
{
	cdf_property_info_t *inp;
	size_t newcount = *maxcount + incr;

	if (newcount > CDF_PROP_LIMIT) {
		DPRINTF(("exceeded property limit %zu > %zu\n", 
		    newcount, CDF_PROP_LIMIT));
		goto out;
	}
	inp = CAST(cdf_property_info_t *,
	    CDF_REALLOC(*info, newcount * sizeof(*inp)));
	if (inp == NULL)
		goto out;

	*info = inp;
	*maxcount = newcount;
	return inp;
out:
	free(*info);
	*maxcount = 0;
	*info = NULL;
	return NULL;
}

static int
cdf_copy_info(cdf_property_info_t *inp, const void *p, const void *e,
    size_t len)
{
	if (inp->pi_type & CDF_VECTOR)
		return 0;

	if ((size_t)(CAST(const char *, e) - CAST(const char *, p)) < len)
		return 0;

	(void)memcpy(&inp->pi_val, p, len);

	switch (len) {
	case 2:
		inp->pi_u16 = CDF_TOLE2(inp->pi_u16);
		break;
	case 4:
		inp->pi_u32 = CDF_TOLE4(inp->pi_u32);
		break;
	case 8:
		inp->pi_u64 = CDF_TOLE8(inp->pi_u64);
		break;
	default:
		abort();
	}
	return 1;
}

int
cdf_read_property_info(const cdf_stream_t *sst, const cdf_header_t *h,
    uint32_t offs, cdf_property_info_t **info, size_t *count, size_t *maxcount)
{
	const cdf_section_header_t *shp;
	cdf_section_header_t sh;
	const uint8_t *p, *q, *e;
	size_t i, o4, nelements, j, slen, left;
	cdf_property_info_t *inp;

	if (offs > UINT32_MAX / 4) {
		errno = EFTYPE;
		goto out;
	}
	shp = CAST(const cdf_section_header_t *,
	    cdf_offset(sst->sst_tab, offs));
	if (cdf_check_stream_offset(sst, h, shp, sizeof(*shp), __LINE__) == -1)
		goto out;
	sh.sh_len = CDF_TOLE4(shp->sh_len);
	if (sh.sh_len > CDF_SHLEN_LIMIT) {
		errno = EFTYPE;
		goto out;
	}

	if (cdf_check_stream_offset(sst, h, shp, sh.sh_len, __LINE__) == -1)
		goto out;

	sh.sh_properties = CDF_TOLE4(shp->sh_properties);
	DPRINTF(("section len: %u properties %u\n", sh.sh_len,
	    sh.sh_properties));
	if (sh.sh_properties > CDF_PROP_LIMIT)
		goto out;
	inp = cdf_grow_info(info, maxcount, sh.sh_properties);
	if (inp == NULL)
		goto out;
	inp += *count;
	*count += sh.sh_properties;
	p = CAST(const uint8_t *, cdf_offset(sst->sst_tab, offs + sizeof(sh)));
	e = CAST(const uint8_t *, cdf_offset(shp, sh.sh_len));
	if (p >= e || cdf_check_stream_offset(sst, h, e, 0, __LINE__) == -1)
		goto out;

	for (i = 0; i < sh.sh_properties; i++) {
		if ((q = cdf_get_property_info_pos(sst, h, p, e, i)) == NULL)
			goto out;
		inp[i].pi_id = CDF_GETUINT32(p, i << 1);
		left = CAST(size_t, e - q);
		if (left < sizeof(uint32_t)) {
			DPRINTF(("short info (no type)_\n"));
			goto out;
		}
		inp[i].pi_type = CDF_GETUINT32(q, 0);
		DPRINTF(("%" SIZE_T_FORMAT "u) id=%#x type=%#x offs=%#tx,%#x\n",
		    i, inp[i].pi_id, inp[i].pi_type, q - p, offs));
		if (inp[i].pi_type & CDF_VECTOR) {
			if (left < sizeof(uint32_t) * 2) {
				DPRINTF(("missing CDF_VECTOR length\n"));
				goto out;
			}
			nelements = CDF_GETUINT32(q, 1);
			if (nelements == 0) {
				DPRINTF(("CDF_VECTOR with nelements == 0\n"));
				goto out;
			}
			slen = 2;
		} else {
			nelements = 1;
			slen = 1;
		}
		o4 = slen * sizeof(uint32_t);
		if (inp[i].pi_type & (CDF_ARRAY|CDF_BYREF|CDF_RESERVED))
			goto unknown;
		switch (inp[i].pi_type & CDF_TYPEMASK) {
		case CDF_NULL:
		case CDF_EMPTY:
			break;
		case CDF_SIGNED16:
			if (!cdf_copy_info(&inp[i], &q[o4], e, sizeof(int16_t)))
				goto unknown;
			break;
		case CDF_SIGNED32:
		case CDF_BOOL:
		case CDF_UNSIGNED32:
		case CDF_FLOAT:
			if (!cdf_copy_info(&inp[i], &q[o4], e, sizeof(int32_t)))
				goto unknown;
			break;
		case CDF_SIGNED64:
		case CDF_UNSIGNED64:
		case CDF_DOUBLE:
		case CDF_FILETIME:
			if (!cdf_copy_info(&inp[i], &q[o4], e, sizeof(int64_t)))
				goto unknown;
			break;
		case CDF_LENGTH32_STRING:
		case CDF_LENGTH32_WSTRING:
			if (nelements > 1) {
				size_t nelem = inp - *info;
				inp = cdf_grow_info(info, maxcount, nelements);
				if (inp == NULL)
					goto out;
				inp += nelem;
			}
			DPRINTF(("nelements = %" SIZE_T_FORMAT "u\n",
			    nelements));
			for (j = 0; j < nelements && i < sh.sh_properties;
			    j++, i++)
			{
				uint32_t l;

				if (o4 + sizeof(uint32_t) > left)
					goto out;

				l = CDF_GETUINT32(q, slen);
				o4 += sizeof(uint32_t);
				if (o4 + l > left)
					goto out;

				inp[i].pi_str.s_len = l;
				inp[i].pi_str.s_buf = CAST(const char *,
				    CAST(const void *, &q[o4]));

				DPRINTF(("o=%zu l=%d(%" SIZE_T_FORMAT
				    "u), t=%zu s=%s\n", o4, l,
				    CDF_ROUND(l, sizeof(l)), left,
				    inp[i].pi_str.s_buf));

				if (l & 1)
					l++;

				slen += l >> 1;
				o4 = slen * sizeof(uint32_t);
			}
			i--;
			break;
		case CDF_CLIPBOARD:
			if (inp[i].pi_type & CDF_VECTOR)
				goto unknown;
			break;
		default:
		unknown:
			memset(&inp[i].pi_val, 0, sizeof(inp[i].pi_val));
			DPRINTF(("Don't know how to deal with %#x\n",
			    inp[i].pi_type));
			break;
		}
	}
	return 0;
out:
	free(*info);
	*info = NULL;
	*count = 0;
	*maxcount = 0;
	errno = EFTYPE;
	return -1;
}

int
cdf_unpack_summary_info(const cdf_stream_t *sst, const cdf_header_t *h,
    cdf_summary_info_header_t *ssi, cdf_property_info_t **info, size_t *count)
{
	size_t maxcount;
	const cdf_summary_info_header_t *si =
	    CAST(const cdf_summary_info_header_t *, sst->sst_tab);
	const cdf_section_declaration_t *sd =
	    CAST(const cdf_section_declaration_t *, (const void *)
	    ((const char *)sst->sst_tab + CDF_SECTION_DECLARATION_OFFSET));

	if (cdf_check_stream_offset(sst, h, si, sizeof(*si), __LINE__) == -1 ||
	    cdf_check_stream_offset(sst, h, sd, sizeof(*sd), __LINE__) == -1)
		return -1;
	ssi->si_byte_order = CDF_TOLE2(si->si_byte_order);
	ssi->si_os_version = CDF_TOLE2(si->si_os_version);
	ssi->si_os = CDF_TOLE2(si->si_os);
	ssi->si_class = si->si_class;
	cdf_swap_class(&ssi->si_class);
	ssi->si_count = CDF_TOLE4(si->si_count);
	*count = 0;
	maxcount = 0;
	*info = NULL;
	if (cdf_read_property_info(sst, h, CDF_TOLE4(sd->sd_offset), info,
	    count, &maxcount) == -1)
		return -1;
	return 0;
}


#define extract_catalog_field(t, f, l) \
    if (b + l + sizeof(cep->f) > eb) { \
	    cep->ce_namlen = 0; \
	    break; \
    } \
    memcpy(&cep->f, b + (l), sizeof(cep->f)); \
    ce[i].f = CAST(t, CDF_TOLE(cep->f))

int
cdf_unpack_catalog(const cdf_header_t *h, const cdf_stream_t *sst,
    cdf_catalog_t **cat)
{
	size_t ss = cdf_check_stream(sst, h);
	const char *b = CAST(const char *, sst->sst_tab);
	const char *nb, *eb = b + ss * sst->sst_len;
	size_t nr, i, j, k;
	cdf_catalog_entry_t *ce;
	uint16_t reclen;
	const uint16_t *np;

	for (nr = 0;; nr++) {
		memcpy(&reclen, b, sizeof(reclen));
		reclen = CDF_TOLE2(reclen);
		if (reclen == 0)
			break;
		b += reclen;
		if (b > eb)
		    break;
	}
	if (nr == 0)
		return -1;
	nr--;
	*cat = CAST(cdf_catalog_t *,
	    CDF_MALLOC(sizeof(cdf_catalog_t) + nr * sizeof(*ce)));
	if (*cat == NULL)
		return -1;
	ce = (*cat)->cat_e;
	memset(ce, 0, nr * sizeof(*ce));
	b = CAST(const char *, sst->sst_tab);
	for (j = i = 0; i < nr; b += reclen) {
		cdf_catalog_entry_t *cep = &ce[j];
		uint16_t rlen;

		extract_catalog_field(uint16_t, ce_namlen, 0);
		extract_catalog_field(uint16_t, ce_num, 4);
		extract_catalog_field(uint64_t, ce_timestamp, 8);
		reclen = cep->ce_namlen;

		if (reclen < 14) {
			cep->ce_namlen = 0;
			continue;
		}

		cep->ce_namlen = __arraycount(cep->ce_name) - 1;
		rlen = reclen - 14;
		if (cep->ce_namlen > rlen)
			cep->ce_namlen = rlen;

		np = CAST(const uint16_t *, CAST(const void *, (b + 16)));
		nb = CAST(const char *, CAST(const void *,
		    (np + cep->ce_namlen)));
		if (nb > eb) {
			cep->ce_namlen = 0;
			break;
		}

		for (k = 0; k < cep->ce_namlen; k++)
			cep->ce_name[k] = np[k]; /* XXX: CDF_TOLE2? */
		cep->ce_name[cep->ce_namlen] = 0;
		j = i;
		i++;
	}
	(*cat)->cat_num = j;
	return 0;
}

int
cdf_print_classid(char *buf, size_t buflen, const cdf_classid_t *id)
{
	return snprintf(buf, buflen, "%.8x-%.4x-%.4x-%.2x%.2x-"
	    "%.2x%.2x%.2x%.2x%.2x%.2x", id->cl_dword, id->cl_word[0],
	    id->cl_word[1], id->cl_two[0], id->cl_two[1], id->cl_six[0],
	    id->cl_six[1], id->cl_six[2], id->cl_six[3], id->cl_six[4],
	    id->cl_six[5]);
}

static const struct {
	uint32_t v;
	const char *n;
} vn[] = {
	{ CDF_PROPERTY_CODE_PAGE, "Code page" },
	{ CDF_PROPERTY_TITLE, "Title" },
	{ CDF_PROPERTY_SUBJECT, "Subject" },
	{ CDF_PROPERTY_AUTHOR, "Author" },
	{ CDF_PROPERTY_KEYWORDS, "Keywords" },
	{ CDF_PROPERTY_COMMENTS, "Comments" },
	{ CDF_PROPERTY_TEMPLATE, "Template" },
	{ CDF_PROPERTY_LAST_SAVED_BY, "Last Saved By" },
	{ CDF_PROPERTY_REVISION_NUMBER, "Revision Number" },
	{ CDF_PROPERTY_TOTAL_EDITING_TIME, "Total Editing Time" },
	{ CDF_PROPERTY_LAST_PRINTED, "Last Printed" },
	{ CDF_PROPERTY_CREATE_TIME, "Create Time/Date" },
	{ CDF_PROPERTY_LAST_SAVED_TIME, "Last Saved Time/Date" },
	{ CDF_PROPERTY_NUMBER_OF_PAGES, "Number of Pages" },
	{ CDF_PROPERTY_NUMBER_OF_WORDS, "Number of Words" },
	{ CDF_PROPERTY_NUMBER_OF_CHARACTERS, "Number of Characters" },
	{ CDF_PROPERTY_THUMBNAIL, "Thumbnail" },
	{ CDF_PROPERTY_NAME_OF_APPLICATION, "Name of Creating Application" },
	{ CDF_PROPERTY_SECURITY, "Security" },
	{ CDF_PROPERTY_LOCALE_ID, "Locale ID" },
};

int
cdf_print_property_name(char *buf, size_t bufsiz, uint32_t p)
{
	size_t i;

	for (i = 0; i < __arraycount(vn); i++)
		if (vn[i].v == p)
			return snprintf(buf, bufsiz, "%s", vn[i].n);
	return snprintf(buf, bufsiz, "%#x", p);
}

int
cdf_print_elapsed_time(char *buf, size_t bufsiz, cdf_timestamp_t ts)
{
	int len = 0;
	int days, hours, mins, secs;

	ts /= CDF_TIME_PREC;
	secs = (int)(ts % 60);
	ts /= 60;
	mins = (int)(ts % 60);
	ts /= 60;
	hours = (int)(ts % 24);
	ts /= 24;
	days = (int)ts;

	if (days) {
		len += snprintf(buf + len, bufsiz - len, "%dd+", days);
		if ((size_t)len >= bufsiz)
			return len;
	}

	if (days || hours) {
		len += snprintf(buf + len, bufsiz - len, "%.2d:", hours);
		if ((size_t)len >= bufsiz)
			return len;
	}

	len += snprintf(buf + len, bufsiz - len, "%.2d:", mins);
	if ((size_t)len >= bufsiz)
		return len;

	len += snprintf(buf + len, bufsiz - len, "%.2d", secs);
	return len;
}

char *
cdf_u16tos8(char *buf, size_t len, const uint16_t *p)
{
	size_t i;
	for (i = 0; i < len && p[i]; i++)
		buf[i] = (char)p[i];
	buf[i] = '\0';
	return buf;
}

#ifdef CDF_DEBUG
void
cdf_dump_header(const cdf_header_t *h)
{
	size_t i;

#define DUMP(a, b) (void)fprintf(stderr, "%40.40s = " a "\n", # b, h->h_ ## b)
#define DUMP2(a, b) (void)fprintf(stderr, "%40.40s = " a " (" a ")\n", # b, \
    h->h_ ## b, 1 << h->h_ ## b)
	DUMP("%d", revision);
	DUMP("%d", version);
	DUMP("%#x", byte_order);
	DUMP2("%d", sec_size_p2);
	DUMP2("%d", short_sec_size_p2);
	DUMP("%d", num_sectors_in_sat);
	DUMP("%d", secid_first_directory);
	DUMP("%d", min_size_standard_stream);
	DUMP("%d", secid_first_sector_in_short_sat);
	DUMP("%d", num_sectors_in_short_sat);
	DUMP("%d", secid_first_sector_in_master_sat);
	DUMP("%d", num_sectors_in_master_sat);
	for (i = 0; i < __arraycount(h->h_master_sat); i++) {
		if (h->h_master_sat[i] == CDF_SECID_FREE)
			break;
		(void)fprintf(stderr, "%35.35s[%.3" SIZE_T_FORMAT "u] = %d\n",
		    "master_sat", i, h->h_master_sat[i]);
	}
}

void
cdf_dump_sat(const char *prefix, const cdf_sat_t *sat, size_t size)
{
	size_t i, j, s = size / sizeof(cdf_secid_t);

	for (i = 0; i < sat->sat_len; i++) {
		(void)fprintf(stderr, "%s[%" SIZE_T_FORMAT "u]:\n%.6"
		    SIZE_T_FORMAT "u: ", prefix, i, i * s);
		for (j = 0; j < s; j++) {
			(void)fprintf(stderr, "%5d, ",
			    CDF_TOLE4(sat->sat_tab[s * i + j]));
			if ((j + 1) % 10 == 0)
				(void)fprintf(stderr, "\n%.6" SIZE_T_FORMAT
				    "u: ", i * s + j + 1);
		}
		(void)fprintf(stderr, "\n");
	}
}

void
cdf_dump(const void *v, size_t len)
{
	size_t i, j;
	const unsigned char *p = v;
	char abuf[16];

	(void)fprintf(stderr, "%.4x: ", 0);
	for (i = 0, j = 0; i < len; i++, p++) {
		(void)fprintf(stderr, "%.2x ", *p);
		abuf[j++] = isprint(*p) ? *p : '.';
		if (j == 16) {
			j = 0;
			abuf[15] = '\0';
			(void)fprintf(stderr, "%s\n%.4" SIZE_T_FORMAT "x: ",
			    abuf, i + 1);
		}
	}
	(void)fprintf(stderr, "\n");
}

void
cdf_dump_stream(const cdf_stream_t *sst)
{
	size_t ss = sst->sst_ss;
	cdf_dump(sst->sst_tab, ss * sst->sst_len);
}

void
cdf_dump_dir(const cdf_info_t *info, const cdf_header_t *h,
    const cdf_sat_t *sat, const cdf_sat_t *ssat, const cdf_stream_t *sst,
    const cdf_dir_t *dir)
{
	size_t i, j;
	cdf_directory_t *d;
	char name[__arraycount(d->d_name)];
	cdf_stream_t scn;
	struct timespec ts;

	static const char *types[] = { "empty", "user storage",
	    "user stream", "lockbytes", "property", "root storage" };

	for (i = 0; i < dir->dir_len; i++) {
		char buf[26];
		d = &dir->dir_tab[i];
		for (j = 0; j < sizeof(name); j++)
			name[j] = (char)CDF_TOLE2(d->d_name[j]);
		(void)fprintf(stderr, "Directory %" SIZE_T_FORMAT "u: %s\n",
		    i, name);
		if (d->d_type < __arraycount(types))
			(void)fprintf(stderr, "Type: %s\n", types[d->d_type]);
		else
			(void)fprintf(stderr, "Type: %d\n", d->d_type);
		(void)fprintf(stderr, "Color: %s\n",
		    d->d_color ? "black" : "red");
		(void)fprintf(stderr, "Left child: %d\n", d->d_left_child);
		(void)fprintf(stderr, "Right child: %d\n", d->d_right_child);
		(void)fprintf(stderr, "Flags: %#x\n", d->d_flags);
		cdf_timestamp_to_timespec(&ts, d->d_created);
		(void)fprintf(stderr, "Created %s", cdf_ctime(&ts.tv_sec, buf));
		cdf_timestamp_to_timespec(&ts, d->d_modified);
		(void)fprintf(stderr, "Modified %s",
		    cdf_ctime(&ts.tv_sec, buf));
		(void)fprintf(stderr, "Stream %d\n", d->d_stream_first_sector);
		(void)fprintf(stderr, "Size %d\n", d->d_size);
		switch (d->d_type) {
		case CDF_DIR_TYPE_USER_STORAGE:
			(void)fprintf(stderr, "Storage: %d\n", d->d_storage);
			break;
		case CDF_DIR_TYPE_USER_STREAM:
			if (sst == NULL)
				break;
			if (cdf_read_sector_chain(info, h, sat, ssat, sst,
			    d->d_stream_first_sector, d->d_size, &scn) == -1) {
				warn("Can't read stream for %s at %d len %d",
				    name, d->d_stream_first_sector, d->d_size);
				break;
			}
			cdf_dump_stream(&scn);
			free(scn.sst_tab);
			break;
		default:
			break;
		}

	}
}

void
cdf_dump_property_info(const cdf_property_info_t *info, size_t count)
{
	cdf_timestamp_t tp;
	struct timespec ts;
	char buf[64];
	size_t i, j;

	for (i = 0; i < count; i++) {
		cdf_print_property_name(buf, sizeof(buf), info[i].pi_id);
		(void)fprintf(stderr, "%" SIZE_T_FORMAT "u) %s: ", i, buf);
		switch (info[i].pi_type) {
		case CDF_NULL:
			break;
		case CDF_SIGNED16:
			(void)fprintf(stderr, "signed 16 [%hd]\n",
			    info[i].pi_s16);
			break;
		case CDF_SIGNED32:
			(void)fprintf(stderr, "signed 32 [%d]\n",
			    info[i].pi_s32);
			break;
		case CDF_UNSIGNED32:
			(void)fprintf(stderr, "unsigned 32 [%u]\n",
			    info[i].pi_u32);
			break;
		case CDF_FLOAT:
			(void)fprintf(stderr, "float [%g]\n",
			    info[i].pi_f);
			break;
		case CDF_DOUBLE:
			(void)fprintf(stderr, "double [%g]\n",
			    info[i].pi_d);
			break;
		case CDF_LENGTH32_STRING:
			(void)fprintf(stderr, "string %u [%.*s]\n",
			    info[i].pi_str.s_len,
			    info[i].pi_str.s_len, info[i].pi_str.s_buf);
			break;
		case CDF_LENGTH32_WSTRING:
			(void)fprintf(stderr, "string %u [",
			    info[i].pi_str.s_len);
			for (j = 0; j < info[i].pi_str.s_len - 1; j++)
			    (void)fputc(info[i].pi_str.s_buf[j << 1], stderr);
			(void)fprintf(stderr, "]\n");
			break;
		case CDF_FILETIME:
			tp = info[i].pi_tp;
			if (tp < 1000000000000000LL) {
				cdf_print_elapsed_time(buf, sizeof(buf), tp);
				(void)fprintf(stderr, "timestamp %s\n", buf);
			} else {
				char tbuf[26];
				cdf_timestamp_to_timespec(&ts, tp);
				(void)fprintf(stderr, "timestamp %s",
				    cdf_ctime(&ts.tv_sec, tbuf));
			}
			break;
		case CDF_CLIPBOARD:
			(void)fprintf(stderr, "CLIPBOARD %u\n", info[i].pi_u32);
			break;
		default:
			DPRINTF(("Don't know how to deal with %#x\n",
			    info[i].pi_type));
			break;
		}
	}
}


void
cdf_dump_summary_info(const cdf_header_t *h, const cdf_stream_t *sst)
{
	char buf[128];
	cdf_summary_info_header_t ssi;
	cdf_property_info_t *info;
	size_t count;

	(void)&h;
	if (cdf_unpack_summary_info(sst, h, &ssi, &info, &count) == -1)
		return;
	(void)fprintf(stderr, "Endian: %#x\n", ssi.si_byte_order);
	(void)fprintf(stderr, "Os Version %d.%d\n", ssi.si_os_version & 0xff,
	    ssi.si_os_version >> 8);
	(void)fprintf(stderr, "Os %d\n", ssi.si_os);
	cdf_print_classid(buf, sizeof(buf), &ssi.si_class);
	(void)fprintf(stderr, "Class %s\n", buf);
	(void)fprintf(stderr, "Count %d\n", ssi.si_count);
	cdf_dump_property_info(info, count);
	free(info);
}


void
cdf_dump_catalog(const cdf_header_t *h, const cdf_stream_t *sst)
{
	cdf_catalog_t *cat;
	cdf_unpack_catalog(h, sst, &cat);
	const cdf_catalog_entry_t *ce = cat->cat_e;
	struct timespec ts;
	char tbuf[64], sbuf[256];
	size_t i;

	printf("Catalog:\n");
	for (i = 0; i < cat->cat_num; i++) {
		cdf_timestamp_to_timespec(&ts, ce[i].ce_timestamp);
		printf("\t%d %s %s", ce[i].ce_num,
		    cdf_u16tos8(sbuf, ce[i].ce_namlen, ce[i].ce_name),
		    cdf_ctime(&ts.tv_sec, tbuf));
	}
	free(cat);
}

#endif

#ifdef TEST
int
main(int argc, char *argv[])
{
	int i;
	cdf_header_t h;
	cdf_sat_t sat, ssat;
	cdf_stream_t sst, scn;
	cdf_dir_t dir;
	cdf_info_t info;
	const cdf_directory_t *root;
#ifdef __linux__
#define getprogname() __progname
	extern char *__progname;
#endif
	if (argc < 2) {
		(void)fprintf(stderr, "Usage: %s <filename>\n", getprogname());
		return -1;
	}

	info.i_buf = NULL;
	info.i_len = 0;
	for (i = 1; i < argc; i++) {
		if ((info.i_fd = open(argv[1], O_RDONLY)) == -1)
			err(EXIT_FAILURE, "Cannot open `%s'", argv[1]);

		if (cdf_read_header(&info, &h) == -1)
			err(EXIT_FAILURE, "Cannot read header");
#ifdef CDF_DEBUG
		cdf_dump_header(&h);
#endif

		if (cdf_read_sat(&info, &h, &sat) == -1)
			err(EXIT_FAILURE, "Cannot read sat");
#ifdef CDF_DEBUG
		cdf_dump_sat("SAT", &sat, CDF_SEC_SIZE(&h));
#endif

		if (cdf_read_ssat(&info, &h, &sat, &ssat) == -1)
			err(EXIT_FAILURE, "Cannot read ssat");
#ifdef CDF_DEBUG
		cdf_dump_sat("SSAT", &ssat, CDF_SHORT_SEC_SIZE(&h));
#endif

		if (cdf_read_dir(&info, &h, &sat, &dir) == -1)
			err(EXIT_FAILURE, "Cannot read dir");

		if (cdf_read_short_stream(&info, &h, &sat, &dir, &sst, &root)
		    == -1)
			err(EXIT_FAILURE, "Cannot read short stream");
#ifdef CDF_DEBUG
		cdf_dump_stream(&sst);
#endif

#ifdef CDF_DEBUG
		cdf_dump_dir(&info, &h, &sat, &ssat, &sst, &dir);
#endif


		if (cdf_read_summary_info(&info, &h, &sat, &ssat, &sst, &dir,
		    &scn) == -1)
			warn("Cannot read summary info");
#ifdef CDF_DEBUG
		else
			cdf_dump_summary_info(&h, &scn);
#endif
		if (cdf_read_user_stream(&info, &h, &sat, &ssat, &sst,
		    &dir, "Catalog", &scn) == -1)
			warn("Cannot read catalog");
#ifdef CDF_DEBUG
		else
			cdf_dump_catalog(&h, &scn);
#endif

		(void)close(info.i_fd);
	}

	return 0;
}
#endif
