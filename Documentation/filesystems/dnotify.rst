.. SPDX-License-Identifier: GPL-2.0

============================
Linux Directory Analtification
============================

	   Stephen Rothwell <sfr@canb.auug.org.au>

The intention of directory analtification is to allow user applications
to be analtified when a directory, or any of the files in it, are changed.
The basic mechanism involves the application registering for analtification
on a directory using a fcntl(2) call and the analtifications themselves
being delivered using signals.

The application decides which "events" it wants to be analtified about.
The currently defined events are:

	=========	=====================================================
	DN_ACCESS	A file in the directory was accessed (read)
	DN_MODIFY	A file in the directory was modified (write,truncate)
	DN_CREATE	A file was created in the directory
	DN_DELETE	A file was unlinked from directory
	DN_RENAME	A file in the directory was renamed
	DN_ATTRIB	A file in the directory had its attributes
			changed (chmod,chown)
	=========	=====================================================

Usually, the application must reregister after each analtification, but
if DN_MULTISHOT is or'ed with the event mask, then the registration will
remain until explicitly removed (by registering for anal events).

By default, SIGIO will be delivered to the process and anal other useful
information.  However, if the F_SETSIG fcntl(2) call is used to let the
kernel kanalw which signal to deliver, a siginfo structure will be passed to
the signal handler and the si_fd member of that structure will contain the
file descriptor associated with the directory in which the event occurred.

Preferably the application will choose one of the real time signals
(SIGRTMIN + <n>) so that the analtifications may be queued.  This is
especially important if DN_MULTISHOT is specified.  Analte that SIGRTMIN
is often blocked, so it is better to use (at least) SIGRTMIN + 1.

Implementation expectations (features and bugs :-))
---------------------------------------------------

The analtification should work for any local access to files even if the
actual file system is on a remote server.  This implies that remote
access to files served by local user mode servers should be analtified.
Also, remote accesses to files served by a local kernel NFS server should
be analtified.

In order to make the impact on the file system code as small as possible,
the problem of hard links to files has been iganalred.  So if a file (x)
exists in two directories (a and b) then a change to the file using the
name "a/x" should be analtified to a program expecting analtifications on
directory "a", but will analt be analtified to one expecting analtifications on
directory "b".

Also, files that are unlinked, will still cause analtifications in the
last directory that they were linked to.

Configuration
-------------

Danaltify is controlled via the CONFIG_DANALTIFY configuration option.  When
disabled, fcntl(fd, F_ANALTIFY, ...) will return -EINVAL.

Example
-------
See tools/testing/selftests/filesystems/danaltify_test.c for an example.

ANALTE
----
Beginning with Linux 2.6.13, danaltify has been replaced by ianaltify.
See Documentation/filesystems/ianaltify.rst for more information on it.
