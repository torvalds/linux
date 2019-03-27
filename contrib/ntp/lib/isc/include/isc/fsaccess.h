/*
 * Copyright (C) 2004-2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: fsaccess.h,v 1.16 2009/01/17 23:47:43 tbox Exp $ */

#ifndef ISC_FSACCESS_H
#define ISC_FSACCESS_H 1

/*! \file isc/fsaccess.h
 * \brief The ISC filesystem access module encapsulates the setting of file
 * and directory access permissions into one API that is meant to be
 * portable to multiple operating systems.
 *
 * The two primary operating system flavors that are initially accommodated
 * are POSIX and Windows NT 4.0 and later.  The Windows NT access model is
 * considerable more flexible than POSIX's model (as much as I am loathe to
 * admit it), and so the ISC API has a higher degree of complexity than would
 * be needed to simply address POSIX's needs.
 *
 * The full breadth of NT's flexibility is not available either, for the
 * present time.  Much of it is to provide compatibility with what Unix
 * programmers are expecting.  This is also due to not yet really needing all
 * of the functionality of an NT system (or, for that matter, a POSIX system)
 * in BIND9, and so resolving how to handle the various incompatibilities has
 * been a purely theoretical exercise with no operational experience to
 * indicate how flawed the thinking may be.
 *
 * Some of the more notable dumbing down of NT for this API includes:
 *
 *\li   Each of FILE_READ_DATA and FILE_READ_EA are set with #ISC_FSACCESS_READ.
 *
 * \li  All of FILE_WRITE_DATA, FILE_WRITE_EA and FILE_APPEND_DATA are
 *     set with #ISC_FSACCESS_WRITE.  FILE_WRITE_ATTRIBUTES is not set
 *     so as to be consistent with Unix, where only the owner of the file
 *     or the superuser can change the attributes/mode of a file.
 *
 * \li  Both of FILE_ADD_FILE and FILE_ADD_SUBDIRECTORY are set with
 *     #ISC_FSACCESS_CREATECHILD.  This is similar to setting the WRITE
 *     permission on a Unix directory.
 *
 * \li  SYNCHRONIZE is always set for files and directories, unless someone
 *     can give me a reason why this is a bad idea.
 *
 * \li  READ_CONTROL and FILE_READ_ATTRIBUTES are always set; this is
 *     consistent with Unix, where any file or directory can be stat()'d
 *     unless the directory path disallows complete access somewhere along
 *     the way.
 *
 * \li  WRITE_DAC is only set for the owner.  This too is consistent with
 *     Unix, and is tighter security than allowing anyone else to be
 *     able to set permissions.
 *
 * \li  DELETE is only set for the owner.  On Unix the ability to delete
 *     a file is controlled by the directory permissions, but it isn't
 *     currently clear to me what happens on NT if the directory has
 *     FILE_DELETE_CHILD set but a file within it does not have DELETE
 *     set.  Always setting DELETE on the file/directory for the owner
 *     gives maximum flexibility to the owner without exposing the
 *     file to deletion by others.
 *
 * \li  WRITE_OWNER is never set.  This too is consistent with Unix,
 *     and is also tighter security than allowing anyone to change the
 *     ownership of the file apart from the superu..ahem, Administrator.
 *
 * \li  Inheritance is set to NO_INHERITANCE.
 *
 * Unix's dumbing down includes:
 *
 * \li  The sticky bit cannot be set.
 *
 * \li  setuid and setgid cannot be set.
 *
 * \li  Only regular files and directories can be set.
 *
 * The rest of this comment discusses a few of the incompatibilities
 * between the two systems that need more thought if this API is to
 * be extended to accommodate them.
 *
 * The Windows standard access right "DELETE" doesn't have a direct
 * equivalent in the Unix world, so it isn't clear what should be done
 * with it.
 *
 * The Unix sticky bit is not supported.  While NT does have a concept
 * of allowing users to create files in a directory but not delete or
 * rename them, it does not have a concept of allowing them to be deleted
 * if they are owned by the user trying to delete/rename.  While it is
 * probable that something could be cobbled together in NT 5 with inheritance,
 * it can't really be done in NT 4 as a single property that you could
 * set on a directory.  You'd need to coordinate something with file creation
 * so that every file created had DELETE set for the owner but noone else.
 *
 * On Unix systems, setting #ISC_FSACCESS_LISTDIRECTORY sets READ.
 * ... setting either #ISC_FSACCESS_CREATECHILD or #ISC_FSACCESS_DELETECHILD
 *      sets WRITE.
 * ... setting #ISC_FSACCESS_ACCESSCHILD sets EXECUTE.
 *
 * On NT systems, setting #ISC_FSACCESS_LISTDIRECTORY sets FILE_LIST_DIRECTORY.
 * ... setting #ISC_FSACCESS_CREATECHILD sets FILE_CREATE_CHILD independently.
 * ... setting #ISC_FSACCESS_DELETECHILD sets FILE_DELETE_CHILD independently.
 * ... setting #ISC_FSACCESS_ACCESSCHILD sets FILE_TRAVERSE.
 *
 * Unresolved:							XXXDCL
 * \li  What NT access right controls the ability to rename a file?
 * \li  How does DELETE work?  If a directory has FILE_DELETE_CHILD but a
 *      file or directory within it does not have DELETE, is that file
 *	or directory deletable?
 * \li  To implement isc_fsaccess_get(), mapping an existing Unix permission
 * 	mode_t back to an isc_fsaccess_t is pretty trivial; however, mapping
 *	an NT DACL could be impossible to do in a responsible way.
 * \li  Similarly, trying to implement the functionality of being able to
 *	say "add group writability to whatever permissions already exist"
 *	could be tricky on NT because of the order-of-entry issue combined
 *	with possibly having one or more matching ACEs already explicitly
 *	granting or denying access.  Because this functionality is
 *	not yet needed by the ISC, no code has been written to try to
 * 	solve this problem.
 */

#include <isc/lang.h>
#include <isc/types.h>

/*
 * Trustees.
 */
#define ISC_FSACCESS_OWNER	0x1 /*%< User account. */
#define ISC_FSACCESS_GROUP	0x2 /*%< Primary group owner. */
#define ISC_FSACCESS_OTHER	0x4 /*%< Not the owner or the group owner. */
#define ISC_FSACCESS_WORLD	0x7 /*%< User, Group, Other. */

/*
 * Types of permission.
 */
#define ISC_FSACCESS_READ		0x00000001 /*%< File only. */
#define ISC_FSACCESS_WRITE		0x00000002 /*%< File only. */
#define ISC_FSACCESS_EXECUTE		0x00000004 /*%< File only. */
#define ISC_FSACCESS_CREATECHILD	0x00000008 /*%< Dir only. */
#define ISC_FSACCESS_DELETECHILD	0x00000010 /*%< Dir only. */
#define ISC_FSACCESS_LISTDIRECTORY	0x00000020 /*%< Dir only. */
#define ISC_FSACCESS_ACCESSCHILD	0x00000040 /*%< Dir only. */

/*%
 * Adding any permission bits beyond 0x200 would mean typedef'ing
 * isc_fsaccess_t as isc_uint64_t, and redefining this value to
 * reflect the new range of permission types, Probably to 21 for
 * maximum flexibility.  The number of bits has to accommodate all of
 * the permission types, and three full sets of them have to fit
 * within an isc_fsaccess_t.
 */
#define ISC__FSACCESS_PERMISSIONBITS 10

ISC_LANG_BEGINDECLS

void
isc_fsaccess_add(int trustee, int permission, isc_fsaccess_t *access);

void
isc_fsaccess_remove(int trustee, int permission, isc_fsaccess_t *access);

isc_result_t
isc_fsaccess_set(const char *path, isc_fsaccess_t access);

ISC_LANG_ENDDECLS

#endif /* ISC_FSACCESS_H */
