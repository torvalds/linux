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
 * support for loopback mount as a branch
 */

#ifndef __AUFS_LOOP_H__
#define __AUFS_LOOP_H__

#ifdef __KERNEL__

struct dentry;
struct super_block;

#ifdef CONFIG_AUFS_BDEV_LOOP
/* drivers/block/loop.c */
struct file *loop_backing_file(struct super_block *sb);

/* loop.c */
int au_test_loopback_overlap(struct super_block *sb, struct dentry *h_adding);
int au_test_loopback_kthread(void);
void au_warn_loopback(struct super_block *h_sb);

int au_loopback_init(void);
void au_loopback_fin(void);

struct file *aufs_real_loop(struct file *file);
#else
AuStub(struct file *, loop_backing_file, return NULL)

AuStubInt0(au_test_loopback_overlap, struct super_block *sb,
	   struct dentry *h_adding)
AuStubInt0(au_test_loopback_kthread, void)
AuStubVoid(au_warn_loopback, struct super_block *h_sb)

AuStubInt0(au_loopback_init, void)
AuStubVoid(au_loopback_fin, void)

AuStub(struct file *, aufs_real_loop, return NULL, struct file *file)
#endif /* BLK_DEV_LOOP */

#endif /* __KERNEL__ */
#endif /* __AUFS_LOOP_H__ */
