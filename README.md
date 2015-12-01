Medusa Voyager Security System
==========================

Medusa is a package that improves the overall security of the Linux OS by
extending the standard Linux (Unix) security architecture while preserving
backward compatibility. Briefly, it supports, at the kernel level, a user-space
authorization server (and is thus fully transparent to any user space
applications). Before the execution of certain operations, the kernel asks the
authorization server for confirmation.  The authorization server then permits
or forbids the operation. The authorization server can also affect the way an
operation is executed in some cases, which are described later. This method
allows the use of almost any security architecture. When the authorization
server is properly configured, it can determine access rights within the system
to a very fine level and do very good auditing.


Architecture
------------

Medusa consists of two basic parts: a small patch to the Linux
kernel and a user space security daemon called "Constable". Constable is the
current implementation of an authorization server. User space implementation
allows kernel changes to be simpler and smaller and thus easier to port to
new versions of the Linux kernel and to be more flexible, so improvements to the
authorization server should not require changes in the kernel.

Communication between Constable and kernel goes through the special device
"/dev/medusa" (char major 111 minor 0), because it should be both fast and
flexible. When the kernel needs confirmation, it writes data to this device,
makes the current process sleep and wakes up Constable. Constable reads the
data from /dev/medusa, chooses a response (depending on his configuration, which
is discussed in doc/Constable), sends it back to the kernel and sleeps. The
kernel gets the data, wakes up the process and determine the result of the
operation. Constable can also send certain commands to the kernel (even if the
kernel doesn't require them), which are then executed by the kernel. The
security daemon has to use a specific communication protocol defined in kernel,
so it is possible to implement a full-featured authorization server by only
knowing this protocol and knowing that the kernel supports it, without worrying
what's really happening in the kernel. Constable is only one example of such an
authorization server. The protocol allows communication in the form of packets
which carry all necessary data.

Installation
-----------
Run the bash file:

```
build.sh
```
