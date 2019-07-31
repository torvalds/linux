============
Introduction
============

  This is the client VFS module for the SMB3 NAS protocol as well
  as for older dialects such as the Common Internet File System (CIFS)
  protocol which was the successor to the Server Message Block
  (SMB) protocol, the native file sharing mechanism for most early
  PC operating systems. New and improved versions of CIFS are now
  called SMB2 and SMB3. Use of SMB3 (and later, including SMB3.1.1)
  is strongly preferred over using older dialects like CIFS due to
  security reaasons. All modern dialects, including the most recent,
  SMB3.1.1 are supported by the CIFS VFS module. The SMB3 protocol
  is implemented and supported by all major file servers
  such as all modern versions of Windows (including Windows 2016
  Server), as well as by Samba (which provides excellent
  CIFS/SMB2/SMB3 server support and tools for Linux and many other
  operating systems).  Apple systems also support SMB3 well, as
  do most Network Attached Storage vendors, so this network
  filesystem client can mount to a wide variety of systems.
  It also supports mounting to the cloud (for example
  Microsoft Azure), including the necessary security features.

  The intent of this module is to provide the most advanced network
  file system function for SMB3 compliant servers, including advanced
  security features, excellent parallelized high performance i/o, better
  POSIX compliance, secure per-user session establishment, encryption,
  high performance safe distributed caching (leases/oplocks), optional packet
  signing, large files, Unicode support and other internationalization
  improvements. Since both Samba server and this filesystem client support
  the CIFS Unix extensions (and in the future SMB3 POSIX extensions),
  the combination can provide a reasonable alternative to other network and
  cluster file systems for fileserving in some Linux to Linux environments,
  not just in Linux to Windows (or Linux to Mac) environments.

  This filesystem has a mount utility (mount.cifs) and various user space
  tools (including smbinfo and setcifsacl) that can be obtained from

      https://git.samba.org/?p=cifs-utils.git

  or

      git://git.samba.org/cifs-utils.git

  mount.cifs should be installed in the directory with the other mount helpers.

  For more information on the module see the project wiki page at

      https://wiki.samba.org/index.php/LinuxCIFS

  and

      https://wiki.samba.org/index.php/LinuxCIFS_utils
