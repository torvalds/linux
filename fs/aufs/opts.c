/*
 * Copyright (C) 2005-2017 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * mount options/flags
 */

#include <linux/namei.h>
#include <linux/types.h> /* a distribution requires */
#include <linux/parser.h>
#include "aufs.h"

/* ---------------------------------------------------------------------- */

enum {
	Opt_br,
	Opt_add, Opt_del, Opt_mod, Opt_append, Opt_prepend,
	Opt_idel, Opt_imod,
	Opt_dirwh, Opt_rdcache, Opt_rdblk, Opt_rdhash,
	Opt_rdblk_def, Opt_rdhash_def,
	Opt_xino, Opt_noxino,
	Opt_trunc_xino, Opt_trunc_xino_v, Opt_notrunc_xino,
	Opt_trunc_xino_path, Opt_itrunc_xino,
	Opt_trunc_xib, Opt_notrunc_xib,
	Opt_shwh, Opt_noshwh,
	Opt_plink, Opt_noplink, Opt_list_plink,
	Opt_udba,
	Opt_dio, Opt_nodio,
	Opt_diropq_a, Opt_diropq_w,
	Opt_warn_perm, Opt_nowarn_perm,
	Opt_wbr_copyup, Opt_wbr_create,
	Opt_fhsm_sec,
	Opt_verbose, Opt_noverbose,
	Opt_sum, Opt_nosum, Opt_wsum,
	Opt_dirperm1, Opt_nodirperm1,
	Opt_dirren, Opt_nodirren,
	Opt_acl, Opt_noacl,
	Opt_tail, Opt_ignore, Opt_ignore_silent, Opt_err
};

static match_table_t options = {
	{Opt_br, "br=%s"},
	{Opt_br, "br:%s"},

	{Opt_add, "add=%d:%s"},
	{Opt_add, "add:%d:%s"},
	{Opt_add, "ins=%d:%s"},
	{Opt_add, "ins:%d:%s"},
	{Opt_append, "append=%s"},
	{Opt_append, "append:%s"},
	{Opt_prepend, "prepend=%s"},
	{Opt_prepend, "prepend:%s"},

	{Opt_del, "del=%s"},
	{Opt_del, "del:%s"},
	/* {Opt_idel, "idel:%d"}, */
	{Opt_mod, "mod=%s"},
	{Opt_mod, "mod:%s"},
	/* {Opt_imod, "imod:%d:%s"}, */

	{Opt_dirwh, "dirwh=%d"},

	{Opt_xino, "xino=%s"},
	{Opt_noxino, "noxino"},
	{Opt_trunc_xino, "trunc_xino"},
	{Opt_trunc_xino_v, "trunc_xino_v=%d:%d"},
	{Opt_notrunc_xino, "notrunc_xino"},
	{Opt_trunc_xino_path, "trunc_xino=%s"},
	{Opt_itrunc_xino, "itrunc_xino=%d"},
	/* {Opt_zxino, "zxino=%s"}, */
	{Opt_trunc_xib, "trunc_xib"},
	{Opt_notrunc_xib, "notrunc_xib"},

#ifdef CONFIG_PROC_FS
	{Opt_plink, "plink"},
#else
	{Opt_ignore_silent, "plink"},
#endif

	{Opt_noplink, "noplink"},

#ifdef CONFIG_AUFS_DEBUG
	{Opt_list_plink, "list_plink"},
#endif

	{Opt_udba, "udba=%s"},

	{Opt_dio, "dio"},
	{Opt_nodio, "nodio"},

#ifdef CONFIG_AUFS_DIRREN
	{Opt_dirren, "dirren"},
	{Opt_nodirren, "nodirren"},
#else
	{Opt_ignore, "dirren"},
	{Opt_ignore_silent, "nodirren"},
#endif

#ifdef CONFIG_AUFS_FHSM
	{Opt_fhsm_sec, "fhsm_sec=%d"},
#else
	{Opt_ignore, "fhsm_sec=%d"},
#endif

	{Opt_diropq_a, "diropq=always"},
	{Opt_diropq_a, "diropq=a"},
	{Opt_diropq_w, "diropq=whiteouted"},
	{Opt_diropq_w, "diropq=w"},

	{Opt_warn_perm, "warn_perm"},
	{Opt_nowarn_perm, "nowarn_perm"},

	/* keep them temporary */
	{Opt_ignore_silent, "nodlgt"},
	{Opt_ignore, "clean_plink"},

#ifdef CONFIG_AUFS_SHWH
	{Opt_shwh, "shwh"},
#endif
	{Opt_noshwh, "noshwh"},

	{Opt_dirperm1, "dirperm1"},
	{Opt_nodirperm1, "nodirperm1"},

	{Opt_verbose, "verbose"},
	{Opt_verbose, "v"},
	{Opt_noverbose, "noverbose"},
	{Opt_noverbose, "quiet"},
	{Opt_noverbose, "q"},
	{Opt_noverbose, "silent"},

	{Opt_sum, "sum"},
	{Opt_nosum, "nosum"},
	{Opt_wsum, "wsum"},

	{Opt_rdcache, "rdcache=%d"},
	{Opt_rdblk, "rdblk=%d"},
	{Opt_rdblk_def, "rdblk=def"},
	{Opt_rdhash, "rdhash=%d"},
	{Opt_rdhash_def, "rdhash=def"},

	{Opt_wbr_create, "create=%s"},
	{Opt_wbr_create, "create_policy=%s"},
	{Opt_wbr_copyup, "cpup=%s"},
	{Opt_wbr_copyup, "copyup=%s"},
	{Opt_wbr_copyup, "copyup_policy=%s"},

	/* generic VFS flag */
#ifdef CONFIG_FS_POSIX_ACL
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
#else
	{Opt_ignore, "acl"},
	{Opt_ignore_silent, "noacl"},
#endif

	/* internal use for the scripts */
	{Opt_ignore_silent, "si=%s"},

	{Opt_br, "dirs=%s"},
	{Opt_ignore, "debug=%d"},
	{Opt_ignore, "delete=whiteout"},
	{Opt_ignore, "delete=all"},
	{Opt_ignore, "imap=%s"},

	/* temporary workaround, due to old mount(8)? */
	{Opt_ignore_silent, "relatime"},

	{Opt_err, NULL}
};

/* ---------------------------------------------------------------------- */

static const char *au_parser_pattern(int val, match_table_t tbl)
{
	struct match_token *p;

	p = tbl;
	while (p->pattern) {
		if (p->token == val)
			return p->pattern;
		p++;
	}
	BUG();
	return "??";
}

static const char *au_optstr(int *val, match_table_t tbl)
{
	struct match_token *p;
	int v;

	v = *val;
	if (!v)
		goto out;
	p = tbl;
	while (p->pattern) {
		if (p->token
		    && (v & p->token) == p->token) {
			*val &= ~p->token;
			return p->pattern;
		}
		p++;
	}

out:
	return NULL;
}

/* ---------------------------------------------------------------------- */

static match_table_t brperm = {
	{AuBrPerm_RO, AUFS_BRPERM_RO},
	{AuBrPerm_RR, AUFS_BRPERM_RR},
	{AuBrPerm_RW, AUFS_BRPERM_RW},
	{0, NULL}
};

static match_table_t brattr = {
	/* general */
	{AuBrAttr_COO_REG, AUFS_BRATTR_COO_REG},
	{AuBrAttr_COO_ALL, AUFS_BRATTR_COO_ALL},
	/* 'unpin' attrib is meaningless since linux-3.18-rc1 */
	{AuBrAttr_UNPIN, AUFS_BRATTR_UNPIN},
#ifdef CONFIG_AUFS_FHSM
	{AuBrAttr_FHSM, AUFS_BRATTR_FHSM},
#endif
#ifdef CONFIG_AUFS_XATTR
	{AuBrAttr_ICEX, AUFS_BRATTR_ICEX},
	{AuBrAttr_ICEX_SEC, AUFS_BRATTR_ICEX_SEC},
	{AuBrAttr_ICEX_SYS, AUFS_BRATTR_ICEX_SYS},
	{AuBrAttr_ICEX_TR, AUFS_BRATTR_ICEX_TR},
	{AuBrAttr_ICEX_USR, AUFS_BRATTR_ICEX_USR},
	{AuBrAttr_ICEX_OTH, AUFS_BRATTR_ICEX_OTH},
#endif

	/* ro/rr branch */
	{AuBrRAttr_WH, AUFS_BRRATTR_WH},

	/* rw branch */
	{AuBrWAttr_MOO, AUFS_BRWATTR_MOO},
	{AuBrWAttr_NoLinkWH, AUFS_BRWATTR_NLWH},

	{0, NULL}
};

static int br_attr_val(char *str, match_table_t table, substring_t args[])
{
	int attr, v;
	char *p;

	attr = 0;
	do {
		p = strchr(str, '+');
		if (p)
			*p = 0;
		v = match_token(str, table, args);
		if (v) {
			if (v & AuBrAttr_CMOO_Mask)
				attr &= ~AuBrAttr_CMOO_Mask;
			attr |= v;
		} else {
			if (p)
				*p = '+';
			pr_warn("ignored branch attribute %s\n", str);
			break;
		}
		if (p)
			str = p + 1;
	} while (p);

	return attr;
}

static int au_do_optstr_br_attr(au_br_perm_str_t *str, int perm)
{
	int sz;
	const char *p;
	char *q;

	q = str->a;
	*q = 0;
	p = au_optstr(&perm, brattr);
	if (p) {
		sz = strlen(p);
		memcpy(q, p, sz + 1);
		q += sz;
	} else
		goto out;

	do {
		p = au_optstr(&perm, brattr);
		if (p) {
			*q++ = '+';
			sz = strlen(p);
			memcpy(q, p, sz + 1);
			q += sz;
		}
	} while (p);

out:
	return q - str->a;
}

static int noinline_for_stack br_perm_val(char *perm)
{
	int val, bad, sz;
	char *p;
	substring_t args[MAX_OPT_ARGS];
	au_br_perm_str_t attr;

	p = strchr(perm, '+');
	if (p)
		*p = 0;
	val = match_token(perm, brperm, args);
	if (!val) {
		if (p)
			*p = '+';
		pr_warn("ignored branch permission %s\n", perm);
		val = AuBrPerm_RO;
		goto out;
	}
	if (!p)
		goto out;

	val |= br_attr_val(p + 1, brattr, args);

	bad = 0;
	switch (val & AuBrPerm_Mask) {
	case AuBrPerm_RO:
	case AuBrPerm_RR:
		bad = val & AuBrWAttr_Mask;
		val &= ~AuBrWAttr_Mask;
		break;
	case AuBrPerm_RW:
		bad = val & AuBrRAttr_Mask;
		val &= ~AuBrRAttr_Mask;
		break;
	}

	/*
	 * 'unpin' attrib becomes meaningless since linux-3.18-rc1, but aufs
	 * does not treat it as an error, just warning.
	 * this is a tiny guard for the user operation.
	 */
	if (val & AuBrAttr_UNPIN) {
		bad |= AuBrAttr_UNPIN;
		val &= ~AuBrAttr_UNPIN;
	}

	if (unlikely(bad)) {
		sz = au_do_optstr_br_attr(&attr, bad);
		AuDebugOn(!sz);
		pr_warn("ignored branch attribute %s\n", attr.a);
	}

out:
	return val;
}

void au_optstr_br_perm(au_br_perm_str_t *str, int perm)
{
	au_br_perm_str_t attr;
	const char *p;
	char *q;
	int sz;

	q = str->a;
	p = au_optstr(&perm, brperm);
	AuDebugOn(!p || !*p);
	sz = strlen(p);
	memcpy(q, p, sz + 1);
	q += sz;

	sz = au_do_optstr_br_attr(&attr, perm);
	if (sz) {
		*q++ = '+';
		memcpy(q, attr.a, sz + 1);
	}

	AuDebugOn(strlen(str->a) >= sizeof(str->a));
}

/* ---------------------------------------------------------------------- */

static match_table_t udbalevel = {
	{AuOpt_UDBA_REVAL, "reval"},
	{AuOpt_UDBA_NONE, "none"},
#ifdef CONFIG_AUFS_HNOTIFY
	{AuOpt_UDBA_HNOTIFY, "notify"}, /* abstraction */
#ifdef CONFIG_AUFS_HFSNOTIFY
	{AuOpt_UDBA_HNOTIFY, "fsnotify"},
#endif
#endif
	{-1, NULL}
};

static int noinline_for_stack udba_val(char *str)
{
	substring_t args[MAX_OPT_ARGS];

	return match_token(str, udbalevel, args);
}

const char *au_optstr_udba(int udba)
{
	return au_parser_pattern(udba, udbalevel);
}

/* ---------------------------------------------------------------------- */

static match_table_t au_wbr_create_policy = {
	{AuWbrCreate_TDP, "tdp"},
	{AuWbrCreate_TDP, "top-down-parent"},
	{AuWbrCreate_RR, "rr"},
	{AuWbrCreate_RR, "round-robin"},
	{AuWbrCreate_MFS, "mfs"},
	{AuWbrCreate_MFS, "most-free-space"},
	{AuWbrCreate_MFSV, "mfs:%d"},
	{AuWbrCreate_MFSV, "most-free-space:%d"},

	/* top-down regardless the parent, and then mfs */
	{AuWbrCreate_TDMFS, "tdmfs:%d"},
	{AuWbrCreate_TDMFSV, "tdmfs:%d:%d"},

	{AuWbrCreate_MFSRR, "mfsrr:%d"},
	{AuWbrCreate_MFSRRV, "mfsrr:%d:%d"},
	{AuWbrCreate_PMFS, "pmfs"},
	{AuWbrCreate_PMFSV, "pmfs:%d"},
	{AuWbrCreate_PMFSRR, "pmfsrr:%d"},
	{AuWbrCreate_PMFSRRV, "pmfsrr:%d:%d"},

	{-1, NULL}
};

static int au_wbr_mfs_wmark(substring_t *arg, char *str,
			    struct au_opt_wbr_create *create)
{
	int err;
	unsigned long long ull;

	err = 0;
	if (!match_u64(arg, &ull))
		create->mfsrr_watermark = ull;
	else {
		pr_err("bad integer in %s\n", str);
		err = -EINVAL;
	}

	return err;
}

static int au_wbr_mfs_sec(substring_t *arg, char *str,
			  struct au_opt_wbr_create *create)
{
	int n, err;

	err = 0;
	if (!match_int(arg, &n) && 0 <= n && n <= AUFS_MFS_MAX_SEC)
		create->mfs_second = n;
	else {
		pr_err("bad integer in %s\n", str);
		err = -EINVAL;
	}

	return err;
}

static int noinline_for_stack
au_wbr_create_val(char *str, struct au_opt_wbr_create *create)
{
	int err, e;
	substring_t args[MAX_OPT_ARGS];

	err = match_token(str, au_wbr_create_policy, args);
	create->wbr_create = err;
	switch (err) {
	case AuWbrCreate_MFSRRV:
	case AuWbrCreate_TDMFSV:
	case AuWbrCreate_PMFSRRV:
		e = au_wbr_mfs_wmark(&args[0], str, create);
		if (!e)
			e = au_wbr_mfs_sec(&args[1], str, create);
		if (unlikely(e))
			err = e;
		break;
	case AuWbrCreate_MFSRR:
	case AuWbrCreate_TDMFS:
	case AuWbrCreate_PMFSRR:
		e = au_wbr_mfs_wmark(&args[0], str, create);
		if (unlikely(e)) {
			err = e;
			break;
		}
		/*FALLTHROUGH*/
	case AuWbrCreate_MFS:
	case AuWbrCreate_PMFS:
		create->mfs_second = AUFS_MFS_DEF_SEC;
		break;
	case AuWbrCreate_MFSV:
	case AuWbrCreate_PMFSV:
		e = au_wbr_mfs_sec(&args[0], str, create);
		if (unlikely(e))
			err = e;
		break;
	}

	return err;
}

const char *au_optstr_wbr_create(int wbr_create)
{
	return au_parser_pattern(wbr_create, au_wbr_create_policy);
}

static match_table_t au_wbr_copyup_policy = {
	{AuWbrCopyup_TDP, "tdp"},
	{AuWbrCopyup_TDP, "top-down-parent"},
	{AuWbrCopyup_BUP, "bup"},
	{AuWbrCopyup_BUP, "bottom-up-parent"},
	{AuWbrCopyup_BU, "bu"},
	{AuWbrCopyup_BU, "bottom-up"},
	{-1, NULL}
};

static int noinline_for_stack au_wbr_copyup_val(char *str)
{
	substring_t args[MAX_OPT_ARGS];

	return match_token(str, au_wbr_copyup_policy, args);
}

const char *au_optstr_wbr_copyup(int wbr_copyup)
{
	return au_parser_pattern(wbr_copyup, au_wbr_copyup_policy);
}

/* ---------------------------------------------------------------------- */

static const int lkup_dirflags = LOOKUP_FOLLOW | LOOKUP_DIRECTORY;

static void dump_opts(struct au_opts *opts)
{
#ifdef CONFIG_AUFS_DEBUG
	/* reduce stack space */
	union {
		struct au_opt_add *add;
		struct au_opt_del *del;
		struct au_opt_mod *mod;
		struct au_opt_xino *xino;
		struct au_opt_xino_itrunc *xino_itrunc;
		struct au_opt_wbr_create *create;
	} u;
	struct au_opt *opt;

	opt = opts->opt;
	while (opt->type != Opt_tail) {
		switch (opt->type) {
		case Opt_add:
			u.add = &opt->add;
			AuDbg("add {b%d, %s, 0x%x, %p}\n",
				  u.add->bindex, u.add->pathname, u.add->perm,
				  u.add->path.dentry);
			break;
		case Opt_del:
		case Opt_idel:
			u.del = &opt->del;
			AuDbg("del {%s, %p}\n",
			      u.del->pathname, u.del->h_path.dentry);
			break;
		case Opt_mod:
		case Opt_imod:
			u.mod = &opt->mod;
			AuDbg("mod {%s, 0x%x, %p}\n",
				  u.mod->path, u.mod->perm, u.mod->h_root);
			break;
		case Opt_append:
			u.add = &opt->add;
			AuDbg("append {b%d, %s, 0x%x, %p}\n",
				  u.add->bindex, u.add->pathname, u.add->perm,
				  u.add->path.dentry);
			break;
		case Opt_prepend:
			u.add = &opt->add;
			AuDbg("prepend {b%d, %s, 0x%x, %p}\n",
				  u.add->bindex, u.add->pathname, u.add->perm,
				  u.add->path.dentry);
			break;
		case Opt_dirwh:
			AuDbg("dirwh %d\n", opt->dirwh);
			break;
		case Opt_rdcache:
			AuDbg("rdcache %d\n", opt->rdcache);
			break;
		case Opt_rdblk:
			AuDbg("rdblk %u\n", opt->rdblk);
			break;
		case Opt_rdblk_def:
			AuDbg("rdblk_def\n");
			break;
		case Opt_rdhash:
			AuDbg("rdhash %u\n", opt->rdhash);
			break;
		case Opt_rdhash_def:
			AuDbg("rdhash_def\n");
			break;
		case Opt_xino:
			u.xino = &opt->xino;
			AuDbg("xino {%s %pD}\n", u.xino->path, u.xino->file);
			break;
		case Opt_trunc_xino:
			AuLabel(trunc_xino);
			break;
		case Opt_notrunc_xino:
			AuLabel(notrunc_xino);
			break;
		case Opt_trunc_xino_path:
		case Opt_itrunc_xino:
			u.xino_itrunc = &opt->xino_itrunc;
			AuDbg("trunc_xino %d\n", u.xino_itrunc->bindex);
			break;
		case Opt_noxino:
			AuLabel(noxino);
			break;
		case Opt_trunc_xib:
			AuLabel(trunc_xib);
			break;
		case Opt_notrunc_xib:
			AuLabel(notrunc_xib);
			break;
		case Opt_shwh:
			AuLabel(shwh);
			break;
		case Opt_noshwh:
			AuLabel(noshwh);
			break;
		case Opt_dirperm1:
			AuLabel(dirperm1);
			break;
		case Opt_nodirperm1:
			AuLabel(nodirperm1);
			break;
		case Opt_plink:
			AuLabel(plink);
			break;
		case Opt_noplink:
			AuLabel(noplink);
			break;
		case Opt_list_plink:
			AuLabel(list_plink);
			break;
		case Opt_udba:
			AuDbg("udba %d, %s\n",
				  opt->udba, au_optstr_udba(opt->udba));
			break;
		case Opt_dio:
			AuLabel(dio);
			break;
		case Opt_nodio:
			AuLabel(nodio);
			break;
		case Opt_diropq_a:
			AuLabel(diropq_a);
			break;
		case Opt_diropq_w:
			AuLabel(diropq_w);
			break;
		case Opt_warn_perm:
			AuLabel(warn_perm);
			break;
		case Opt_nowarn_perm:
			AuLabel(nowarn_perm);
			break;
		case Opt_verbose:
			AuLabel(verbose);
			break;
		case Opt_noverbose:
			AuLabel(noverbose);
			break;
		case Opt_sum:
			AuLabel(sum);
			break;
		case Opt_nosum:
			AuLabel(nosum);
			break;
		case Opt_wsum:
			AuLabel(wsum);
			break;
		case Opt_wbr_create:
			u.create = &opt->wbr_create;
			AuDbg("create %d, %s\n", u.create->wbr_create,
				  au_optstr_wbr_create(u.create->wbr_create));
			switch (u.create->wbr_create) {
			case AuWbrCreate_MFSV:
			case AuWbrCreate_PMFSV:
				AuDbg("%d sec\n", u.create->mfs_second);
				break;
			case AuWbrCreate_MFSRR:
			case AuWbrCreate_TDMFS:
				AuDbg("%llu watermark\n",
					  u.create->mfsrr_watermark);
				break;
			case AuWbrCreate_MFSRRV:
			case AuWbrCreate_TDMFSV:
			case AuWbrCreate_PMFSRRV:
				AuDbg("%llu watermark, %d sec\n",
					  u.create->mfsrr_watermark,
					  u.create->mfs_second);
				break;
			}
			break;
		case Opt_wbr_copyup:
			AuDbg("copyup %d, %s\n", opt->wbr_copyup,
				  au_optstr_wbr_copyup(opt->wbr_copyup));
			break;
		case Opt_fhsm_sec:
			AuDbg("fhsm_sec %u\n", opt->fhsm_second);
			break;
		case Opt_dirren:
			AuLabel(dirren);
			break;
		case Opt_nodirren:
			AuLabel(nodirren);
			break;
		case Opt_acl:
			AuLabel(acl);
			break;
		case Opt_noacl:
			AuLabel(noacl);
			break;
		default:
			BUG();
		}
		opt++;
	}
#endif
}

void au_opts_free(struct au_opts *opts)
{
	struct au_opt *opt;

	opt = opts->opt;
	while (opt->type != Opt_tail) {
		switch (opt->type) {
		case Opt_add:
		case Opt_append:
		case Opt_prepend:
			path_put(&opt->add.path);
			break;
		case Opt_del:
		case Opt_idel:
			path_put(&opt->del.h_path);
			break;
		case Opt_mod:
		case Opt_imod:
			dput(opt->mod.h_root);
			break;
		case Opt_xino:
			fput(opt->xino.file);
			break;
		}
		opt++;
	}
}

static int opt_add(struct au_opt *opt, char *opt_str, unsigned long sb_flags,
		   aufs_bindex_t bindex)
{
	int err;
	struct au_opt_add *add = &opt->add;
	char *p;

	add->bindex = bindex;
	add->perm = AuBrPerm_RO;
	add->pathname = opt_str;
	p = strchr(opt_str, '=');
	if (p) {
		*p++ = 0;
		if (*p)
			add->perm = br_perm_val(p);
	}

	err = vfsub_kern_path(add->pathname, lkup_dirflags, &add->path);
	if (!err) {
		if (!p) {
			add->perm = AuBrPerm_RO;
			if (au_test_fs_rr(add->path.dentry->d_sb))
				add->perm = AuBrPerm_RR;
			else if (!bindex && !(sb_flags & SB_RDONLY))
				add->perm = AuBrPerm_RW;
		}
		opt->type = Opt_add;
		goto out;
	}
	pr_err("lookup failed %s (%d)\n", add->pathname, err);
	err = -EINVAL;

out:
	return err;
}

static int au_opts_parse_del(struct au_opt_del *del, substring_t args[])
{
	int err;

	del->pathname = args[0].from;
	AuDbg("del path %s\n", del->pathname);

	err = vfsub_kern_path(del->pathname, lkup_dirflags, &del->h_path);
	if (unlikely(err))
		pr_err("lookup failed %s (%d)\n", del->pathname, err);

	return err;
}

#if 0 /* reserved for future use */
static int au_opts_parse_idel(struct super_block *sb, aufs_bindex_t bindex,
			      struct au_opt_del *del, substring_t args[])
{
	int err;
	struct dentry *root;

	err = -EINVAL;
	root = sb->s_root;
	aufs_read_lock(root, AuLock_FLUSH);
	if (bindex < 0 || au_sbbot(sb) < bindex) {
		pr_err("out of bounds, %d\n", bindex);
		goto out;
	}

	err = 0;
	del->h_path.dentry = dget(au_h_dptr(root, bindex));
	del->h_path.mnt = mntget(au_sbr_mnt(sb, bindex));

out:
	aufs_read_unlock(root, !AuLock_IR);
	return err;
}
#endif

static int noinline_for_stack
au_opts_parse_mod(struct au_opt_mod *mod, substring_t args[])
{
	int err;
	struct path path;
	char *p;

	err = -EINVAL;
	mod->path = args[0].from;
	p = strchr(mod->path, '=');
	if (unlikely(!p)) {
		pr_err("no permssion %s\n", args[0].from);
		goto out;
	}

	*p++ = 0;
	err = vfsub_kern_path(mod->path, lkup_dirflags, &path);
	if (unlikely(err)) {
		pr_err("lookup failed %s (%d)\n", mod->path, err);
		goto out;
	}

	mod->perm = br_perm_val(p);
	AuDbg("mod path %s, perm 0x%x, %s\n", mod->path, mod->perm, p);
	mod->h_root = dget(path.dentry);
	path_put(&path);

out:
	return err;
}

#if 0 /* reserved for future use */
static int au_opts_parse_imod(struct super_block *sb, aufs_bindex_t bindex,
			      struct au_opt_mod *mod, substring_t args[])
{
	int err;
	struct dentry *root;

	err = -EINVAL;
	root = sb->s_root;
	aufs_read_lock(root, AuLock_FLUSH);
	if (bindex < 0 || au_sbbot(sb) < bindex) {
		pr_err("out of bounds, %d\n", bindex);
		goto out;
	}

	err = 0;
	mod->perm = br_perm_val(args[1].from);
	AuDbg("mod path %s, perm 0x%x, %s\n",
	      mod->path, mod->perm, args[1].from);
	mod->h_root = dget(au_h_dptr(root, bindex));

out:
	aufs_read_unlock(root, !AuLock_IR);
	return err;
}
#endif

static int au_opts_parse_xino(struct super_block *sb, struct au_opt_xino *xino,
			      substring_t args[])
{
	int err;
	struct file *file;

	file = au_xino_create(sb, args[0].from, /*silent*/0);
	err = PTR_ERR(file);
	if (IS_ERR(file))
		goto out;

	err = -EINVAL;
	if (unlikely(file->f_path.dentry->d_sb == sb)) {
		fput(file);
		pr_err("%s must be outside\n", args[0].from);
		goto out;
	}

	err = 0;
	xino->file = file;
	xino->path = args[0].from;

out:
	return err;
}

static int noinline_for_stack
au_opts_parse_xino_itrunc_path(struct super_block *sb,
			       struct au_opt_xino_itrunc *xino_itrunc,
			       substring_t args[])
{
	int err;
	aufs_bindex_t bbot, bindex;
	struct path path;
	struct dentry *root;

	err = vfsub_kern_path(args[0].from, lkup_dirflags, &path);
	if (unlikely(err)) {
		pr_err("lookup failed %s (%d)\n", args[0].from, err);
		goto out;
	}

	xino_itrunc->bindex = -1;
	root = sb->s_root;
	aufs_read_lock(root, AuLock_FLUSH);
	bbot = au_sbbot(sb);
	for (bindex = 0; bindex <= bbot; bindex++) {
		if (au_h_dptr(root, bindex) == path.dentry) {
			xino_itrunc->bindex = bindex;
			break;
		}
	}
	aufs_read_unlock(root, !AuLock_IR);
	path_put(&path);

	if (unlikely(xino_itrunc->bindex < 0)) {
		pr_err("no such branch %s\n", args[0].from);
		err = -EINVAL;
	}

out:
	return err;
}

/* called without aufs lock */
int au_opts_parse(struct super_block *sb, char *str, struct au_opts *opts)
{
	int err, n, token;
	aufs_bindex_t bindex;
	unsigned char skipped;
	struct dentry *root;
	struct au_opt *opt, *opt_tail;
	char *opt_str;
	/* reduce the stack space */
	union {
		struct au_opt_xino_itrunc *xino_itrunc;
		struct au_opt_wbr_create *create;
	} u;
	struct {
		substring_t args[MAX_OPT_ARGS];
	} *a;

	err = -ENOMEM;
	a = kmalloc(sizeof(*a), GFP_NOFS);
	if (unlikely(!a))
		goto out;

	root = sb->s_root;
	err = 0;
	bindex = 0;
	opt = opts->opt;
	opt_tail = opt + opts->max_opt - 1;
	opt->type = Opt_tail;
	while (!err && (opt_str = strsep(&str, ",")) && *opt_str) {
		err = -EINVAL;
		skipped = 0;
		token = match_token(opt_str, options, a->args);
		switch (token) {
		case Opt_br:
			err = 0;
			while (!err && (opt_str = strsep(&a->args[0].from, ":"))
			       && *opt_str) {
				err = opt_add(opt, opt_str, opts->sb_flags,
					      bindex++);
				if (unlikely(!err && ++opt > opt_tail)) {
					err = -E2BIG;
					break;
				}
				opt->type = Opt_tail;
				skipped = 1;
			}
			break;
		case Opt_add:
			if (unlikely(match_int(&a->args[0], &n))) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			bindex = n;
			err = opt_add(opt, a->args[1].from, opts->sb_flags,
				      bindex);
			if (!err)
				opt->type = token;
			break;
		case Opt_append:
			err = opt_add(opt, a->args[0].from, opts->sb_flags,
				      /*dummy bindex*/1);
			if (!err)
				opt->type = token;
			break;
		case Opt_prepend:
			err = opt_add(opt, a->args[0].from, opts->sb_flags,
				      /*bindex*/0);
			if (!err)
				opt->type = token;
			break;
		case Opt_del:
			err = au_opts_parse_del(&opt->del, a->args);
			if (!err)
				opt->type = token;
			break;
#if 0 /* reserved for future use */
		case Opt_idel:
			del->pathname = "(indexed)";
			if (unlikely(match_int(&args[0], &n))) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			err = au_opts_parse_idel(sb, n, &opt->del, a->args);
			if (!err)
				opt->type = token;
			break;
#endif
		case Opt_mod:
			err = au_opts_parse_mod(&opt->mod, a->args);
			if (!err)
				opt->type = token;
			break;
#ifdef IMOD /* reserved for future use */
		case Opt_imod:
			u.mod->path = "(indexed)";
			if (unlikely(match_int(&a->args[0], &n))) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			err = au_opts_parse_imod(sb, n, &opt->mod, a->args);
			if (!err)
				opt->type = token;
			break;
#endif
		case Opt_xino:
			err = au_opts_parse_xino(sb, &opt->xino, a->args);
			if (!err)
				opt->type = token;
			break;

		case Opt_trunc_xino_path:
			err = au_opts_parse_xino_itrunc_path
				(sb, &opt->xino_itrunc, a->args);
			if (!err)
				opt->type = token;
			break;

		case Opt_itrunc_xino:
			u.xino_itrunc = &opt->xino_itrunc;
			if (unlikely(match_int(&a->args[0], &n))) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			u.xino_itrunc->bindex = n;
			aufs_read_lock(root, AuLock_FLUSH);
			if (n < 0 || au_sbbot(sb) < n) {
				pr_err("out of bounds, %d\n", n);
				aufs_read_unlock(root, !AuLock_IR);
				break;
			}
			aufs_read_unlock(root, !AuLock_IR);
			err = 0;
			opt->type = token;
			break;

		case Opt_dirwh:
			if (unlikely(match_int(&a->args[0], &opt->dirwh)))
				break;
			err = 0;
			opt->type = token;
			break;

		case Opt_rdcache:
			if (unlikely(match_int(&a->args[0], &n))) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			if (unlikely(n > AUFS_RDCACHE_MAX)) {
				pr_err("rdcache must be smaller than %d\n",
				       AUFS_RDCACHE_MAX);
				break;
			}
			opt->rdcache = n;
			err = 0;
			opt->type = token;
			break;
		case Opt_rdblk:
			if (unlikely(match_int(&a->args[0], &n)
				     || n < 0
				     || n > KMALLOC_MAX_SIZE)) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			if (unlikely(n && n < NAME_MAX)) {
				pr_err("rdblk must be larger than %d\n",
				       NAME_MAX);
				break;
			}
			opt->rdblk = n;
			err = 0;
			opt->type = token;
			break;
		case Opt_rdhash:
			if (unlikely(match_int(&a->args[0], &n)
				     || n < 0
				     || n * sizeof(struct hlist_head)
				     > KMALLOC_MAX_SIZE)) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			opt->rdhash = n;
			err = 0;
			opt->type = token;
			break;

		case Opt_trunc_xino:
		case Opt_notrunc_xino:
		case Opt_noxino:
		case Opt_trunc_xib:
		case Opt_notrunc_xib:
		case Opt_shwh:
		case Opt_noshwh:
		case Opt_dirperm1:
		case Opt_nodirperm1:
		case Opt_plink:
		case Opt_noplink:
		case Opt_list_plink:
		case Opt_dio:
		case Opt_nodio:
		case Opt_diropq_a:
		case Opt_diropq_w:
		case Opt_warn_perm:
		case Opt_nowarn_perm:
		case Opt_verbose:
		case Opt_noverbose:
		case Opt_sum:
		case Opt_nosum:
		case Opt_wsum:
		case Opt_rdblk_def:
		case Opt_rdhash_def:
		case Opt_dirren:
		case Opt_nodirren:
		case Opt_acl:
		case Opt_noacl:
			err = 0;
			opt->type = token;
			break;

		case Opt_udba:
			opt->udba = udba_val(a->args[0].from);
			if (opt->udba >= 0) {
				err = 0;
				opt->type = token;
			} else
				pr_err("wrong value, %s\n", opt_str);
			break;

		case Opt_wbr_create:
			u.create = &opt->wbr_create;
			u.create->wbr_create
				= au_wbr_create_val(a->args[0].from, u.create);
			if (u.create->wbr_create >= 0) {
				err = 0;
				opt->type = token;
			} else
				pr_err("wrong value, %s\n", opt_str);
			break;
		case Opt_wbr_copyup:
			opt->wbr_copyup = au_wbr_copyup_val(a->args[0].from);
			if (opt->wbr_copyup >= 0) {
				err = 0;
				opt->type = token;
			} else
				pr_err("wrong value, %s\n", opt_str);
			break;

		case Opt_fhsm_sec:
			if (unlikely(match_int(&a->args[0], &n)
				     || n < 0)) {
				pr_err("bad integer in %s\n", opt_str);
				break;
			}
			if (sysaufs_brs) {
				opt->fhsm_second = n;
				opt->type = token;
			} else
				pr_warn("ignored %s\n", opt_str);
			err = 0;
			break;

		case Opt_ignore:
			pr_warn("ignored %s\n", opt_str);
			/*FALLTHROUGH*/
		case Opt_ignore_silent:
			skipped = 1;
			err = 0;
			break;
		case Opt_err:
			pr_err("unknown option %s\n", opt_str);
			break;
		}

		if (!err && !skipped) {
			if (unlikely(++opt > opt_tail)) {
				err = -E2BIG;
				opt--;
				opt->type = Opt_tail;
				break;
			}
			opt->type = Opt_tail;
		}
	}

	kfree(a);
	dump_opts(opts);
	if (unlikely(err))
		au_opts_free(opts);

out:
	return err;
}

static int au_opt_wbr_create(struct super_block *sb,
			     struct au_opt_wbr_create *create)
{
	int err;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	err = 1; /* handled */
	sbinfo = au_sbi(sb);
	if (sbinfo->si_wbr_create_ops->fin) {
		err = sbinfo->si_wbr_create_ops->fin(sb);
		if (!err)
			err = 1;
	}

	sbinfo->si_wbr_create = create->wbr_create;
	sbinfo->si_wbr_create_ops = au_wbr_create_ops + create->wbr_create;
	switch (create->wbr_create) {
	case AuWbrCreate_MFSRRV:
	case AuWbrCreate_MFSRR:
	case AuWbrCreate_TDMFS:
	case AuWbrCreate_TDMFSV:
	case AuWbrCreate_PMFSRR:
	case AuWbrCreate_PMFSRRV:
		sbinfo->si_wbr_mfs.mfsrr_watermark = create->mfsrr_watermark;
		/*FALLTHROUGH*/
	case AuWbrCreate_MFS:
	case AuWbrCreate_MFSV:
	case AuWbrCreate_PMFS:
	case AuWbrCreate_PMFSV:
		sbinfo->si_wbr_mfs.mfs_expire
			= msecs_to_jiffies(create->mfs_second * MSEC_PER_SEC);
		break;
	}

	if (sbinfo->si_wbr_create_ops->init)
		sbinfo->si_wbr_create_ops->init(sb); /* ignore */

	return err;
}

/*
 * returns,
 * plus: processed without an error
 * zero: unprocessed
 */
static int au_opt_simple(struct super_block *sb, struct au_opt *opt,
			 struct au_opts *opts)
{
	int err;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	err = 1; /* handled */
	sbinfo = au_sbi(sb);
	switch (opt->type) {
	case Opt_udba:
		sbinfo->si_mntflags &= ~AuOptMask_UDBA;
		sbinfo->si_mntflags |= opt->udba;
		opts->given_udba |= opt->udba;
		break;

	case Opt_plink:
		au_opt_set(sbinfo->si_mntflags, PLINK);
		break;
	case Opt_noplink:
		if (au_opt_test(sbinfo->si_mntflags, PLINK))
			au_plink_put(sb, /*verbose*/1);
		au_opt_clr(sbinfo->si_mntflags, PLINK);
		break;
	case Opt_list_plink:
		if (au_opt_test(sbinfo->si_mntflags, PLINK))
			au_plink_list(sb);
		break;

	case Opt_dio:
		au_opt_set(sbinfo->si_mntflags, DIO);
		au_fset_opts(opts->flags, REFRESH_DYAOP);
		break;
	case Opt_nodio:
		au_opt_clr(sbinfo->si_mntflags, DIO);
		au_fset_opts(opts->flags, REFRESH_DYAOP);
		break;

	case Opt_fhsm_sec:
		au_fhsm_set(sbinfo, opt->fhsm_second);
		break;

	case Opt_diropq_a:
		au_opt_set(sbinfo->si_mntflags, ALWAYS_DIROPQ);
		break;
	case Opt_diropq_w:
		au_opt_clr(sbinfo->si_mntflags, ALWAYS_DIROPQ);
		break;

	case Opt_warn_perm:
		au_opt_set(sbinfo->si_mntflags, WARN_PERM);
		break;
	case Opt_nowarn_perm:
		au_opt_clr(sbinfo->si_mntflags, WARN_PERM);
		break;

	case Opt_verbose:
		au_opt_set(sbinfo->si_mntflags, VERBOSE);
		break;
	case Opt_noverbose:
		au_opt_clr(sbinfo->si_mntflags, VERBOSE);
		break;

	case Opt_sum:
		au_opt_set(sbinfo->si_mntflags, SUM);
		break;
	case Opt_wsum:
		au_opt_clr(sbinfo->si_mntflags, SUM);
		au_opt_set(sbinfo->si_mntflags, SUM_W);
	case Opt_nosum:
		au_opt_clr(sbinfo->si_mntflags, SUM);
		au_opt_clr(sbinfo->si_mntflags, SUM_W);
		break;

	case Opt_wbr_create:
		err = au_opt_wbr_create(sb, &opt->wbr_create);
		break;
	case Opt_wbr_copyup:
		sbinfo->si_wbr_copyup = opt->wbr_copyup;
		sbinfo->si_wbr_copyup_ops = au_wbr_copyup_ops + opt->wbr_copyup;
		break;

	case Opt_dirwh:
		sbinfo->si_dirwh = opt->dirwh;
		break;

	case Opt_rdcache:
		sbinfo->si_rdcache
			= msecs_to_jiffies(opt->rdcache * MSEC_PER_SEC);
		break;
	case Opt_rdblk:
		sbinfo->si_rdblk = opt->rdblk;
		break;
	case Opt_rdblk_def:
		sbinfo->si_rdblk = AUFS_RDBLK_DEF;
		break;
	case Opt_rdhash:
		sbinfo->si_rdhash = opt->rdhash;
		break;
	case Opt_rdhash_def:
		sbinfo->si_rdhash = AUFS_RDHASH_DEF;
		break;

	case Opt_shwh:
		au_opt_set(sbinfo->si_mntflags, SHWH);
		break;
	case Opt_noshwh:
		au_opt_clr(sbinfo->si_mntflags, SHWH);
		break;

	case Opt_dirperm1:
		au_opt_set(sbinfo->si_mntflags, DIRPERM1);
		break;
	case Opt_nodirperm1:
		au_opt_clr(sbinfo->si_mntflags, DIRPERM1);
		break;

	case Opt_trunc_xino:
		au_opt_set(sbinfo->si_mntflags, TRUNC_XINO);
		break;
	case Opt_notrunc_xino:
		au_opt_clr(sbinfo->si_mntflags, TRUNC_XINO);
		break;

	case Opt_trunc_xino_path:
	case Opt_itrunc_xino:
		err = au_xino_trunc(sb, opt->xino_itrunc.bindex);
		if (!err)
			err = 1;
		break;

	case Opt_trunc_xib:
		au_fset_opts(opts->flags, TRUNC_XIB);
		break;
	case Opt_notrunc_xib:
		au_fclr_opts(opts->flags, TRUNC_XIB);
		break;

	case Opt_dirren:
		err = 1;
		if (!au_opt_test(sbinfo->si_mntflags, DIRREN)) {
			err = au_dr_opt_set(sb);
			if (!err)
				err = 1;
		}
		if (err == 1)
			au_opt_set(sbinfo->si_mntflags, DIRREN);
		break;
	case Opt_nodirren:
		err = 1;
		if (au_opt_test(sbinfo->si_mntflags, DIRREN)) {
			err = au_dr_opt_clr(sb, au_ftest_opts(opts->flags,
							      DR_FLUSHED));
			if (!err)
				err = 1;
		}
		if (err == 1)
			au_opt_clr(sbinfo->si_mntflags, DIRREN);
		break;

	case Opt_acl:
		sb->s_flags |= SB_POSIXACL;
		break;
	case Opt_noacl:
		sb->s_flags &= ~SB_POSIXACL;
		break;

	default:
		err = 0;
		break;
	}

	return err;
}

/*
 * returns tri-state.
 * plus: processed without an error
 * zero: unprocessed
 * minus: error
 */
static int au_opt_br(struct super_block *sb, struct au_opt *opt,
		     struct au_opts *opts)
{
	int err, do_refresh;

	err = 0;
	switch (opt->type) {
	case Opt_append:
		opt->add.bindex = au_sbbot(sb) + 1;
		if (opt->add.bindex < 0)
			opt->add.bindex = 0;
		goto add;
	case Opt_prepend:
		opt->add.bindex = 0;
	add: /* indented label */
	case Opt_add:
		err = au_br_add(sb, &opt->add,
				au_ftest_opts(opts->flags, REMOUNT));
		if (!err) {
			err = 1;
			au_fset_opts(opts->flags, REFRESH);
		}
		break;

	case Opt_del:
	case Opt_idel:
		err = au_br_del(sb, &opt->del,
				au_ftest_opts(opts->flags, REMOUNT));
		if (!err) {
			err = 1;
			au_fset_opts(opts->flags, TRUNC_XIB);
			au_fset_opts(opts->flags, REFRESH);
		}
		break;

	case Opt_mod:
	case Opt_imod:
		err = au_br_mod(sb, &opt->mod,
				au_ftest_opts(opts->flags, REMOUNT),
				&do_refresh);
		if (!err) {
			err = 1;
			if (do_refresh)
				au_fset_opts(opts->flags, REFRESH);
		}
		break;
	}
	return err;
}

static int au_opt_xino(struct super_block *sb, struct au_opt *opt,
		       struct au_opt_xino **opt_xino,
		       struct au_opts *opts)
{
	int err;
	aufs_bindex_t bbot, bindex;
	struct dentry *root, *parent, *h_root;

	err = 0;
	switch (opt->type) {
	case Opt_xino:
		err = au_xino_set(sb, &opt->xino,
				  !!au_ftest_opts(opts->flags, REMOUNT));
		if (unlikely(err))
			break;

		*opt_xino = &opt->xino;
		au_xino_brid_set(sb, -1);

		/* safe d_parent access */
		parent = opt->xino.file->f_path.dentry->d_parent;
		root = sb->s_root;
		bbot = au_sbbot(sb);
		for (bindex = 0; bindex <= bbot; bindex++) {
			h_root = au_h_dptr(root, bindex);
			if (h_root == parent) {
				au_xino_brid_set(sb, au_sbr_id(sb, bindex));
				break;
			}
		}
		break;

	case Opt_noxino:
		au_xino_clr(sb);
		au_xino_brid_set(sb, -1);
		*opt_xino = (void *)-1;
		break;
	}

	return err;
}

int au_opts_verify(struct super_block *sb, unsigned long sb_flags,
		   unsigned int pending)
{
	int err, fhsm;
	aufs_bindex_t bindex, bbot;
	unsigned char do_plink, skip, do_free, can_no_dreval;
	struct au_branch *br;
	struct au_wbr *wbr;
	struct dentry *root, *dentry;
	struct inode *dir, *h_dir;
	struct au_sbinfo *sbinfo;
	struct au_hinode *hdir;

	SiMustAnyLock(sb);

	sbinfo = au_sbi(sb);
	AuDebugOn(!(sbinfo->si_mntflags & AuOptMask_UDBA));

	if (!(sb_flags & SB_RDONLY)) {
		if (unlikely(!au_br_writable(au_sbr_perm(sb, 0))))
			pr_warn("first branch should be rw\n");
		if (unlikely(au_opt_test(sbinfo->si_mntflags, SHWH)))
			pr_warn_once("shwh should be used with ro\n");
	}

	if (au_opt_test((sbinfo->si_mntflags | pending), UDBA_HNOTIFY)
	    && !au_opt_test(sbinfo->si_mntflags, XINO))
		pr_warn_once("udba=*notify requires xino\n");

	if (au_opt_test(sbinfo->si_mntflags, DIRPERM1))
		pr_warn_once("dirperm1 breaks the protection"
			     " by the permission bits on the lower branch\n");

	err = 0;
	fhsm = 0;
	root = sb->s_root;
	dir = d_inode(root);
	do_plink = !!au_opt_test(sbinfo->si_mntflags, PLINK);
	can_no_dreval = !!au_opt_test((sbinfo->si_mntflags | pending),
				      UDBA_NONE);
	bbot = au_sbbot(sb);
	for (bindex = 0; !err && bindex <= bbot; bindex++) {
		skip = 0;
		h_dir = au_h_iptr(dir, bindex);
		br = au_sbr(sb, bindex);

		if ((br->br_perm & AuBrAttr_ICEX)
		    && !h_dir->i_op->listxattr)
			br->br_perm &= ~AuBrAttr_ICEX;
#if 0
		if ((br->br_perm & AuBrAttr_ICEX_SEC)
		    && (au_br_sb(br)->s_flags & SB_NOSEC))
			br->br_perm &= ~AuBrAttr_ICEX_SEC;
#endif

		do_free = 0;
		wbr = br->br_wbr;
		if (wbr)
			wbr_wh_read_lock(wbr);

		if (!au_br_writable(br->br_perm)) {
			do_free = !!wbr;
			skip = (!wbr
				|| (!wbr->wbr_whbase
				    && !wbr->wbr_plink
				    && !wbr->wbr_orph));
		} else if (!au_br_wh_linkable(br->br_perm)) {
			/* skip = (!br->br_whbase && !br->br_orph); */
			skip = (!wbr || !wbr->wbr_whbase);
			if (skip && wbr) {
				if (do_plink)
					skip = !!wbr->wbr_plink;
				else
					skip = !wbr->wbr_plink;
			}
		} else {
			/* skip = (br->br_whbase && br->br_ohph); */
			skip = (wbr && wbr->wbr_whbase);
			if (skip) {
				if (do_plink)
					skip = !!wbr->wbr_plink;
				else
					skip = !wbr->wbr_plink;
			}
		}
		if (wbr)
			wbr_wh_read_unlock(wbr);

		if (can_no_dreval) {
			dentry = br->br_path.dentry;
			spin_lock(&dentry->d_lock);
			if (dentry->d_flags &
			    (DCACHE_OP_REVALIDATE | DCACHE_OP_WEAK_REVALIDATE))
				can_no_dreval = 0;
			spin_unlock(&dentry->d_lock);
		}

		if (au_br_fhsm(br->br_perm)) {
			fhsm++;
			AuDebugOn(!br->br_fhsm);
		}

		if (skip)
			continue;

		hdir = au_hi(dir, bindex);
		au_hn_inode_lock_nested(hdir, AuLsc_I_PARENT);
		if (wbr)
			wbr_wh_write_lock(wbr);
		err = au_wh_init(br, sb);
		if (wbr)
			wbr_wh_write_unlock(wbr);
		au_hn_inode_unlock(hdir);

		if (!err && do_free) {
			kfree(wbr);
			br->br_wbr = NULL;
		}
	}

	if (can_no_dreval)
		au_fset_si(sbinfo, NO_DREVAL);
	else
		au_fclr_si(sbinfo, NO_DREVAL);

	if (fhsm >= 2) {
		au_fset_si(sbinfo, FHSM);
		for (bindex = bbot; bindex >= 0; bindex--) {
			br = au_sbr(sb, bindex);
			if (au_br_fhsm(br->br_perm)) {
				au_fhsm_set_bottom(sb, bindex);
				break;
			}
		}
	} else {
		au_fclr_si(sbinfo, FHSM);
		au_fhsm_set_bottom(sb, -1);
	}

	return err;
}

int au_opts_mount(struct super_block *sb, struct au_opts *opts)
{
	int err;
	unsigned int tmp;
	aufs_bindex_t bindex, bbot;
	struct au_opt *opt;
	struct au_opt_xino *opt_xino, xino;
	struct au_sbinfo *sbinfo;
	struct au_branch *br;
	struct inode *dir;

	SiMustWriteLock(sb);

	err = 0;
	opt_xino = NULL;
	opt = opts->opt;
	while (err >= 0 && opt->type != Opt_tail)
		err = au_opt_simple(sb, opt++, opts);
	if (err > 0)
		err = 0;
	else if (unlikely(err < 0))
		goto out;

	/* disable xino and udba temporary */
	sbinfo = au_sbi(sb);
	tmp = sbinfo->si_mntflags;
	au_opt_clr(sbinfo->si_mntflags, XINO);
	au_opt_set_udba(sbinfo->si_mntflags, UDBA_REVAL);

	opt = opts->opt;
	while (err >= 0 && opt->type != Opt_tail)
		err = au_opt_br(sb, opt++, opts);
	if (err > 0)
		err = 0;
	else if (unlikely(err < 0))
		goto out;

	bbot = au_sbbot(sb);
	if (unlikely(bbot < 0)) {
		err = -EINVAL;
		pr_err("no branches\n");
		goto out;
	}

	if (au_opt_test(tmp, XINO))
		au_opt_set(sbinfo->si_mntflags, XINO);
	opt = opts->opt;
	while (!err && opt->type != Opt_tail)
		err = au_opt_xino(sb, opt++, &opt_xino, opts);
	if (unlikely(err))
		goto out;

	err = au_opts_verify(sb, sb->s_flags, tmp);
	if (unlikely(err))
		goto out;

	/* restore xino */
	if (au_opt_test(tmp, XINO) && !opt_xino) {
		xino.file = au_xino_def(sb);
		err = PTR_ERR(xino.file);
		if (IS_ERR(xino.file))
			goto out;

		err = au_xino_set(sb, &xino, /*remount*/0);
		fput(xino.file);
		if (unlikely(err))
			goto out;
	}

	/* restore udba */
	tmp &= AuOptMask_UDBA;
	sbinfo->si_mntflags &= ~AuOptMask_UDBA;
	sbinfo->si_mntflags |= tmp;
	bbot = au_sbbot(sb);
	for (bindex = 0; bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		err = au_hnotify_reset_br(tmp, br, br->br_perm);
		if (unlikely(err))
			AuIOErr("hnotify failed on br %d, %d, ignored\n",
				bindex, err);
		/* go on even if err */
	}
	if (au_opt_test(tmp, UDBA_HNOTIFY)) {
		dir = d_inode(sb->s_root);
		au_hn_reset(dir, au_hi_flags(dir, /*isdir*/1) & ~AuHi_XINO);
	}

out:
	return err;
}

int au_opts_remount(struct super_block *sb, struct au_opts *opts)
{
	int err, rerr;
	unsigned char no_dreval;
	struct inode *dir;
	struct au_opt_xino *opt_xino;
	struct au_opt *opt;
	struct au_sbinfo *sbinfo;

	SiMustWriteLock(sb);

	err = au_dr_opt_flush(sb);
	if (unlikely(err))
		goto out;
	au_fset_opts(opts->flags, DR_FLUSHED);

	dir = d_inode(sb->s_root);
	sbinfo = au_sbi(sb);
	opt_xino = NULL;
	opt = opts->opt;
	while (err >= 0 && opt->type != Opt_tail) {
		err = au_opt_simple(sb, opt, opts);
		if (!err)
			err = au_opt_br(sb, opt, opts);
		if (!err)
			err = au_opt_xino(sb, opt, &opt_xino, opts);
		opt++;
	}
	if (err > 0)
		err = 0;
	AuTraceErr(err);
	/* go on even err */

	no_dreval = !!au_ftest_si(sbinfo, NO_DREVAL);
	rerr = au_opts_verify(sb, opts->sb_flags, /*pending*/0);
	if (unlikely(rerr && !err))
		err = rerr;

	if (no_dreval != !!au_ftest_si(sbinfo, NO_DREVAL))
		au_fset_opts(opts->flags, REFRESH_IDOP);

	if (au_ftest_opts(opts->flags, TRUNC_XIB)) {
		rerr = au_xib_trunc(sb);
		if (unlikely(rerr && !err))
			err = rerr;
	}

	/* will be handled by the caller */
	if (!au_ftest_opts(opts->flags, REFRESH)
	    && (opts->given_udba
		|| au_opt_test(sbinfo->si_mntflags, XINO)
		|| au_ftest_opts(opts->flags, REFRESH_IDOP)
		    ))
		au_fset_opts(opts->flags, REFRESH);

	AuDbg("status 0x%x\n", opts->flags);

out:
	return err;
}

/* ---------------------------------------------------------------------- */

unsigned int au_opt_udba(struct super_block *sb)
{
	return au_mntflags(sb) & AuOptMask_UDBA;
}
