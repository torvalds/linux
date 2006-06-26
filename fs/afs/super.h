/* super.h: AFS filesystem internal private data
 *
 * Copyright (c) 2002 Red Hat, Inc. All rights reserved.
 *
 * This software may be freely redistributed under the terms of the
 * GNU General Public License.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Authors: David Woodhouse <dwmw2@cambridge.redhat.com>
 *          David Howells <dhowells@redhat.com>
 *
 */

#ifndef _LINUX_AFS_SUPER_H
#define _LINUX_AFS_SUPER_H

#include <linux/fs.h>
#include "server.h"

#ifdef __KERNEL__

/*****************************************************************************/
/*
 * AFS superblock private data
 * - there's one superblock per volume
 */
struct afs_super_info
{
	struct afs_volume	*volume;	/* volume record */
	char			rwparent;	/* T if parent is R/W AFS volume */
};

static inline struct afs_super_info *AFS_FS_S(struct super_block *sb)
{
	return sb->s_fs_info;
}

extern struct file_system_type afs_fs_type;

#endif /* __KERNEL__ */

#endif /* _LINUX_AFS_SUPER_H */
