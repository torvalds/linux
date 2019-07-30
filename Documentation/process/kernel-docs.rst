.. _kernel_docs:

Index of Documentation for People Interested in Writing and/or Understanding the Linux Kernel
=============================================================================================

          Juan-Mariano de Goyeneche <jmseyas@dit.upm.es>

The need for a document like this one became apparent in the
linux-kernel mailing list as the same questions, asking for pointers
to information, appeared again and again.

Fortunately, as more and more people get to GNU/Linux, more and more
get interested in the Kernel. But reading the sources is not always
enough. It is easy to understand the code, but miss the concepts, the
philosophy and design decisions behind this code.

Unfortunately, not many documents are available for beginners to
start. And, even if they exist, there was no "well-known" place which
kept track of them. These lines try to cover this lack. All documents
available on line known by the author are listed, while some reference
books are also mentioned.

PLEASE, if you know any paper not listed here or write a new document,
send me an e-mail, and I'll include a reference to it here. Any
corrections, ideas or comments are also welcomed.

The papers that follow are listed in no particular order. All are
cataloged with the following fields: the document's "Title", the
"Author"/s, the "URL" where they can be found, some "Keywords" helpful
when searching for specific topics, and a brief "Description" of the
Document.

Enjoy!

.. note::

   The documents on each section of this document are ordered by its
   published date, from the newest to the oldest.

Docs at the Linux Kernel tree
-----------------------------

The Sphinx books should be built with ``make {htmldocs | pdfdocs | epubdocs}``.

    * Name: **linux/Documentation**

      :Author: Many.
      :Location: Documentation/
      :Keywords: text files, Sphinx.
      :Description: Documentation that comes with the kernel sources,
        inside the Documentation directory. Some pages from this document
        (including this document itself) have been moved there, and might
        be more up to date than the web version.

On-line docs
------------

    * Title: **Linux Kernel Mailing List Glossary**

      :Author: various
      :URL: https://kernelnewbies.org/KernelGlossary
      :Date: rolling version
      :Keywords: glossary, terms, linux-kernel.
      :Description: From the introduction: "This glossary is intended as
        a brief description of some of the acronyms and terms you may hear
        during discussion of the Linux kernel".

    * Title: **Tracing the Way of Data in a TCP Connection through the Linux Kernel**

      :Author: Richard Sailer
      :URL: https://archive.org/details/linux_kernel_data_flow_short_paper
      :Date: 2016
      :Keywords: Linux Kernel Networking, TCP, tracing, ftrace
      :Description: A seminar paper explaining ftrace and how to use it for
        understanding linux kernel internals,
        illustrated at tracing the way of a TCP packet through the kernel.
      :Abstract: *This short paper outlines the usage of ftrace a tracing framework
        as a tool to understand a running Linux system.
        Having obtained a trace-log a kernel hacker can read and understand
        source code more determined and with context.
        In a detailed example this approach is demonstrated in tracing
        and the way of data in a TCP Connection through the kernel.
        Finally this trace-log is used as base for more a exact conceptual
        exploration and description of the Linux TCP/IP implementation.*

    * Title: **On submitting kernel Patches**

      :Author: Andi Kleen
      :URL: http://halobates.de/on-submitting-kernel-patches.pdf
      :Date: 2008
      :Keywords: patches, review process, types of submissions, basic rules, case studies
      :Description: This paper gives several experience values on what types of patches
        there are and how likley they get merged.
      :Abstract:
        [...]. This paper examines some common problems for
        submitting larger changes and some strategies to avoid problems.

    * Title: **Overview of the Virtual File System**

      :Author: Richard Gooch.
      :URL: http://www.mjmwired.net/kernel/Documentation/filesystems/vfs.txt
      :Date: 2007
      :Keywords: VFS, File System, mounting filesystems, opening files,
        dentries, dcache.
      :Description: Brief introduction to the Linux Virtual File System.
        What is it, how it works, operations taken when opening a file or
        mounting a file system and description of important data
        structures explaining the purpose of each of their entries.

    * Title: **Linux Device Drivers, Third Edition**

      :Author: Jonathan Corbet, Alessandro Rubini, Greg Kroah-Hartman
      :URL: http://lwn.net/Kernel/LDD3/
      :Date: 2005
      :Description: A 600-page book covering the (2.6.10) driver
        programming API and kernel hacking in general.  Available under the
        Creative Commons Attribution-ShareAlike 2.0 license.
      :note: You can also :ref:`purchase a copy from O'Reilly or elsewhere  <ldd3_published>`.

    * Title: **Writing an ALSA Driver**

      :Author: Takashi Iwai <tiwai@suse.de>
      :URL: http://www.alsa-project.org/~iwai/writing-an-alsa-driver/index.html
      :Date: 2005
      :Keywords: ALSA, sound, soundcard, driver, lowlevel, hardware.
      :Description: Advanced Linux Sound Architecture for developers,
        both at kernel and user-level sides. ALSA is the Linux kernel
        sound architecture in the 2.6 kernel version.

    * Title: **Linux PCMCIA Programmer's Guide**

      :Author: David Hinds.
      :URL: http://pcmcia-cs.sourceforge.net/ftp/doc/PCMCIA-PROG.html
      :Date: 2003
      :Keywords: PCMCIA.
      :Description: "This document describes how to write kernel device
        drivers for the Linux PCMCIA Card Services interface. It also
        describes how to write user-mode utilities for communicating with
        Card Services.

    * Title: **Linux Kernel Module Programming Guide**

      :Author: Ori Pomerantz.
      :URL: http://tldp.org/LDP/lkmpg/2.6/html/index.html
      :Date: 2001
      :Keywords: modules, GPL book, /proc, ioctls, system calls,
        interrupt handlers .
      :Description: Very nice 92 pages GPL book on the topic of modules
        programming. Lots of examples.

    * Title: **Global spinlock list and usage**

      :Author: Rick Lindsley.
      :URL: http://lse.sourceforge.net/lockhier/global-spin-lock
      :Date: 2001
      :Keywords: spinlock.
      :Description: This is an attempt to document both the existence and
        usage of the spinlocks in the Linux 2.4.5 kernel. Comprehensive
        list of spinlocks showing when they are used, which functions
        access them, how each lock is acquired, under what conditions it
        is held, whether interrupts can occur or not while it is held...

    * Title: **A Linux vm README**

      :Author: Kanoj Sarcar.
      :URL: http://kos.enix.org/pub/linux-vmm.html
      :Date: 2001
      :Keywords: virtual memory, mm, pgd, vma, page, page flags, page
        cache, swap cache, kswapd.
      :Description: Telegraphic, short descriptions and definitions
        relating the Linux virtual memory implementation.

    * Title: **Video4linux Drivers, Part 1: Video-Capture Device**

      :Author: Alan Cox.
      :URL: http://www.linux-mag.com/id/406
      :Date: 2000
      :Keywords: video4linux, driver, video capture, capture devices,
        camera driver.
      :Description: The title says it all.

    * Title: **Video4linux Drivers, Part 2: Video-capture Devices**

      :Author: Alan Cox.
      :URL: http://www.linux-mag.com/id/429
      :Date: 2000
      :Keywords: video4linux, driver, video capture, capture devices,
        camera driver, control, query capabilities, capability, facility.
      :Description: The title says it all.

    * Title: **Linux IP Networking. A Guide to the Implementation and Modification of the Linux Protocol Stack.**

      :Author: Glenn Herrin.
      :URL: http://www.cs.unh.edu/cnrg/gherrin
      :Date: 2000
      :Keywords: network, networking, protocol, IP, UDP, TCP, connection,
        socket, receiving, transmitting, forwarding, routing, packets,
        modules, /proc, sk_buff, FIB, tags.
      :Description: Excellent paper devoted to the Linux IP Networking,
        explaining anything from the kernel's to the user space
        configuration tools' code. Very good to get a general overview of
        the kernel networking implementation and understand all steps
        packets follow from the time they are received at the network
        device till they are delivered to applications. The studied kernel
        code is from 2.2.14 version. Provides code for a working packet
        dropper example.

    * Title: **How To Make Sure Your Driver Will Work On The Power Macintosh**

      :Author: Paul Mackerras.
      :URL: http://www.linux-mag.com/id/261
      :Date: 1999
      :Keywords: Mac, Power Macintosh, porting, drivers, compatibility.
      :Description: The title says it all.

    * Title: **An Introduction to SCSI Drivers**

      :Author: Alan Cox.
      :URL: http://www.linux-mag.com/id/284
      :Date: 1999
      :Keywords: SCSI, device, driver.
      :Description: The title says it all.

    * Title: **Advanced SCSI Drivers And Other Tales**

      :Author: Alan Cox.
      :URL: http://www.linux-mag.com/id/307
      :Date: 1999
      :Keywords: SCSI, device, driver, advanced.
      :Description: The title says it all.

    * Title: **Writing Linux Mouse Drivers**

      :Author: Alan Cox.
      :URL: http://www.linux-mag.com/id/330
      :Date: 1999
      :Keywords: mouse, driver, gpm.
      :Description: The title says it all.

    * Title: **More on Mouse Drivers**

      :Author: Alan Cox.
      :URL: http://www.linux-mag.com/id/356
      :Date: 1999
      :Keywords: mouse, driver, gpm, races, asynchronous I/O.
      :Description: The title still says it all.

    * Title: **Writing Video4linux Radio Driver**

      :Author: Alan Cox.
      :URL: http://www.linux-mag.com/id/381
      :Date: 1999
      :Keywords: video4linux, driver, radio, radio devices.
      :Description: The title says it all.

    * Title: **I/O Event Handling Under Linux**

      :Author: Richard Gooch.
      :URL: http://web.mit.edu/~yandros/doc/io-events.html
      :Date: 1999
      :Keywords: IO, I/O, select(2), poll(2), FDs, aio_read(2), readiness
        event queues.
      :Description: From the Introduction: "I/O Event handling is about
        how your Operating System allows you to manage a large number of
        open files (file descriptors in UNIX/POSIX, or FDs) in your
        application. You want the OS to notify you when FDs become active
        (have data ready to be read or are ready for writing). Ideally you
        want a mechanism that is scalable. This means a large number of
        inactive FDs cost very little in memory and CPU time to manage".

    * Title: **(nearly) Complete Linux Loadable Kernel Modules. The definitive guide for hackers, virus coders and system administrators.**

      :Author: pragmatic/THC.
      :URL: http://packetstormsecurity.org/docs/hack/LKM_HACKING.html
      :Date: 1999
      :Keywords: syscalls, intercept, hide, abuse, symbol table.
      :Description: Interesting paper on how to abuse the Linux kernel in
        order to intercept and modify syscalls, make
        files/directories/processes invisible, become root, hijack ttys,
        write kernel modules based virus... and solutions for admins to
        avoid all those abuses.
      :Notes: For 2.0.x kernels. Gives guidances to port it to 2.2.x
        kernels.

    * Name: **Linux Virtual File System**

      :Author: Peter J. Braam.
      :URL: http://www.coda.cs.cmu.edu/doc/talks/linuxvfs/
      :Date: 1998
      :Keywords: slides, VFS, inode, superblock, dentry, dcache.
      :Description: Set of slides, presumably from a presentation on the
        Linux VFS layer. Covers version 2.1.x, with dentries and the
        dcache.

    * Title: **The Venus kernel interface**

      :Author: Peter J. Braam.
      :URL: http://www.coda.cs.cmu.edu/doc/html/kernel-venus-protocol.html
      :Date: 1998
      :Keywords: coda, filesystem, venus, cache manager.
      :Description: "This document describes the communication between
        Venus and kernel level file system code needed for the operation
        of the Coda filesystem. This version document is meant to describe
        the current interface (version 1.0) as well as improvements we
        envisage".

    * Title: **Design and Implementation of the Second Extended Filesystem**

      :Author: Rémy Card, Theodore Ts'o, Stephen Tweedie.
      :URL: http://web.mit.edu/tytso/www/linux/ext2intro.html
      :Date: 1998
      :Keywords: ext2, linux fs history, inode, directory, link, devices,
        VFS, physical structure, performance, benchmarks, ext2fs library,
        ext2fs tools, e2fsck.
      :Description: Paper written by three of the top ext2 hackers.
        Covers Linux filesystems history, ext2 motivation, ext2 features,
        design, physical structure on disk, performance, benchmarks,
        e2fsck's passes description... A must read!
      :Notes: This paper was first published in the Proceedings of the
        First Dutch International Symposium on Linux, ISBN 90-367-0385-9.

    * Title: **The Linux RAID-1, 4, 5 Code**

      :Author: Ingo Molnar, Gadi Oxman and Miguel de Icaza.
      :URL: http://www.linuxjournal.com/article.php?sid=2391
      :Date: 1997
      :Keywords: RAID, MD driver.
      :Description: Linux Journal Kernel Korner article. Here is its
      :Abstract: *A description of the implementation of the RAID-1,
        RAID-4 and RAID-5 personalities of the MD device driver in the
        Linux kernel, providing users with high performance and reliable,
        secondary-storage capability using software*.

    * Title: **Linux Kernel Hackers' Guide**

      :Author: Michael K. Johnson.
      :URL: http://www.tldp.org/LDP/khg/HyperNews/get/khg.html
      :Date: 1997
      :Keywords: device drivers, files, VFS, kernel interface, character vs
        block devices, hardware interrupts, scsi, DMA, access to user memory,
        memory allocation, timers.
      :Description: A guide designed to help you get up to speed on the
        concepts that are not intuitevly obvious, and to document the internal
        structures of Linux.

    * Title: **Dynamic Kernels: Modularized Device Drivers**

      :Author: Alessandro Rubini.
      :URL: http://www.linuxjournal.com/article.php?sid=1219
      :Date: 1996
      :Keywords: device driver, module, loading/unloading modules,
        allocating resources.
      :Description: Linux Journal Kernel Korner article. Here is its
      :Abstract: *This is the first of a series of four articles
        co-authored by Alessandro Rubini and Georg Zezchwitz which present
        a practical approach to writing Linux device drivers as kernel
        loadable modules. This installment presents an introduction to the
        topic, preparing the reader to understand next month's
        installment*.

    * Title: **Dynamic Kernels: Discovery**

      :Author: Alessandro Rubini.
      :URL: http://www.linuxjournal.com/article.php?sid=1220
      :Date: 1996
      :Keywords: character driver, init_module, clean_up module,
        autodetection, mayor number, minor number, file operations,
        open(), close().
      :Description: Linux Journal Kernel Korner article. Here is its
      :Abstract: *This article, the second of four, introduces part of
        the actual code to create custom module implementing a character
        device driver. It describes the code for module initialization and
        cleanup, as well as the open() and close() system calls*.

    * Title: **The Devil's in the Details**

      :Author: Georg v. Zezschwitz and Alessandro Rubini.
      :URL: http://www.linuxjournal.com/article.php?sid=1221
      :Date: 1996
      :Keywords: read(), write(), select(), ioctl(), blocking/non
        blocking mode, interrupt handler.
      :Description: Linux Journal Kernel Korner article. Here is its
      :Abstract: *This article, the third of four on writing character
        device drivers, introduces concepts of reading, writing, and using
        ioctl-calls*.

    * Title: **Dissecting Interrupts and Browsing DMA**

      :Author: Alessandro Rubini and Georg v. Zezschwitz.
      :URL: http://www.linuxjournal.com/article.php?sid=1222
      :Date: 1996
      :Keywords: interrupts, irqs, DMA, bottom halves, task queues.
      :Description: Linux Journal Kernel Korner article. Here is its
      :Abstract: *This is the fourth in a series of articles about
        writing character device drivers as loadable kernel modules. This
        month, we further investigate the field of interrupt handling.
        Though it is conceptually simple, practical limitations and
        constraints make this an ''interesting'' part of device driver
        writing, and several different facilities have been provided for
        different situations. We also investigate the complex topic of
        DMA*.

    * Title: **Device Drivers Concluded**

      :Author: Georg v. Zezschwitz.
      :URL: http://www.linuxjournal.com/article.php?sid=1287
      :Date: 1996
      :Keywords: address spaces, pages, pagination, page management,
        demand loading, swapping, memory protection, memory mapping, mmap,
        virtual memory areas (VMAs), vremap, PCI.
      :Description: Finally, the above turned out into a five articles
        series. This latest one's introduction reads: "This is the last of
        five articles about character device drivers. In this final
        section, Georg deals with memory mapping devices, beginning with
        an overall description of the Linux memory management concepts".

    * Title: **Network Buffers And Memory Management**

      :Author: Alan Cox.
      :URL: http://www.linuxjournal.com/article.php?sid=1312
      :Date: 1996
      :Keywords: sk_buffs, network devices, protocol/link layer
        variables, network devices flags, transmit, receive,
        configuration, multicast.
      :Description: Linux Journal Kernel Korner.
      :Abstract: *Writing a network device driver for Linux is fundamentally
        simple---most of the complexity (other than talking to the
        hardware) involves managing network packets in memory*.

    * Title: **Analysis of the Ext2fs structure**

      :Author: Louis-Dominique Dubeau.
      :URL: http://teaching.csse.uwa.edu.au/units/CITS2002/fs-ext2/
      :Date: 1994
      :Keywords: ext2, filesystem, ext2fs.
      :Description: Description of ext2's blocks, directories, inodes,
        bitmaps, invariants...

Published books
---------------

    * Title: **Linux Treiber entwickeln**

      :Author: Jürgen Quade, Eva-Katharina Kunst
      :Publisher: dpunkt.verlag
      :Date: Oct 2015 (4th edition)
      :Pages: 688
      :ISBN: 978-3-86490-288-8
      :Note: German. The third edition from 2011 is
         much cheaper and still quite up-to-date.

    * Title: **Linux Kernel Networking: Implementation and Theory**

      :Author: Rami Rosen
      :Publisher: Apress
      :Date: December 22, 2013
      :Pages: 648
      :ISBN: 978-1430261964

    * Title: **Embedded Linux Primer: A practical Real-World Approach, 2nd Edition**

      :Author: Christopher Hallinan
      :Publisher: Pearson
      :Date: November, 2010
      :Pages: 656
      :ISBN: 978-0137017836

    * Title: **Linux Kernel Development, 3rd Edition**

      :Author: Robert Love
      :Publisher: Addison-Wesley
      :Date: July, 2010
      :Pages: 440
      :ISBN: 978-0672329463

    * Title: **Essential Linux Device Drivers**

      :Author: Sreekrishnan Venkateswaran
      :Published: Prentice Hall
      :Date: April, 2008
      :Pages: 744
      :ISBN: 978-0132396554

.. _ldd3_published:

    * Title: **Linux Device Drivers, 3rd Edition**

      :Authors: Jonathan Corbet, Alessandro Rubini, and Greg Kroah-Hartman
      :Publisher: O'Reilly & Associates
      :Date: 2005
      :Pages: 636
      :ISBN: 0-596-00590-3
      :Notes: Further information in
        http://www.oreilly.com/catalog/linuxdrive3/
        PDF format, URL: http://lwn.net/Kernel/LDD3/

    * Title: **Linux Kernel Internals**

      :Author: Michael Beck
      :Publisher: Addison-Wesley
      :Date: 1997
      :ISBN: 0-201-33143-8 (second edition)

    * Title: **Programmation Linux 2.0 API systeme et fonctionnement du noyau**

      :Author: Remy Card, Eric Dumas, Franck Mevel
      :Publisher: Eyrolles
      :Date: 1997
      :Pages: 520
      :ISBN: 2-212-08932-5
      :Notes: French

    * Title: **The Design and Implementation of the 4.4 BSD UNIX Operating System**

      :Author: Marshall Kirk McKusick, Keith Bostic, Michael J. Karels,
        John S. Quarterman
      :Publisher: Addison-Wesley
      :Date: 1996
      :ISBN: 0-201-54979-4

    * Title: **Unix internals -- the new frontiers**

      :Author: Uresh Vahalia
      :Publisher: Prentice Hall
      :Date: 1996
      :Pages: 600
      :ISBN: 0-13-101908-2

    * Title: **Programming for the real world - POSIX.4**

      :Author: Bill O. Gallmeister
      :Publisher: O'Reilly & Associates, Inc
      :Date: 1995
      :Pages: 552
      :ISBN: I-56592-074-0
      :Notes: Though not being directly about Linux, Linux aims to be
        POSIX. Good reference.

    * Title:  **UNIX  Systems  for  Modern Architectures: Symmetric Multiprocessing and Caching for Kernel Programmers**

      :Author: Curt Schimmel
      :Publisher: Addison Wesley
      :Date: June, 1994
      :Pages: 432
      :ISBN: 0-201-63338-8

    * Title: **The Design and Implementation of the 4.3 BSD UNIX Operating System**

      :Author: Samuel J. Leffler, Marshall Kirk McKusick, Michael J
        Karels, John S. Quarterman
      :Publisher: Addison-Wesley
      :Date: 1989 (reprinted with corrections on October, 1990)
      :ISBN: 0-201-06196-1

    * Title: **The Design of the UNIX Operating System**

      :Author: Maurice J. Bach
      :Publisher: Prentice Hall
      :Date: 1986
      :Pages: 471
      :ISBN: 0-13-201757-1

Miscellaneous
-------------

    * Name: **Cross-Referencing Linux**

      :URL: https://elixir.bootlin.com/
      :Keywords: Browsing source code.
      :Description: Another web-based Linux kernel source code browser.
        Lots of cross references to variables and functions. You can see
        where they are defined and where they are used.

    * Name: **Linux Weekly News**

      :URL: http://lwn.net
      :Keywords: latest kernel news.
      :Description: The title says it all. There's a fixed kernel section
        summarizing developers' work, bug fixes, new features and versions
        produced during the week. Published every Thursday.

    * Name: **The home page of Linux-MM**

      :Author: The Linux-MM team.
      :URL: http://linux-mm.org/
      :Keywords: memory management, Linux-MM, mm patches, TODO, docs,
        mailing list.
      :Description: Site devoted to Linux Memory Management development.
        Memory related patches, HOWTOs, links, mm developers... Don't miss
        it if you are interested in memory management development!

    * Name: **Kernel Newbies IRC Channel and Website**

      :URL: http://www.kernelnewbies.org
      :Keywords: IRC, newbies, channel, asking doubts.
      :Description: #kernelnewbies on irc.oftc.net.
        #kernelnewbies is an IRC network dedicated to the 'newbie'
        kernel hacker. The audience mostly consists of people who are
        learning about the kernel, working on kernel projects or
        professional kernel hackers that want to help less seasoned kernel
        people.
        #kernelnewbies is on the OFTC IRC Network.
        Try irc.oftc.net as your server and then /join #kernelnewbies.
        The kernelnewbies website also hosts articles, documents, FAQs...

    * Name: **linux-kernel mailing list archives and search engines**

      :URL: http://vger.kernel.org/vger-lists.html
      :URL: http://www.uwsg.indiana.edu/hypermail/linux/kernel/index.html
      :URL: http://groups.google.com/group/mlist.linux.kernel
      :Keywords: linux-kernel, archives, search.
      :Description: Some of the linux-kernel mailing list archivers. If
        you have a better/another one, please let me know.

-------

Document last updated on Tue 2016-Sep-20

This document is based on:
 http://www.dit.upm.es/~jmseyas/linux/kernel/hackers-docs.html
