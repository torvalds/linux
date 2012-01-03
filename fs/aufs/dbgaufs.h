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
 * debugfs interface
 */

#ifndef __DBGAUFS_H__
#define __DBGAUFS_H__

#ifdef __KERNEL__

struct super_block;
struct au_sbinfo;

#ifdef CONFIG_DEBUG_FS
/* dbgaufs.c */
void dbgaufs_brs_del(struct super_block *sb, aufs_bindex_t bindex);
void dbgaufs_brs_add(struct super_block *sb, aufs_bindex_t bindex);
void dbgaufs_si_fin(struct au_sbinfo *sbinfo);
int dbgaufs_si_init(struct au_sbinfo *sbinfo);
void dbgaufs_fin(void);
int __init dbgaufs_init(void);
#else
AuStubVoid(dbgaufs_brs_del, struct super_block *sb, aufs_bindex_t bindex)
AuStubVoid(dbgaufs_brs_add, struct super_block *sb, aufs_bindex_t bindex)
AuStubVoid(dbgaufs_si_fin, struct au_sbinfo *sbinfo)
AuStubInt0(dbgaufs_si_init, struct au_sbinfo *sbinfo)
AuStubVoid(dbgaufs_fin, void)
AuStubInt0(__init dbgaufs_init, void)
#endif /* CONFIG_DEBUG_FS */

#endif /* __KERNEL__ */
#endif /* __DBGAUFS_H__ */
