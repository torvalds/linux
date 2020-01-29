===================
NFS Fault Injection
===================

Fault injection is a method for forcing errors that may not normally occur, or
may be difficult to reproduce.  Forcing these errors in a controlled environment
can help the developer find and fix bugs before their code is shipped in a
production system.  Injecting an error on the Linux NFS server will allow us to
observe how the client reacts and if it manages to recover its state correctly.

NFSD_FAULT_INJECTION must be selected when configuring the kernel to use this
feature.


Using Fault Injection
=====================
On the client, mount the fault injection server through NFS v4.0+ and do some
work over NFS (open files, take locks, ...).

On the server, mount the debugfs filesystem to <debug_dir> and ls
<debug_dir>/nfsd.  This will show a list of files that will be used for
injecting faults on the NFS server.  As root, write a number n to the file
corresponding to the action you want the server to take.  The server will then
process the first n items it finds.  So if you want to forget 5 locks, echo '5'
to <debug_dir>/nfsd/forget_locks.  A value of 0 will tell the server to forget
all corresponding items.  A log message will be created containing the number
of items forgotten (check dmesg).

Go back to work on the client and check if the client recovered from the error
correctly.


Available Faults
================
forget_clients:
     The NFS server keeps a list of clients that have placed a mount call.  If
     this list is cleared, the server will have no knowledge of who the client
     is, forcing the client to reauthenticate with the server.

forget_openowners:
     The NFS server keeps a list of what files are currently opened and who
     they were opened by.  Clearing this list will force the client to reopen
     its files.

forget_locks:
     The NFS server keeps a list of what files are currently locked in the VFS.
     Clearing this list will force the client to reclaim its locks (files are
     unlocked through the VFS as they are cleared from this list).

forget_delegations:
     A delegation is used to assure the client that a file, or part of a file,
     has not changed since the delegation was awarded.  Clearing this list will
     force the client to reacquire its delegation before accessing the file
     again.

recall_delegations:
     Delegations can be recalled by the server when another client attempts to
     access a file.  This test will notify the client that its delegation has
     been revoked, forcing the client to reacquire the delegation before using
     the file again.


tools/nfs/inject_faults.sh script
=================================
This script has been created to ease the fault injection process.  This script
will detect the mounted debugfs directory and write to the files located there
based on the arguments passed by the user.  For example, running
`inject_faults.sh forget_locks 1` as root will instruct the server to forget
one lock.  Running `inject_faults forget_locks` will instruct the server to
forgetall locks.
