=======================================
Oracle Data Analytics Accelerator (DAX)
=======================================

DAX is a coprocessor which resides on the SPARC M7 (DAX1) and M8
(DAX2) processor chips, and has direct access to the CPU's L3 caches
as well as physical memory. It can perform several operations on data
streams with various input and output formats.  A driver provides a
transport mechanism and has limited knowledge of the various opcodes
and data formats. A user space library provides high level services
and translates these into low level commands which are then passed
into the driver and subsequently the Hypervisor and the coprocessor.
The library is the recommended way for applications to use the
coprocessor, and the driver interface is not intended for general use.
This document describes the general flow of the driver, its
structures, and its programmatic interface. It also provides example
code sufficient to write user or kernel applications that use DAX
functionality.

The user library is open source and available at:

    https://oss.oracle.com/git/gitweb.cgi?p=libdax.git

The Hypervisor interface to the coprocessor is described in detail in
the accompanying document, dax-hv-api.txt, which is a plain text
excerpt of the (Oracle internal) "UltraSPARC Virtual Machine
Specification" version 3.0.20+15, dated 2017-09-25.


High Level Overview
===================

A coprocessor request is described by a Command Control Block
(CCB). The CCB contains an opcode and various parameters. The opcode
specifies what operation is to be done, and the parameters specify
options, flags, sizes, and addresses.  The CCB (or an array of CCBs)
is passed to the Hypervisor, which handles queueing and scheduling of
requests to the available coprocessor execution units. A status code
returned indicates if the request was submitted successfully or if
there was an error.  One of the addresses given in each CCB is a
pointer to a "completion area", which is a 128 byte memory block that
is written by the coprocessor to provide execution status. No
interrupt is generated upon completion; the completion area must be
polled by software to find out when a transaction has finished, but
the M7 and later processors provide a mechanism to pause the virtual
processor until the completion status has been updated by the
coprocessor. This is done using the monitored load and mwait
instructions, which are described in more detail later.  The DAX
coprocessor was designed so that after a request is submitted, the
kernel is no longer involved in the processing of it.  The polling is
done at the user level, which results in almost zero latency between
completion of a request and resumption of execution of the requesting
thread.


Addressing Memory
=================

The kernel does not have access to physical memory in the Sun4v
architecture, as there is an additional level of memory virtualization
present. This intermediate level is called "real" memory, and the
kernel treats this as if it were physical.  The Hypervisor handles the
translations between real memory and physical so that each logical
domain (LDOM) can have a partition of physical memory that is isolated
from that of other LDOMs.  When the kernel sets up a virtual mapping,
it specifies a virtual address and the real address to which it should
be mapped.

The DAX coprocessor can only operate on physical memory, so before a
request can be fed to the coprocessor, all the addresses in a CCB must
be converted into physical addresses. The kernel cannot do this since
it has no visibility into physical addresses. So a CCB may contain
either the virtual or real addresses of the buffers or a combination
of them. An "address type" field is available for each address that
may be given in the CCB. In all cases, the Hypervisor will translate
all the addresses to physical before dispatching to hardware. Address
translations are performed using the context of the process initiating
the request.


The Driver API
==============

An application makes requests to the driver via the write() system
call, and gets results (if any) via read(). The completion areas are
made accessible via mmap(), and are read-only for the application.

The request may either be an immediate command or an array of CCBs to
be submitted to the hardware.

Each open instance of the device is exclusive to the thread that
opened it, and must be used by that thread for all subsequent
operations. The driver open function creates a new context for the
thread and initializes it for use.  This context contains pointers and
values used internally by the driver to keep track of submitted
requests. The completion area buffer is also allocated, and this is
large enough to contain the completion areas for many concurrent
requests.  When the device is closed, any outstanding transactions are
flushed and the context is cleaned up.

On a DAX1 system (M7), the device will be called "oradax1", while on a
DAX2 system (M8) it will be "oradax2". If an application requires one
or the other, it should simply attempt to open the appropriate
device. Only one of the devices will exist on any given system, so the
name can be used to determine what the platform supports.

The immediate commands are CCB_DEQUEUE, CCB_KILL, and CCB_INFO. For
all of these, success is indicated by a return value from write()
equal to the number of bytes given in the call. Otherwise -1 is
returned and errno is set.

CCB_DEQUEUE
-----------

Tells the driver to clean up resources associated with past
requests. Since no interrupt is generated upon the completion of a
request, the driver must be told when it may reclaim resources.  No
further status information is returned, so the user should not
subsequently call read().

CCB_KILL
--------

Kills a CCB during execution. The CCB is guaranteed to not continue
executing once this call returns successfully. On success, read() must
be called to retrieve the result of the action.

CCB_INFO
--------

Retrieves information about a currently executing CCB. Note that some
Hypervisors might return 'notfound' when the CCB is in 'inprogress'
state. To ensure a CCB in the 'notfound' state will never be executed,
CCB_KILL must be invoked on that CCB. Upon success, read() must be
called to retrieve the details of the action.

Submission of an array of CCBs for execution
---------------------------------------------

A write() whose length is a multiple of the CCB size is treated as a
submit operation. The file offset is treated as the index of the
completion area to use, and may be set via lseek() or using the
pwrite() system call. If -1 is returned then errno is set to indicate
the error. Otherwise, the return value is the length of the array that
was actually accepted by the coprocessor. If the accepted length is
equal to the requested length, then the submission was completely
successful and there is no further status needed; hence, the user
should not subsequently call read(). Partial acceptance of the CCB
array is indicated by a return value less than the requested length,
and read() must be called to retrieve further status information.  The
status will reflect the error caused by the first CCB that was not
accepted, and status_data will provide additional data in some cases.

MMAP
----

The mmap() function provides access to the completion area allocated
in the driver.  Note that the completion area is not writeable by the
user process, and the mmap call must not specify PROT_WRITE.


Completion of a Request
=======================

The first byte in each completion area is the command status which is
updated by the coprocessor hardware. Software may take advantage of
new M7/M8 processor capabilities to efficiently poll this status byte.
First, a "monitored load" is achieved via a Load from Alternate Space
(ldxa, lduba, etc.) with ASI 0x84 (ASI_MONITOR_PRIMARY).  Second, a
"monitored wait" is achieved via the mwait instruction (a write to
%asr28). This instruction is like pause in that it suspends execution
of the virtual processor for the given number of nanoseconds, but in
addition will terminate early when one of several events occur. If the
block of data containing the monitored location is modified, then the
mwait terminates. This causes software to resume execution immediately
(without a context switch or kernel to user transition) after a
transaction completes. Thus the latency between transaction completion
and resumption of execution may be just a few nanoseconds.


Application Life Cycle of a DAX Submission
==========================================

 - open dax device
 - call mmap() to get the completion area address
 - allocate a CCB and fill in the opcode, flags, parameters, addresses, etc.
 - submit CCB via write() or pwrite()
 - go into a loop executing monitored load + monitored wait and
   terminate when the command status indicates the request is complete
   (CCB_KILL or CCB_INFO may be used any time as necessary)
 - perform a CCB_DEQUEUE
 - call munmap() for completion area
 - close the dax device


Memory Constraints
==================

The DAX hardware operates only on physical addresses. Therefore, it is
not aware of virtual memory mappings and the discontiguities that may
exist in the physical memory that a virtual buffer maps to. There is
no I/O TLB or any scatter/gather mechanism. All buffers, whether input
or output, must reside in a physically contiguous region of memory.

The Hypervisor translates all addresses within a CCB to physical
before handing off the CCB to DAX. The Hypervisor determines the
virtual page size for each virtual address given, and uses this to
program a size limit for each address. This prevents the coprocessor
from reading or writing beyond the bound of the virtual page, even
though it is accessing physical memory directly. A simpler way of
saying this is that a DAX operation will never "cross" a virtual page
boundary. If an 8k virtual page is used, then the data is strictly
limited to 8k. If a user's buffer is larger than 8k, then a larger
page size must be used, or the transaction size will be truncated to
8k.

Huge pages. A user may allocate huge pages using standard interfaces.
Memory buffers residing on huge pages may be used to achieve much
larger DAX transaction sizes, but the rules must still be followed,
and no transaction will cross a page boundary, even a huge page.  A
major caveat is that Linux on Sparc presents 8Mb as one of the huge
page sizes. Sparc does not actually provide a 8Mb hardware page size,
and this size is synthesized by pasting together two 4Mb pages. The
reasons for this are historical, and it creates an issue because only
half of this 8Mb page can actually be used for any given buffer in a
DAX request, and it must be either the first half or the second half;
it cannot be a 4Mb chunk in the middle, since that crosses a
(hardware) page boundary. Note that this entire issue may be hidden by
higher level libraries.


CCB Structure
-------------
A CCB is an array of 8 64-bit words. Several of these words provide
command opcodes, parameters, flags, etc., and the rest are addresses
for the completion area, output buffer, and various inputs::

   struct ccb {
       u64   control;
       u64   completion;
       u64   input0;
       u64   access;
       u64   input1;
       u64   op_data;
       u64   output;
       u64   table;
   };

See libdax/common/sys/dax1/dax1_ccb.h for a detailed description of
each of these fields, and see dax-hv-api.txt for a complete description
of the Hypervisor API available to the guest OS (ie, Linux kernel).

The first word (control) is examined by the driver for the following:
 - CCB version, which must be consistent with hardware version
 - Opcode, which must be one of the documented allowable commands
 - Address types, which must be set to "virtual" for all the addresses
   given by the user, thereby ensuring that the application can
   only access memory that it owns


Example Code
============

The DAX is accessible to both user and kernel code.  The kernel code
can make hypercalls directly while the user code must use wrappers
provided by the driver. The setup of the CCB is nearly identical for
both; the only difference is in preparation of the completion area. An
example of user code is given now, with kernel code afterwards.

In order to program using the driver API, the file
arch/sparc/include/uapi/asm/oradax.h must be included.

First, the proper device must be opened. For M7 it will be
/dev/oradax1 and for M8 it will be /dev/oradax2. The simplest
procedure is to attempt to open both, as only one will succeed::

	fd = open("/dev/oradax1", O_RDWR);
	if (fd < 0)
		fd = open("/dev/oradax2", O_RDWR);
	if (fd < 0)
	       /* No DAX found */

Next, the completion area must be mapped::

      completion_area = mmap(NULL, DAX_MMAP_LEN, PROT_READ, MAP_SHARED, fd, 0);

All input and output buffers must be fully contained in one hardware
page, since as explained above, the DAX is strictly constrained by
virtual page boundaries.  In addition, the output buffer must be
64-byte aligned and its size must be a multiple of 64 bytes because
the coprocessor writes in units of cache lines.

This example demonstrates the DAX Scan command, which takes as input a
vector and a match value, and produces a bitmap as the output. For
each input element that matches the value, the corresponding bit is
set in the output.

In this example, the input vector consists of a series of single bits,
and the match value is 0. So each 0 bit in the input will produce a 1
in the output, and vice versa, which produces an output bitmap which
is the input bitmap inverted.

For details of all the parameters and bits used in this CCB, please
refer to section 36.2.1.3 of the DAX Hypervisor API document, which
describes the Scan command in detail::

	ccb->control =       /* Table 36.1, CCB Header Format */
		  (2L << 48)     /* command = Scan Value */
		| (3L << 40)     /* output address type = primary virtual */
		| (3L << 34)     /* primary input address type = primary virtual */
		             /* Section 36.2.1, Query CCB Command Formats */
		| (1 << 28)     /* 36.2.1.1.1 primary input format = fixed width bit packed */
		| (0 << 23)     /* 36.2.1.1.2 primary input element size = 0 (1 bit) */
		| (8 << 10)     /* 36.2.1.1.6 output format = bit vector */
		| (0 <<  5)	/* 36.2.1.3 First scan criteria size = 0 (1 byte) */
		| (31 << 0);	/* 36.2.1.3 Disable second scan criteria */

	ccb->completion = 0;    /* Completion area address, to be filled in by driver */

	ccb->input0 = (unsigned long) input; /* primary input address */

	ccb->access =       /* Section 36.2.1.2, Data Access Control */
		  (2 << 24)    /* Primary input length format = bits */
		| (nbits - 1); /* number of bits in primary input stream, minus 1 */

	ccb->input1 = 0;       /* secondary input address, unused */

	ccb->op_data = 0;      /* scan criteria (value to be matched) */

	ccb->output = (unsigned long) output;	/* output address */

	ccb->table = 0;	       /* table address, unused */

The CCB submission is a write() or pwrite() system call to the
driver. If the call fails, then a read() must be used to retrieve the
status::

	if (pwrite(fd, ccb, 64, 0) != 64) {
		struct ccb_exec_result status;
		read(fd, &status, sizeof(status));
		/* bail out */
	}

After a successful submission of the CCB, the completion area may be
polled to determine when the DAX is finished. Detailed information on
the contents of the completion area can be found in section 36.2.2 of
the DAX HV API document::

	while (1) {
		/* Monitored Load */
		__asm__ __volatile__("lduba [%1] 0x84, %0\n"
				     : "=r" (status)
				     : "r"  (completion_area));

		if (status)	     /* 0 indicates command in progress */
			break;

		/* MWAIT */
		__asm__ __volatile__("wr %%g0, 1000, %%asr28\n" ::);    /* 1000 ns */
	}

A completion area status of 1 indicates successful completion of the
CCB and validity of the output bitmap, which may be used immediately.
All other non-zero values indicate error conditions which are
described in section 36.2.2::

	if (completion_area[0] != 1) {	/* section 36.2.2, 1 = command ran and succeeded */
		/* completion_area[0] contains the completion status */
		/* completion_area[1] contains an error code, see 36.2.2 */
	}

After the completion area has been processed, the driver must be
notified that it can release any resources associated with the
request. This is done via the dequeue operation::

	struct dax_command cmd;
	cmd.command = CCB_DEQUEUE;
	if (write(fd, &cmd, sizeof(cmd)) != sizeof(cmd)) {
		/* bail out */
	}

Finally, normal program cleanup should be done, i.e., unmapping
completion area, closing the dax device, freeing memory etc.

Kernel example
--------------

The only difference in using the DAX in kernel code is the treatment
of the completion area. Unlike user applications which mmap the
completion area allocated by the driver, kernel code must allocate its
own memory to use for the completion area, and this address and its
type must be given in the CCB::

	ccb->control |=      /* Table 36.1, CCB Header Format */
	        (3L << 32);     /* completion area address type = primary virtual */

	ccb->completion = (unsigned long) completion_area;   /* Completion area address */

The dax submit hypercall is made directly. The flags used in the
ccb_submit call are documented in the DAX HV API in section 36.3.1/

::

  #include <asm/hypervisor.h>

	hv_rv = sun4v_ccb_submit((unsigned long)ccb, 64,
				 HV_CCB_QUERY_CMD |
				 HV_CCB_ARG0_PRIVILEGED | HV_CCB_ARG0_TYPE_PRIMARY |
				 HV_CCB_VA_PRIVILEGED,
				 0, &bytes_accepted, &status_data);

	if (hv_rv != HV_EOK) {
		/* hv_rv is an error code, status_data contains */
		/* potential additional status, see 36.3.1.1 */
	}

After the submission, the completion area polling code is identical to
that in user land::

	while (1) {
		/* Monitored Load */
		__asm__ __volatile__("lduba [%1] 0x84, %0\n"
				     : "=r" (status)
				     : "r"  (completion_area));

		if (status)	     /* 0 indicates command in progress */
			break;

		/* MWAIT */
		__asm__ __volatile__("wr %%g0, 1000, %%asr28\n" ::);    /* 1000 ns */
	}

	if (completion_area[0] != 1) {	/* section 36.2.2, 1 = command ran and succeeded */
		/* completion_area[0] contains the completion status */
		/* completion_area[1] contains an error code, see 36.2.2 */
	}

The output bitmap is ready for consumption immediately after the
completion status indicates success.

Excer[t from UltraSPARC Virtual Machine Specification
=====================================================

 .. include:: dax-hv-api.txt
    :literal:
