/*
 * Copyright (C) 2005-2012 Junjiro R. Okajima
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * debug print functions
 */

#ifndef __AUFS_DEBUG_H__
#define __AUFS_DEBUG_H__

#ifdef __KERNEL__

#include <asm/system.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/sysrq.h>

#ifdef CONFIG_AUFS_DEBUG
#define AuDebugOn(a)		BUG_ON(a)

/* module parameter */
extern int aufs_debug;
static inline void au_debug(int n)
{
	aufs_debug = n;
	smp_mb();
}

static inline int au_debug_test(void)
{
	return aufs_debug;
}
#else
#define AuDebugOn(a)		do {} while (0)
AuStubVoid(au_debug, int n)
AuStubInt0(au_debug_test, void)
#endif /* CONFIG_AUFS_DEBUG */

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
		pr_warning(fmt, ##__VA_ARGS__); \
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
#define AuDLNPair(d)		AuLNPair(&(d)->d_name)

/* ---------------------------------------------------------------------- */

struct au_sbinfo;
struct au_finfo;
struct dentry;
#ifdef CONFIG_AUFS_DEBUG
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

void au_dbg_sleep_jiffy(int jiffy);
struct iattr;
void au_dbg_iattr(struct iattr *ia);

#define au_dbg_verify_dinode(d) __au_dbg_verify_dinode(d, __func__, __LINE__)
void __au_dbg_verify_dinode(struct dentry *dentry, const char *func, int line);
void au_dbg_verify_dir_parent(struct dentry *dentry, unsigned int sigen);
void au_dbg_verify_nondir_parent(struct dentry *dentry, unsigned int sigen);
void au_dbg_verify_gen(struct dentry *parent, unsigned int sigen);
void au_dbg_verify_kthread(void);

int __init au_debug_init(void);
void au_debug_sbinfo_init(struct au_sbinfo *sbinfo);
#define AuDbgWhlist(w) do { \
	AuDbg(#w "\n"); \
	au_dpri_whlist(w); \
} while (0)

#define AuDbgVdir(v) do { \
	AuDbg(#v "\n"); \
	au_dpri_vdir(v); \
} while (0)

#define AuDbgInode(i) do { \
	AuDbg(#i "\n"); \
	au_dpri_inode(i); \
} while (0)

#define AuDbgDAlias(i) do { \
	AuDbg(#i "\n"); \
	au_dpri_dalias(i); \
} while (0)

#define AuDbgDentry(d) do { \
	AuDbg(#d "\n"); \
	au_dpri_dentry(d); \
} while (0)

#define AuDbgFile(f) do { \
	AuDbg(#f "\n"); \
	au_dpri_file(f); \
} while (0)

#define AuDbgSb(sb) do { \
	AuDbg(#sb "\n"); \
	au_dpri_sb(sb); \
} while (0)

#define AuDbgSleep(sec) do { \
	AuDbg("sleep %d sec\n", sec); \
	ssleep(sec); \
} while (0)

#define AuDbgSleepJiffy(jiffy) do { \
	AuDbg("sleep %d jiffies\n", jiffy); \
	au_dbg_sleep_jiffy(jiffy); \
} while (0)

#define AuDbgIAttr(ia) do { \
	AuDbg("ia_valid 0x%x\n", (ia)->ia_valid); \
	au_dbg_iattr(ia); \
} while (0)

#define AuDbgSym(addr) do {				\
	char sym[KSYM_SYMBOL_LEN];			\
	sprint_symbol(sym, (unsigned long)addr);	\
	AuDbg("%s\n", sym);				\
} while (0)

#define AuInfoSym(addr) do {				\
	char sym[KSYM_SYMBOL_LEN];			\
	sprint_symbol(sym, (unsigned long)addr);	\
	AuInfo("%s\n", sym);				\
} while (0)
#else
AuStubVoid(au_dbg_verify_dinode, struct dentry *dentry)
AuStubVoid(au_dbg_verify_dir_parent, struct dentry *dentry, unsigned int sigen)
AuStubVoid(au_dbg_verify_nondir_parent, struct dentry *dentry,
	   unsigned int sigen)
AuStubVoid(au_dbg_verify_gen, struct dentry *parent, unsigned int sigen)
AuStubVoid(au_dbg_verify_kthread, void)
AuStubInt0(__init au_debug_init, void)
AuStubVoid(au_debug_sbinfo_init, struct au_sbinfo *sbinfo)

#define AuDbgWhlist(w)		do {} while (0)
#define AuDbgVdir(v)		do {} while (0)
#define AuDbgInode(i)		do {} while (0)
#define AuDbgDAlias(i)		do {} while (0)
#define AuDbgDentry(d)		do {} while (0)
#define AuDbgFile(f)		do {} while (0)
#define AuDbgSb(sb)		do {} while (0)
#define AuDbgSleep(sec)		do {} while (0)
#define AuDbgSleepJiffy(jiffy)	do {} while (0)
#define AuDbgIAttr(ia)		do {} while (0)
#define AuDbgSym(addr)		do {} while (0)
#define AuInfoSym(addr)		do {} while (0)
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
