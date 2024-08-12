.. _kernel_docs:

Index of Further Kernel Documentation
=====================================

The need for a document like this one became apparent in the linux-kernel
mailing list as the same questions, asking for pointers to information,
appeared again and again.

Fortunately, as more and more people get to GNU/Linux, more and more get
interested in the Kernel. But reading the sources is not always enough. It
is easy to understand the code, but miss the concepts, the philosophy and
design decisions behind this code.

Unfortunately, not many documents are available for beginners to start.
And, even if they exist, there was no "well-known" place which kept track
of them. These lines try to cover this lack.

PLEASE, if you know any paper not listed here or write a new document,
include a reference to it here, following the kernel's patch submission
process. Any corrections, ideas or comments are also welcome.

All documents are cataloged with the following fields: the document's
"Title", the "Author"/s, the "URL" where they can be found, some "Keywords"
helpful when searching for specific topics, and a brief "Description" of
the Document.

.. note::

   The documents on each section of this document are ordered by its
   published date, from the newest to the oldest. The maintainer(s) should
   periodically retire resources as they become obsolete or outdated; with
   the exception of foundational books.

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

    * Title: **The Linux Kernel Module Programming Guide**

      :Author: Peter Jay Salzman, Michael Burian, Ori Pomerantz, Bob Mottram,
        Jim Huang.
      :URL: https://sysprog21.github.io/lkmpg/
      :Date: 2021
      :Keywords: modules, GPL book, /proc, ioctls, system calls,
        interrupt handlers .
      :Description: A very nice GPL book on the topic of modules
        programming. Lots of examples. Currently the new version is being
        actively maintained at https://github.com/sysprog21/lkmpg.

    * Title: **Rust for Linux**

      :Author: various
      :URL: https://rust-for-linux.com/
      :Date: rolling version
      :Keywords: glossary, terms, linux-kernel.
      :Description: From the website: "Rust for Linux is the project adding
        support for the Rust language to the Linux kernel. This website is
        intended as a hub of links, documentation and resources related to
        the project".

Published books
---------------

    * Title: **Practical Linux System Administration: A Guide to Installation, Configuration, and Management, 1st Edition**

      :Author: Kenneth Hess
      :Publisher: O'Reilly Media
      :Date: May, 2023
      :Pages: 246
      :ISBN: 978-1098109035
      :Notes: System administration

    * Title: **Linux Kernel Debugging: Leverage proven tools and advanced techniques to effectively debug Linux kernels and kernel modules**

      :Author: Kaiwan N Billimoria
      :Publisher: Packt Publishing Ltd
      :Date: August, 2022
      :Pages: 638
      :ISBN: 978-1801075039
      :Notes: Debugging book

    * Title: **Linux Kernel Programming: A Comprehensive Guide to Kernel Internals, Writing Kernel Modules, and Kernel Synchronization**

      :Author: Kaiwan N Billimoria
      :Publisher: Packt Publishing Ltd
      :Date: March, 2021 (Second Edition published in 2024)
      :Pages: 754
      :ISBN: 978-1789953435 (Second Edition ISBN is 978-1803232225)

    * Title: **Linux Kernel Programming Part 2 - Char Device Drivers and Kernel Synchronization: Create user-kernel interfaces, work with peripheral I/O, and handle hardware interrupts**

      :Author: Kaiwan N Billimoria
      :Publisher: Packt Publishing Ltd
      :Date: March, 2021
      :Pages: 452
      :ISBN: 978-1801079518

    * Title: **Linux System Programming: Talking Directly to the Kernel and C Library**

      :Author: Robert Love
      :Publisher: O'Reilly Media
      :Date: June, 2013
      :Pages: 456
      :ISBN: 978-1449339531
      :Notes: Foundational book

    * Title: **Linux Kernel Development, 3rd Edition**

      :Author: Robert Love
      :Publisher: Addison-Wesley
      :Date: July, 2010
      :Pages: 440
      :ISBN: 978-0672329463
      :Notes: Foundational book

.. _ldd3_published:

    * Title: **Linux Device Drivers, 3rd Edition**

      :Authors: Jonathan Corbet, Alessandro Rubini, and Greg Kroah-Hartman
      :Publisher: O'Reilly & Associates
      :Date: 2005
      :Pages: 636
      :ISBN: 0-596-00590-3
      :Notes: Foundational book. Further information in
        http://www.oreilly.com/catalog/linuxdrive3/
        PDF format, URL: https://lwn.net/Kernel/LDD3/

    * Title: **The Design of the UNIX Operating System**

      :Author: Maurice J. Bach
      :Publisher: Prentice Hall
      :Date: 1986
      :Pages: 471
      :ISBN: 0-13-201757-1
      :Notes: Foundational book

Miscellaneous
-------------

    * Name: **Cross-Referencing Linux**

      :URL: https://elixir.bootlin.com/
      :Keywords: Browsing source code.
      :Description: Another web-based Linux kernel source code browser.
        Lots of cross references to variables and functions. You can see
        where they are defined and where they are used.

    * Name: **Linux Weekly News**

      :URL: https://lwn.net
      :Keywords: latest kernel news.
      :Description: The title says it all. There's a fixed kernel section
        summarizing developers' work, bug fixes, new features and versions
        produced during the week.

    * Name: **The home page of Linux-MM**

      :Author: The Linux-MM team.
      :URL: https://linux-mm.org/
      :Keywords: memory management, Linux-MM, mm patches, TODO, docs,
        mailing list.
      :Description: Site devoted to Linux Memory Management development.
        Memory related patches, HOWTOs, links, mm developers... Don't miss
        it if you are interested in memory management development!

    * Name: **Kernel Newbies IRC Channel and Website**

      :URL: https://www.kernelnewbies.org
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

      :URL: https://subspace.kernel.org
      :URL: https://lore.kernel.org
      :Keywords: linux-kernel, archives, search.
      :Description: Some of the linux-kernel mailing list archivers. If
        you have a better/another one, please let me know.

    * Name: **The Linux Foundation YouTube channel**

      :URL: https://www.youtube.com/user/thelinuxfoundation
      :Keywords: linux, videos, linux-foundation, youtube.
      :Description: The Linux Foundation uploads video recordings of their
        collaborative events, Linux conferences including LinuxCon, and
        other original research and content related to Linux and software
        development.

-------

This document was originally based on:

 https://www.dit.upm.es/~jmseyas/linux/kernel/hackers-docs.html

and written by Juan-Mariano de Goyeneche
