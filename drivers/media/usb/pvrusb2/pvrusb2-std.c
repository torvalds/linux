// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 */

#include "pvrusb2-std.h"
#include "pvrusb2-debug.h"
#include <asm/string.h>
#include <linux/slab.h>

struct std_name {
	const char *name;
	v4l2_std_id id;
};


#define CSTD_PAL \
	(V4L2_STD_PAL_B| \
	 V4L2_STD_PAL_B1| \
	 V4L2_STD_PAL_G| \
	 V4L2_STD_PAL_H| \
	 V4L2_STD_PAL_I| \
	 V4L2_STD_PAL_D| \
	 V4L2_STD_PAL_D1| \
	 V4L2_STD_PAL_K| \
	 V4L2_STD_PAL_M| \
	 V4L2_STD_PAL_N| \
	 V4L2_STD_PAL_Nc| \
	 V4L2_STD_PAL_60)

#define CSTD_NTSC \
	(V4L2_STD_NTSC_M| \
	 V4L2_STD_NTSC_M_JP| \
	 V4L2_STD_NTSC_M_KR| \
	 V4L2_STD_NTSC_443)

#define CSTD_ATSC \
	(V4L2_STD_ATSC_8_VSB| \
	 V4L2_STD_ATSC_16_VSB)

#define CSTD_SECAM \
	(V4L2_STD_SECAM_B| \
	 V4L2_STD_SECAM_D| \
	 V4L2_STD_SECAM_G| \
	 V4L2_STD_SECAM_H| \
	 V4L2_STD_SECAM_K| \
	 V4L2_STD_SECAM_K1| \
	 V4L2_STD_SECAM_L| \
	 V4L2_STD_SECAM_LC)

#define TSTD_B   (V4L2_STD_PAL_B|V4L2_STD_SECAM_B)
#define TSTD_B1  (V4L2_STD_PAL_B1)
#define TSTD_D   (V4L2_STD_PAL_D|V4L2_STD_SECAM_D)
#define TSTD_D1  (V4L2_STD_PAL_D1)
#define TSTD_G   (V4L2_STD_PAL_G|V4L2_STD_SECAM_G)
#define TSTD_H   (V4L2_STD_PAL_H|V4L2_STD_SECAM_H)
#define TSTD_I   (V4L2_STD_PAL_I)
#define TSTD_K   (V4L2_STD_PAL_K|V4L2_STD_SECAM_K)
#define TSTD_K1  (V4L2_STD_SECAM_K1)
#define TSTD_L   (V4L2_STD_SECAM_L)
#define TSTD_M   (V4L2_STD_PAL_M|V4L2_STD_NTSC_M)
#define TSTD_N   (V4L2_STD_PAL_N)
#define TSTD_Nc  (V4L2_STD_PAL_Nc)
#define TSTD_60  (V4L2_STD_PAL_60)

#define CSTD_ALL (CSTD_PAL|CSTD_NTSC|CSTD_ATSC|CSTD_SECAM)

/* Mapping of standard bits to color system */
static const struct std_name std_groups[] = {
	{"PAL",CSTD_PAL},
	{"NTSC",CSTD_NTSC},
	{"SECAM",CSTD_SECAM},
	{"ATSC",CSTD_ATSC},
};

/* Mapping of standard bits to modulation system */
static const struct std_name std_items[] = {
	{"B",TSTD_B},
	{"B1",TSTD_B1},
	{"D",TSTD_D},
	{"D1",TSTD_D1},
	{"G",TSTD_G},
	{"H",TSTD_H},
	{"I",TSTD_I},
	{"K",TSTD_K},
	{"K1",TSTD_K1},
	{"L",TSTD_L},
	{"LC",V4L2_STD_SECAM_LC},
	{"M",TSTD_M},
	{"Mj",V4L2_STD_NTSC_M_JP},
	{"443",V4L2_STD_NTSC_443},
	{"Mk",V4L2_STD_NTSC_M_KR},
	{"N",TSTD_N},
	{"Nc",TSTD_Nc},
	{"60",TSTD_60},
	{"8VSB",V4L2_STD_ATSC_8_VSB},
	{"16VSB",V4L2_STD_ATSC_16_VSB},
};


// Search an array of std_name structures and return a pointer to the
// element with the matching name.
static const struct std_name *find_std_name(const struct std_name *arrPtr,
					    unsigned int arrSize,
					    const char *bufPtr,
					    unsigned int bufSize)
{
	unsigned int idx;
	const struct std_name *p;
	for (idx = 0; idx < arrSize; idx++) {
		p = arrPtr + idx;
		if (strlen(p->name) != bufSize) continue;
		if (!memcmp(bufPtr,p->name,bufSize)) return p;
	}
	return NULL;
}


int pvr2_std_str_to_id(v4l2_std_id *idPtr,const char *bufPtr,
		       unsigned int bufSize)
{
	v4l2_std_id id = 0;
	v4l2_std_id cmsk = 0;
	v4l2_std_id t;
	int mMode = 0;
	unsigned int cnt;
	char ch;
	const struct std_name *sp;

	while (bufSize) {
		if (!mMode) {
			cnt = 0;
			while ((cnt < bufSize) && (bufPtr[cnt] != '-')) cnt++;
			if (cnt >= bufSize) return 0; // No more characters
			sp = find_std_name(std_groups, ARRAY_SIZE(std_groups),
					   bufPtr,cnt);
			if (!sp) return 0; // Illegal color system name
			cnt++;
			bufPtr += cnt;
			bufSize -= cnt;
			mMode = !0;
			cmsk = sp->id;
			continue;
		}
		cnt = 0;
		while (cnt < bufSize) {
			ch = bufPtr[cnt];
			if (ch == ';') {
				mMode = 0;
				break;
			}
			if (ch == '/') break;
			cnt++;
		}
		sp = find_std_name(std_items, ARRAY_SIZE(std_items),
				   bufPtr,cnt);
		if (!sp) return 0; // Illegal modulation system ID
		t = sp->id & cmsk;
		if (!t) return 0; // Specific color + modulation system illegal
		id |= t;
		if (cnt < bufSize) cnt++;
		bufPtr += cnt;
		bufSize -= cnt;
	}

	if (idPtr) *idPtr = id;
	return !0;
}


unsigned int pvr2_std_id_to_str(char *bufPtr, unsigned int bufSize,
				v4l2_std_id id)
{
	unsigned int idx1,idx2;
	const struct std_name *ip,*gp;
	int gfl,cfl;
	unsigned int c1,c2;
	cfl = 0;
	c1 = 0;
	for (idx1 = 0; idx1 < ARRAY_SIZE(std_groups); idx1++) {
		gp = std_groups + idx1;
		gfl = 0;
		for (idx2 = 0; idx2 < ARRAY_SIZE(std_items); idx2++) {
			ip = std_items + idx2;
			if (!(gp->id & ip->id & id)) continue;
			if (!gfl) {
				if (cfl) {
					c2 = scnprintf(bufPtr,bufSize,";");
					c1 += c2;
					bufSize -= c2;
					bufPtr += c2;
				}
				cfl = !0;
				c2 = scnprintf(bufPtr,bufSize,
					       "%s-",gp->name);
				gfl = !0;
			} else {
				c2 = scnprintf(bufPtr,bufSize,"/");
			}
			c1 += c2;
			bufSize -= c2;
			bufPtr += c2;
			c2 = scnprintf(bufPtr,bufSize,
				       ip->name);
			c1 += c2;
			bufSize -= c2;
			bufPtr += c2;
		}
	}
	return c1;
}


v4l2_std_id pvr2_std_get_usable(void)
{
	return CSTD_ALL;
}
