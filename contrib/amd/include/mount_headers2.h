/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1990 Jan-Simon Pendry
 * Copyright (c) 1990 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/include/mount_headers2.h
 *
 */



#ifdef HAVE_RPCSVC_MOUNT_H
# include <rpcsvc/mount.h>
#endif /* HAVE_RPCSVC_MOUNT_H */

#ifdef HAVE_MOUNT_H
# include <mount.h>
#endif /* HAVE_MOUNT_H */

#ifdef HAVE_NFS_NFS_GFS_H
# include <nfs/nfs_gfs.h>
#endif /* HAVE_NFS_NFS_GFS_H */

#ifdef HAVE_NFS_MOUNT_H
# include <nfs/mount.h>
#endif /* HAVE_NFS_MOUNT_H */

#ifdef HAVE_SYS_FS_NFS_CLNT_H
# include <sys/fs/nfs_clnt.h>
#endif /* HAVE_SYS_FS_NFS_CLNT_H */

#ifdef HAVE_LINUX_NFS_MOUNT_H
# ifndef _LINUX_NFS_H
#  define _LINUX_NFS_H
# endif /* not _LINUX_NFS_H */
# ifndef _LINUX_NFS2_H
#  define _LINUX_NFS2_H
# endif /* not _LINUX_NFS2_H */
# ifndef _LINUX_NFS3_H
#  define _LINUX_NFS3_H
# endif /* not _LINUX_NFS3_H */
# ifndef _LINUX_NFS_FS_H
#  define _LINUX_NFS_FS_H
# endif /* not _LINUX_NFS_FS_H */
# ifndef _LINUX_IN_H
#  define _LINUX_IN_H
# endif /* not _LINUX_IN_H */
# ifndef __KERNEL__
#  define __KERNEL__
# endif /* __KERNEL__ */
# include <linux/nfs_mount.h>
# undef __KERNEL__
#endif /* HAVE_LINUX_NFS_MOUNT_H */
