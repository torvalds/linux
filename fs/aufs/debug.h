/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2018 Junjiro R. Okajima
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
 * debug print functions
 */

#ifndef __AUFS_DEBUG_H__
#define __AUFS_DEBUG_H__

#ifdef __KERNEL__

#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/sysrq.h>

#ifdef CONFIG_AUFS_DEBUG
#define AuDebugOn(a)		BUG_ON(a)

/* module parameter */
extern atomic_t aufs_debug;
static inline void au_debug_on(void)
{
	atomic_inc(&aufs_debug);
}
static inline void au_debug_off(void)
{
	atomic_dec_if_positive(&aufs_debug);
}

static inline int au_debug_test(void)
{
	return atomic_read(&aufs_debug) > 0;
}
#else
#define AuDebugOn(a)		do {} while (0)
AuStubVoid(au_debug_on, void)
AuStubVoid(au_debug_off, void)
AuStubInt0(au_debug_test, void)
#endif /* CONFIG_AUFS_DEBUG */

#define param_check_atomic_t(name, p) __param_check(name, p, atomic_t)

/* ---------------------------------------------------------------------- */

/* debug print */

#define AuDbg(fmt, ...) do { \
	if (au_debug_test()) \
		pr_debug("DEBUG: " fmt, ##__VA_ARGS__); \
} while (0)
#define AuLabel(l)		AuDbg(#l "\n")
#define AuIOErr(fmt, ...)	pr_err("I/O Error, " fmt, ##__VA_ARGS__)
#define AuWarn1(fmt, ...) do { \
	static unsigned char _c; \
	if (!_c++) \
		pr_warn(fmt, ##__VA_ARGS__); \
} while (0)

#define AuErr1(fmt, ...) do { \
	static unsigned char _c; \
	if (!_c++) \
		pr_err(fmt, ##__VA_ARGS__); \
} while (0)

#define AuIOErr1(fmt, ...) do { \
	static unsigned char _c; \
	if (!_c++) \
		AuIOErr(fmt, ##__VA_ARGS__); \
} while (0)

#define AuUnsupportMsg	"This operation is not supported." \
			" Please report this application to aufs-users ML."
#define AuUnsupport(fmt, ...) do { \
	pr_err(AuUnsupportMsg "\n" fmt, ##__VA_ARGS__); \
	dump_stack(); \
} while (0)

#define AuTraceErr(e) do { \
	if (unlikely((e) < 0)) \
		AuDbg("err %d\n", (int)(e)); \
} while (0)

#define AuTraceErrPtr(p) do { \
	if (IS_ERR(p)) \
		AuDbg("err %ld\n", PTR_ERR(p)); \
} while (0)

/* dirty macros for debug print, use with "%.*s" and caution */
#define AuLNPair(qstr)		(qstr)->len, (qstr)->name

/* ---------------------------------------------------------------------- */

struct dentry;
#ifdef CONFIG_AUFS_DEBUG
extern struct mutex au_dbg_mtx;
extern char *au_plevel;
struct au_nhash;
void au_dpri_whlist(struct au_nhash *whlist);
struct au_vdir;
void au_dpri_vdir(struct au_vdir *vdir);
struct inode;
void au_dpri_inode(struct inode *inode);
void au_dpri_dalias(struct inode *inode);
void au_dpri_dentry(struct dentry *dentry);
struct file;
void au_dpri_file(struct file *filp);
struct super_block;
void au_dpri_sb(struct super_block *sb);

#define au_dbg_verify_dinode(d) __au_dbg_verify_dinode(d, __func__, __LINE__)
void __au_dbg_verify_dinode(struct dentry *dentry, const char *func, int line);
void au_dbg_verify_gen(struct dentry *parent, unsigned int sigen);
void au_dbg_verify_kthread(void);

int __init au_debug_init(void);

#define AuDbgWhlist(w) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#w "\n"); \
	au_dpri_whlist(w); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)

#define AuDbgVdir(v) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#v "\n"); \
	au_dpri_vdir(v); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)

#define AuDbgInode(i) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#i "\n"); \
	au_dpri_inode(i); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)

#define AuDbgDAlias(i) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#i "\n"); \
	au_dpri_dalias(i); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)

#define AuDbgDentry(d) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#d "\n"); \
	au_dpri_dentry(d); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)

#define AuDbgFile(f) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#f "\n"); \
	au_dpri_file(f); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)

#define AuDbgSb(sb) do { \
	mutex_lock(&au_dbg_mtx); \
	AuDbg(#sb "\n"); \
	au_dpri_sb(sb); \
	mutex_unlock(&au_dbg_mtx); \
} while (0)

#define AuDbgSym(addr) do {				\
	char sym[KSYM_SYMBOL_LEN];			\
	sprint_symbol(sym, (unsigned long)addr);	\
	AuDbg("%s\n", sym);				\
} while (0)
#else
AuStubVoid(au_dbg_verify_dinode, struct dentry *dentry)
AuStubVoid(au_dbg_verify_gen, struct dentry *parent, unsigned int sigen)
AuStubVoid(au_dbg_verify_kthread, void)
AuStubInt0(__init au_debug_init, void)

#define AuDbgWhlist(w)		do {} while (0)
#define AuDbgVdir(v)		do {} while (0)
#define AuDbgInode(i)		do {} while (0)
#define AuDbgDAlias(i)		do {} while (0)
#define AuDbgDentry(d)		do {} while (0)
#define AuDbgFile(f)		do {} while (0)
#define AuDbgSb(sb)		do {} while (0)
#define AuDbgSym(addr)		do {} while (0)
#endif /* CONFIG_AUFS_DEBUG */

/* ---------------------------------------------------------------------- */

#ifdef CONFIG_AUFS_MAGIC_SYSRQ
int __init au_sysrq_init(void);
void au_sysrq_fin(void);

#ifdef CONFIG_HW_CONSOLE
#define au_dbg_blocked() do { \
	WARN_ON(1); \
	handle_sysrq('w'); \
} while (0)
#else
AuStubVoid(au_dbg_blocked, void)
#endif

#else
AuStubInt0(__init au_sysrq_init, void)
AuStubVoid(au_sysrq_fin, void)
AuStubVoid(au_dbg_blocked, void)
#endif /* CONFIG_AUFS_MAGIC_SYSRQ */

#endif /* __KERNEL__ */
#endif /* __AUFS_DEBUG_H__ */
