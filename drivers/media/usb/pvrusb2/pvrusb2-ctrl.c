// SPDX-License-Identifier: GPL-2.0-only
/*
 *
 *  Copyright (C) 2005 Mike Isely <isely@pobox.com>
 */

#include "pvrusb2-ctrl.h"
#include "pvrusb2-hdw-internal.h"
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mutex.h>


static int pvr2_ctrl_range_check(struct pvr2_ctrl *cptr,int val)
{
	if (cptr->info->check_value) {
		if (!cptr->info->check_value(cptr,val)) return -ERANGE;
	} else if (cptr->info->type == pvr2_ctl_enum) {
		if (val < 0) return -ERANGE;
		if (val >= cptr->info->def.type_enum.count) return -ERANGE;
	} else {
		int lim;
		lim = cptr->info->def.type_int.min_value;
		if (cptr->info->get_min_value) {
			cptr->info->get_min_value(cptr,&lim);
		}
		if (val < lim) return -ERANGE;
		lim = cptr->info->def.type_int.max_value;
		if (cptr->info->get_max_value) {
			cptr->info->get_max_value(cptr,&lim);
		}
		if (val > lim) return -ERANGE;
	}
	return 0;
}


/* Set the given control. */
int pvr2_ctrl_set_value(struct pvr2_ctrl *cptr,int val)
{
	return pvr2_ctrl_set_mask_value(cptr,~0,val);
}


/* Set/clear specific bits of the given control. */
int pvr2_ctrl_set_mask_value(struct pvr2_ctrl *cptr,int mask,int val)
{
	int ret = 0;
	if (!cptr) return -EINVAL;
	LOCK_TAKE(cptr->hdw->big_lock); do {
		if (cptr->info->set_value) {
			if (cptr->info->type == pvr2_ctl_bitmask) {
				mask &= cptr->info->def.type_bitmask.valid_bits;
			} else if ((cptr->info->type == pvr2_ctl_int)||
				   (cptr->info->type == pvr2_ctl_enum)) {
				ret = pvr2_ctrl_range_check(cptr,val);
				if (ret < 0) break;
			} else if (cptr->info->type != pvr2_ctl_bool) {
				break;
			}
			ret = cptr->info->set_value(cptr,mask,val);
		} else {
			ret = -EPERM;
		}
	} while(0); LOCK_GIVE(cptr->hdw->big_lock);
	return ret;
}


/* Get the current value of the given control. */
int pvr2_ctrl_get_value(struct pvr2_ctrl *cptr,int *valptr)
{
	int ret = 0;
	if (!cptr) return -EINVAL;
	LOCK_TAKE(cptr->hdw->big_lock); do {
		ret = cptr->info->get_value(cptr,valptr);
	} while(0); LOCK_GIVE(cptr->hdw->big_lock);
	return ret;
}


/* Retrieve control's type */
enum pvr2_ctl_type pvr2_ctrl_get_type(struct pvr2_ctrl *cptr)
{
	if (!cptr) return pvr2_ctl_int;
	return cptr->info->type;
}


/* Retrieve control's maximum value (int type) */
int pvr2_ctrl_get_max(struct pvr2_ctrl *cptr)
{
	int ret = 0;
	if (!cptr) return 0;
	LOCK_TAKE(cptr->hdw->big_lock); do {
		if (cptr->info->get_max_value) {
			cptr->info->get_max_value(cptr,&ret);
		} else if (cptr->info->type == pvr2_ctl_int) {
			ret = cptr->info->def.type_int.max_value;
		}
	} while(0); LOCK_GIVE(cptr->hdw->big_lock);
	return ret;
}


/* Retrieve control's minimum value (int type) */
int pvr2_ctrl_get_min(struct pvr2_ctrl *cptr)
{
	int ret = 0;
	if (!cptr) return 0;
	LOCK_TAKE(cptr->hdw->big_lock); do {
		if (cptr->info->get_min_value) {
			cptr->info->get_min_value(cptr,&ret);
		} else if (cptr->info->type == pvr2_ctl_int) {
			ret = cptr->info->def.type_int.min_value;
		}
	} while(0); LOCK_GIVE(cptr->hdw->big_lock);
	return ret;
}


/* Retrieve control's default value (any type) */
int pvr2_ctrl_get_def(struct pvr2_ctrl *cptr, int *valptr)
{
	int ret = 0;
	if (!cptr) return -EINVAL;
	LOCK_TAKE(cptr->hdw->big_lock); do {
		if (cptr->info->get_def_value) {
			ret = cptr->info->get_def_value(cptr, valptr);
		} else {
			*valptr = cptr->info->default_value;
		}
	} while(0); LOCK_GIVE(cptr->hdw->big_lock);
	return ret;
}


/* Retrieve control's enumeration count (enum only) */
int pvr2_ctrl_get_cnt(struct pvr2_ctrl *cptr)
{
	int ret = 0;
	if (!cptr) return 0;
	LOCK_TAKE(cptr->hdw->big_lock); do {
		if (cptr->info->type == pvr2_ctl_enum) {
			ret = cptr->info->def.type_enum.count;
		}
	} while(0); LOCK_GIVE(cptr->hdw->big_lock);
	return ret;
}


/* Retrieve control's valid mask bits (bit mask only) */
int pvr2_ctrl_get_mask(struct pvr2_ctrl *cptr)
{
	int ret = 0;
	if (!cptr) return 0;
	LOCK_TAKE(cptr->hdw->big_lock); do {
		if (cptr->info->type == pvr2_ctl_bitmask) {
			ret = cptr->info->def.type_bitmask.valid_bits;
		}
	} while(0); LOCK_GIVE(cptr->hdw->big_lock);
	return ret;
}


/* Retrieve the control's name */
const char *pvr2_ctrl_get_name(struct pvr2_ctrl *cptr)
{
	if (!cptr) return NULL;
	return cptr->info->name;
}


/* Retrieve the control's desc */
const char *pvr2_ctrl_get_desc(struct pvr2_ctrl *cptr)
{
	if (!cptr) return NULL;
	return cptr->info->desc;
}


/* Retrieve a control enumeration or bit mask value */
int pvr2_ctrl_get_valname(struct pvr2_ctrl *cptr,int val,
			  char *bptr,unsigned int bmax,
			  unsigned int *blen)
{
	int ret = -EINVAL;
	if (!cptr) return 0;
	*blen = 0;
	LOCK_TAKE(cptr->hdw->big_lock); do {
		if (cptr->info->type == pvr2_ctl_enum) {
			const char * const *names;
			names = cptr->info->def.type_enum.value_names;
			if (pvr2_ctrl_range_check(cptr,val) == 0) {
				if (names[val]) {
					*blen = scnprintf(
						bptr,bmax,"%s",
						names[val]);
				} else {
					*blen = 0;
				}
				ret = 0;
			}
		} else if (cptr->info->type == pvr2_ctl_bitmask) {
			const char **names;
			unsigned int idx;
			int msk;
			names = cptr->info->def.type_bitmask.bit_names;
			val &= cptr->info->def.type_bitmask.valid_bits;
			for (idx = 0, msk = 1; val; idx++, msk <<= 1) {
				if (val & msk) {
					*blen = scnprintf(bptr,bmax,"%s",
							  names[idx]);
					ret = 0;
					break;
				}
			}
		}
	} while(0); LOCK_GIVE(cptr->hdw->big_lock);
	return ret;
}


/* Return V4L ID for this control or zero if none */
int pvr2_ctrl_get_v4lid(struct pvr2_ctrl *cptr)
{
	if (!cptr) return 0;
	return cptr->info->v4l_id;
}


unsigned int pvr2_ctrl_get_v4lflags(struct pvr2_ctrl *cptr)
{
	unsigned int flags = 0;

	if (cptr->info->get_v4lflags) {
		flags = cptr->info->get_v4lflags(cptr);
	}

	if (cptr->info->set_value) {
		flags &= ~V4L2_CTRL_FLAG_READ_ONLY;
	} else {
		flags |= V4L2_CTRL_FLAG_READ_ONLY;
	}

	return flags;
}


/* Return true if control is writable */
int pvr2_ctrl_is_writable(struct pvr2_ctrl *cptr)
{
	if (!cptr) return 0;
	return cptr->info->set_value != NULL;
}


/* Return true if control has custom symbolic representation */
int pvr2_ctrl_has_custom_symbols(struct pvr2_ctrl *cptr)
{
	if (!cptr) return 0;
	if (!cptr->info->val_to_sym) return 0;
	if (!cptr->info->sym_to_val) return 0;
	return !0;
}


/* Convert a given mask/val to a custom symbolic value */
int pvr2_ctrl_custom_value_to_sym(struct pvr2_ctrl *cptr,
				  int mask,int val,
				  char *buf,unsigned int maxlen,
				  unsigned int *len)
{
	if (!cptr) return -EINVAL;
	if (!cptr->info->val_to_sym) return -EINVAL;
	return cptr->info->val_to_sym(cptr,mask,val,buf,maxlen,len);
}


/* Convert a symbolic value to a mask/value pair */
int pvr2_ctrl_custom_sym_to_value(struct pvr2_ctrl *cptr,
				  const char *buf,unsigned int len,
				  int *maskptr,int *valptr)
{
	if (!cptr) return -EINVAL;
	if (!cptr->info->sym_to_val) return -EINVAL;
	return cptr->info->sym_to_val(cptr,buf,len,maskptr,valptr);
}


static unsigned int gen_bitmask_string(int msk,int val,int msk_only,
				       const char **names,
				       char *ptr,unsigned int len)
{
	unsigned int idx;
	long sm,um;
	int spcFl;
	unsigned int uc,cnt;
	const char *idStr;

	spcFl = 0;
	uc = 0;
	um = 0;
	for (idx = 0, sm = 1; msk; idx++, sm <<= 1) {
		if (sm & msk) {
			msk &= ~sm;
			idStr = names[idx];
			if (idStr) {
				cnt = scnprintf(ptr,len,"%s%s%s",
						(spcFl ? " " : ""),
						(msk_only ? "" :
						 ((val & sm) ? "+" : "-")),
						idStr);
				ptr += cnt; len -= cnt; uc += cnt;
				spcFl = !0;
			} else {
				um |= sm;
			}
		}
	}
	if (um) {
		if (msk_only) {
			cnt = scnprintf(ptr,len,"%s0x%lx",
					(spcFl ? " " : ""),
					um);
			ptr += cnt; len -= cnt; uc += cnt;
			spcFl = !0;
		} else if (um & val) {
			cnt = scnprintf(ptr,len,"%s+0x%lx",
					(spcFl ? " " : ""),
					um & val);
			ptr += cnt; len -= cnt; uc += cnt;
			spcFl = !0;
		} else if (um & ~val) {
			cnt = scnprintf(ptr,len,"%s+0x%lx",
					(spcFl ? " " : ""),
					um & ~val);
			ptr += cnt; len -= cnt; uc += cnt;
			spcFl = !0;
		}
	}
	return uc;
}


static const char *boolNames[] = {
	"false",
	"true",
	"no",
	"yes",
};


static int parse_token(const char *ptr,unsigned int len,
		       int *valptr,
		       const char * const *names, unsigned int namecnt)
{
	unsigned int slen;
	unsigned int idx;
	*valptr = 0;
	if (!names) namecnt = 0;
	for (idx = 0; idx < namecnt; idx++) {
		if (!names[idx]) continue;
		slen = strlen(names[idx]);
		if (slen != len) continue;
		if (memcmp(names[idx],ptr,slen)) continue;
		*valptr = idx;
		return 0;
	}
	return kstrtoint(ptr, 0, valptr) ? -EINVAL : 1;
}


static int parse_mtoken(const char *ptr,unsigned int len,
			int *valptr,
			const char **names,int valid_bits)
{
	unsigned int slen;
	unsigned int idx;
	int msk;
	*valptr = 0;
	for (idx = 0, msk = 1; valid_bits; idx++, msk <<= 1) {
		if (!(msk & valid_bits)) continue;
		valid_bits &= ~msk;
		if (!names[idx]) continue;
		slen = strlen(names[idx]);
		if (slen != len) continue;
		if (memcmp(names[idx],ptr,slen)) continue;
		*valptr = msk;
		return 0;
	}
	return kstrtoint(ptr, 0, valptr);
}


static int parse_tlist(const char *ptr,unsigned int len,
		       int *maskptr,int *valptr,
		       const char **names,int valid_bits)
{
	unsigned int cnt;
	int mask,val,kv,mode,ret;
	mask = 0;
	val = 0;
	ret = 0;
	while (len) {
		cnt = 0;
		while ((cnt < len) &&
		       ((ptr[cnt] <= 32) ||
			(ptr[cnt] >= 127))) cnt++;
		ptr += cnt;
		len -= cnt;
		mode = 0;
		if ((*ptr == '-') || (*ptr == '+')) {
			mode = (*ptr == '-') ? -1 : 1;
			ptr++;
			len--;
		}
		cnt = 0;
		while (cnt < len) {
			if (ptr[cnt] <= 32) break;
			if (ptr[cnt] >= 127) break;
			cnt++;
		}
		if (!cnt) break;
		if (parse_mtoken(ptr,cnt,&kv,names,valid_bits)) {
			ret = -EINVAL;
			break;
		}
		ptr += cnt;
		len -= cnt;
		switch (mode) {
		case 0:
			mask = valid_bits;
			val |= kv;
			break;
		case -1:
			mask |= kv;
			val &= ~kv;
			break;
		case 1:
			mask |= kv;
			val |= kv;
			break;
		default:
			break;
		}
	}
	*maskptr = mask;
	*valptr = val;
	return ret;
}


/* Convert a symbolic value to a mask/value pair */
int pvr2_ctrl_sym_to_value(struct pvr2_ctrl *cptr,
			   const char *ptr,unsigned int len,
			   int *maskptr,int *valptr)
{
	int ret = -EINVAL;
	unsigned int cnt;

	*maskptr = 0;
	*valptr = 0;

	cnt = 0;
	while ((cnt < len) && ((ptr[cnt] <= 32) || (ptr[cnt] >= 127))) cnt++;
	len -= cnt; ptr += cnt;
	cnt = 0;
	while ((cnt < len) && ((ptr[len-(cnt+1)] <= 32) ||
			       (ptr[len-(cnt+1)] >= 127))) cnt++;
	len -= cnt;

	if (!len) return -EINVAL;

	LOCK_TAKE(cptr->hdw->big_lock); do {
		if (cptr->info->type == pvr2_ctl_int) {
			ret = parse_token(ptr,len,valptr,NULL,0);
			if (ret >= 0) {
				ret = pvr2_ctrl_range_check(cptr,*valptr);
			}
			*maskptr = ~0;
		} else if (cptr->info->type == pvr2_ctl_bool) {
			ret = parse_token(ptr,len,valptr,boolNames,
					  ARRAY_SIZE(boolNames));
			if (ret == 1) {
				*valptr = *valptr ? !0 : 0;
			} else if (ret == 0) {
				*valptr = (*valptr & 1) ? !0 : 0;
			}
			*maskptr = 1;
		} else if (cptr->info->type == pvr2_ctl_enum) {
			ret = parse_token(
				ptr,len,valptr,
				cptr->info->def.type_enum.value_names,
				cptr->info->def.type_enum.count);
			if (ret >= 0) {
				ret = pvr2_ctrl_range_check(cptr,*valptr);
			}
			*maskptr = ~0;
		} else if (cptr->info->type == pvr2_ctl_bitmask) {
			ret = parse_tlist(
				ptr,len,maskptr,valptr,
				cptr->info->def.type_bitmask.bit_names,
				cptr->info->def.type_bitmask.valid_bits);
		}
	} while(0); LOCK_GIVE(cptr->hdw->big_lock);
	return ret;
}


/* Convert a given mask/val to a symbolic value */
int pvr2_ctrl_value_to_sym_internal(struct pvr2_ctrl *cptr,
				    int mask,int val,
				    char *buf,unsigned int maxlen,
				    unsigned int *len)
{
	int ret = -EINVAL;

	*len = 0;
	if (cptr->info->type == pvr2_ctl_int) {
		*len = scnprintf(buf,maxlen,"%d",val);
		ret = 0;
	} else if (cptr->info->type == pvr2_ctl_bool) {
		*len = scnprintf(buf,maxlen,"%s",val ? "true" : "false");
		ret = 0;
	} else if (cptr->info->type == pvr2_ctl_enum) {
		const char * const *names;
		names = cptr->info->def.type_enum.value_names;
		if ((val >= 0) &&
		    (val < cptr->info->def.type_enum.count)) {
			if (names[val]) {
				*len = scnprintf(
					buf,maxlen,"%s",
					names[val]);
			} else {
				*len = 0;
			}
			ret = 0;
		}
	} else if (cptr->info->type == pvr2_ctl_bitmask) {
		*len = gen_bitmask_string(
			val & mask & cptr->info->def.type_bitmask.valid_bits,
			~0,!0,
			cptr->info->def.type_bitmask.bit_names,
			buf,maxlen);
	}
	return ret;
}


/* Convert a given mask/val to a symbolic value */
int pvr2_ctrl_value_to_sym(struct pvr2_ctrl *cptr,
			   int mask,int val,
			   char *buf,unsigned int maxlen,
			   unsigned int *len)
{
	int ret;
	LOCK_TAKE(cptr->hdw->big_lock); do {
		ret = pvr2_ctrl_value_to_sym_internal(cptr,mask,val,
						      buf,maxlen,len);
	} while(0); LOCK_GIVE(cptr->hdw->big_lock);
	return ret;
}
