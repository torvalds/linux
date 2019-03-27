/*-
 * Copyright (c) 2008, 2016 Christos Zoulas
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
#include "file.h"

#ifndef lint
FILE_RCSID("@(#)$File: readcdf.c,v 1.67 2018/04/15 19:57:07 christos Exp $")
#endif

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include "cdf.h"
#include "magic.h"

#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof(a[0]))
#endif

#define NOTMIME(ms) (((ms)->flags & MAGIC_MIME) == 0)

static const struct nv {
	const char *pattern;
	const char *mime;
} app2mime[] =  {
	{ "Word",			"msword",		},
	{ "Excel",			"vnd.ms-excel",		},
	{ "Powerpoint",			"vnd.ms-powerpoint",	},
	{ "Crystal Reports",		"x-rpt",		},
	{ "Advanced Installer",		"vnd.ms-msi",		},
	{ "InstallShield",		"vnd.ms-msi",		},
	{ "Microsoft Patch Compiler",	"vnd.ms-msi",		},
	{ "NAnt",			"vnd.ms-msi",		},
	{ "Windows Installer",		"vnd.ms-msi",		},
	{ NULL,				NULL,			},
}, name2mime[] = {
	{ "Book",			"vnd.ms-excel",		},
	{ "Workbook",			"vnd.ms-excel",		},
	{ "WordDocument",		"msword",		},
	{ "PowerPoint",			"vnd.ms-powerpoint",	},
	{ "DigitalSignature",		"vnd.ms-msi",		},
	{ NULL,				NULL,			},
}, name2desc[] = {
	{ "Book",			"Microsoft Excel",	},
	{ "Workbook",			"Microsoft Excel",	},
	{ "WordDocument",		"Microsoft Word",	},
	{ "PowerPoint",			"Microsoft PowerPoint",	},
	{ "DigitalSignature",		"Microsoft Installer",	},
	{ NULL,				NULL,			},
};

static const struct cv {
	uint64_t clsid[2];
	const char *mime;
} clsid2mime[] = {
	{
		{ 0x00000000000c1084ULL, 0x46000000000000c0ULL  },
		"x-msi",
	},
	{	{ 0,			 0			},
		NULL,
	},
}, clsid2desc[] = {
	{
		{ 0x00000000000c1084ULL, 0x46000000000000c0ULL  },
		"MSI Installer",
	},
	{	{ 0,			 0			},
		NULL,
	},
};

private const char *
cdf_clsid_to_mime(const uint64_t clsid[2], const struct cv *cv)
{
	size_t i;
	for (i = 0; cv[i].mime != NULL; i++) {
		if (clsid[0] == cv[i].clsid[0] && clsid[1] == cv[i].clsid[1])
			return cv[i].mime;
	}
#ifdef CDF_DEBUG
	fprintf(stderr, "unknown mime %" PRIx64 ", %" PRIx64 "\n", clsid[0],
	    clsid[1]);
#endif
	return NULL;
}

private const char *
cdf_app_to_mime(const char *vbuf, const struct nv *nv)
{
	size_t i;
	const char *rv = NULL;
#ifdef USE_C_LOCALE
	locale_t old_lc_ctype, c_lc_ctype;

	c_lc_ctype = newlocale(LC_CTYPE_MASK, "C", 0);
	assert(c_lc_ctype != NULL);
	old_lc_ctype = uselocale(c_lc_ctype);
	assert(old_lc_ctype != NULL);
#else
	char *old_lc_ctype = setlocale(LC_CTYPE, "C");
#endif
	for (i = 0; nv[i].pattern != NULL; i++)
		if (strcasestr(vbuf, nv[i].pattern) != NULL) {
			rv = nv[i].mime;
			break;
		}
#ifdef CDF_DEBUG
	fprintf(stderr, "unknown app %s\n", vbuf);
#endif
#ifdef USE_C_LOCALE
	(void)uselocale(old_lc_ctype);
	freelocale(c_lc_ctype);
#else
	setlocale(LC_CTYPE, old_lc_ctype);
#endif
	return rv;
}

private int
cdf_file_property_info(struct magic_set *ms, const cdf_property_info_t *info,
    size_t count, const cdf_directory_t *root_storage)
{
	size_t i;
	cdf_timestamp_t tp;
	struct timespec ts;
	char buf[64];
	const char *str = NULL;
	const char *s, *e;
	int len;

	if (!NOTMIME(ms) && root_storage)
		str = cdf_clsid_to_mime(root_storage->d_storage_uuid,
		    clsid2mime);

	for (i = 0; i < count; i++) {
		cdf_print_property_name(buf, sizeof(buf), info[i].pi_id);
		switch (info[i].pi_type) {
		case CDF_NULL:
			break;
		case CDF_SIGNED16:
			if (NOTMIME(ms) && file_printf(ms, ", %s: %hd", buf,
			    info[i].pi_s16) == -1)
				return -1;
			break;
		case CDF_SIGNED32:
			if (NOTMIME(ms) && file_printf(ms, ", %s: %d", buf,
			    info[i].pi_s32) == -1)
				return -1;
			break;
		case CDF_UNSIGNED32:
			if (NOTMIME(ms) && file_printf(ms, ", %s: %u", buf,
			    info[i].pi_u32) == -1)
				return -1;
			break;
		case CDF_FLOAT:
			if (NOTMIME(ms) && file_printf(ms, ", %s: %g", buf,
			    info[i].pi_f) == -1)
				return -1;
			break;
		case CDF_DOUBLE:
			if (NOTMIME(ms) && file_printf(ms, ", %s: %g", buf,
			    info[i].pi_d) == -1)
				return -1;
			break;
		case CDF_LENGTH32_STRING:
		case CDF_LENGTH32_WSTRING:
			len = info[i].pi_str.s_len;
			if (len > 1) {
				char vbuf[1024];
				size_t j, k = 1;

				if (info[i].pi_type == CDF_LENGTH32_WSTRING)
				    k++;
				s = info[i].pi_str.s_buf;
				e = info[i].pi_str.s_buf + len;
				for (j = 0; s < e && j < sizeof(vbuf)
				    && len--; s += k) {
					if (*s == '\0')
						break;
					if (isprint((unsigned char)*s))
						vbuf[j++] = *s;
				}
				if (j == sizeof(vbuf))
					--j;
				vbuf[j] = '\0';
				if (NOTMIME(ms)) {
					if (vbuf[0]) {
						if (file_printf(ms, ", %s: %s",
						    buf, vbuf) == -1)
							return -1;
					}
				} else if (str == NULL && info[i].pi_id ==
				    CDF_PROPERTY_NAME_OF_APPLICATION) {
					str = cdf_app_to_mime(vbuf, app2mime);
				}
			}
			break;
		case CDF_FILETIME:
			tp = info[i].pi_tp;
			if (tp != 0) {
				char tbuf[64];
				if (tp < 1000000000000000LL) {
					cdf_print_elapsed_time(tbuf,
					    sizeof(tbuf), tp);
					if (NOTMIME(ms) && file_printf(ms,
					    ", %s: %s", buf, tbuf) == -1)
						return -1;
				} else {
					char *c, *ec;
					cdf_timestamp_to_timespec(&ts, tp);
					c = cdf_ctime(&ts.tv_sec, tbuf);
					if (c != NULL &&
					    (ec = strchr(c, '\n')) != NULL)
						*ec = '\0';

					if (NOTMIME(ms) && file_printf(ms,
					    ", %s: %s", buf, c) == -1)
						return -1;
				}
			}
			break;
		case CDF_CLIPBOARD:
			break;
		default:
			return -1;
		}
	}
	if (!NOTMIME(ms)) {
		if (str == NULL)
			return 0;
		if (file_printf(ms, "application/%s", str) == -1)
			return -1;
	}
	return 1;
}

private int
cdf_file_catalog(struct magic_set *ms, const cdf_header_t *h,
    const cdf_stream_t *sst)
{
	cdf_catalog_t *cat;
	size_t i;
	char buf[256];
	cdf_catalog_entry_t *ce;

	if (NOTMIME(ms)) {
		if (file_printf(ms, "Microsoft Thumbs.db [") == -1)
			return -1;
		if (cdf_unpack_catalog(h, sst, &cat) == -1)
			return -1;
		ce = cat->cat_e;
		/* skip first entry since it has a , or paren */
		for (i = 1; i < cat->cat_num; i++)
			if (file_printf(ms, "%s%s",
			    cdf_u16tos8(buf, ce[i].ce_namlen, ce[i].ce_name),
			    i == cat->cat_num - 1 ? "]" : ", ") == -1) {
				free(cat);
				return -1;
			}
		free(cat);
	} else {
		if (file_printf(ms, "application/CDFV2") == -1)
			return -1;
	}
	return 1;
}

private int
cdf_file_summary_info(struct magic_set *ms, const cdf_header_t *h,
    const cdf_stream_t *sst, const cdf_directory_t *root_storage)
{
	cdf_summary_info_header_t si;
	cdf_property_info_t *info;
	size_t count;
	int m;

	if (cdf_unpack_summary_info(sst, h, &si, &info, &count) == -1)
		return -1;

	if (NOTMIME(ms)) {
		const char *str;

		if (file_printf(ms, "Composite Document File V2 Document")
		    == -1)
			return -1;

		if (file_printf(ms, ", %s Endian",
		    si.si_byte_order == 0xfffe ?  "Little" : "Big") == -1)
			return -2;
		switch (si.si_os) {
		case 2:
			if (file_printf(ms, ", Os: Windows, Version %d.%d",
			    si.si_os_version & 0xff,
			    (uint32_t)si.si_os_version >> 8) == -1)
				return -2;
			break;
		case 1:
			if (file_printf(ms, ", Os: MacOS, Version %d.%d",
			    (uint32_t)si.si_os_version >> 8,
			    si.si_os_version & 0xff) == -1)
				return -2;
			break;
		default:
			if (file_printf(ms, ", Os %d, Version: %d.%d", si.si_os,
			    si.si_os_version & 0xff,
			    (uint32_t)si.si_os_version >> 8) == -1)
				return -2;
			break;
		}
		if (root_storage) {
			str = cdf_clsid_to_mime(root_storage->d_storage_uuid,
			    clsid2desc);
			if (str) {
				if (file_printf(ms, ", %s", str) == -1)
					return -2;
			}
		}
	}

	m = cdf_file_property_info(ms, info, count, root_storage);
	free(info);

	return m == -1 ? -2 : m;
}

#ifdef notdef
private char *
format_clsid(char *buf, size_t len, const uint64_t uuid[2]) {
	snprintf(buf, len, "%.8" PRIx64 "-%.4" PRIx64 "-%.4" PRIx64 "-%.4" 
	    PRIx64 "-%.12" PRIx64,
	    (uuid[0] >> 32) & (uint64_t)0x000000000ffffffffULL,
	    (uuid[0] >> 16) & (uint64_t)0x0000000000000ffffULL,
	    (uuid[0] >>  0) & (uint64_t)0x0000000000000ffffULL, 
	    (uuid[1] >> 48) & (uint64_t)0x0000000000000ffffULL,
	    (uuid[1] >>  0) & (uint64_t)0x0000fffffffffffffULL);
	return buf;
}
#endif

private int
cdf_file_catalog_info(struct magic_set *ms, const cdf_info_t *info,
    const cdf_header_t *h, const cdf_sat_t *sat, const cdf_sat_t *ssat,
    const cdf_stream_t *sst, const cdf_dir_t *dir, cdf_stream_t *scn)
{
	int i;

	if ((i = cdf_read_user_stream(info, h, sat, ssat, sst,
	    dir, "Catalog", scn)) == -1)
		return i;
#ifdef CDF_DEBUG
	cdf_dump_catalog(h, scn);
#endif
	if ((i = cdf_file_catalog(ms, h, scn)) == -1)
		return -1;
	return i;
}

private int
cdf_check_summary_info(struct magic_set *ms, const cdf_info_t *info,
    const cdf_header_t *h, const cdf_sat_t *sat, const cdf_sat_t *ssat,
    const cdf_stream_t *sst, const cdf_dir_t *dir, cdf_stream_t *scn,
    const cdf_directory_t *root_storage, const char **expn)
{
	int i;
	const char *str = NULL;
	cdf_directory_t *d;
	char name[__arraycount(d->d_name)];
	size_t j, k;

#ifdef CDF_DEBUG
	cdf_dump_summary_info(h, scn);
#endif
	if ((i = cdf_file_summary_info(ms, h, scn, root_storage)) < 0) {
	    *expn = "Can't expand summary_info";
	    return i;
	}
	if (i == 1)
		return i;
	for (j = 0; str == NULL && j < dir->dir_len; j++) {
		d = &dir->dir_tab[j];
		for (k = 0; k < sizeof(name); k++)
			name[k] = (char)cdf_tole2(d->d_name[k]);
		str = cdf_app_to_mime(name,
				      NOTMIME(ms) ? name2desc : name2mime);
	}
	if (NOTMIME(ms)) {
		if (str != NULL) {
			if (file_printf(ms, "%s", str) == -1)
				return -1;
			i = 1;
		}
	} else {
		if (str == NULL)
			str = "vnd.ms-office";
		if (file_printf(ms, "application/%s", str) == -1)
			return -1;
		i = 1;
	}
	if (i <= 0) {
		i = cdf_file_catalog_info(ms, info, h, sat, ssat, sst,
					  dir, scn);
	}
	return i;
}

private struct sinfo {
	const char *name;
	const char *mime;
	const char *sections[5];
	const int  types[5];
} sectioninfo[] = {
	{ "Encrypted", "encrypted", 
		{
			"EncryptedPackage", "EncryptedSummary",
			NULL, NULL, NULL,
		},
		{
			CDF_DIR_TYPE_USER_STREAM,
			CDF_DIR_TYPE_USER_STREAM,
			0, 0, 0,

		},
	},
	{ "QuickBooks", "quickbooks", 
		{
#if 0
			"TaxForms", "PDFTaxForms", "modulesInBackup",
#endif
			"mfbu_header", NULL, NULL, NULL, NULL,
		},
		{
#if 0
			CDF_DIR_TYPE_USER_STORAGE,
			CDF_DIR_TYPE_USER_STORAGE,
			CDF_DIR_TYPE_USER_STREAM,
#endif
			CDF_DIR_TYPE_USER_STREAM,
			0, 0, 0, 0
		},
	},
	{ "Microsoft Excel", "vnd.ms-excel",
		{
			"Book", "Workbook", NULL, NULL, NULL,
		},
		{
			CDF_DIR_TYPE_USER_STREAM,
			CDF_DIR_TYPE_USER_STREAM,
			0, 0, 0,
		},
	},
	{ "Microsoft Word", "msword",
		{
			"WordDocument", NULL, NULL, NULL, NULL,
		},
		{
			CDF_DIR_TYPE_USER_STREAM,
			0, 0, 0, 0,
		},
	},
	{ "Microsoft PowerPoint", "vnd.ms-powerpoint",
		{
			"PowerPoint", NULL, NULL, NULL, NULL,
		},
		{
			CDF_DIR_TYPE_USER_STREAM,
			0, 0, 0, 0,
		},
	},
	{ "Microsoft Outlook Message", "vnd.ms-outlook",
		{
			"__properties_version1.0",
			"__recip_version1.0_#00000000",
			NULL, NULL, NULL,
		},
		{
			CDF_DIR_TYPE_USER_STREAM,
			CDF_DIR_TYPE_USER_STORAGE,
			0, 0, 0,
		},
	},
};

private int
cdf_file_dir_info(struct magic_set *ms, const cdf_dir_t *dir)
{
	size_t sd, j;

	for (sd = 0; sd < __arraycount(sectioninfo); sd++) {
		const struct sinfo *si = &sectioninfo[sd];
		for (j = 0; si->sections[j]; j++) {
			if (cdf_find_stream(dir, si->sections[j], si->types[j])
			    > 0)
				break;
#ifdef CDF_DEBUG
			fprintf(stderr, "Can't read %s\n", si->sections[j]);
#endif
		}
		if (si->sections[j] == NULL)
			continue;
		if (NOTMIME(ms)) {
			if (file_printf(ms, "CDFV2 %s", si->name) == -1)
				return -1;
		} else {
			if (file_printf(ms, "application/%s", si->mime) == -1)
				return -1;
		}
		return 1;
	}
	return -1;
}

protected int
file_trycdf(struct magic_set *ms, const struct buffer *b)
{
	int fd = b->fd;
	const unsigned char *buf = b->fbuf;
	size_t nbytes = b->flen;
	cdf_info_t info;
	cdf_header_t h;
	cdf_sat_t sat, ssat;
	cdf_stream_t sst, scn;
	cdf_dir_t dir;
	int i;
	const char *expn = "";
	const cdf_directory_t *root_storage;

	scn.sst_tab = NULL;
	info.i_fd = fd;
	info.i_buf = buf;
	info.i_len = nbytes;
	if (ms->flags & (MAGIC_APPLE|MAGIC_EXTENSION))
		return 0;
	if (cdf_read_header(&info, &h) == -1)
		return 0;
#ifdef CDF_DEBUG
	cdf_dump_header(&h);
#endif

	if ((i = cdf_read_sat(&info, &h, &sat)) == -1) {
		expn = "Can't read SAT";
		goto out0;
	}
#ifdef CDF_DEBUG
	cdf_dump_sat("SAT", &sat, CDF_SEC_SIZE(&h));
#endif

	if ((i = cdf_read_ssat(&info, &h, &sat, &ssat)) == -1) {
		expn = "Can't read SSAT";
		goto out1;
	}
#ifdef CDF_DEBUG
	cdf_dump_sat("SSAT", &ssat, CDF_SHORT_SEC_SIZE(&h));
#endif

	if ((i = cdf_read_dir(&info, &h, &sat, &dir)) == -1) {
		expn = "Can't read directory";
		goto out2;
	}

	if ((i = cdf_read_short_stream(&info, &h, &sat, &dir, &sst,
	    &root_storage)) == -1) {
		expn = "Cannot read short stream";
		goto out3;
	}
#ifdef CDF_DEBUG
	cdf_dump_dir(&info, &h, &sat, &ssat, &sst, &dir);
#endif
#ifdef notdef
	if (root_storage) {
		if (NOTMIME(ms)) {
			char clsbuf[128];
			if (file_printf(ms, "CLSID %s, ",
			    format_clsid(clsbuf, sizeof(clsbuf),
			    root_storage->d_storage_uuid)) == -1)
				return -1;
		}
	}
#endif

	if ((i = cdf_read_user_stream(&info, &h, &sat, &ssat, &sst, &dir,
	    "FileHeader", &scn)) != -1) {
#define HWP5_SIGNATURE "HWP Document File"
		if (scn.sst_len * scn.sst_ss >= sizeof(HWP5_SIGNATURE) - 1
		    && memcmp(scn.sst_tab, HWP5_SIGNATURE,
		    sizeof(HWP5_SIGNATURE) - 1) == 0) {
		    if (NOTMIME(ms)) {
			if (file_printf(ms,
			    "Hangul (Korean) Word Processor File 5.x") == -1)
			    return -1;
		    } else {
			if (file_printf(ms, "application/x-hwp") == -1)
			    return -1;
		    }
		    i = 1;
		    goto out5;
		} else {
		    cdf_zero_stream(&scn);
		}
	}

	if ((i = cdf_read_summary_info(&info, &h, &sat, &ssat, &sst, &dir,
	    &scn)) == -1) {
		if (errno != ESRCH) {
			expn = "Cannot read summary info";
		}
	} else {
		i = cdf_check_summary_info(ms, &info, &h,
		    &sat, &ssat, &sst, &dir, &scn, root_storage, &expn);
		cdf_zero_stream(&scn);
	}
	if (i <= 0) {
		if ((i = cdf_read_doc_summary_info(&info, &h, &sat, &ssat,
		    &sst, &dir, &scn)) == -1) {
			if (errno != ESRCH) {
				expn = "Cannot read summary info";
			}
		} else {
			i = cdf_check_summary_info(ms, &info, &h, &sat, &ssat,
			    &sst, &dir, &scn, root_storage, &expn);
		}
	}
	if (i <= 0) {
		i = cdf_file_dir_info(ms, &dir);
		if (i < 0)
			expn = "Cannot read section info";
	}
out5:
	cdf_zero_stream(&scn);
	cdf_zero_stream(&sst);
out3:
	free(dir.dir_tab);
out2:
	free(ssat.sat_tab);
out1:
	free(sat.sat_tab);
out0:
	if (i == -1) {
	    if (NOTMIME(ms)) {
		if (file_printf(ms,
		    "Composite Document File V2 Document") == -1)
		    return -1;
		if (*expn)
		    if (file_printf(ms, ", %s", expn) == -1)
			return -1;
	    } else {
		if (file_printf(ms, "application/CDFV2") == -1)
		    return -1;
	    }
	    i = 1;
	}
	return i;
}
