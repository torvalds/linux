=====================================
Assignment 4 - SO2 Transport Protocol
=====================================

- Deadline: :command:`Monday, May, 31, 2021, 23:00`
- This assignment can be made in teams (max 2). Only one of them must submit the assignment, and the names of the student should be listed in a README file.

Implement a simple datagram transport protocol - STP (*SO2 Transport Protocol*).

Assignment's Objectives
=======================

* gaining knowledge about the operation of the networking subsystem in the Linux kernel
* obtaining skills to work with the basic structures of the networking subsystem in Linux
* deepening the notions related to communication and networking protocols by implementing a protocol in an existing protocol stack

Statement
=========

Implement, in the Linux kernel, a protocol called STP (*SO2 Transport Protocol*), at network and transport level, that works using datagrams (it is not connection-oriented and does not use flow-control elements).

The STP protocol acts as a Transport layer protocol (port-based multiplexing) but operates at level 3 (Network) of `the OSI stack <http://en.wikipedia.org/wiki/OSI_model>`__, above the Data Link level.

The STP header is defined by the ``struct stp_header`` structure:

.. code-block:: c

  struct stp_header {
          __be16 dst;
          __be16 src;
          __be16 len;
          __u8 flags;
          __u8 csum;
  };


where:

  * ``len`` is the length of the packet in bytes (including the header);
  * ``dst`` and ``src`` are the destination and source ports, respectively;
  * ``flags`` contains various flags, currently unused (marked *reserved*);
  * ``csum`` is the checksum of the entire package including the header; the checksum is calculated by exclusive OR (XOR) between all bytes.

Sockets using this protocol will use the ``AF_STP`` family.

The protocol must work directly over Ethernet. The ports used are between ``1`` and ``65535``. Port ``0`` is not used.

The definition of STP-related structures and macros can be found in the `assignment support header <https://github.com/linux-kernel-labs/linux/blob/master/tools/labs/templates/assignments/4-stp/stp.h>`__.

Implementation Details
======================

The kernel module will be named **af_stp.ko**.

You have to define a structure of type `net_proto_family <http://elixir.free-electrons.com/linux/v4.9.11/source/include/linux/net.h#L200>`__, which provides the operation to create STP sockets.
Newly created sockets are not associated with any port or interface and cannot receive / send packets.
You must initialize the `socket ops field <http://elixir.free-electrons.com/linux/v4.9.11/source/include/linux/net.h#L124>`__ with the list of operations specific to the STP family.
This field refers to a structure `proto_ops <http://elixir.free-electrons.com/linux/v4.9.11/source/include/linux/net.h#L136>`__ which must include the following functions:

* ``release``: releases an STP socket
* ``bind``: associates a socket with a port (possibly also an interface) on which packets will be received / sent:

  * there may be bind sockets only on one port (not on an interface)
  * sockets associated with only one port will be able to receive packets sent to that port on all interfaces (analogous to UDP sockets associated with only one port); these sockets cannot send packets because the interface from which they can be sent via the standard sockets API cannot be specified
  * two sockets cannot be binded to the same port-interface combination:

    * if there is a socket already binded with a port and an interface then a second socket cannot be binded to the same port and the same interface or without a specified interface
    * if there is a socket already binded to a port but without a specified interface then a second socket cannot be binded to the same port (with or without a specified interface)

  * we recommend using a hash table for bind instead of other data structures (list, array); in the kernel there is a hash table implementation in the `hashtable.h header <http://elixir.free-electrons.com/linux/v4.9.11/source/include/linux/hashtable.h>`__

* ``connect``: associates a socket with a remote port and hardware address (MAC address) to which packets will be sent / received:

  * this should allow ``send`` / ``recv`` operations on the socket instead of ``sendmsg`` / ``recvmsg`` or ``sendto`` / ``recvfrom``
  * once connected to a host, sockets will only accept packets from that host
  * once connected, the sockets can no longer be disconnected

* ``sendmsg``, ``recvmsg``: send or receive a datagram on an STP socket:

  * for the *receive* part, metainformation about the host that sent the packet can be stored in the `cb field in sk_buff <http://elixir.free-electrons.com/linux/v4.9.11/source/include/linux/skbuff.h#L650>`__

* ``poll``: the default function ``datagram_poll`` will have to be used
* for the rest of the operations the predefined stubs in the kernel will have to be used (``sock_no_*``)

.. code-block:: c

    static const struct proto_ops stp_ops = {
            .family = PF_STP,
            .owner = THIS_MODULE,
            .release = stp_release,
            .bind = stp_bind,
            .connect = stp_connect,
            .socketpair = sock_no_socketpair,
            .accept = sock_no_accept,
            .getname = sock_no_getname,
            .poll = datagram_poll,
            .ioctl = sock_no_ioctl,
            .listen = sock_no_listen,
            .shutdown = sock_no_shutdown,
            .setsockopt = sock_no_setsockopt,
            .getsockopt = sock_no_getsockopt,
            .sendmsg = stp_sendmsg,
            .recvmsg = stp_recvmsg,
            .mmap = sock_no_mmap,
            .sendpage = sock_no_sendpage,
    };

Socket operations use a type of address called ``sockaddr_stp``, a type defined in the `assignment support header <https://github.com/linux-kernel-labs/linux/blob/master/tools/labs/templates/assignments/4-stp/stp.h>`__.
For the *bind* operation, only the port and the index of the interface on which the socket is bind will be considered.
For the *receive* operation, only the ``addr`` and ``port`` fields in the structure will be filled in with the MAC address of the host that sent the packet and with the port from which it was sent.
Also, when sending a packet, the destination host will be obtained from the ``addr`` and ``port`` fields of this structure.

You need to register a structure `packet_type <http://elixir.free-electrons.com/linux/v4.9.11/source/include/linux/netdevice.h#L2222>`__, using the call `dev_add_pack <http://elixir.free-electrons.com/linux/v4.9.11/source/net/core/dev.c#L386>`__ to be able to receive STP packets from the network layer.

The protocol will need to provide an interface through the *procfs* file system for statistics on sent / received packets.
The file must be named ``/proc/net/stp_stats``, specified by the ``STP_PROC_FULL_FILENAME`` macro in `assignment support header <https://github.com/linux-kernel-labs/linux/blob/master/tools/labs/templates/assignments/4-stp/stp.h>`__.
The format must be of simple table type with ``2`` rows: on the first row the header of the table, and on the second row the statistics corresponding to the columns.
The columns of the table must be in order:

.. code::

    RxPkts HdrErr CsumErr NoSock NoBuffs TxPkts

where:

* ``RxPkts`` - the number of packets received
* ``HdrErr`` - the number of packets received with header errors (packets too short or with source or destination 0 ports)
* ``CsumErr`` - the number of packets received with checksum errors
* ``NoSock`` - the number of received packets for which no destination socket was found
* ``NoBuffs`` - the number of received packets that could not be received because the socket queue was full
* ``TxPkts`` - the number of packets sent

To create or delete the entry specified by ``STP_PROC_FULL_FILENAME`` we recommend using the functions `proc_create <http://elixir.free-electrons.com/linux/v4.9.11/source/include/linux/proc_fs.h#L30>`__ and `proc_remove <http://elixir.free-electrons.com/linux/v4.9.11/source/fs/proc/generic.c#L636>`__.

Sample Protocol Implementations
-------------------------------

For examples of protocol implementation, we recommend the implementation of `PF_PACKET <http://elixir.free-electrons.com/linux/v4.9.11/source/net/packet/af_packet.c>`__ sockets and the various functions in `UDP implementation <http: //elixir.free-electrons.com/linux/v4.9.11/source/net/ipv4/udp.c>`__ or `IP implementation <http://elixir.free-electrons.com/linux/v4.9.11/source/net/ipv4/af_inet.c>`__.

Testing
=======

In order to simplify the assignment evaluation process, but also to reduce the mistakes of the submitted assignments, the assignment evaluation will be done automatically with with the help of public tests that are in the new infrastructure.
For local testing, use the following commands:

.. code-block:: console

   $ git clone https://github.com/linux-kernel-labs/linux.git
   $ cd linux/tools/labs
   $ LABS=assignments/4-stp make skels
   $ #the development of the assignment will be written in the 4-stp directory
   $ make build
   $ make copy
   $ make boot

tcpdump
-------

You can use the ``tcpdump`` utility to troubleshoot sent packets.
The tests use the loopback interface; to track sent packets you can use a command line of the form:

.. code:: console

    tcpdump -i lo -XX

You can use a static version of `tcpdump <http://elf.cs.pub.ro/so2/res/teme/tcpdump>`__.
To add to the ``PATH`` environment variable in the virtual machine, copy this file to ``qemu-so2/fsimg/bin``.
Create the directory if it does not exist. Remember to give the ``tcpdump`` file execution permissions:

.. code:: console

    # In the qemu-so2 directory
    mkdir fsimg / bin
    wget http://elf.cs.pub.ro/so2/res/teme/tcpdump -O fsimg / bin / tcpdump
    chmod 755 fsimg / bin / tcpdump

Tips
----

To increase your chances of getting the highest grade, read and follow the Linux kernel coding style described in the `Coding Style document <https://elixir.bootlin.com/linux/v4.19.19/source/Documentation/process/coding-style.rst>`__.

Also, use the following static analysis tools to verify the code:

* checkpatch.pl

  .. code-block:: console

     $ linux/scripts/checkpatch.pl --no-tree --terse -f /path/to/your/file.c

* sparse

  .. code-block:: console

     $ sudo apt-get install sparse
     $ cd linux
     $ make C=2 /path/to/your/file.c

* cppcheck

  .. code-block:: console

     $ sudo apt-get install cppcheck
     $ cppcheck /path/to/your/file.c

Penalties
---------

Information about assigments penalties can be found on the `General Directions page <https://ocw.cs.pub.ro/courses/so2/teme/general>`__.

In exceptional cases (the assigment passes the tests by not complying with the requirements) and if the assigment does not pass all the tests, the grade will may decrease more than mentioned above.

Submitting the assigment
------------------------

The assignment archive will be submitted to vmchecker, according to the rules on the `rules page <https://ocw.cs.pub.ro/courses/so2/reguli-notare#reguli_de_trimitere_a_temelor>`__.

From the vmchecker interface choose the `Transport Protocol` option for this assigment.

Resources
=========

* `Lecture 10 - Networking <https://linux-kernel-labs.github.io/refs/heads/master/so2/lec10-networking.html>`__
* `Lab 10 - Networking <https://linux-kernel-labs.github.io/refs/heads/master/so2/lab10-networking.html>`__
* Linux kernel sources

  * `Implementing PF_PACKET sockets <http://elixir.free-electrons.com/linux/v4.9.11/source/net/packet/af_packet.c>`__
  * `Implementation of the UDP protocol <http://elixir.free-electrons.com/linux/v4.9.11/source/net/ipv4/udp.c>`__
  * `Implementation of the IP protocol <http://elixir.free-electrons.com/linux/v4.9.11/source/net/ipv4/af_inet.c>`__

* Understanding Linux Network Internals

  * chapters 8-13

* `assignment support header <https://github.com/linux-kernel-labs/linux/blob/master/tools/labs/templates/assignments/4-stp/stp.h>`__

We recommend that you use GitLab to store your homework. Follow the directions in `README <https://github.com/systems-cs-pub-ro/so2-assignments/blob/master/README.md>`__ and on the dedicated `git wiki page <https://ocw.cs.pub.ro/courses/so2/teme/folosire-gitlab>`__.

The resources for the assignment can also be found in the `so2-assignments <https://github.com/systems-cs-pub-ro/so2-assignments>`__ repo on GitHub.
The repo contains a `Bash script <https://github.com/systems-cs-pub-ro/so2-assignments/blob/master/so2-create-repo.sh>`__ that helps you create a private repository on the faculty `GitLab <https://gitlab.cs.pub.ro/users/sign_in>`__ instance.
Follow the tips from the `README <https://github.com/systems-cs-pub-ro/so2-assignments/blob/master/README.md>`__ and on the dedicated `Wiki page <https://ocw.cs.pub.ro/courses/so2/teme/folosire-gitlab>`__.

Questions
=========

For questions about the assigment, you can consult the mailing `list archives <http://cursuri.cs.pub.ro/pipermail/so2/>`__ or send an e-mail (you must be `registered <http://cursuri.cs.pub.ro/cgi-bin/mailman/listinfo/so2>`__).
Please follow and follow `the tips for use of the list <https://ocw.cs.pub.ro/courses/so2/resurse/lista-discutii#mailing-list-guidelines>`__.

Before you ask a question, make sure that:

* you have read the statement of the assigment well
* the question is not already presented on the `FAQ page <https://ocw.cs.pub.ro/courses/so2/teme/tema2/faq>`__
* the answer cannot be found in the `mailing list archives <http://cursuri.cs.pub.ro/pipermail/so2/>`__
