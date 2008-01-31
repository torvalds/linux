/*
 * linux/drivers/ide/ide-tape.c		Version 1.19	Nov, 2003
 *
 * Copyright (C) 1995 - 1999 Gadi Oxman <gadio@netvision.net.il>
 *
 * $Header$
 *
 * This driver was constructed as a student project in the software laboratory
 * of the faculty of electrical engineering in the Technion - Israel's
 * Institute Of Technology, with the guide of Avner Lottem and Dr. Ilana David.
 *
 * It is hereby placed under the terms of the GNU general public license.
 * (See linux/COPYING).
 */
 
/*
 * IDE ATAPI streaming tape driver.
 *
 * This driver is a part of the Linux ide driver and works in co-operation
 * with linux/drivers/block/ide.c.
 *
 * The driver, in co-operation with ide.c, basically traverses the 
 * request-list for the block device interface. The character device
 * interface, on the other hand, creates new requests, adds them
 * to the request-list of the block device, and waits for their completion.
 *
 * Pipelined operation mode is now supported on both reads and writes.
 *
 * The block device major and minor numbers are determined from the
 * tape's relative position in the ide interfaces, as explained in ide.c.
 *
 * The character device interface consists of the following devices:
 *
 * ht0		major 37, minor 0	first  IDE tape, rewind on close.
 * ht1		major 37, minor 1	second IDE tape, rewind on close.
 * ...
 * nht0		major 37, minor 128	first  IDE tape, no rewind on close.
 * nht1		major 37, minor 129	second IDE tape, no rewind on close.
 * ...
 *
 * Run linux/scripts/MAKEDEV.ide to create the above entries.
 *
 * The general magnetic tape commands compatible interface, as defined by
 * include/linux/mtio.h, is accessible through the character device.
 *
 * General ide driver configuration options, such as the interrupt-unmask
 * flag, can be configured by issuing an ioctl to the block device interface,
 * as any other ide device.
 *
 * Our own ide-tape ioctl's can be issued to either the block device or
 * the character device interface.
 *
 * Maximal throughput with minimal bus load will usually be achieved in the
 * following scenario:
 *
 *	1.	ide-tape is operating in the pipelined operation mode.
 *	2.	No buffering is performed by the user backup program.
 *
 * Testing was done with a 2 GB CONNER CTMA 4000 IDE ATAPI Streaming Tape Drive.
 * 
 * Ver 0.1   Nov  1 95   Pre-working code :-)
 * Ver 0.2   Nov 23 95   A short backup (few megabytes) and restore procedure
 *                        was successful ! (Using tar cvf ... on the block
 *                        device interface).
 *                       A longer backup resulted in major swapping, bad
 *                        overall Linux performance and eventually failed as
 *                        we received non serial read-ahead requests from the
 *                        buffer cache.
 * Ver 0.3   Nov 28 95   Long backups are now possible, thanks to the
 *                        character device interface. Linux's responsiveness
 *                        and performance doesn't seem to be much affected
 *                        from the background backup procedure.
 *                       Some general mtio.h magnetic tape operations are
 *                        now supported by our character device. As a result,
 *                        popular tape utilities are starting to work with
 *                        ide tapes :-)
 *                       The following configurations were tested:
 *                       	1. An IDE ATAPI TAPE shares the same interface
 *                       	   and irq with an IDE ATAPI CDROM.
 *                        	2. An IDE ATAPI TAPE shares the same interface
 *                          	   and irq with a normal IDE disk.
 *                        Both configurations seemed to work just fine !
 *                        However, to be on the safe side, it is meanwhile
 *                        recommended to give the IDE TAPE its own interface
 *                        and irq.
 *                       The one thing which needs to be done here is to
 *                        add a "request postpone" feature to ide.c,
 *                        so that we won't have to wait for the tape to finish
 *                        performing a long media access (DSC) request (such
 *                        as a rewind) before we can access the other device
 *                        on the same interface. This effect doesn't disturb
 *                        normal operation most of the time because read/write
 *                        requests are relatively fast, and once we are
 *                        performing one tape r/w request, a lot of requests
 *                        from the other device can be queued and ide.c will
 *			  service all of them after this single tape request.
 * Ver 1.0   Dec 11 95   Integrated into Linux 1.3.46 development tree.
 *                       On each read / write request, we now ask the drive
 *                        if we can transfer a constant number of bytes
 *                        (a parameter of the drive) only to its buffers,
 *                        without causing actual media access. If we can't,
 *                        we just wait until we can by polling the DSC bit.
 *                        This ensures that while we are not transferring
 *                        more bytes than the constant referred to above, the
 *                        interrupt latency will not become too high and
 *                        we won't cause an interrupt timeout, as happened
 *                        occasionally in the previous version.
 *                       While polling for DSC, the current request is
 *                        postponed and ide.c is free to handle requests from
 *                        the other device. This is handled transparently to
 *                        ide.c. The hwgroup locking method which was used
 *                        in the previous version was removed.
 *                       Use of new general features which are provided by
 *                        ide.c for use with atapi devices.
 *                        (Programming done by Mark Lord)
 *                       Few potential bug fixes (Again, suggested by Mark)
 *                       Single character device data transfers are now
 *                        not limited in size, as they were before.
 *                       We are asking the tape about its recommended
 *                        transfer unit and send a larger data transfer
 *                        as several transfers of the above size.
 *                        For best results, use an integral number of this
 *                        basic unit (which is shown during driver
 *                        initialization). I will soon add an ioctl to get
 *                        this important parameter.
 *                       Our data transfer buffer is allocated on startup,
 *                        rather than before each data transfer. This should
 *                        ensure that we will indeed have a data buffer.
 * Ver 1.1   Dec 14 95   Fixed random problems which occurred when the tape
 *                        shared an interface with another device.
 *                        (poll_for_dsc was a complete mess).
 *                       Removed some old (non-active) code which had
 *                        to do with supporting buffer cache originated
 *                        requests.
 *                       The block device interface can now be opened, so
 *                        that general ide driver features like the unmask
 *                        interrupts flag can be selected with an ioctl.
 *                        This is the only use of the block device interface.
 *                       New fast pipelined operation mode (currently only on
 *                        writes). When using the pipelined mode, the
 *                        throughput can potentially reach the maximum
 *                        tape supported throughput, regardless of the
 *                        user backup program. On my tape drive, it sometimes
 *                        boosted performance by a factor of 2. Pipelined
 *                        mode is enabled by default, but since it has a few
 *                        downfalls as well, you may want to disable it.
 *                        A short explanation of the pipelined operation mode
 *                        is available below.
 * Ver 1.2   Jan  1 96   Eliminated pipelined mode race condition.
 *                       Added pipeline read mode. As a result, restores
 *                        are now as fast as backups.
 *                       Optimized shared interface behavior. The new behavior
 *                        typically results in better IDE bus efficiency and
 *                        higher tape throughput.
 *                       Pre-calculation of the expected read/write request
 *                        service time, based on the tape's parameters. In
 *                        the pipelined operation mode, this allows us to
 *                        adjust our polling frequency to a much lower value,
 *                        and thus to dramatically reduce our load on Linux,
 *                        without any decrease in performance.
 *                       Implemented additional mtio.h operations.
 *                       The recommended user block size is returned by
 *                        the MTIOCGET ioctl.
 *                       Additional minor changes.
 * Ver 1.3   Feb  9 96   Fixed pipelined read mode bug which prevented the
 *                        use of some block sizes during a restore procedure.
 *                       The character device interface will now present a
 *                        continuous view of the media - any mix of block sizes
 *                        during a backup/restore procedure is supported. The
 *                        driver will buffer the requests internally and
 *                        convert them to the tape's recommended transfer
 *                        unit, making performance almost independent of the
 *                        chosen user block size.
 *                       Some improvements in error recovery.
 *                       By cooperating with ide-dma.c, bus mastering DMA can
 *                        now sometimes be used with IDE tape drives as well.
 *                        Bus mastering DMA has the potential to dramatically
 *                        reduce the CPU's overhead when accessing the device,
 *                        and can be enabled by using hdparm -d1 on the tape's
 *                        block device interface. For more info, read the
 *                        comments in ide-dma.c.
 * Ver 1.4   Mar 13 96   Fixed serialize support.
 * Ver 1.5   Apr 12 96   Fixed shared interface operation, broken in 1.3.85.
 *                       Fixed pipelined read mode inefficiency.
 *                       Fixed nasty null dereferencing bug.
 * Ver 1.6   Aug 16 96   Fixed FPU usage in the driver.
 *                       Fixed end of media bug.
 * Ver 1.7   Sep 10 96   Minor changes for the CONNER CTT8000-A model.
 * Ver 1.8   Sep 26 96   Attempt to find a better balance between good
 *                        interactive response and high system throughput.
 * Ver 1.9   Nov  5 96   Automatically cross encountered filemarks rather
 *                        than requiring an explicit FSF command.
 *                       Abort pending requests at end of media.
 *                       MTTELL was sometimes returning incorrect results.
 *                       Return the real block size in the MTIOCGET ioctl.
 *                       Some error recovery bug fixes.
 * Ver 1.10  Nov  5 96   Major reorganization.
 *                       Reduced CPU overhead a bit by eliminating internal
 *                        bounce buffers.
 *                       Added module support.
 *                       Added multiple tape drives support.
 *                       Added partition support.
 *                       Rewrote DSC handling.
 *                       Some portability fixes.
 *                       Removed ide-tape.h.
 *                       Additional minor changes.
 * Ver 1.11  Dec  2 96   Bug fix in previous DSC timeout handling.
 *                       Use ide_stall_queue() for DSC overlap.
 *                       Use the maximum speed rather than the current speed
 *                        to compute the request service time.
 * Ver 1.12  Dec  7 97   Fix random memory overwriting and/or last block data
 *                        corruption, which could occur if the total number
 *                        of bytes written to the tape was not an integral
 *                        number of tape blocks.
 *                       Add support for INTERRUPT DRQ devices.
 * Ver 1.13  Jan  2 98   Add "speed == 0" work-around for HP COLORADO 5GB
 * Ver 1.14  Dec 30 98   Partial fixes for the Sony/AIWA tape drives.
 *                       Replace cli()/sti() with hwgroup spinlocks.
 * Ver 1.15  Mar 25 99   Fix SMP race condition by replacing hwgroup
 *                        spinlock with private per-tape spinlock.
 * Ver 1.16  Sep  1 99   Add OnStream tape support.
 *                       Abort read pipeline on EOD.
 *                       Wait for the tape to become ready in case it returns
 *                        "in the process of becoming ready" on open().
 *                       Fix zero padding of the last written block in
 *                        case the tape block size is larger than PAGE_SIZE.
 *                       Decrease the default disconnection time to tn.
 * Ver 1.16e Oct  3 99   Minor fixes.
 * Ver 1.16e1 Oct 13 99  Patches by Arnold Niessen,
 *                          niessen@iae.nl / arnold.niessen@philips.com
 *                   GO-1)  Undefined code in idetape_read_position
 *				according to Gadi's email
 *                   AJN-1) Minor fix asc == 11 should be asc == 0x11
 *                               in idetape_issue_packet_command (did effect
 *                               debugging output only)
 *                   AJN-2) Added more debugging output, and
 *                              added ide-tape: where missing. I would also
 *				like to add tape->name where possible
 *                   AJN-3) Added different debug_level's 
 *                              via /proc/ide/hdc/settings
 * 				"debug_level" determines amount of debugging output;
 * 				can be changed using /proc/ide/hdx/settings
 * 				0 : almost no debugging output
 * 				1 : 0+output errors only
 * 				2 : 1+output all sensekey/asc
 * 				3 : 2+follow all chrdev related procedures
 * 				4 : 3+follow all procedures
 * 				5 : 4+include pc_stack rq_stack info
 * 				6 : 5+USE_COUNT updates
 *                   AJN-4) Fixed timeout for retension in idetape_queue_pc_tail
 *				from 5 to 10 minutes
 *                   AJN-5) Changed maximum number of blocks to skip when
 *                              reading tapes with multiple consecutive write
 *                              errors from 100 to 1000 in idetape_get_logical_blk
 *                   Proposed changes to code:
 *                   1) output "logical_blk_num" via /proc
 *                   2) output "current_operation" via /proc
 *                   3) Either solve or document the fact that `mt rewind' is
 *                      required after reading from /dev/nhtx to be
 *			able to rmmod the idetape module;
 *			Also, sometimes an application finishes but the
 *			device remains `busy' for some time. Same cause ?
 *                   Proposed changes to release-notes:
 *		     4) write a simple `quickstart' section in the
 *                      release notes; I volunteer if you don't want to
 * 		     5) include a pointer to video4linux in the doc
 *                      to stimulate video applications
 *                   6) release notes lines 331 and 362: explain what happens
 *			if the application data rate is higher than 1100 KB/s; 
 *			similar approach to lower-than-500 kB/s ?
 *		     7) 6.6 Comparison; wouldn't it be better to allow different 
 *			strategies for read and write ?
 *			Wouldn't it be better to control the tape buffer
 *			contents instead of the bandwidth ?
 *		     8) line 536: replace will by would (if I understand
 *			this section correctly, a hypothetical and unwanted situation
 *			 is being described)
 * Ver 1.16f Dec 15 99   Change place of the secondary OnStream header frames.
 * Ver 1.17  Nov 2000 / Jan 2001  Marcel Mol, marcel@mesa.nl
 *			- Add idetape_onstream_mode_sense_tape_parameter_page
 *			  function to get tape capacity in frames: tape->capacity.
 *			- Add support for DI-50 drives( or any DI- drive).
 *			- 'workaround' for read error/blank block around block 3000.
 *			- Implement Early warning for end of media for Onstream.
 *			- Cosmetic code changes for readability.
 *			- Idetape_position_tape should not use SKIP bit during
 *			  Onstream read recovery.
 *			- Add capacity, logical_blk_num and first/last_frame_position
 *			  to /proc/ide/hd?/settings.
 *			- Module use count was gone in the Linux 2.4 driver.
 * Ver 1.17a Apr 2001 Willem Riede osst@riede.org
 * 			- Get drive's actual block size from mode sense block descriptor
 * 			- Limit size of pipeline
 * Ver 1.17b Oct 2002   Alan Stern <stern@rowland.harvard.edu>
 *			Changed IDETAPE_MIN_PIPELINE_STAGES to 1 and actually used
 *			 it in the code!
 *			Actually removed aborted stages in idetape_abort_pipeline
 *			 instead of just changing the command code.
 *			Made the transfer byte count for Request Sense equal to the
 *			 actual length of the data transfer.
 *			Changed handling of partial data transfers: they do not
 *			 cause DMA errors.
 *			Moved initiation of DMA transfers to the correct place.
 *			Removed reference to unallocated memory.
 *			Made __idetape_discard_read_pipeline return the number of
 *			 sectors skipped, not the number of stages.
 *			Replaced errant kfree() calls with __idetape_kfree_stage().
 *			Fixed off-by-one error in testing the pipeline length.
 *			Fixed handling of filemarks in the read pipeline.
 *			Small code optimization for MTBSF and MTBSFM ioctls.
 *			Don't try to unlock the door during device close if is
 *			 already unlocked!
 *			Cosmetic fixes to miscellaneous debugging output messages.
 *			Set the minimum /proc/ide/hd?/settings values for "pipeline",
 *			 "pipeline_min", and "pipeline_max" to 1.
 *
 * Here are some words from the first releases of hd.c, which are quoted
 * in ide.c and apply here as well:
 *
 * | Special care is recommended.  Have Fun!
 *
 */

/*
 * An overview of the pipelined operation mode.
 *
 * In the pipelined write mode, we will usually just add requests to our
 * pipeline and return immediately, before we even start to service them. The
 * user program will then have enough time to prepare the next request while
 * we are still busy servicing previous requests. In the pipelined read mode,
 * the situation is similar - we add read-ahead requests into the pipeline,
 * before the user even requested them.
 *
 * The pipeline can be viewed as a "safety net" which will be activated when
 * the system load is high and prevents the user backup program from keeping up
 * with the current tape speed. At this point, the pipeline will get
 * shorter and shorter but the tape will still be streaming at the same speed.
 * Assuming we have enough pipeline stages, the system load will hopefully
 * decrease before the pipeline is completely empty, and the backup program
 * will be able to "catch up" and refill the pipeline again.
 * 
 * When using the pipelined mode, it would be best to disable any type of
 * buffering done by the user program, as ide-tape already provides all the
 * benefits in the kernel, where it can be done in a more efficient way.
 * As we will usually not block the user program on a request, the most
 * efficient user code will then be a simple read-write-read-... cycle.
 * Any additional logic will usually just slow down the backup process.
 *
 * Using the pipelined mode, I get a constant over 400 KBps throughput,
 * which seems to be the maximum throughput supported by my tape.
 *
 * However, there are some downfalls:
 *
 *	1.	We use memory (for data buffers) in proportional to the number
 *		of pipeline stages (each stage is about 26 KB with my tape).
 *	2.	In the pipelined write mode, we cheat and postpone error codes
 *		to the user task. In read mode, the actual tape position
 *		will be a bit further than the last requested block.
 *
 * Concerning (1):
 *
 *	1.	We allocate stages dynamically only when we need them. When
 *		we don't need them, we don't consume additional memory. In
 *		case we can't allocate stages, we just manage without them
 *		(at the expense of decreased throughput) so when Linux is
 *		tight in memory, we will not pose additional difficulties.
 *
 *	2.	The maximum number of stages (which is, in fact, the maximum
 *		amount of memory) which we allocate is limited by the compile
 *		time parameter IDETAPE_MAX_PIPELINE_STAGES.
 *
 *	3.	The maximum number of stages is a controlled parameter - We
 *		don't start from the user defined maximum number of stages
 *		but from the lower IDETAPE_MIN_PIPELINE_STAGES (again, we
 *		will not even allocate this amount of stages if the user
 *		program can't handle the speed). We then implement a feedback
 *		loop which checks if the pipeline is empty, and if it is, we
 *		increase the maximum number of stages as necessary until we
 *		reach the optimum value which just manages to keep the tape
 *		busy with minimum allocated memory or until we reach
 *		IDETAPE_MAX_PIPELINE_STAGES.
 *
 * Concerning (2):
 *
 *	In pipelined write mode, ide-tape can not return accurate error codes
 *	to the user program since we usually just add the request to the
 *      pipeline without waiting for it to be serviced. In case an error
 *      occurs, I will report it on the next user request.
 *
 *	In the pipelined read mode, subsequent read requests or forward
 *	filemark spacing will perform correctly, as we preserve all blocks
 *	and filemarks which we encountered during our excess read-ahead.
 * 
 *	For accurate tape positioning and error reporting, disabling
 *	pipelined mode might be the best option.
 *
 * You can enable/disable/tune the pipelined operation mode by adjusting
 * the compile time parameters below.
 */

/*
 *	Possible improvements.
 *
 *	1.	Support for the ATAPI overlap protocol.
 *
 *		In order to maximize bus throughput, we currently use the DSC
 *		overlap method which enables ide.c to service requests from the
 *		other device while the tape is busy executing a command. The
 *		DSC overlap method involves polling the tape's status register
 *		for the DSC bit, and servicing the other device while the tape
 *		isn't ready.
 *
 *		In the current QIC development standard (December 1995),
 *		it is recommended that new tape drives will *in addition* 
 *		implement the ATAPI overlap protocol, which is used for the
 *		same purpose - efficient use of the IDE bus, but is interrupt
 *		driven and thus has much less CPU overhead.
 *
 *		ATAPI overlap is likely to be supported in most new ATAPI
 *		devices, including new ATAPI cdroms, and thus provides us
 *		a method by which we can achieve higher throughput when
 *		sharing a (fast) ATA-2 disk with any (slow) new ATAPI device.
 */

#define IDETAPE_VERSION "1.19"

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <linux/bitops.h>
#include <linux/mutex.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/unaligned.h>

/*
 * partition
 */
typedef struct os_partition_s {
	__u8	partition_num;
	__u8	par_desc_ver;
	__u16	wrt_pass_cntr;
	__u32	first_frame_addr;
	__u32	last_frame_addr;
	__u32	eod_frame_addr;
} os_partition_t;

/*
 * DAT entry
 */
typedef struct os_dat_entry_s {
	__u32	blk_sz;
	__u16	blk_cnt;
	__u8	flags;
	__u8	reserved;
} os_dat_entry_t;

/*
 * DAT
 */
#define OS_DAT_FLAGS_DATA	(0xc)
#define OS_DAT_FLAGS_MARK	(0x1)

typedef struct os_dat_s {
	__u8		dat_sz;
	__u8		reserved1;
	__u8		entry_cnt;
	__u8		reserved3;
	os_dat_entry_t	dat_list[16];
} os_dat_t;

#include <linux/mtio.h>

/**************************** Tunable parameters *****************************/


/*
 *	Pipelined mode parameters.
 *
 *	We try to use the minimum number of stages which is enough to
 *	keep the tape constantly streaming. To accomplish that, we implement
 *	a feedback loop around the maximum number of stages:
 *
 *	We start from MIN maximum stages (we will not even use MIN stages
 *      if we don't need them), increment it by RATE*(MAX-MIN)
 *	whenever we sense that the pipeline is empty, until we reach
 *	the optimum value or until we reach MAX.
 *
 *	Setting the following parameter to 0 is illegal: the pipelined mode
 *	cannot be disabled (calculate_speeds() divides by tape->max_stages.)
 */
#define IDETAPE_MIN_PIPELINE_STAGES	  1
#define IDETAPE_MAX_PIPELINE_STAGES	400
#define IDETAPE_INCREASE_STAGES_RATE	 20

/*
 *	The following are used to debug the driver:
 *
 *	Setting IDETAPE_DEBUG_INFO to 1 will report device capabilities.
 *	Setting IDETAPE_DEBUG_LOG to 1 will log driver flow control.
 *	Setting IDETAPE_DEBUG_BUGS to 1 will enable self-sanity checks in
 *	some places.
 *
 *	Setting them to 0 will restore normal operation mode:
 *
 *		1.	Disable logging normal successful operations.
 *		2.	Disable self-sanity checks.
 *		3.	Errors will still be logged, of course.
 *
 *	All the #if DEBUG code will be removed some day, when the driver
 *	is verified to be stable enough. This will make it much more
 *	esthetic.
 */
#define IDETAPE_DEBUG_INFO		0
#define IDETAPE_DEBUG_LOG		0
#define IDETAPE_DEBUG_BUGS		1

/*
 *	After each failed packet command we issue a request sense command
 *	and retry the packet command IDETAPE_MAX_PC_RETRIES times.
 *
 *	Setting IDETAPE_MAX_PC_RETRIES to 0 will disable retries.
 */
#define IDETAPE_MAX_PC_RETRIES		3

/*
 *	With each packet command, we allocate a buffer of
 *	IDETAPE_PC_BUFFER_SIZE bytes. This is used for several packet
 *	commands (Not for READ/WRITE commands).
 */
#define IDETAPE_PC_BUFFER_SIZE		256

/*
 *	In various places in the driver, we need to allocate storage
 *	for packet commands and requests, which will remain valid while
 *	we leave the driver to wait for an interrupt or a timeout event.
 */
#define IDETAPE_PC_STACK		(10 + IDETAPE_MAX_PC_RETRIES)

/*
 * Some drives (for example, Seagate STT3401A Travan) require a very long
 * timeout, because they don't return an interrupt or clear their busy bit
 * until after the command completes (even retension commands).
 */
#define IDETAPE_WAIT_CMD		(900*HZ)

/*
 *	The following parameter is used to select the point in the internal
 *	tape fifo in which we will start to refill the buffer. Decreasing
 *	the following parameter will improve the system's latency and
 *	interactive response, while using a high value might improve system
 *	throughput.
 */
#define IDETAPE_FIFO_THRESHOLD 		2

/*
 *	DSC polling parameters.
 *
 *	Polling for DSC (a single bit in the status register) is a very
 *	important function in ide-tape. There are two cases in which we
 *	poll for DSC:
 *
 *	1.	Before a read/write packet command, to ensure that we
 *		can transfer data from/to the tape's data buffers, without
 *		causing an actual media access. In case the tape is not
 *		ready yet, we take out our request from the device
 *		request queue, so that ide.c will service requests from
 *		the other device on the same interface meanwhile.
 *
 *	2.	After the successful initialization of a "media access
 *		packet command", which is a command which can take a long
 *		time to complete (it can be several seconds or even an hour).
 *
 *		Again, we postpone our request in the middle to free the bus
 *		for the other device. The polling frequency here should be
 *		lower than the read/write frequency since those media access
 *		commands are slow. We start from a "fast" frequency -
 *		IDETAPE_DSC_MA_FAST (one second), and if we don't receive DSC
 *		after IDETAPE_DSC_MA_THRESHOLD (5 minutes), we switch it to a
 *		lower frequency - IDETAPE_DSC_MA_SLOW (1 minute).
 *
 *	We also set a timeout for the timer, in case something goes wrong.
 *	The timeout should be longer then the maximum execution time of a
 *	tape operation.
 */
 
/*
 *	DSC timings.
 */
#define IDETAPE_DSC_RW_MIN		5*HZ/100	/* 50 msec */
#define IDETAPE_DSC_RW_MAX		40*HZ/100	/* 400 msec */
#define IDETAPE_DSC_RW_TIMEOUT		2*60*HZ		/* 2 minutes */
#define IDETAPE_DSC_MA_FAST		2*HZ		/* 2 seconds */
#define IDETAPE_DSC_MA_THRESHOLD	5*60*HZ		/* 5 minutes */
#define IDETAPE_DSC_MA_SLOW		30*HZ		/* 30 seconds */
#define IDETAPE_DSC_MA_TIMEOUT		2*60*60*HZ	/* 2 hours */

/*************************** End of tunable parameters ***********************/

/*
 *	Read/Write error simulation
 */
#define SIMULATE_ERRORS			0

/*
 *	For general magnetic tape device compatibility.
 */
typedef enum {
	idetape_direction_none,
	idetape_direction_read,
	idetape_direction_write
} idetape_chrdev_direction_t;

struct idetape_bh {
	u32 b_size;
	atomic_t b_count;
	struct idetape_bh *b_reqnext;
	char *b_data;
};

/*
 *	Our view of a packet command.
 */
typedef struct idetape_packet_command_s {
	u8 c[12];				/* Actual packet bytes */
	int retries;				/* On each retry, we increment retries */
	int error;				/* Error code */
	int request_transfer;			/* Bytes to transfer */
	int actually_transferred;		/* Bytes actually transferred */
	int buffer_size;			/* Size of our data buffer */
	struct idetape_bh *bh;
	char *b_data;
	int b_count;
	u8 *buffer;				/* Data buffer */
	u8 *current_position;			/* Pointer into the above buffer */
	ide_startstop_t (*callback) (ide_drive_t *);	/* Called when this packet command is completed */
	u8 pc_buffer[IDETAPE_PC_BUFFER_SIZE];	/* Temporary buffer */
	unsigned long flags;			/* Status/Action bit flags: long for set_bit */
} idetape_pc_t;

/*
 *	Packet command flag bits.
 */
/* Set when an error is considered normal - We won't retry */
#define	PC_ABORT			0
/* 1 When polling for DSC on a media access command */
#define PC_WAIT_FOR_DSC			1
/* 1 when we prefer to use DMA if possible */
#define PC_DMA_RECOMMENDED		2
/* 1 while DMA in progress */
#define	PC_DMA_IN_PROGRESS		3
/* 1 when encountered problem during DMA */
#define	PC_DMA_ERROR			4
/* Data direction */
#define	PC_WRITING			5

/*
 *	Capabilities and Mechanical Status Page
 */
typedef struct {
	unsigned	page_code	:6;	/* Page code - Should be 0x2a */
	__u8		reserved0_6	:1;
	__u8		ps		:1;	/* parameters saveable */
	__u8		page_length;		/* Page Length - Should be 0x12 */
	__u8		reserved2, reserved3;
	unsigned	ro		:1;	/* Read Only Mode */
	unsigned	reserved4_1234	:4;
	unsigned	sprev		:1;	/* Supports SPACE in the reverse direction */
	unsigned	reserved4_67	:2;
	unsigned	reserved5_012	:3;
	unsigned	efmt		:1;	/* Supports ERASE command initiated formatting */
	unsigned	reserved5_4	:1;
	unsigned	qfa		:1;	/* Supports the QFA two partition formats */
	unsigned	reserved5_67	:2;
	unsigned	lock		:1;	/* Supports locking the volume */
	unsigned	locked		:1;	/* The volume is locked */
	unsigned	prevent		:1;	/* The device defaults in the prevent state after power up */	
	unsigned	eject		:1;	/* The device can eject the volume */
	__u8		disconnect	:1;	/* The device can break request > ctl */	
	__u8		reserved6_5	:1;
	unsigned	ecc		:1;	/* Supports error correction */
	unsigned	cmprs		:1;	/* Supports data compression */
	unsigned	reserved7_0	:1;
	unsigned	blk512		:1;	/* Supports 512 bytes block size */
	unsigned	blk1024		:1;	/* Supports 1024 bytes block size */
	unsigned	reserved7_3_6	:4;
	unsigned	blk32768	:1;	/* slowb - the device restricts the byte count for PIO */
						/* transfers for slow buffer memory ??? */
						/* Also 32768 block size in some cases */
	__u16		max_speed;		/* Maximum speed supported in KBps */
	__u8		reserved10, reserved11;
	__u16		ctl;			/* Continuous Transfer Limit in blocks */
	__u16		speed;			/* Current Speed, in KBps */
	__u16		buffer_size;		/* Buffer Size, in 512 bytes */
	__u8		reserved18, reserved19;
} idetape_capabilities_page_t;

/*
 *	Block Size Page
 */
typedef struct {
	unsigned	page_code	:6;	/* Page code - Should be 0x30 */
	unsigned	reserved1_6	:1;
	unsigned	ps		:1;
	__u8		page_length;		/* Page Length - Should be 2 */
	__u8		reserved2;
	unsigned	play32		:1;
	unsigned	play32_5	:1;
	unsigned	reserved2_23	:2;
	unsigned	record32	:1;
	unsigned	record32_5	:1;
	unsigned	reserved2_6	:1;
	unsigned	one		:1;
} idetape_block_size_page_t;

/*
 *	A pipeline stage.
 */
typedef struct idetape_stage_s {
	struct request rq;			/* The corresponding request */
	struct idetape_bh *bh;			/* The data buffers */
	struct idetape_stage_s *next;		/* Pointer to the next stage */
} idetape_stage_t;

/*
 *	REQUEST SENSE packet command result - Data Format.
 */
typedef struct {
	unsigned	error_code	:7;	/* Current of deferred errors */
	unsigned	valid		:1;	/* The information field conforms to QIC-157C */
	__u8		reserved1	:8;	/* Segment Number - Reserved */
	unsigned	sense_key	:4;	/* Sense Key */
	unsigned	reserved2_4	:1;	/* Reserved */
	unsigned	ili		:1;	/* Incorrect Length Indicator */
	unsigned	eom		:1;	/* End Of Medium */
	unsigned	filemark 	:1;	/* Filemark */
	__u32		information __attribute__ ((packed));
	__u8		asl;			/* Additional sense length (n-7) */
	__u32		command_specific;	/* Additional command specific information */
	__u8		asc;			/* Additional Sense Code */
	__u8		ascq;			/* Additional Sense Code Qualifier */
	__u8		replaceable_unit_code;	/* Field Replaceable Unit Code */
	unsigned	sk_specific1 	:7;	/* Sense Key Specific */
	unsigned	sksv		:1;	/* Sense Key Specific information is valid */
	__u8		sk_specific2;		/* Sense Key Specific */
	__u8		sk_specific3;		/* Sense Key Specific */
	__u8		pad[2];			/* Padding to 20 bytes */
} idetape_request_sense_result_t;


/*
 *	Most of our global data which we need to save even as we leave the
 *	driver due to an interrupt or a timer event is stored in a variable
 *	of type idetape_tape_t, defined below.
 */
typedef struct ide_tape_obj {
	ide_drive_t	*drive;
	ide_driver_t	*driver;
	struct gendisk	*disk;
	struct kref	kref;

	/*
	 *	Since a typical character device operation requires more
	 *	than one packet command, we provide here enough memory
	 *	for the maximum of interconnected packet commands.
	 *	The packet commands are stored in the circular array pc_stack.
	 *	pc_stack_index points to the last used entry, and warps around
	 *	to the start when we get to the last array entry.
	 *
	 *	pc points to the current processed packet command.
	 *
	 *	failed_pc points to the last failed packet command, or contains
	 *	NULL if we do not need to retry any packet command. This is
	 *	required since an additional packet command is needed before the
	 *	retry, to get detailed information on what went wrong.
	 */
	/* Current packet command */
	idetape_pc_t *pc;
	/* Last failed packet command */
	idetape_pc_t *failed_pc;
	/* Packet command stack */
	idetape_pc_t pc_stack[IDETAPE_PC_STACK];
	/* Next free packet command storage space */
	int pc_stack_index;
	struct request rq_stack[IDETAPE_PC_STACK];
	/* We implement a circular array */
	int rq_stack_index;

	/*
	 *	DSC polling variables.
	 *
	 *	While polling for DSC we use postponed_rq to postpone the
	 *	current request so that ide.c will be able to service
	 *	pending requests on the other device. Note that at most
	 *	we will have only one DSC (usually data transfer) request
	 *	in the device request queue. Additional requests can be
	 *	queued in our internal pipeline, but they will be visible
	 *	to ide.c only one at a time.
	 */
	struct request *postponed_rq;
	/* The time in which we started polling for DSC */
	unsigned long dsc_polling_start;
	/* Timer used to poll for dsc */
	struct timer_list dsc_timer;
	/* Read/Write dsc polling frequency */
	unsigned long best_dsc_rw_frequency;
	/* The current polling frequency */
	unsigned long dsc_polling_frequency;
	/* Maximum waiting time */
	unsigned long dsc_timeout;

	/*
	 *	Read position information
	 */
	u8 partition;
	/* Current block */
	unsigned int first_frame_position;
	unsigned int last_frame_position;
	unsigned int blocks_in_buffer;

	/*
	 *	Last error information
	 */
	u8 sense_key, asc, ascq;

	/*
	 *	Character device operation
	 */
	unsigned int minor;
	/* device name */
	char name[4];
	/* Current character device data transfer direction */
	idetape_chrdev_direction_t chrdev_direction;

	/*
	 *	Device information
	 */
	/* Usually 512 or 1024 bytes */
	unsigned short tape_block_size;
	int user_bs_factor;
	/* Copy of the tape's Capabilities and Mechanical Page */
	idetape_capabilities_page_t capabilities;

	/*
	 *	Active data transfer request parameters.
	 *
	 *	At most, there is only one ide-tape originated data transfer
	 *	request in the device request queue. This allows ide.c to
	 *	easily service requests from the other device when we
	 *	postpone our active request. In the pipelined operation
	 *	mode, we use our internal pipeline structure to hold
	 *	more data requests.
	 *
	 *	The data buffer size is chosen based on the tape's
	 *	recommendation.
	 */
	/* Pointer to the request which is waiting in the device request queue */
	struct request *active_data_request;
	/* Data buffer size (chosen based on the tape's recommendation */
	int stage_size;
	idetape_stage_t *merge_stage;
	int merge_stage_size;
	struct idetape_bh *bh;
	char *b_data;
	int b_count;
	
	/*
	 *	Pipeline parameters.
	 *
	 *	To accomplish non-pipelined mode, we simply set the following
	 *	variables to zero (or NULL, where appropriate).
	 */
	/* Number of currently used stages */
	int nr_stages;
	/* Number of pending stages */
	int nr_pending_stages;
	/* We will not allocate more than this number of stages */
	int max_stages, min_pipeline, max_pipeline;
	/* The first stage which will be removed from the pipeline */
	idetape_stage_t *first_stage;
	/* The currently active stage */
	idetape_stage_t *active_stage;
	/* Will be serviced after the currently active request */
	idetape_stage_t *next_stage;
	/* New requests will be added to the pipeline here */
	idetape_stage_t *last_stage;
	/* Optional free stage which we can use */
	idetape_stage_t *cache_stage;
	int pages_per_stage;
	/* Wasted space in each stage */
	int excess_bh_size;

	/* Status/Action flags: long for set_bit */
	unsigned long flags;
	/* protects the ide-tape queue */
	spinlock_t spinlock;

	/*
	 * Measures average tape speed
	 */
	unsigned long avg_time;
	int avg_size;
	int avg_speed;

	/* last sense information */
	idetape_request_sense_result_t sense;

	char vendor_id[10];
	char product_id[18];
	char firmware_revision[6];
	int firmware_revision_num;

	/* the door is currently locked */
	int door_locked;
	/* the tape hardware is write protected */
	char drv_write_prot;
	/* the tape is write protected (hardware or opened as read-only) */
	char write_prot;

	/*
	 * Limit the number of times a request can
	 * be postponed, to avoid an infinite postpone
	 * deadlock.
	 */
	/* request postpone count limit */
	int postpone_cnt;

	/*
	 * Measures number of frames:
	 *
	 * 1. written/read to/from the driver pipeline (pipeline_head).
	 * 2. written/read to/from the tape buffers (idetape_bh).
	 * 3. written/read by the tape to/from the media (tape_head).
	 */
	int pipeline_head;
	int buffer_head;
	int tape_head;
	int last_tape_head;

	/*
	 * Speed control at the tape buffers input/output
	 */
	unsigned long insert_time;
	int insert_size;
	int insert_speed;
	int max_insert_speed;
	int measure_insert_time;

	/*
	 * Measure tape still time, in milliseconds
	 */
	unsigned long tape_still_time_begin;
	int tape_still_time;

	/*
	 * Speed regulation negative feedback loop
	 */
	int speed_control;
	int pipeline_head_speed;
	int controlled_pipeline_head_speed;
	int uncontrolled_pipeline_head_speed;
	int controlled_last_pipeline_head;
	int uncontrolled_last_pipeline_head;
	unsigned long uncontrolled_pipeline_head_time;
	unsigned long controlled_pipeline_head_time;
	int controlled_previous_pipeline_head;
	int uncontrolled_previous_pipeline_head;
	unsigned long controlled_previous_head_time;
	unsigned long uncontrolled_previous_head_time;
	int restart_speed_control_req;

        /*
         * Debug_level determines amount of debugging output;
         * can be changed using /proc/ide/hdx/settings
         * 0 : almost no debugging output
         * 1 : 0+output errors only
         * 2 : 1+output all sensekey/asc
         * 3 : 2+follow all chrdev related procedures
         * 4 : 3+follow all procedures
         * 5 : 4+include pc_stack rq_stack info
         * 6 : 5+USE_COUNT updates
         */
         int debug_level; 
} idetape_tape_t;

static DEFINE_MUTEX(idetape_ref_mutex);

static struct class *idetape_sysfs_class;

#define to_ide_tape(obj) container_of(obj, struct ide_tape_obj, kref)

#define ide_tape_g(disk) \
	container_of((disk)->private_data, struct ide_tape_obj, driver)

static struct ide_tape_obj *ide_tape_get(struct gendisk *disk)
{
	struct ide_tape_obj *tape = NULL;

	mutex_lock(&idetape_ref_mutex);
	tape = ide_tape_g(disk);
	if (tape)
		kref_get(&tape->kref);
	mutex_unlock(&idetape_ref_mutex);
	return tape;
}

static void ide_tape_release(struct kref *);

static void ide_tape_put(struct ide_tape_obj *tape)
{
	mutex_lock(&idetape_ref_mutex);
	kref_put(&tape->kref, ide_tape_release);
	mutex_unlock(&idetape_ref_mutex);
}

/*
 *	Tape door status
 */
#define DOOR_UNLOCKED			0
#define DOOR_LOCKED			1
#define DOOR_EXPLICITLY_LOCKED		2

/*
 *	Tape flag bits values.
 */
#define IDETAPE_IGNORE_DSC		0
#define IDETAPE_ADDRESS_VALID		1	/* 0 When the tape position is unknown */
#define IDETAPE_BUSY			2	/* Device already opened */
#define IDETAPE_PIPELINE_ERROR		3	/* Error detected in a pipeline stage */
#define IDETAPE_DETECT_BS		4	/* Attempt to auto-detect the current user block size */
#define IDETAPE_FILEMARK		5	/* Currently on a filemark */
#define IDETAPE_DRQ_INTERRUPT		6	/* DRQ interrupt device */
#define IDETAPE_READ_ERROR		7
#define IDETAPE_PIPELINE_ACTIVE		8	/* pipeline active */
/* 0 = no tape is loaded, so we don't rewind after ejecting */
#define IDETAPE_MEDIUM_PRESENT		9

/*
 *	Supported ATAPI tape drives packet commands
 */
#define IDETAPE_TEST_UNIT_READY_CMD	0x00
#define IDETAPE_REWIND_CMD		0x01
#define IDETAPE_REQUEST_SENSE_CMD	0x03
#define IDETAPE_READ_CMD		0x08
#define IDETAPE_WRITE_CMD		0x0a
#define IDETAPE_WRITE_FILEMARK_CMD	0x10
#define IDETAPE_SPACE_CMD		0x11
#define IDETAPE_INQUIRY_CMD		0x12
#define IDETAPE_ERASE_CMD		0x19
#define IDETAPE_MODE_SENSE_CMD		0x1a
#define IDETAPE_MODE_SELECT_CMD		0x15
#define IDETAPE_LOAD_UNLOAD_CMD		0x1b
#define IDETAPE_PREVENT_CMD		0x1e
#define IDETAPE_LOCATE_CMD		0x2b
#define IDETAPE_READ_POSITION_CMD	0x34
#define IDETAPE_READ_BUFFER_CMD		0x3c
#define IDETAPE_SET_SPEED_CMD		0xbb

/*
 *	Some defines for the READ BUFFER command
 */
#define IDETAPE_RETRIEVE_FAULTY_BLOCK	6

/*
 *	Some defines for the SPACE command
 */
#define IDETAPE_SPACE_OVER_FILEMARK	1
#define IDETAPE_SPACE_TO_EOD		3

/*
 *	Some defines for the LOAD UNLOAD command
 */
#define IDETAPE_LU_LOAD_MASK		1
#define IDETAPE_LU_RETENSION_MASK	2
#define IDETAPE_LU_EOT_MASK		4

/*
 *	Special requests for our block device strategy routine.
 *
 *	In order to service a character device command, we add special
 *	requests to the tail of our block device request queue and wait
 *	for their completion.
 */

enum {
	REQ_IDETAPE_PC1		= (1 << 0), /* packet command (first stage) */
	REQ_IDETAPE_PC2		= (1 << 1), /* packet command (second stage) */
	REQ_IDETAPE_READ	= (1 << 2),
	REQ_IDETAPE_WRITE	= (1 << 3),
	REQ_IDETAPE_READ_BUFFER	= (1 << 4),
};

/*
 *	Error codes which are returned in rq->errors to the higher part
 *	of the driver.
 */
#define	IDETAPE_ERROR_GENERAL		101
#define	IDETAPE_ERROR_FILEMARK		102
#define	IDETAPE_ERROR_EOD		103

/*
 *	The following is used to format the general configuration word of
 *	the ATAPI IDENTIFY DEVICE command.
 */
struct idetape_id_gcw {	
	unsigned packet_size		:2;	/* Packet Size */
	unsigned reserved234		:3;	/* Reserved */
	unsigned drq_type		:2;	/* Command packet DRQ type */
	unsigned removable		:1;	/* Removable media */
	unsigned device_type		:5;	/* Device type */
	unsigned reserved13		:1;	/* Reserved */
	unsigned protocol		:2;	/* Protocol type */
};

/*
 *	INQUIRY packet command - Data Format (From Table 6-8 of QIC-157C)
 */
typedef struct {
	unsigned	device_type	:5;	/* Peripheral Device Type */
	unsigned	reserved0_765	:3;	/* Peripheral Qualifier - Reserved */
	unsigned	reserved1_6t0	:7;	/* Reserved */
	unsigned	rmb		:1;	/* Removable Medium Bit */
	unsigned	ansi_version	:3;	/* ANSI Version */
	unsigned	ecma_version	:3;	/* ECMA Version */
	unsigned	iso_version	:2;	/* ISO Version */
	unsigned	response_format :4;	/* Response Data Format */
	unsigned	reserved3_45	:2;	/* Reserved */
	unsigned	reserved3_6	:1;	/* TrmIOP - Reserved */
	unsigned	reserved3_7	:1;	/* AENC - Reserved */
	__u8		additional_length;	/* Additional Length (total_length-4) */
	__u8		rsv5, rsv6, rsv7;	/* Reserved */
	__u8		vendor_id[8];		/* Vendor Identification */
	__u8		product_id[16];		/* Product Identification */
	__u8		revision_level[4];	/* Revision Level */
	__u8		vendor_specific[20];	/* Vendor Specific - Optional */
	__u8		reserved56t95[40];	/* Reserved - Optional */
						/* Additional information may be returned */
} idetape_inquiry_result_t;

/*
 *	READ POSITION packet command - Data Format (From Table 6-57)
 */
typedef struct {
	unsigned	reserved0_10	:2;	/* Reserved */
	unsigned	bpu		:1;	/* Block Position Unknown */	
	unsigned	reserved0_543	:3;	/* Reserved */
	unsigned	eop		:1;	/* End Of Partition */
	unsigned	bop		:1;	/* Beginning Of Partition */
	u8		partition;		/* Partition Number */
	u8		reserved2, reserved3;	/* Reserved */
	u32		first_block;		/* First Block Location */
	u32		last_block;		/* Last Block Location (Optional) */
	u8		reserved12;		/* Reserved */
	u8		blocks_in_buffer[3];	/* Blocks In Buffer - (Optional) */
	u32		bytes_in_buffer;	/* Bytes In Buffer (Optional) */
} idetape_read_position_result_t;

/*
 *	Follows structures which are related to the SELECT SENSE / MODE SENSE
 *	packet commands. Those packet commands are still not supported
 *	by ide-tape.
 */
#define IDETAPE_BLOCK_DESCRIPTOR	0
#define	IDETAPE_CAPABILITIES_PAGE	0x2a
#define IDETAPE_PARAMTR_PAGE		0x2b   /* Onstream DI-x0 only */
#define IDETAPE_BLOCK_SIZE_PAGE		0x30
#define IDETAPE_BUFFER_FILLING_PAGE	0x33

/*
 *	Mode Parameter Header for the MODE SENSE packet command
 */
typedef struct {
	__u8	mode_data_length;	/* Length of the following data transfer */
	__u8	medium_type;		/* Medium Type */
	__u8	dsp;			/* Device Specific Parameter */
	__u8	bdl;			/* Block Descriptor Length */
#if 0
	/* data transfer page */
	__u8	page_code	:6;
	__u8	reserved0_6	:1;
	__u8	ps		:1;	/* parameters saveable */
	__u8	page_length;		/* page Length == 0x02 */
	__u8	reserved2;
	__u8	read32k		:1;	/* 32k blk size (data only) */
	__u8	read32k5	:1;	/* 32.5k blk size (data&AUX) */
	__u8	reserved3_23	:2;
	__u8	write32k	:1;	/* 32k blk size (data only) */
	__u8	write32k5	:1;	/* 32.5k blk size (data&AUX) */
	__u8	reserved3_6	:1;
	__u8	streaming	:1;	/* streaming mode enable */
#endif
} idetape_mode_parameter_header_t;

/*
 *	Mode Parameter Block Descriptor the MODE SENSE packet command
 *
 *	Support for block descriptors is optional.
 */
typedef struct {
	__u8		density_code;		/* Medium density code */
	__u8		blocks[3];		/* Number of blocks */
	__u8		reserved4;		/* Reserved */
	__u8		length[3];		/* Block Length */
} idetape_parameter_block_descriptor_t;

/*
 *	The Data Compression Page, as returned by the MODE SENSE packet command.
 */
typedef struct {
	unsigned	page_code	:6;	/* Page Code - Should be 0xf */
	unsigned	reserved0	:1;	/* Reserved */
	unsigned	ps		:1;
	__u8		page_length;		/* Page Length - Should be 14 */
	unsigned	reserved2	:6;	/* Reserved */
	unsigned	dcc		:1;	/* Data Compression Capable */
	unsigned	dce		:1;	/* Data Compression Enable */
	unsigned	reserved3	:5;	/* Reserved */
	unsigned	red		:2;	/* Report Exception on Decompression */
	unsigned	dde		:1;	/* Data Decompression Enable */
	__u32		ca;			/* Compression Algorithm */
	__u32		da;			/* Decompression Algorithm */
	__u8		reserved[4];		/* Reserved */
} idetape_data_compression_page_t;

/*
 *	The Medium Partition Page, as returned by the MODE SENSE packet command.
 */
typedef struct {
	unsigned	page_code	:6;	/* Page Code - Should be 0x11 */
	unsigned	reserved1_6	:1;	/* Reserved */
	unsigned	ps		:1;
	__u8		page_length;		/* Page Length - Should be 6 */
	__u8		map;			/* Maximum Additional Partitions - Should be 0 */
	__u8		apd;			/* Additional Partitions Defined - Should be 0 */
	unsigned	reserved4_012	:3;	/* Reserved */
	unsigned	psum		:2;	/* Should be 0 */
	unsigned	idp		:1;	/* Should be 0 */
	unsigned	sdp		:1;	/* Should be 0 */
	unsigned	fdp		:1;	/* Fixed Data Partitions */
	__u8		mfr;			/* Medium Format Recognition */
	__u8		reserved[2];		/* Reserved */
} idetape_medium_partition_page_t;

/*
 *	Run time configurable parameters.
 */
typedef struct {
	int	dsc_rw_frequency;
	int	dsc_media_access_frequency;
	int	nr_stages;
} idetape_config_t;

/*
 *	The variables below are used for the character device interface.
 *	Additional state variables are defined in our ide_drive_t structure.
 */
static struct ide_tape_obj * idetape_devs[MAX_HWIFS * MAX_DRIVES];

#define ide_tape_f(file) ((file)->private_data)

static struct ide_tape_obj *ide_tape_chrdev_get(unsigned int i)
{
	struct ide_tape_obj *tape = NULL;

	mutex_lock(&idetape_ref_mutex);
	tape = idetape_devs[i];
	if (tape)
		kref_get(&tape->kref);
	mutex_unlock(&idetape_ref_mutex);
	return tape;
}

/*
 *      Function declarations
 *
 */
static int idetape_chrdev_release (struct inode *inode, struct file *filp);
static void idetape_write_release (ide_drive_t *drive, unsigned int minor);

/*
 * Too bad. The drive wants to send us data which we are not ready to accept.
 * Just throw it away.
 */
static void idetape_discard_data (ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		(void) HWIF(drive)->INB(IDE_DATA_REG);
}

static void idetape_input_buffers (ide_drive_t *drive, idetape_pc_t *pc, unsigned int bcount)
{
	struct idetape_bh *bh = pc->bh;
	int count;

	while (bcount) {
#if IDETAPE_DEBUG_BUGS
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in "
				"idetape_input_buffers\n");
			idetape_discard_data(drive, bcount);
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		count = min((unsigned int)(bh->b_size - atomic_read(&bh->b_count)), bcount);
		HWIF(drive)->atapi_input_bytes(drive, bh->b_data + atomic_read(&bh->b_count), count);
		bcount -= count;
		atomic_add(count, &bh->b_count);
		if (atomic_read(&bh->b_count) == bh->b_size) {
			bh = bh->b_reqnext;
			if (bh)
				atomic_set(&bh->b_count, 0);
		}
	}
	pc->bh = bh;
}

static void idetape_output_buffers (ide_drive_t *drive, idetape_pc_t *pc, unsigned int bcount)
{
	struct idetape_bh *bh = pc->bh;
	int count;

	while (bcount) {
#if IDETAPE_DEBUG_BUGS
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in "
				"idetape_output_buffers\n");
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		count = min((unsigned int)pc->b_count, (unsigned int)bcount);
		HWIF(drive)->atapi_output_bytes(drive, pc->b_data, count);
		bcount -= count;
		pc->b_data += count;
		pc->b_count -= count;
		if (!pc->b_count) {
			pc->bh = bh = bh->b_reqnext;
			if (bh) {
				pc->b_data = bh->b_data;
				pc->b_count = atomic_read(&bh->b_count);
			}
		}
	}
}

static void idetape_update_buffers (idetape_pc_t *pc)
{
	struct idetape_bh *bh = pc->bh;
	int count;
	unsigned int bcount = pc->actually_transferred;

	if (test_bit(PC_WRITING, &pc->flags))
		return;
	while (bcount) {
#if IDETAPE_DEBUG_BUGS
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in "
				"idetape_update_buffers\n");
			return;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		count = min((unsigned int)bh->b_size, (unsigned int)bcount);
		atomic_set(&bh->b_count, count);
		if (atomic_read(&bh->b_count) == bh->b_size)
			bh = bh->b_reqnext;
		bcount -= count;
	}
	pc->bh = bh;
}

/*
 *	idetape_next_pc_storage returns a pointer to a place in which we can
 *	safely store a packet command, even though we intend to leave the
 *	driver. A storage space for a maximum of IDETAPE_PC_STACK packet
 *	commands is allocated at initialization time.
 */
static idetape_pc_t *idetape_next_pc_storage (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 5)
		printk(KERN_INFO "ide-tape: pc_stack_index=%d\n",
			tape->pc_stack_index);
#endif /* IDETAPE_DEBUG_LOG */
	if (tape->pc_stack_index == IDETAPE_PC_STACK)
		tape->pc_stack_index=0;
	return (&tape->pc_stack[tape->pc_stack_index++]);
}

/*
 *	idetape_next_rq_storage is used along with idetape_next_pc_storage.
 *	Since we queue packet commands in the request queue, we need to
 *	allocate a request, along with the allocation of a packet command.
 */
 
/**************************************************************
 *                                                            *
 *  This should get fixed to use kmalloc(.., GFP_ATOMIC)      *
 *  followed later on by kfree().   -ml                       *
 *                                                            *
 **************************************************************/
 
static struct request *idetape_next_rq_storage (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 5)
		printk(KERN_INFO "ide-tape: rq_stack_index=%d\n",
			tape->rq_stack_index);
#endif /* IDETAPE_DEBUG_LOG */
	if (tape->rq_stack_index == IDETAPE_PC_STACK)
		tape->rq_stack_index=0;
	return (&tape->rq_stack[tape->rq_stack_index++]);
}

/*
 *	idetape_init_pc initializes a packet command.
 */
static void idetape_init_pc (idetape_pc_t *pc)
{
	memset(pc->c, 0, 12);
	pc->retries = 0;
	pc->flags = 0;
	pc->request_transfer = 0;
	pc->buffer = pc->pc_buffer;
	pc->buffer_size = IDETAPE_PC_BUFFER_SIZE;
	pc->bh = NULL;
	pc->b_data = NULL;
}

/*
 *	idetape_analyze_error is called on each failed packet command retry
 *	to analyze the request sense. We currently do not utilize this
 *	information.
 */
static void idetape_analyze_error (ide_drive_t *drive, idetape_request_sense_result_t *result)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->failed_pc;

	tape->sense     = *result;
	tape->sense_key = result->sense_key;
	tape->asc       = result->asc;
	tape->ascq      = result->ascq;
#if IDETAPE_DEBUG_LOG
	/*
	 *	Without debugging, we only log an error if we decided to
	 *	give up retrying.
	 */
	if (tape->debug_level >= 1)
		printk(KERN_INFO "ide-tape: pc = %x, sense key = %x, "
			"asc = %x, ascq = %x\n",
			pc->c[0], result->sense_key,
			result->asc, result->ascq);
#endif /* IDETAPE_DEBUG_LOG */

	/*
	 *	Correct pc->actually_transferred by asking the tape.
	 */
	if (test_bit(PC_DMA_ERROR, &pc->flags)) {
		pc->actually_transferred = pc->request_transfer - tape->tape_block_size * ntohl(get_unaligned(&result->information));
		idetape_update_buffers(pc);
	}

	/*
	 * If error was the result of a zero-length read or write command,
	 * with sense key=5, asc=0x22, ascq=0, let it slide.  Some drives
	 * (i.e. Seagate STT3401A Travan) don't support 0-length read/writes.
	 */
	if ((pc->c[0] == IDETAPE_READ_CMD || pc->c[0] == IDETAPE_WRITE_CMD)
	    && pc->c[4] == 0 && pc->c[3] == 0 && pc->c[2] == 0) { /* length==0 */
		if (result->sense_key == 5) {
			/* don't report an error, everything's ok */
			pc->error = 0;
			/* don't retry read/write */
			set_bit(PC_ABORT, &pc->flags);
		}
	}
	if (pc->c[0] == IDETAPE_READ_CMD && result->filemark) {
		pc->error = IDETAPE_ERROR_FILEMARK;
		set_bit(PC_ABORT, &pc->flags);
	}
	if (pc->c[0] == IDETAPE_WRITE_CMD) {
		if (result->eom ||
		    (result->sense_key == 0xd && result->asc == 0x0 &&
		     result->ascq == 0x2)) {
			pc->error = IDETAPE_ERROR_EOD;
			set_bit(PC_ABORT, &pc->flags);
		}
	}
	if (pc->c[0] == IDETAPE_READ_CMD || pc->c[0] == IDETAPE_WRITE_CMD) {
		if (result->sense_key == 8) {
			pc->error = IDETAPE_ERROR_EOD;
			set_bit(PC_ABORT, &pc->flags);
		}
		if (!test_bit(PC_ABORT, &pc->flags) &&
		    pc->actually_transferred)
			pc->retries = IDETAPE_MAX_PC_RETRIES + 1;
	}
}

/*
 * idetape_active_next_stage will declare the next stage as "active".
 */
static void idetape_active_next_stage (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage = tape->next_stage;
	struct request *rq = &stage->rq;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_active_next_stage\n");
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (stage == NULL) {
		printk(KERN_ERR "ide-tape: bug: Trying to activate a non existing stage\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */	

	rq->rq_disk = tape->disk;
	rq->buffer = NULL;
	rq->special = (void *)stage->bh;
	tape->active_data_request = rq;
	tape->active_stage = stage;
	tape->next_stage = stage->next;
}

/*
 *	idetape_increase_max_pipeline_stages is a part of the feedback
 *	loop which tries to find the optimum number of stages. In the
 *	feedback loop, we are starting from a minimum maximum number of
 *	stages, and if we sense that the pipeline is empty, we try to
 *	increase it, until we reach the user compile time memory limit.
 */
static void idetape_increase_max_pipeline_stages (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	int increase = (tape->max_pipeline - tape->min_pipeline) / 10;
	
#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk (KERN_INFO "ide-tape: Reached idetape_increase_max_pipeline_stages\n");
#endif /* IDETAPE_DEBUG_LOG */

	tape->max_stages += max(increase, 1);
	tape->max_stages = max(tape->max_stages, tape->min_pipeline);
	tape->max_stages = min(tape->max_stages, tape->max_pipeline);
}

/*
 *	idetape_kfree_stage calls kfree to completely free a stage, along with
 *	its related buffers.
 */
static void __idetape_kfree_stage (idetape_stage_t *stage)
{
	struct idetape_bh *prev_bh, *bh = stage->bh;
	int size;

	while (bh != NULL) {
		if (bh->b_data != NULL) {
			size = (int) bh->b_size;
			while (size > 0) {
				free_page((unsigned long) bh->b_data);
				size -= PAGE_SIZE;
				bh->b_data += PAGE_SIZE;
			}
		}
		prev_bh = bh;
		bh = bh->b_reqnext;
		kfree(prev_bh);
	}
	kfree(stage);
}

static void idetape_kfree_stage (idetape_tape_t *tape, idetape_stage_t *stage)
{
	__idetape_kfree_stage(stage);
}

/*
 *	idetape_remove_stage_head removes tape->first_stage from the pipeline.
 *	The caller should avoid race conditions.
 */
static void idetape_remove_stage_head (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage;
	
#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_remove_stage_head\n");
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (tape->first_stage == NULL) {
		printk(KERN_ERR "ide-tape: bug: tape->first_stage is NULL\n");
		return;		
	}
	if (tape->active_stage == tape->first_stage) {
		printk(KERN_ERR "ide-tape: bug: Trying to free our active pipeline stage\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	stage = tape->first_stage;
	tape->first_stage = stage->next;
	idetape_kfree_stage(tape, stage);
	tape->nr_stages--;
	if (tape->first_stage == NULL) {
		tape->last_stage = NULL;
#if IDETAPE_DEBUG_BUGS
		if (tape->next_stage != NULL)
			printk(KERN_ERR "ide-tape: bug: tape->next_stage != NULL\n");
		if (tape->nr_stages)
			printk(KERN_ERR "ide-tape: bug: nr_stages should be 0 now\n");
#endif /* IDETAPE_DEBUG_BUGS */
	}
}

/*
 * This will free all the pipeline stages starting from new_last_stage->next
 * to the end of the list, and point tape->last_stage to new_last_stage.
 */
static void idetape_abort_pipeline(ide_drive_t *drive,
				   idetape_stage_t *new_last_stage)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage = new_last_stage->next;
	idetape_stage_t *nstage;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: %s: idetape_abort_pipeline called\n", tape->name);
#endif
	while (stage) {
		nstage = stage->next;
		idetape_kfree_stage(tape, stage);
		--tape->nr_stages;
		--tape->nr_pending_stages;
		stage = nstage;
	}
	if (new_last_stage)
		new_last_stage->next = NULL;
	tape->last_stage = new_last_stage;
	tape->next_stage = NULL;
}

/*
 *	idetape_end_request is used to finish servicing a request, and to
 *	insert a pending pipeline request into the main device queue.
 */
static int idetape_end_request(ide_drive_t *drive, int uptodate, int nr_sects)
{
	struct request *rq = HWGROUP(drive)->rq;
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;
	int error;
	int remove_stage = 0;
	idetape_stage_t *active_stage;

#if IDETAPE_DEBUG_LOG
        if (tape->debug_level >= 4)
	printk(KERN_INFO "ide-tape: Reached idetape_end_request\n");
#endif /* IDETAPE_DEBUG_LOG */

	switch (uptodate) {
		case 0:	error = IDETAPE_ERROR_GENERAL; break;
		case 1: error = 0; break;
		default: error = uptodate;
	}
	rq->errors = error;
	if (error)
		tape->failed_pc = NULL;

	if (!blk_special_request(rq)) {
		ide_end_request(drive, uptodate, nr_sects);
		return 0;
	}

	spin_lock_irqsave(&tape->spinlock, flags);

	/* The request was a pipelined data transfer request */
	if (tape->active_data_request == rq) {
		active_stage = tape->active_stage;
		tape->active_stage = NULL;
		tape->active_data_request = NULL;
		tape->nr_pending_stages--;
		if (rq->cmd[0] & REQ_IDETAPE_WRITE) {
			remove_stage = 1;
			if (error) {
				set_bit(IDETAPE_PIPELINE_ERROR, &tape->flags);
				if (error == IDETAPE_ERROR_EOD)
					idetape_abort_pipeline(drive, active_stage);
			}
		} else if (rq->cmd[0] & REQ_IDETAPE_READ) {
			if (error == IDETAPE_ERROR_EOD) {
				set_bit(IDETAPE_PIPELINE_ERROR, &tape->flags);
				idetape_abort_pipeline(drive, active_stage);
			}
		}
		if (tape->next_stage != NULL) {
			idetape_active_next_stage(drive);

			/*
			 * Insert the next request into the request queue.
			 */
			(void) ide_do_drive_cmd(drive, tape->active_data_request, ide_end);
		} else if (!error) {
				idetape_increase_max_pipeline_stages(drive);
		}
	}
	ide_end_drive_cmd(drive, 0, 0);
//	blkdev_dequeue_request(rq);
//	drive->rq = NULL;
//	end_that_request_last(rq);

	if (remove_stage)
		idetape_remove_stage_head(drive);
	if (tape->active_data_request == NULL)
		clear_bit(IDETAPE_PIPELINE_ACTIVE, &tape->flags);
	spin_unlock_irqrestore(&tape->spinlock, flags);
	return 0;
}

static ide_startstop_t idetape_request_sense_callback (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_request_sense_callback\n");
#endif /* IDETAPE_DEBUG_LOG */
	if (!tape->pc->error) {
		idetape_analyze_error(drive, (idetape_request_sense_result_t *) tape->pc->buffer);
		idetape_end_request(drive, 1, 0);
	} else {
		printk(KERN_ERR "ide-tape: Error in REQUEST SENSE itself - Aborting request!\n");
		idetape_end_request(drive, 0, 0);
	}
	return ide_stopped;
}

static void idetape_create_request_sense_cmd (idetape_pc_t *pc)
{
	idetape_init_pc(pc);	
	pc->c[0] = IDETAPE_REQUEST_SENSE_CMD;
	pc->c[4] = 20;
	pc->request_transfer = 20;
	pc->callback = &idetape_request_sense_callback;
}

static void idetape_init_rq(struct request *rq, u8 cmd)
{
	memset(rq, 0, sizeof(*rq));
	rq->cmd_type = REQ_TYPE_SPECIAL;
	rq->cmd[0] = cmd;
}

/*
 *	idetape_queue_pc_head generates a new packet command request in front
 *	of the request queue, before the current request, so that it will be
 *	processed immediately, on the next pass through the driver.
 *
 *	idetape_queue_pc_head is called from the request handling part of
 *	the driver (the "bottom" part). Safe storage for the request should
 *	be allocated with idetape_next_pc_storage and idetape_next_rq_storage
 *	before calling idetape_queue_pc_head.
 *
 *	Memory for those requests is pre-allocated at initialization time, and
 *	is limited to IDETAPE_PC_STACK requests. We assume that we have enough
 *	space for the maximum possible number of inter-dependent packet commands.
 *
 *	The higher level of the driver - The ioctl handler and the character
 *	device handling functions should queue request to the lower level part
 *	and wait for their completion using idetape_queue_pc_tail or
 *	idetape_queue_rw_tail.
 */
static void idetape_queue_pc_head (ide_drive_t *drive, idetape_pc_t *pc,struct request *rq)
{
	struct ide_tape_obj *tape = drive->driver_data;

	idetape_init_rq(rq, REQ_IDETAPE_PC1);
	rq->buffer = (char *) pc;
	rq->rq_disk = tape->disk;
	(void) ide_do_drive_cmd(drive, rq, ide_preempt);
}

/*
 *	idetape_retry_pc is called when an error was detected during the
 *	last packet command. We queue a request sense packet command in
 *	the head of the request list.
 */
static ide_startstop_t idetape_retry_pc (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc;
	struct request *rq;

	(void)drive->hwif->INB(IDE_ERROR_REG);
	pc = idetape_next_pc_storage(drive);
	rq = idetape_next_rq_storage(drive);
	idetape_create_request_sense_cmd(pc);
	set_bit(IDETAPE_IGNORE_DSC, &tape->flags);
	idetape_queue_pc_head(drive, pc, rq);
	return ide_stopped;
}

/*
 *	idetape_postpone_request postpones the current request so that
 *	ide.c will be able to service requests from another device on
 *	the same hwgroup while we are polling for DSC.
 */
static void idetape_postpone_request (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: idetape_postpone_request\n");
#endif
	tape->postponed_rq = HWGROUP(drive)->rq;
	ide_stall_queue(drive, tape->dsc_polling_frequency);
}

/*
 *	idetape_pc_intr is the usual interrupt handler which will be called
 *	during a packet command. We will transfer some of the data (as
 *	requested by the drive) and will re-point interrupt handler to us.
 *	When data transfer is finished, we will act according to the
 *	algorithm described before idetape_issue_packet_command.
 *
 */
static ide_startstop_t idetape_pc_intr (ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->pc;
	unsigned int temp;
#if SIMULATE_ERRORS
	static int error_sim_count = 0;
#endif
	u16 bcount;
	u8 stat, ireason;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_pc_intr "
				"interrupt handler\n");
#endif /* IDETAPE_DEBUG_LOG */	

	/* Clear the interrupt */
	stat = hwif->INB(IDE_STATUS_REG);

	if (test_bit(PC_DMA_IN_PROGRESS, &pc->flags)) {
		if (hwif->ide_dma_end(drive) || (stat & ERR_STAT)) {
			/*
			 * A DMA error is sometimes expected. For example,
			 * if the tape is crossing a filemark during a
			 * READ command, it will issue an irq and position
			 * itself before the filemark, so that only a partial
			 * data transfer will occur (which causes the DMA
			 * error). In that case, we will later ask the tape
			 * how much bytes of the original request were
			 * actually transferred (we can't receive that
			 * information from the DMA engine on most chipsets).
			 */

			/*
			 * On the contrary, a DMA error is never expected;
			 * it usually indicates a hardware error or abort.
			 * If the tape crosses a filemark during a READ
			 * command, it will issue an irq and position itself
			 * after the filemark (not before). Only a partial
			 * data transfer will occur, but no DMA error.
			 * (AS, 19 Apr 2001)
			 */
			set_bit(PC_DMA_ERROR, &pc->flags);
		} else {
			pc->actually_transferred = pc->request_transfer;
			idetape_update_buffers(pc);
		}
#if IDETAPE_DEBUG_LOG
		if (tape->debug_level >= 4)
			printk(KERN_INFO "ide-tape: DMA finished\n");
#endif /* IDETAPE_DEBUG_LOG */
	}

	/* No more interrupts */
	if ((stat & DRQ_STAT) == 0) {
#if IDETAPE_DEBUG_LOG
		if (tape->debug_level >= 2)
			printk(KERN_INFO "ide-tape: Packet command completed, %d bytes transferred\n", pc->actually_transferred);
#endif /* IDETAPE_DEBUG_LOG */
		clear_bit(PC_DMA_IN_PROGRESS, &pc->flags);

		local_irq_enable();

#if SIMULATE_ERRORS
		if ((pc->c[0] == IDETAPE_WRITE_CMD ||
		     pc->c[0] == IDETAPE_READ_CMD) &&
		    (++error_sim_count % 100) == 0) {
			printk(KERN_INFO "ide-tape: %s: simulating error\n",
				tape->name);
			stat |= ERR_STAT;
		}
#endif
		if ((stat & ERR_STAT) && pc->c[0] == IDETAPE_REQUEST_SENSE_CMD)
			stat &= ~ERR_STAT;
		if ((stat & ERR_STAT) || test_bit(PC_DMA_ERROR, &pc->flags)) {
			/* Error detected */
#if IDETAPE_DEBUG_LOG
			if (tape->debug_level >= 1)
				printk(KERN_INFO "ide-tape: %s: I/O error\n",
					tape->name);
#endif /* IDETAPE_DEBUG_LOG */
			if (pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
				printk(KERN_ERR "ide-tape: I/O error in request sense command\n");
				return ide_do_reset(drive);
			}
#if IDETAPE_DEBUG_LOG
			if (tape->debug_level >= 1)
				printk(KERN_INFO "ide-tape: [cmd %x]: check condition\n", pc->c[0]);
#endif
			/* Retry operation */
			return idetape_retry_pc(drive);
		}
		pc->error = 0;
		if (test_bit(PC_WAIT_FOR_DSC, &pc->flags) &&
		    (stat & SEEK_STAT) == 0) {
			/* Media access command */
			tape->dsc_polling_start = jiffies;
			tape->dsc_polling_frequency = IDETAPE_DSC_MA_FAST;
			tape->dsc_timeout = jiffies + IDETAPE_DSC_MA_TIMEOUT;
			/* Allow ide.c to handle other requests */
			idetape_postpone_request(drive);
			return ide_stopped;
		}
		if (tape->failed_pc == pc)
			tape->failed_pc = NULL;
		/* Command finished - Call the callback function */
		return pc->callback(drive);
	}
	if (test_and_clear_bit(PC_DMA_IN_PROGRESS, &pc->flags)) {
		printk(KERN_ERR "ide-tape: The tape wants to issue more "
				"interrupts in DMA mode\n");
		printk(KERN_ERR "ide-tape: DMA disabled, reverting to PIO\n");
		ide_dma_off(drive);
		return ide_do_reset(drive);
	}
	/* Get the number of bytes to transfer on this interrupt. */
	bcount = (hwif->INB(IDE_BCOUNTH_REG) << 8) |
		  hwif->INB(IDE_BCOUNTL_REG);

	ireason = hwif->INB(IDE_IREASON_REG);

	if (ireason & CD) {
		printk(KERN_ERR "ide-tape: CoD != 0 in idetape_pc_intr\n");
		return ide_do_reset(drive);
	}
	if (((ireason & IO) == IO) == test_bit(PC_WRITING, &pc->flags)) {
		/* Hopefully, we will never get here */
		printk(KERN_ERR "ide-tape: We wanted to %s, ",
				(ireason & IO) ? "Write" : "Read");
		printk(KERN_ERR "ide-tape: but the tape wants us to %s !\n",
				(ireason & IO) ? "Read" : "Write");
		return ide_do_reset(drive);
	}
	if (!test_bit(PC_WRITING, &pc->flags)) {
		/* Reading - Check that we have enough space */
		temp = pc->actually_transferred + bcount;
		if (temp > pc->request_transfer) {
			if (temp > pc->buffer_size) {
				printk(KERN_ERR "ide-tape: The tape wants to send us more data than expected - discarding data\n");
				idetape_discard_data(drive, bcount);
				ide_set_handler(drive, &idetape_pc_intr, IDETAPE_WAIT_CMD, NULL);
				return ide_started;
			}
#if IDETAPE_DEBUG_LOG
			if (tape->debug_level >= 2)
				printk(KERN_NOTICE "ide-tape: The tape wants to send us more data than expected - allowing transfer\n");
#endif /* IDETAPE_DEBUG_LOG */
		}
	}
	if (test_bit(PC_WRITING, &pc->flags)) {
		if (pc->bh != NULL)
			idetape_output_buffers(drive, pc, bcount);
		else
			/* Write the current buffer */
			hwif->atapi_output_bytes(drive, pc->current_position,
						 bcount);
	} else {
		if (pc->bh != NULL)
			idetape_input_buffers(drive, pc, bcount);
		else
			/* Read the current buffer */
			hwif->atapi_input_bytes(drive, pc->current_position,
						bcount);
	}
	/* Update the current position */
	pc->actually_transferred += bcount;
	pc->current_position += bcount;
#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 2)
		printk(KERN_INFO "ide-tape: [cmd %x] transferred %d bytes "
				 "on that interrupt\n", pc->c[0], bcount);
#endif
	/* And set the interrupt handler again */
	ide_set_handler(drive, &idetape_pc_intr, IDETAPE_WAIT_CMD, NULL);
	return ide_started;
}

/*
 *	Packet Command Interface
 *
 *	The current Packet Command is available in tape->pc, and will not
 *	change until we finish handling it. Each packet command is associated
 *	with a callback function that will be called when the command is
 *	finished.
 *
 *	The handling will be done in three stages:
 *
 *	1.	idetape_issue_packet_command will send the packet command to the
 *		drive, and will set the interrupt handler to idetape_pc_intr.
 *
 *	2.	On each interrupt, idetape_pc_intr will be called. This step
 *		will be repeated until the device signals us that no more
 *		interrupts will be issued.
 *
 *	3.	ATAPI Tape media access commands have immediate status with a
 *		delayed process. In case of a successful initiation of a
 *		media access packet command, the DSC bit will be set when the
 *		actual execution of the command is finished. 
 *		Since the tape drive will not issue an interrupt, we have to
 *		poll for this event. In this case, we define the request as
 *		"low priority request" by setting rq_status to
 *		IDETAPE_RQ_POSTPONED, 	set a timer to poll for DSC and exit
 *		the driver.
 *
 *		ide.c will then give higher priority to requests which
 *		originate from the other device, until will change rq_status
 *		to RQ_ACTIVE.
 *
 *	4.	When the packet command is finished, it will be checked for errors.
 *
 *	5.	In case an error was found, we queue a request sense packet
 *		command in front of the request queue and retry the operation
 *		up to IDETAPE_MAX_PC_RETRIES times.
 *
 *	6.	In case no error was found, or we decided to give up and not
 *		to retry again, the callback function will be called and then
 *		we will handle the next request.
 *
 */
static ide_startstop_t idetape_transfer_pc(ide_drive_t *drive)
{
	ide_hwif_t *hwif = drive->hwif;
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->pc;
	int retries = 100;
	ide_startstop_t startstop;
	u8 ireason;

	if (ide_wait_stat(&startstop,drive,DRQ_STAT,BUSY_STAT,WAIT_READY)) {
		printk(KERN_ERR "ide-tape: Strange, packet command initiated yet DRQ isn't asserted\n");
		return startstop;
	}
	ireason = hwif->INB(IDE_IREASON_REG);
	while (retries-- && ((ireason & CD) == 0 || (ireason & IO))) {
		printk(KERN_ERR "ide-tape: (IO,CoD != (0,1) while issuing "
				"a packet command, retrying\n");
		udelay(100);
		ireason = hwif->INB(IDE_IREASON_REG);
		if (retries == 0) {
			printk(KERN_ERR "ide-tape: (IO,CoD != (0,1) while "
					"issuing a packet command, ignoring\n");
			ireason |= CD;
			ireason &= ~IO;
		}
	}
	if ((ireason & CD) == 0 || (ireason & IO)) {
		printk(KERN_ERR "ide-tape: (IO,CoD) != (0,1) while issuing "
				"a packet command\n");
		return ide_do_reset(drive);
	}
	/* Set the interrupt routine */
	ide_set_handler(drive, &idetape_pc_intr, IDETAPE_WAIT_CMD, NULL);
#ifdef CONFIG_BLK_DEV_IDEDMA
	/* Begin DMA, if necessary */
	if (test_bit(PC_DMA_IN_PROGRESS, &pc->flags))
		hwif->dma_start(drive);
#endif
	/* Send the actual packet */
	HWIF(drive)->atapi_output_bytes(drive, pc->c, 12);
	return ide_started;
}

static ide_startstop_t idetape_issue_packet_command (ide_drive_t *drive, idetape_pc_t *pc)
{
	ide_hwif_t *hwif = drive->hwif;
	idetape_tape_t *tape = drive->driver_data;
	int dma_ok = 0;
	u16 bcount;

#if IDETAPE_DEBUG_BUGS
	if (tape->pc->c[0] == IDETAPE_REQUEST_SENSE_CMD &&
	    pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
		printk(KERN_ERR "ide-tape: possible ide-tape.c bug - "
			"Two request sense in serial were issued\n");
	}
#endif /* IDETAPE_DEBUG_BUGS */

	if (tape->failed_pc == NULL && pc->c[0] != IDETAPE_REQUEST_SENSE_CMD)
		tape->failed_pc = pc;
	/* Set the current packet command */
	tape->pc = pc;

	if (pc->retries > IDETAPE_MAX_PC_RETRIES ||
	    test_bit(PC_ABORT, &pc->flags)) {
		/*
		 *	We will "abort" retrying a packet command in case
		 *	a legitimate error code was received (crossing a
		 *	filemark, or end of the media, for example).
		 */
		if (!test_bit(PC_ABORT, &pc->flags)) {
			if (!(pc->c[0] == IDETAPE_TEST_UNIT_READY_CMD &&
			      tape->sense_key == 2 && tape->asc == 4 &&
			     (tape->ascq == 1 || tape->ascq == 8))) {
				printk(KERN_ERR "ide-tape: %s: I/O error, "
						"pc = %2x, key = %2x, "
						"asc = %2x, ascq = %2x\n",
						tape->name, pc->c[0],
						tape->sense_key, tape->asc,
						tape->ascq);
			}
			/* Giving up */
			pc->error = IDETAPE_ERROR_GENERAL;
		}
		tape->failed_pc = NULL;
		return pc->callback(drive);
	}
#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 2)
		printk(KERN_INFO "ide-tape: Retry number - %d, cmd = %02X\n", pc->retries, pc->c[0]);
#endif /* IDETAPE_DEBUG_LOG */

	pc->retries++;
	/* We haven't transferred any data yet */
	pc->actually_transferred = 0;
	pc->current_position = pc->buffer;
	/* Request to transfer the entire buffer at once */
	bcount = pc->request_transfer;

	if (test_and_clear_bit(PC_DMA_ERROR, &pc->flags)) {
		printk(KERN_WARNING "ide-tape: DMA disabled, "
				"reverting to PIO\n");
		ide_dma_off(drive);
	}
	if (test_bit(PC_DMA_RECOMMENDED, &pc->flags) && drive->using_dma)
		dma_ok = !hwif->dma_setup(drive);

	ide_pktcmd_tf_load(drive, IDE_TFLAG_NO_SELECT_MASK |
			   IDE_TFLAG_OUT_DEVICE, bcount, dma_ok);

	if (dma_ok)			/* Will begin DMA later */
		set_bit(PC_DMA_IN_PROGRESS, &pc->flags);
	if (test_bit(IDETAPE_DRQ_INTERRUPT, &tape->flags)) {
		ide_set_handler(drive, &idetape_transfer_pc, IDETAPE_WAIT_CMD, NULL);
		hwif->OUTB(WIN_PACKETCMD, IDE_COMMAND_REG);
		return ide_started;
	} else {
		hwif->OUTB(WIN_PACKETCMD, IDE_COMMAND_REG);
		return idetape_transfer_pc(drive);
	}
}

/*
 *	General packet command callback function.
 */
static ide_startstop_t idetape_pc_callback (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	
#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_pc_callback\n");
#endif /* IDETAPE_DEBUG_LOG */

	idetape_end_request(drive, tape->pc->error ? 0 : 1, 0);
	return ide_stopped;
}

/*
 *	A mode sense command is used to "sense" tape parameters.
 */
static void idetape_create_mode_sense_cmd (idetape_pc_t *pc, u8 page_code)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_MODE_SENSE_CMD;
	if (page_code != IDETAPE_BLOCK_DESCRIPTOR)
		pc->c[1] = 8;	/* DBD = 1 - Don't return block descriptors */
	pc->c[2] = page_code;
	/*
	 * Changed pc->c[3] to 0 (255 will at best return unused info).
	 *
	 * For SCSI this byte is defined as subpage instead of high byte
	 * of length and some IDE drives seem to interpret it this way
	 * and return an error when 255 is used.
	 */
	pc->c[3] = 0;
	pc->c[4] = 255;		/* (We will just discard data in that case) */
	if (page_code == IDETAPE_BLOCK_DESCRIPTOR)
		pc->request_transfer = 12;
	else if (page_code == IDETAPE_CAPABILITIES_PAGE)
		pc->request_transfer = 24;
	else
		pc->request_transfer = 50;
	pc->callback = &idetape_pc_callback;
}

static void calculate_speeds(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	int full = 125, empty = 75;

	if (time_after(jiffies, tape->controlled_pipeline_head_time + 120 * HZ)) {
		tape->controlled_previous_pipeline_head = tape->controlled_last_pipeline_head;
		tape->controlled_previous_head_time = tape->controlled_pipeline_head_time;
		tape->controlled_last_pipeline_head = tape->pipeline_head;
		tape->controlled_pipeline_head_time = jiffies;
	}
	if (time_after(jiffies, tape->controlled_pipeline_head_time + 60 * HZ))
		tape->controlled_pipeline_head_speed = (tape->pipeline_head - tape->controlled_last_pipeline_head) * 32 * HZ / (jiffies - tape->controlled_pipeline_head_time);
	else if (time_after(jiffies, tape->controlled_previous_head_time))
		tape->controlled_pipeline_head_speed = (tape->pipeline_head - tape->controlled_previous_pipeline_head) * 32 * HZ / (jiffies - tape->controlled_previous_head_time);

	if (tape->nr_pending_stages < tape->max_stages /*- 1 */) {
		/* -1 for read mode error recovery */
		if (time_after(jiffies, tape->uncontrolled_previous_head_time + 10 * HZ)) {
			tape->uncontrolled_pipeline_head_time = jiffies;
			tape->uncontrolled_pipeline_head_speed = (tape->pipeline_head - tape->uncontrolled_previous_pipeline_head) * 32 * HZ / (jiffies - tape->uncontrolled_previous_head_time);
		}
	} else {
		tape->uncontrolled_previous_head_time = jiffies;
		tape->uncontrolled_previous_pipeline_head = tape->pipeline_head;
		if (time_after(jiffies, tape->uncontrolled_pipeline_head_time + 30 * HZ)) {
			tape->uncontrolled_pipeline_head_time = jiffies;
		}
	}
	tape->pipeline_head_speed = max(tape->uncontrolled_pipeline_head_speed, tape->controlled_pipeline_head_speed);
	if (tape->speed_control == 0) {
		tape->max_insert_speed = 5000;
	} else if (tape->speed_control == 1) {
		if (tape->nr_pending_stages >= tape->max_stages / 2)
			tape->max_insert_speed = tape->pipeline_head_speed +
				(1100 - tape->pipeline_head_speed) * 2 * (tape->nr_pending_stages - tape->max_stages / 2) / tape->max_stages;
		else
			tape->max_insert_speed = 500 +
				(tape->pipeline_head_speed - 500) * 2 * tape->nr_pending_stages / tape->max_stages;
		if (tape->nr_pending_stages >= tape->max_stages * 99 / 100)
			tape->max_insert_speed = 5000;
	} else if (tape->speed_control == 2) {
		tape->max_insert_speed = tape->pipeline_head_speed * empty / 100 +
			(tape->pipeline_head_speed * full / 100 - tape->pipeline_head_speed * empty / 100) * tape->nr_pending_stages / tape->max_stages;
	} else
		tape->max_insert_speed = tape->speed_control;
	tape->max_insert_speed = max(tape->max_insert_speed, 500);
}

static ide_startstop_t idetape_media_access_finished (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = tape->pc;
	u8 stat;

	stat = drive->hwif->INB(IDE_STATUS_REG);
	if (stat & SEEK_STAT) {
		if (stat & ERR_STAT) {
			/* Error detected */
			if (pc->c[0] != IDETAPE_TEST_UNIT_READY_CMD)
				printk(KERN_ERR "ide-tape: %s: I/O error, ",
						tape->name);
			/* Retry operation */
			return idetape_retry_pc(drive);
		}
		pc->error = 0;
		if (tape->failed_pc == pc)
			tape->failed_pc = NULL;
	} else {
		pc->error = IDETAPE_ERROR_GENERAL;
		tape->failed_pc = NULL;
	}
	return pc->callback(drive);
}

static ide_startstop_t idetape_rw_callback (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	struct request *rq = HWGROUP(drive)->rq;
	int blocks = tape->pc->actually_transferred / tape->tape_block_size;

	tape->avg_size += blocks * tape->tape_block_size;
	tape->insert_size += blocks * tape->tape_block_size;
	if (tape->insert_size > 1024 * 1024)
		tape->measure_insert_time = 1;
	if (tape->measure_insert_time) {
		tape->measure_insert_time = 0;
		tape->insert_time = jiffies;
		tape->insert_size = 0;
	}
	if (time_after(jiffies, tape->insert_time))
		tape->insert_speed = tape->insert_size / 1024 * HZ / (jiffies - tape->insert_time);
	if (time_after_eq(jiffies, tape->avg_time + HZ)) {
		tape->avg_speed = tape->avg_size * HZ / (jiffies - tape->avg_time) / 1024;
		tape->avg_size = 0;
		tape->avg_time = jiffies;
	}

#if IDETAPE_DEBUG_LOG	
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_rw_callback\n");
#endif /* IDETAPE_DEBUG_LOG */

	tape->first_frame_position += blocks;
	rq->current_nr_sectors -= blocks;

	if (!tape->pc->error)
		idetape_end_request(drive, 1, 0);
	else
		idetape_end_request(drive, tape->pc->error, 0);
	return ide_stopped;
}

static void idetape_create_read_cmd(idetape_tape_t *tape, idetape_pc_t *pc, unsigned int length, struct idetape_bh *bh)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_READ_CMD;
	put_unaligned(htonl(length), (unsigned int *) &pc->c[1]);
	pc->c[1] = 1;
	pc->callback = &idetape_rw_callback;
	pc->bh = bh;
	atomic_set(&bh->b_count, 0);
	pc->buffer = NULL;
	pc->request_transfer = pc->buffer_size = length * tape->tape_block_size;
	if (pc->request_transfer == tape->stage_size)
		set_bit(PC_DMA_RECOMMENDED, &pc->flags);
}

static void idetape_create_read_buffer_cmd(idetape_tape_t *tape, idetape_pc_t *pc, unsigned int length, struct idetape_bh *bh)
{
	int size = 32768;
	struct idetape_bh *p = bh;

	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_READ_BUFFER_CMD;
	pc->c[1] = IDETAPE_RETRIEVE_FAULTY_BLOCK;
	pc->c[7] = size >> 8;
	pc->c[8] = size & 0xff;
	pc->callback = &idetape_pc_callback;
	pc->bh = bh;
	atomic_set(&bh->b_count, 0);
	pc->buffer = NULL;
	while (p) {
		atomic_set(&p->b_count, 0);
		p = p->b_reqnext;
	}
	pc->request_transfer = pc->buffer_size = size;
}

static void idetape_create_write_cmd(idetape_tape_t *tape, idetape_pc_t *pc, unsigned int length, struct idetape_bh *bh)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_WRITE_CMD;
	put_unaligned(htonl(length), (unsigned int *) &pc->c[1]);
	pc->c[1] = 1;
	pc->callback = &idetape_rw_callback;
	set_bit(PC_WRITING, &pc->flags);
	pc->bh = bh;
	pc->b_data = bh->b_data;
	pc->b_count = atomic_read(&bh->b_count);
	pc->buffer = NULL;
	pc->request_transfer = pc->buffer_size = length * tape->tape_block_size;
	if (pc->request_transfer == tape->stage_size)
		set_bit(PC_DMA_RECOMMENDED, &pc->flags);
}

/*
 * idetape_do_request is our request handling function.	
 */
static ide_startstop_t idetape_do_request(ide_drive_t *drive,
					  struct request *rq, sector_t block)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t *pc = NULL;
	struct request *postponed_rq = tape->postponed_rq;
	u8 stat;

#if IDETAPE_DEBUG_LOG
#if 0
	if (tape->debug_level >= 5)
		printk(KERN_INFO "ide-tape:  %d, "
			"dev: %s, cmd: %ld, errors: %d\n",
			 rq->rq_disk->disk_name, rq->cmd[0], rq->errors);
#endif
	if (tape->debug_level >= 2)
		printk(KERN_INFO "ide-tape: sector: %ld, "
			"nr_sectors: %ld, current_nr_sectors: %d\n",
			rq->sector, rq->nr_sectors, rq->current_nr_sectors);
#endif /* IDETAPE_DEBUG_LOG */

	if (!blk_special_request(rq)) {
		/*
		 * We do not support buffer cache originated requests.
		 */
		printk(KERN_NOTICE "ide-tape: %s: Unsupported request in "
			"request queue (%d)\n", drive->name, rq->cmd_type);
		ide_end_request(drive, 0, 0);
		return ide_stopped;
	}

	/*
	 *	Retry a failed packet command
	 */
	if (tape->failed_pc != NULL &&
	    tape->pc->c[0] == IDETAPE_REQUEST_SENSE_CMD) {
		return idetape_issue_packet_command(drive, tape->failed_pc);
	}
#if IDETAPE_DEBUG_BUGS
	if (postponed_rq != NULL)
		if (rq != postponed_rq) {
			printk(KERN_ERR "ide-tape: ide-tape.c bug - "
					"Two DSC requests were queued\n");
			idetape_end_request(drive, 0, 0);
			return ide_stopped;
		}
#endif /* IDETAPE_DEBUG_BUGS */

	tape->postponed_rq = NULL;

	/*
	 * If the tape is still busy, postpone our request and service
	 * the other device meanwhile.
	 */
	stat = drive->hwif->INB(IDE_STATUS_REG);

	if (!drive->dsc_overlap && !(rq->cmd[0] & REQ_IDETAPE_PC2))
		set_bit(IDETAPE_IGNORE_DSC, &tape->flags);

	if (drive->post_reset == 1) {
		set_bit(IDETAPE_IGNORE_DSC, &tape->flags);
		drive->post_reset = 0;
	}

	if (tape->tape_still_time > 100 && tape->tape_still_time < 200)
		tape->measure_insert_time = 1;
	if (time_after(jiffies, tape->insert_time))
		tape->insert_speed = tape->insert_size / 1024 * HZ / (jiffies - tape->insert_time);
	calculate_speeds(drive);
	if (!test_and_clear_bit(IDETAPE_IGNORE_DSC, &tape->flags) &&
	    (stat & SEEK_STAT) == 0) {
		if (postponed_rq == NULL) {
			tape->dsc_polling_start = jiffies;
			tape->dsc_polling_frequency = tape->best_dsc_rw_frequency;
			tape->dsc_timeout = jiffies + IDETAPE_DSC_RW_TIMEOUT;
		} else if (time_after(jiffies, tape->dsc_timeout)) {
			printk(KERN_ERR "ide-tape: %s: DSC timeout\n",
				tape->name);
			if (rq->cmd[0] & REQ_IDETAPE_PC2) {
				idetape_media_access_finished(drive);
				return ide_stopped;
			} else {
				return ide_do_reset(drive);
			}
		} else if (time_after(jiffies, tape->dsc_polling_start + IDETAPE_DSC_MA_THRESHOLD))
			tape->dsc_polling_frequency = IDETAPE_DSC_MA_SLOW;
		idetape_postpone_request(drive);
		return ide_stopped;
	}
	if (rq->cmd[0] & REQ_IDETAPE_READ) {
		tape->buffer_head++;
		tape->postpone_cnt = 0;
		pc = idetape_next_pc_storage(drive);
		idetape_create_read_cmd(tape, pc, rq->current_nr_sectors, (struct idetape_bh *)rq->special);
		goto out;
	}
	if (rq->cmd[0] & REQ_IDETAPE_WRITE) {
		tape->buffer_head++;
		tape->postpone_cnt = 0;
		pc = idetape_next_pc_storage(drive);
		idetape_create_write_cmd(tape, pc, rq->current_nr_sectors, (struct idetape_bh *)rq->special);
		goto out;
	}
	if (rq->cmd[0] & REQ_IDETAPE_READ_BUFFER) {
		tape->postpone_cnt = 0;
		pc = idetape_next_pc_storage(drive);
		idetape_create_read_buffer_cmd(tape, pc, rq->current_nr_sectors, (struct idetape_bh *)rq->special);
		goto out;
	}
	if (rq->cmd[0] & REQ_IDETAPE_PC1) {
		pc = (idetape_pc_t *) rq->buffer;
		rq->cmd[0] &= ~(REQ_IDETAPE_PC1);
		rq->cmd[0] |= REQ_IDETAPE_PC2;
		goto out;
	}
	if (rq->cmd[0] & REQ_IDETAPE_PC2) {
		idetape_media_access_finished(drive);
		return ide_stopped;
	}
	BUG();
out:
	return idetape_issue_packet_command(drive, pc);
}

/*
 *	Pipeline related functions
 */
static inline int idetape_pipeline_active (idetape_tape_t *tape)
{
	int rc1, rc2;

	rc1 = test_bit(IDETAPE_PIPELINE_ACTIVE, &tape->flags);
	rc2 = (tape->active_data_request != NULL);
	return rc1;
}

/*
 *	idetape_kmalloc_stage uses __get_free_page to allocate a pipeline
 *	stage, along with all the necessary small buffers which together make
 *	a buffer of size tape->stage_size (or a bit more). We attempt to
 *	combine sequential pages as much as possible.
 *
 *	Returns a pointer to the new allocated stage, or NULL if we
 *	can't (or don't want to) allocate a stage.
 *
 *	Pipeline stages are optional and are used to increase performance.
 *	If we can't allocate them, we'll manage without them.
 */
static idetape_stage_t *__idetape_kmalloc_stage (idetape_tape_t *tape, int full, int clear)
{
	idetape_stage_t *stage;
	struct idetape_bh *prev_bh, *bh;
	int pages = tape->pages_per_stage;
	char *b_data = NULL;

	if ((stage = kmalloc(sizeof (idetape_stage_t),GFP_KERNEL)) == NULL)
		return NULL;
	stage->next = NULL;

	bh = stage->bh = kmalloc(sizeof(struct idetape_bh), GFP_KERNEL);
	if (bh == NULL)
		goto abort;
	bh->b_reqnext = NULL;
	if ((bh->b_data = (char *) __get_free_page (GFP_KERNEL)) == NULL)
		goto abort;
	if (clear)
		memset(bh->b_data, 0, PAGE_SIZE);
	bh->b_size = PAGE_SIZE;
	atomic_set(&bh->b_count, full ? bh->b_size : 0);

	while (--pages) {
		if ((b_data = (char *) __get_free_page (GFP_KERNEL)) == NULL)
			goto abort;
		if (clear)
			memset(b_data, 0, PAGE_SIZE);
		if (bh->b_data == b_data + PAGE_SIZE) {
			bh->b_size += PAGE_SIZE;
			bh->b_data -= PAGE_SIZE;
			if (full)
				atomic_add(PAGE_SIZE, &bh->b_count);
			continue;
		}
		if (b_data == bh->b_data + bh->b_size) {
			bh->b_size += PAGE_SIZE;
			if (full)
				atomic_add(PAGE_SIZE, &bh->b_count);
			continue;
		}
		prev_bh = bh;
		if ((bh = kmalloc(sizeof(struct idetape_bh), GFP_KERNEL)) == NULL) {
			free_page((unsigned long) b_data);
			goto abort;
		}
		bh->b_reqnext = NULL;
		bh->b_data = b_data;
		bh->b_size = PAGE_SIZE;
		atomic_set(&bh->b_count, full ? bh->b_size : 0);
		prev_bh->b_reqnext = bh;
	}
	bh->b_size -= tape->excess_bh_size;
	if (full)
		atomic_sub(tape->excess_bh_size, &bh->b_count);
	return stage;
abort:
	__idetape_kfree_stage(stage);
	return NULL;
}

static idetape_stage_t *idetape_kmalloc_stage (idetape_tape_t *tape)
{
	idetape_stage_t *cache_stage = tape->cache_stage;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_kmalloc_stage\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->nr_stages >= tape->max_stages)
		return NULL;
	if (cache_stage != NULL) {
		tape->cache_stage = NULL;
		return cache_stage;
	}
	return __idetape_kmalloc_stage(tape, 0, 0);
}

static int idetape_copy_stage_from_user (idetape_tape_t *tape, idetape_stage_t *stage, const char __user *buf, int n)
{
	struct idetape_bh *bh = tape->bh;
	int count;
	int ret = 0;

	while (n) {
#if IDETAPE_DEBUG_BUGS
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in "
				"idetape_copy_stage_from_user\n");
			return 1;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		count = min((unsigned int)(bh->b_size - atomic_read(&bh->b_count)), (unsigned int)n);
		if (copy_from_user(bh->b_data + atomic_read(&bh->b_count), buf, count))
			ret = 1;
		n -= count;
		atomic_add(count, &bh->b_count);
		buf += count;
		if (atomic_read(&bh->b_count) == bh->b_size) {
			bh = bh->b_reqnext;
			if (bh)
				atomic_set(&bh->b_count, 0);
		}
	}
	tape->bh = bh;
	return ret;
}

static int idetape_copy_stage_to_user (idetape_tape_t *tape, char __user *buf, idetape_stage_t *stage, int n)
{
	struct idetape_bh *bh = tape->bh;
	int count;
	int ret = 0;

	while (n) {
#if IDETAPE_DEBUG_BUGS
		if (bh == NULL) {
			printk(KERN_ERR "ide-tape: bh == NULL in "
				"idetape_copy_stage_to_user\n");
			return 1;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		count = min(tape->b_count, n);
		if  (copy_to_user(buf, tape->b_data, count))
			ret = 1;
		n -= count;
		tape->b_data += count;
		tape->b_count -= count;
		buf += count;
		if (!tape->b_count) {
			tape->bh = bh = bh->b_reqnext;
			if (bh) {
				tape->b_data = bh->b_data;
				tape->b_count = atomic_read(&bh->b_count);
			}
		}
	}
	return ret;
}

static void idetape_init_merge_stage (idetape_tape_t *tape)
{
	struct idetape_bh *bh = tape->merge_stage->bh;
	
	tape->bh = bh;
	if (tape->chrdev_direction == idetape_direction_write)
		atomic_set(&bh->b_count, 0);
	else {
		tape->b_data = bh->b_data;
		tape->b_count = atomic_read(&bh->b_count);
	}
}

static void idetape_switch_buffers (idetape_tape_t *tape, idetape_stage_t *stage)
{
	struct idetape_bh *tmp;

	tmp = stage->bh;
	stage->bh = tape->merge_stage->bh;
	tape->merge_stage->bh = tmp;
	idetape_init_merge_stage(tape);
}

/*
 *	idetape_add_stage_tail adds a new stage at the end of the pipeline.
 */
static void idetape_add_stage_tail (ide_drive_t *drive,idetape_stage_t *stage)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;
	
#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk (KERN_INFO "ide-tape: Reached idetape_add_stage_tail\n");
#endif /* IDETAPE_DEBUG_LOG */
	spin_lock_irqsave(&tape->spinlock, flags);
	stage->next = NULL;
	if (tape->last_stage != NULL)
		tape->last_stage->next=stage;
	else
		tape->first_stage = tape->next_stage=stage;
	tape->last_stage = stage;
	if (tape->next_stage == NULL)
		tape->next_stage = tape->last_stage;
	tape->nr_stages++;
	tape->nr_pending_stages++;
	spin_unlock_irqrestore(&tape->spinlock, flags);
}

/*
 *	idetape_wait_for_request installs a completion in a pending request
 *	and sleeps until it is serviced.
 *
 *	The caller should ensure that the request will not be serviced
 *	before we install the completion (usually by disabling interrupts).
 */
static void idetape_wait_for_request (ide_drive_t *drive, struct request *rq)
{
	DECLARE_COMPLETION_ONSTACK(wait);
	idetape_tape_t *tape = drive->driver_data;

#if IDETAPE_DEBUG_BUGS
	if (rq == NULL || !blk_special_request(rq)) {
		printk (KERN_ERR "ide-tape: bug: Trying to sleep on non-valid request\n");
		return;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	rq->end_io_data = &wait;
	rq->end_io = blk_end_sync_rq;
	spin_unlock_irq(&tape->spinlock);
	wait_for_completion(&wait);
	/* The stage and its struct request have been deallocated */
	spin_lock_irq(&tape->spinlock);
}

static ide_startstop_t idetape_read_position_callback (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_read_position_result_t *result;
	
#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_read_position_callback\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (!tape->pc->error) {
		result = (idetape_read_position_result_t *) tape->pc->buffer;
#if IDETAPE_DEBUG_LOG
		if (tape->debug_level >= 2)
			printk(KERN_INFO "ide-tape: BOP - %s\n",result->bop ? "Yes":"No");
		if (tape->debug_level >= 2)
			printk(KERN_INFO "ide-tape: EOP - %s\n",result->eop ? "Yes":"No");
#endif /* IDETAPE_DEBUG_LOG */
		if (result->bpu) {
			printk(KERN_INFO "ide-tape: Block location is unknown to the tape\n");
			clear_bit(IDETAPE_ADDRESS_VALID, &tape->flags);
			idetape_end_request(drive, 0, 0);
		} else {
#if IDETAPE_DEBUG_LOG
			if (tape->debug_level >= 2)
				printk(KERN_INFO "ide-tape: Block Location - %u\n", ntohl(result->first_block));
#endif /* IDETAPE_DEBUG_LOG */
			tape->partition = result->partition;
			tape->first_frame_position = ntohl(result->first_block);
			tape->last_frame_position = ntohl(result->last_block);
			tape->blocks_in_buffer = result->blocks_in_buffer[2];
			set_bit(IDETAPE_ADDRESS_VALID, &tape->flags);
			idetape_end_request(drive, 1, 0);
		}
	} else {
		idetape_end_request(drive, 0, 0);
	}
	return ide_stopped;
}

/*
 *	idetape_create_write_filemark_cmd will:
 *
 *		1.	Write a filemark if write_filemark=1.
 *		2.	Flush the device buffers without writing a filemark
 *			if write_filemark=0.
 *
 */
static void idetape_create_write_filemark_cmd (ide_drive_t *drive, idetape_pc_t *pc,int write_filemark)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_WRITE_FILEMARK_CMD;
	pc->c[4] = write_filemark;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_test_unit_ready_cmd(idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_TEST_UNIT_READY_CMD;
	pc->callback = &idetape_pc_callback;
}

/*
 *	idetape_queue_pc_tail is based on the following functions:
 *
 *	ide_do_drive_cmd from ide.c
 *	cdrom_queue_request and cdrom_queue_packet_command from ide-cd.c
 *
 *	We add a special packet command request to the tail of the request
 *	queue, and wait for it to be serviced.
 *
 *	This is not to be called from within the request handling part
 *	of the driver ! We allocate here data in the stack, and it is valid
 *	until the request is finished. This is not the case for the bottom
 *	part of the driver, where we are always leaving the functions to wait
 *	for an interrupt or a timer event.
 *
 *	From the bottom part of the driver, we should allocate safe memory
 *	using idetape_next_pc_storage and idetape_next_rq_storage, and add
 *	the request to the request list without waiting for it to be serviced !
 *	In that case, we usually use idetape_queue_pc_head.
 */
static int __idetape_queue_pc_tail (ide_drive_t *drive, idetape_pc_t *pc)
{
	struct ide_tape_obj *tape = drive->driver_data;
	struct request rq;

	idetape_init_rq(&rq, REQ_IDETAPE_PC1);
	rq.buffer = (char *) pc;
	rq.rq_disk = tape->disk;
	return ide_do_drive_cmd(drive, &rq, ide_wait);
}

static void idetape_create_load_unload_cmd (ide_drive_t *drive, idetape_pc_t *pc,int cmd)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_LOAD_UNLOAD_CMD;
	pc->c[4] = cmd;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static int idetape_wait_ready(ide_drive_t *drive, unsigned long timeout)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	int load_attempted = 0;

	/*
	 * Wait for the tape to become ready
	 */
	set_bit(IDETAPE_MEDIUM_PRESENT, &tape->flags);
	timeout += jiffies;
	while (time_before(jiffies, timeout)) {
		idetape_create_test_unit_ready_cmd(&pc);
		if (!__idetape_queue_pc_tail(drive, &pc))
			return 0;
		if ((tape->sense_key == 2 && tape->asc == 4 && tape->ascq == 2)
		    || (tape->asc == 0x3A)) {	/* no media */
			if (load_attempted)
				return -ENOMEDIUM;
			idetape_create_load_unload_cmd(drive, &pc, IDETAPE_LU_LOAD_MASK);
			__idetape_queue_pc_tail(drive, &pc);
			load_attempted = 1;
		/* not about to be ready */
		} else if (!(tape->sense_key == 2 && tape->asc == 4 &&
			     (tape->ascq == 1 || tape->ascq == 8)))
			return -EIO;
		msleep(100);
	}
	return -EIO;
}

static int idetape_queue_pc_tail (ide_drive_t *drive,idetape_pc_t *pc)
{
	return __idetape_queue_pc_tail(drive, pc);
}

static int idetape_flush_tape_buffers (ide_drive_t *drive)
{
	idetape_pc_t pc;
	int rc;

	idetape_create_write_filemark_cmd(drive, &pc, 0);
	if ((rc = idetape_queue_pc_tail(drive, &pc)))
		return rc;
	idetape_wait_ready(drive, 60 * 5 * HZ);
	return 0;
}

static void idetape_create_read_position_cmd (idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_READ_POSITION_CMD;
	pc->request_transfer = 20;
	pc->callback = &idetape_read_position_callback;
}

static int idetape_read_position (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	int position;

#if IDETAPE_DEBUG_LOG
        if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_read_position\n");
#endif /* IDETAPE_DEBUG_LOG */

	idetape_create_read_position_cmd(&pc);
	if (idetape_queue_pc_tail(drive, &pc))
		return -1;
	position = tape->first_frame_position;
	return position;
}

static void idetape_create_locate_cmd (ide_drive_t *drive, idetape_pc_t *pc, unsigned int block, u8 partition, int skip)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_LOCATE_CMD;
	pc->c[1] = 2;
	put_unaligned(htonl(block), (unsigned int *) &pc->c[3]);
	pc->c[8] = partition;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static int idetape_create_prevent_cmd (ide_drive_t *drive, idetape_pc_t *pc, int prevent)
{
	idetape_tape_t *tape = drive->driver_data;

	if (!tape->capabilities.lock)
		return 0;

	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_PREVENT_CMD;
	pc->c[4] = prevent;
	pc->callback = &idetape_pc_callback;
	return 1;
}

static int __idetape_discard_read_pipeline (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;
	int cnt;

	if (tape->chrdev_direction != idetape_direction_read)
		return 0;

	/* Remove merge stage. */
	cnt = tape->merge_stage_size / tape->tape_block_size;
	if (test_and_clear_bit(IDETAPE_FILEMARK, &tape->flags))
		++cnt;		/* Filemarks count as 1 sector */
	tape->merge_stage_size = 0;
	if (tape->merge_stage != NULL) {
		__idetape_kfree_stage(tape->merge_stage);
		tape->merge_stage = NULL;
	}

	/* Clear pipeline flags. */
	clear_bit(IDETAPE_PIPELINE_ERROR, &tape->flags);
	tape->chrdev_direction = idetape_direction_none;

	/* Remove pipeline stages. */
	if (tape->first_stage == NULL)
		return 0;

	spin_lock_irqsave(&tape->spinlock, flags);
	tape->next_stage = NULL;
	if (idetape_pipeline_active(tape))
		idetape_wait_for_request(drive, tape->active_data_request);
	spin_unlock_irqrestore(&tape->spinlock, flags);

	while (tape->first_stage != NULL) {
		struct request *rq_ptr = &tape->first_stage->rq;

		cnt += rq_ptr->nr_sectors - rq_ptr->current_nr_sectors; 
		if (rq_ptr->errors == IDETAPE_ERROR_FILEMARK)
			++cnt;
		idetape_remove_stage_head(drive);
	}
	tape->nr_pending_stages = 0;
	tape->max_stages = tape->min_pipeline;
	return cnt;
}

/*
 *	idetape_position_tape positions the tape to the requested block
 *	using the LOCATE packet command. A READ POSITION command is then
 *	issued to check where we are positioned.
 *
 *	Like all higher level operations, we queue the commands at the tail
 *	of the request queue and wait for their completion.
 *	
 */
static int idetape_position_tape (ide_drive_t *drive, unsigned int block, u8 partition, int skip)
{
	idetape_tape_t *tape = drive->driver_data;
	int retval;
	idetape_pc_t pc;

	if (tape->chrdev_direction == idetape_direction_read)
		__idetape_discard_read_pipeline(drive);
	idetape_wait_ready(drive, 60 * 5 * HZ);
	idetape_create_locate_cmd(drive, &pc, block, partition, skip);
	retval = idetape_queue_pc_tail(drive, &pc);
	if (retval)
		return (retval);

	idetape_create_read_position_cmd(&pc);
	return (idetape_queue_pc_tail(drive, &pc));
}

static void idetape_discard_read_pipeline (ide_drive_t *drive, int restore_position)
{
	idetape_tape_t *tape = drive->driver_data;
	int cnt;
	int seek, position;

	cnt = __idetape_discard_read_pipeline(drive);
	if (restore_position) {
		position = idetape_read_position(drive);
		seek = position > cnt ? position - cnt : 0;
		if (idetape_position_tape(drive, seek, 0, 0)) {
			printk(KERN_INFO "ide-tape: %s: position_tape failed in discard_pipeline()\n", tape->name);
			return;
		}
	}
}

/*
 * idetape_queue_rw_tail generates a read/write request for the block
 * device interface and wait for it to be serviced.
 */
static int idetape_queue_rw_tail(ide_drive_t *drive, int cmd, int blocks, struct idetape_bh *bh)
{
	idetape_tape_t *tape = drive->driver_data;
	struct request rq;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 2)
		printk(KERN_INFO "ide-tape: idetape_queue_rw_tail: cmd=%d\n",cmd);
#endif /* IDETAPE_DEBUG_LOG */
#if IDETAPE_DEBUG_BUGS
	if (idetape_pipeline_active(tape)) {
		printk(KERN_ERR "ide-tape: bug: the pipeline is active in idetape_queue_rw_tail\n");
		return (0);
	}
#endif /* IDETAPE_DEBUG_BUGS */	

	idetape_init_rq(&rq, cmd);
	rq.rq_disk = tape->disk;
	rq.special = (void *)bh;
	rq.sector = tape->first_frame_position;
	rq.nr_sectors = rq.current_nr_sectors = blocks;
	(void) ide_do_drive_cmd(drive, &rq, ide_wait);

	if ((cmd & (REQ_IDETAPE_READ | REQ_IDETAPE_WRITE)) == 0)
		return 0;

	if (tape->merge_stage)
		idetape_init_merge_stage(tape);
	if (rq.errors == IDETAPE_ERROR_GENERAL)
		return -EIO;
	return (tape->tape_block_size * (blocks-rq.current_nr_sectors));
}

/*
 *	idetape_insert_pipeline_into_queue is used to start servicing the
 *	pipeline stages, starting from tape->next_stage.
 */
static void idetape_insert_pipeline_into_queue (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	if (tape->next_stage == NULL)
		return;
	if (!idetape_pipeline_active(tape)) {
		set_bit(IDETAPE_PIPELINE_ACTIVE, &tape->flags);
		idetape_active_next_stage(drive);
		(void) ide_do_drive_cmd(drive, tape->active_data_request, ide_end);
	}
}

static void idetape_create_inquiry_cmd (idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_INQUIRY_CMD;
	pc->c[4] = pc->request_transfer = 254;
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_rewind_cmd (ide_drive_t *drive, idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_REWIND_CMD;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

#if 0
static void idetape_create_mode_select_cmd (idetape_pc_t *pc, int length)
{
	idetape_init_pc(pc);
	set_bit(PC_WRITING, &pc->flags);
	pc->c[0] = IDETAPE_MODE_SELECT_CMD;
	pc->c[1] = 0x10;
	put_unaligned(htons(length), (unsigned short *) &pc->c[3]);
	pc->request_transfer = 255;
	pc->callback = &idetape_pc_callback;
}
#endif

static void idetape_create_erase_cmd (idetape_pc_t *pc)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_ERASE_CMD;
	pc->c[1] = 1;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_create_space_cmd (idetape_pc_t *pc,int count, u8 cmd)
{
	idetape_init_pc(pc);
	pc->c[0] = IDETAPE_SPACE_CMD;
	put_unaligned(htonl(count), (unsigned int *) &pc->c[1]);
	pc->c[1] = cmd;
	set_bit(PC_WAIT_FOR_DSC, &pc->flags);
	pc->callback = &idetape_pc_callback;
}

static void idetape_wait_first_stage (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;

	if (tape->first_stage == NULL)
		return;
	spin_lock_irqsave(&tape->spinlock, flags);
	if (tape->active_stage == tape->first_stage)
		idetape_wait_for_request(drive, tape->active_data_request);
	spin_unlock_irqrestore(&tape->spinlock, flags);
}

/*
 *	idetape_add_chrdev_write_request tries to add a character device
 *	originated write request to our pipeline. In case we don't succeed,
 *	we revert to non-pipelined operation mode for this request.
 *
 *	1.	Try to allocate a new pipeline stage.
 *	2.	If we can't, wait for more and more requests to be serviced
 *		and try again each time.
 *	3.	If we still can't allocate a stage, fallback to
 *		non-pipelined operation mode for this request.
 */
static int idetape_add_chrdev_write_request (ide_drive_t *drive, int blocks)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *new_stage;
	unsigned long flags;
	struct request *rq;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 3)
		printk(KERN_INFO "ide-tape: Reached idetape_add_chrdev_write_request\n");
#endif /* IDETAPE_DEBUG_LOG */

     	/*
     	 *	Attempt to allocate a new stage.
	 *	Pay special attention to possible race conditions.
	 */
	while ((new_stage = idetape_kmalloc_stage(tape)) == NULL) {
		spin_lock_irqsave(&tape->spinlock, flags);
		if (idetape_pipeline_active(tape)) {
			idetape_wait_for_request(drive, tape->active_data_request);
			spin_unlock_irqrestore(&tape->spinlock, flags);
		} else {
			spin_unlock_irqrestore(&tape->spinlock, flags);
			idetape_insert_pipeline_into_queue(drive);
			if (idetape_pipeline_active(tape))
				continue;
			/*
			 *	Linux is short on memory. Fallback to
			 *	non-pipelined operation mode for this request.
			 */
			return idetape_queue_rw_tail(drive, REQ_IDETAPE_WRITE, blocks, tape->merge_stage->bh);
		}
	}
	rq = &new_stage->rq;
	idetape_init_rq(rq, REQ_IDETAPE_WRITE);
	/* Doesn't actually matter - We always assume sequential access */
	rq->sector = tape->first_frame_position;
	rq->nr_sectors = rq->current_nr_sectors = blocks;

	idetape_switch_buffers(tape, new_stage);
	idetape_add_stage_tail(drive, new_stage);
	tape->pipeline_head++;
	calculate_speeds(drive);

	/*
	 *	Estimate whether the tape has stopped writing by checking
	 *	if our write pipeline is currently empty. If we are not
	 *	writing anymore, wait for the pipeline to be full enough
	 *	(90%) before starting to service requests, so that we will
	 *	be able to keep up with the higher speeds of the tape.
	 */
	if (!idetape_pipeline_active(tape)) {
		if (tape->nr_stages >= tape->max_stages * 9 / 10 ||
		    tape->nr_stages >= tape->max_stages - tape->uncontrolled_pipeline_head_speed * 3 * 1024 / tape->tape_block_size) {
			tape->measure_insert_time = 1;
			tape->insert_time = jiffies;
			tape->insert_size = 0;
			tape->insert_speed = 0;
			idetape_insert_pipeline_into_queue(drive);
		}
	}
	if (test_and_clear_bit(IDETAPE_PIPELINE_ERROR, &tape->flags))
		/* Return a deferred error */
		return -EIO;
	return blocks;
}

/*
 *	idetape_wait_for_pipeline will wait until all pending pipeline
 *	requests are serviced. Typically called on device close.
 */
static void idetape_wait_for_pipeline (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;

	while (tape->next_stage || idetape_pipeline_active(tape)) {
		idetape_insert_pipeline_into_queue(drive);
		spin_lock_irqsave(&tape->spinlock, flags);
		if (idetape_pipeline_active(tape))
			idetape_wait_for_request(drive, tape->active_data_request);
		spin_unlock_irqrestore(&tape->spinlock, flags);
	}
}

static void idetape_empty_write_pipeline (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	int blocks, min;
	struct idetape_bh *bh;
	
#if IDETAPE_DEBUG_BUGS
	if (tape->chrdev_direction != idetape_direction_write) {
		printk(KERN_ERR "ide-tape: bug: Trying to empty write pipeline, but we are not writing.\n");
		return;
	}
	if (tape->merge_stage_size > tape->stage_size) {
		printk(KERN_ERR "ide-tape: bug: merge_buffer too big\n");
		tape->merge_stage_size = tape->stage_size;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	if (tape->merge_stage_size) {
		blocks = tape->merge_stage_size / tape->tape_block_size;
		if (tape->merge_stage_size % tape->tape_block_size) {
			unsigned int i;

			blocks++;
			i = tape->tape_block_size - tape->merge_stage_size % tape->tape_block_size;
			bh = tape->bh->b_reqnext;
			while (bh) {
				atomic_set(&bh->b_count, 0);
				bh = bh->b_reqnext;
			}
			bh = tape->bh;
			while (i) {
				if (bh == NULL) {

					printk(KERN_INFO "ide-tape: bug, bh NULL\n");
					break;
				}
				min = min(i, (unsigned int)(bh->b_size - atomic_read(&bh->b_count)));
				memset(bh->b_data + atomic_read(&bh->b_count), 0, min);
				atomic_add(min, &bh->b_count);
				i -= min;
				bh = bh->b_reqnext;
			}
		}
		(void) idetape_add_chrdev_write_request(drive, blocks);
		tape->merge_stage_size = 0;
	}
	idetape_wait_for_pipeline(drive);
	if (tape->merge_stage != NULL) {
		__idetape_kfree_stage(tape->merge_stage);
		tape->merge_stage = NULL;
	}
	clear_bit(IDETAPE_PIPELINE_ERROR, &tape->flags);
	tape->chrdev_direction = idetape_direction_none;

	/*
	 *	On the next backup, perform the feedback loop again.
	 *	(I don't want to keep sense information between backups,
	 *	 as some systems are constantly on, and the system load
	 *	 can be totally different on the next backup).
	 */
	tape->max_stages = tape->min_pipeline;
#if IDETAPE_DEBUG_BUGS
	if (tape->first_stage != NULL ||
	    tape->next_stage != NULL ||
	    tape->last_stage != NULL ||
	    tape->nr_stages != 0) {
		printk(KERN_ERR "ide-tape: ide-tape pipeline bug, "
			"first_stage %p, next_stage %p, "
			"last_stage %p, nr_stages %d\n",
			tape->first_stage, tape->next_stage,
			tape->last_stage, tape->nr_stages);
	}
#endif /* IDETAPE_DEBUG_BUGS */
}

static void idetape_restart_speed_control (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	tape->restart_speed_control_req = 0;
	tape->pipeline_head = 0;
	tape->controlled_last_pipeline_head = tape->uncontrolled_last_pipeline_head = 0;
	tape->controlled_previous_pipeline_head = tape->uncontrolled_previous_pipeline_head = 0;
	tape->pipeline_head_speed = tape->controlled_pipeline_head_speed = 5000;
	tape->uncontrolled_pipeline_head_speed = 0;
	tape->controlled_pipeline_head_time = tape->uncontrolled_pipeline_head_time = jiffies;
	tape->controlled_previous_head_time = tape->uncontrolled_previous_head_time = jiffies;
}

static int idetape_initiate_read (ide_drive_t *drive, int max_stages)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *new_stage;
	struct request rq;
	int bytes_read;
	int blocks = tape->capabilities.ctl;

	/* Initialize read operation */
	if (tape->chrdev_direction != idetape_direction_read) {
		if (tape->chrdev_direction == idetape_direction_write) {
			idetape_empty_write_pipeline(drive);
			idetape_flush_tape_buffers(drive);
		}
#if IDETAPE_DEBUG_BUGS
		if (tape->merge_stage || tape->merge_stage_size) {
			printk (KERN_ERR "ide-tape: merge_stage_size should be 0 now\n");
			tape->merge_stage_size = 0;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		if ((tape->merge_stage = __idetape_kmalloc_stage(tape, 0, 0)) == NULL)
			return -ENOMEM;
		tape->chrdev_direction = idetape_direction_read;

		/*
		 *	Issue a read 0 command to ensure that DSC handshake
		 *	is switched from completion mode to buffer available
		 *	mode.
		 *	No point in issuing this if DSC overlap isn't supported,
		 *	some drives (Seagate STT3401A) will return an error.
		 */
		if (drive->dsc_overlap) {
			bytes_read = idetape_queue_rw_tail(drive, REQ_IDETAPE_READ, 0, tape->merge_stage->bh);
			if (bytes_read < 0) {
				__idetape_kfree_stage(tape->merge_stage);
				tape->merge_stage = NULL;
				tape->chrdev_direction = idetape_direction_none;
				return bytes_read;
			}
		}
	}
	if (tape->restart_speed_control_req)
		idetape_restart_speed_control(drive);
	idetape_init_rq(&rq, REQ_IDETAPE_READ);
	rq.sector = tape->first_frame_position;
	rq.nr_sectors = rq.current_nr_sectors = blocks;
	if (!test_bit(IDETAPE_PIPELINE_ERROR, &tape->flags) &&
	    tape->nr_stages < max_stages) {
		new_stage = idetape_kmalloc_stage(tape);
		while (new_stage != NULL) {
			new_stage->rq = rq;
			idetape_add_stage_tail(drive, new_stage);
			if (tape->nr_stages >= max_stages)
				break;
			new_stage = idetape_kmalloc_stage(tape);
		}
	}
	if (!idetape_pipeline_active(tape)) {
		if (tape->nr_pending_stages >= 3 * max_stages / 4) {
			tape->measure_insert_time = 1;
			tape->insert_time = jiffies;
			tape->insert_size = 0;
			tape->insert_speed = 0;
			idetape_insert_pipeline_into_queue(drive);
		}
	}
	return 0;
}

/*
 *	idetape_add_chrdev_read_request is called from idetape_chrdev_read
 *	to service a character device read request and add read-ahead
 *	requests to our pipeline.
 */
static int idetape_add_chrdev_read_request (ide_drive_t *drive,int blocks)
{
	idetape_tape_t *tape = drive->driver_data;
	unsigned long flags;
	struct request *rq_ptr;
	int bytes_read;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_add_chrdev_read_request, %d blocks\n", blocks);
#endif /* IDETAPE_DEBUG_LOG */

	/*
	 * If we are at a filemark, return a read length of 0
	 */
	if (test_bit(IDETAPE_FILEMARK, &tape->flags))
		return 0;

	/*
	 * Wait for the next block to be available at the head
	 * of the pipeline
	 */
	idetape_initiate_read(drive, tape->max_stages);
	if (tape->first_stage == NULL) {
		if (test_bit(IDETAPE_PIPELINE_ERROR, &tape->flags))
			return 0;
		return idetape_queue_rw_tail(drive, REQ_IDETAPE_READ, blocks, tape->merge_stage->bh);
	}
	idetape_wait_first_stage(drive);
	rq_ptr = &tape->first_stage->rq;
	bytes_read = tape->tape_block_size * (rq_ptr->nr_sectors - rq_ptr->current_nr_sectors);
	rq_ptr->nr_sectors = rq_ptr->current_nr_sectors = 0;


	if (rq_ptr->errors == IDETAPE_ERROR_EOD)
		return 0;
	else {
		idetape_switch_buffers(tape, tape->first_stage);
		if (rq_ptr->errors == IDETAPE_ERROR_FILEMARK)
			set_bit(IDETAPE_FILEMARK, &tape->flags);
		spin_lock_irqsave(&tape->spinlock, flags);
		idetape_remove_stage_head(drive);
		spin_unlock_irqrestore(&tape->spinlock, flags);
		tape->pipeline_head++;
		calculate_speeds(drive);
	}
#if IDETAPE_DEBUG_BUGS
	if (bytes_read > blocks * tape->tape_block_size) {
		printk(KERN_ERR "ide-tape: bug: trying to return more bytes than requested\n");
		bytes_read = blocks * tape->tape_block_size;
	}
#endif /* IDETAPE_DEBUG_BUGS */
	return (bytes_read);
}

static void idetape_pad_zeros (ide_drive_t *drive, int bcount)
{
	idetape_tape_t *tape = drive->driver_data;
	struct idetape_bh *bh;
	int blocks;
	
	while (bcount) {
		unsigned int count;

		bh = tape->merge_stage->bh;
		count = min(tape->stage_size, bcount);
		bcount -= count;
		blocks = count / tape->tape_block_size;
		while (count) {
			atomic_set(&bh->b_count, min(count, (unsigned int)bh->b_size));
			memset(bh->b_data, 0, atomic_read(&bh->b_count));
			count -= atomic_read(&bh->b_count);
			bh = bh->b_reqnext;
		}
		idetape_queue_rw_tail(drive, REQ_IDETAPE_WRITE, blocks, tape->merge_stage->bh);
	}
}

static int idetape_pipeline_size (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_stage_t *stage;
	struct request *rq;
	int size = 0;

	idetape_wait_for_pipeline(drive);
	stage = tape->first_stage;
	while (stage != NULL) {
		rq = &stage->rq;
		size += tape->tape_block_size * (rq->nr_sectors-rq->current_nr_sectors);
		if (rq->errors == IDETAPE_ERROR_FILEMARK)
			size += tape->tape_block_size;
		stage = stage->next;
	}
	size += tape->merge_stage_size;
	return size;
}

/*
 *	Rewinds the tape to the Beginning Of the current Partition (BOP).
 *
 *	We currently support only one partition.
 */ 
static int idetape_rewind_tape (ide_drive_t *drive)
{
	int retval;
	idetape_pc_t pc;
#if IDETAPE_DEBUG_LOG
	idetape_tape_t *tape = drive->driver_data;
	if (tape->debug_level >= 2)
		printk(KERN_INFO "ide-tape: Reached idetape_rewind_tape\n");
#endif /* IDETAPE_DEBUG_LOG */	
	
	idetape_create_rewind_cmd(drive, &pc);
	retval = idetape_queue_pc_tail(drive, &pc);
	if (retval)
		return retval;

	idetape_create_read_position_cmd(&pc);
	retval = idetape_queue_pc_tail(drive, &pc);
	if (retval)
		return retval;
	return 0;
}

/*
 *	Our special ide-tape ioctl's.
 *
 *	Currently there aren't any ioctl's.
 *	mtio.h compatible commands should be issued to the character device
 *	interface.
 */
static int idetape_blkdev_ioctl(ide_drive_t *drive, unsigned int cmd, unsigned long arg)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_config_t config;
	void __user *argp = (void __user *)arg;

#if IDETAPE_DEBUG_LOG	
	if (tape->debug_level >= 4)
		printk(KERN_INFO "ide-tape: Reached idetape_blkdev_ioctl\n");
#endif /* IDETAPE_DEBUG_LOG */
	switch (cmd) {
		case 0x0340:
			if (copy_from_user(&config, argp, sizeof (idetape_config_t)))
				return -EFAULT;
			tape->best_dsc_rw_frequency = config.dsc_rw_frequency;
			tape->max_stages = config.nr_stages;
			break;
		case 0x0350:
			config.dsc_rw_frequency = (int) tape->best_dsc_rw_frequency;
			config.nr_stages = tape->max_stages; 
			if (copy_to_user(argp, &config, sizeof (idetape_config_t)))
				return -EFAULT;
			break;
		default:
			return -EIO;
	}
	return 0;
}

/*
 *	idetape_space_over_filemarks is now a bit more complicated than just
 *	passing the command to the tape since we may have crossed some
 *	filemarks during our pipelined read-ahead mode.
 *
 *	As a minor side effect, the pipeline enables us to support MTFSFM when
 *	the filemark is in our internal pipeline even if the tape doesn't
 *	support spacing over filemarks in the reverse direction.
 */
static int idetape_space_over_filemarks (ide_drive_t *drive,short mt_op,int mt_count)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	unsigned long flags;
	int retval,count=0;

	if (mt_count == 0)
		return 0;
	if (MTBSF == mt_op || MTBSFM == mt_op) {
		if (!tape->capabilities.sprev)
			return -EIO;
		mt_count = - mt_count;
	}

	if (tape->chrdev_direction == idetape_direction_read) {
		/*
		 *	We have a read-ahead buffer. Scan it for crossed
		 *	filemarks.
		 */
		tape->merge_stage_size = 0;
		if (test_and_clear_bit(IDETAPE_FILEMARK, &tape->flags))
			++count;
		while (tape->first_stage != NULL) {
			if (count == mt_count) {
				if (mt_op == MTFSFM)
					set_bit(IDETAPE_FILEMARK, &tape->flags);
				return 0;
			}
			spin_lock_irqsave(&tape->spinlock, flags);
			if (tape->first_stage == tape->active_stage) {
				/*
				 *	We have reached the active stage in the read pipeline.
				 *	There is no point in allowing the drive to continue
				 *	reading any farther, so we stop the pipeline.
				 *
				 *	This section should be moved to a separate subroutine,
				 *	because a similar function is performed in
				 *	__idetape_discard_read_pipeline(), for example.
				 */
				tape->next_stage = NULL;
				spin_unlock_irqrestore(&tape->spinlock, flags);
				idetape_wait_first_stage(drive);
				tape->next_stage = tape->first_stage->next;
			} else
				spin_unlock_irqrestore(&tape->spinlock, flags);
			if (tape->first_stage->rq.errors == IDETAPE_ERROR_FILEMARK)
				++count;
			idetape_remove_stage_head(drive);
		}
		idetape_discard_read_pipeline(drive, 0);
	}

	/*
	 *	The filemark was not found in our internal pipeline.
	 *	Now we can issue the space command.
	 */
	switch (mt_op) {
		case MTFSF:
		case MTBSF:
			idetape_create_space_cmd(&pc,mt_count-count,IDETAPE_SPACE_OVER_FILEMARK);
			return (idetape_queue_pc_tail(drive, &pc));
		case MTFSFM:
		case MTBSFM:
			if (!tape->capabilities.sprev)
				return (-EIO);
			retval = idetape_space_over_filemarks(drive, MTFSF, mt_count-count);
			if (retval) return (retval);
			count = (MTBSFM == mt_op ? 1 : -1);
			return (idetape_space_over_filemarks(drive, MTFSF, count));
		default:
			printk(KERN_ERR "ide-tape: MTIO operation %d not supported\n",mt_op);
			return (-EIO);
	}
}


/*
 *	Our character device read / write functions.
 *
 *	The tape is optimized to maximize throughput when it is transferring
 *	an integral number of the "continuous transfer limit", which is
 *	a parameter of the specific tape (26 KB on my particular tape).
 *      (32 kB for Onstream)
 *
 *	As of version 1.3 of the driver, the character device provides an
 *	abstract continuous view of the media - any mix of block sizes (even 1
 *	byte) on the same backup/restore procedure is supported. The driver
 *	will internally convert the requests to the recommended transfer unit,
 *	so that an unmatch between the user's block size to the recommended
 *	size will only result in a (slightly) increased driver overhead, but
 *	will no longer hit performance.
 *      This is not applicable to Onstream.
 */
static ssize_t idetape_chrdev_read (struct file *file, char __user *buf,
				    size_t count, loff_t *ppos)
{
	struct ide_tape_obj *tape = ide_tape_f(file);
	ide_drive_t *drive = tape->drive;
	ssize_t bytes_read,temp, actually_read = 0, rc;
	ssize_t ret = 0;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 3)
		printk(KERN_INFO "ide-tape: Reached idetape_chrdev_read, count %Zd\n", count);
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->chrdev_direction != idetape_direction_read) {
		if (test_bit(IDETAPE_DETECT_BS, &tape->flags))
			if (count > tape->tape_block_size &&
			    (count % tape->tape_block_size) == 0)
				tape->user_bs_factor = count / tape->tape_block_size;
	}
	if ((rc = idetape_initiate_read(drive, tape->max_stages)) < 0)
		return rc;
	if (count == 0)
		return (0);
	if (tape->merge_stage_size) {
		actually_read = min((unsigned int)(tape->merge_stage_size), (unsigned int)count);
		if (idetape_copy_stage_to_user(tape, buf, tape->merge_stage, actually_read))
			ret = -EFAULT;
		buf += actually_read;
		tape->merge_stage_size -= actually_read;
		count -= actually_read;
	}
	while (count >= tape->stage_size) {
		bytes_read = idetape_add_chrdev_read_request(drive, tape->capabilities.ctl);
		if (bytes_read <= 0)
			goto finish;
		if (idetape_copy_stage_to_user(tape, buf, tape->merge_stage, bytes_read))
			ret = -EFAULT;
		buf += bytes_read;
		count -= bytes_read;
		actually_read += bytes_read;
	}
	if (count) {
		bytes_read = idetape_add_chrdev_read_request(drive, tape->capabilities.ctl);
		if (bytes_read <= 0)
			goto finish;
		temp = min((unsigned long)count, (unsigned long)bytes_read);
		if (idetape_copy_stage_to_user(tape, buf, tape->merge_stage, temp))
			ret = -EFAULT;
		actually_read += temp;
		tape->merge_stage_size = bytes_read-temp;
	}
finish:
	if (!actually_read && test_bit(IDETAPE_FILEMARK, &tape->flags)) {
#if IDETAPE_DEBUG_LOG
		if (tape->debug_level >= 2)
			printk(KERN_INFO "ide-tape: %s: spacing over filemark\n", tape->name);
#endif
		idetape_space_over_filemarks(drive, MTFSF, 1);
		return 0;
	}

	return (ret) ? ret : actually_read;
}

static ssize_t idetape_chrdev_write (struct file *file, const char __user *buf,
				     size_t count, loff_t *ppos)
{
	struct ide_tape_obj *tape = ide_tape_f(file);
	ide_drive_t *drive = tape->drive;
	ssize_t actually_written = 0;
	ssize_t ret = 0;

	/* The drive is write protected. */
	if (tape->write_prot)
		return -EACCES;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 3)
		printk(KERN_INFO "ide-tape: Reached idetape_chrdev_write, "
			"count %Zd\n", count);
#endif /* IDETAPE_DEBUG_LOG */

	/* Initialize write operation */
	if (tape->chrdev_direction != idetape_direction_write) {
		if (tape->chrdev_direction == idetape_direction_read)
			idetape_discard_read_pipeline(drive, 1);
#if IDETAPE_DEBUG_BUGS
		if (tape->merge_stage || tape->merge_stage_size) {
			printk(KERN_ERR "ide-tape: merge_stage_size "
				"should be 0 now\n");
			tape->merge_stage_size = 0;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		if ((tape->merge_stage = __idetape_kmalloc_stage(tape, 0, 0)) == NULL)
			return -ENOMEM;
		tape->chrdev_direction = idetape_direction_write;
		idetape_init_merge_stage(tape);

		/*
		 *	Issue a write 0 command to ensure that DSC handshake
		 *	is switched from completion mode to buffer available
		 *	mode.
		 *	No point in issuing this if DSC overlap isn't supported,
		 *	some drives (Seagate STT3401A) will return an error.
		 */
		if (drive->dsc_overlap) {
			ssize_t retval = idetape_queue_rw_tail(drive, REQ_IDETAPE_WRITE, 0, tape->merge_stage->bh);
			if (retval < 0) {
				__idetape_kfree_stage(tape->merge_stage);
				tape->merge_stage = NULL;
				tape->chrdev_direction = idetape_direction_none;
				return retval;
			}
		}
	}
	if (count == 0)
		return (0);
	if (tape->restart_speed_control_req)
		idetape_restart_speed_control(drive);
	if (tape->merge_stage_size) {
#if IDETAPE_DEBUG_BUGS
		if (tape->merge_stage_size >= tape->stage_size) {
			printk(KERN_ERR "ide-tape: bug: merge buffer too big\n");
			tape->merge_stage_size = 0;
		}
#endif /* IDETAPE_DEBUG_BUGS */
		actually_written = min((unsigned int)(tape->stage_size - tape->merge_stage_size), (unsigned int)count);
		if (idetape_copy_stage_from_user(tape, tape->merge_stage, buf, actually_written))
				ret = -EFAULT;
		buf += actually_written;
		tape->merge_stage_size += actually_written;
		count -= actually_written;

		if (tape->merge_stage_size == tape->stage_size) {
			ssize_t retval;
			tape->merge_stage_size = 0;
			retval = idetape_add_chrdev_write_request(drive, tape->capabilities.ctl);
			if (retval <= 0)
				return (retval);
		}
	}
	while (count >= tape->stage_size) {
		ssize_t retval;
		if (idetape_copy_stage_from_user(tape, tape->merge_stage, buf, tape->stage_size))
			ret = -EFAULT;
		buf += tape->stage_size;
		count -= tape->stage_size;
		retval = idetape_add_chrdev_write_request(drive, tape->capabilities.ctl);
		actually_written += tape->stage_size;
		if (retval <= 0)
			return (retval);
	}
	if (count) {
		actually_written += count;
		if (idetape_copy_stage_from_user(tape, tape->merge_stage, buf, count))
			ret = -EFAULT;
		tape->merge_stage_size += count;
	}
	return (ret) ? ret : actually_written;
}

static int idetape_write_filemark (ide_drive_t *drive)
{
	idetape_pc_t pc;

	/* Write a filemark */
	idetape_create_write_filemark_cmd(drive, &pc, 1);
	if (idetape_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-tape: Couldn't write a filemark\n");
		return -EIO;
	}
	return 0;
}

/*
 *	idetape_mtioctop is called from idetape_chrdev_ioctl when
 *	the general mtio MTIOCTOP ioctl is requested.
 *
 *	We currently support the following mtio.h operations:
 *
 *	MTFSF	-	Space over mt_count filemarks in the positive direction.
 *			The tape is positioned after the last spaced filemark.
 *
 *	MTFSFM	-	Same as MTFSF, but the tape is positioned before the
 *			last filemark.
 *
 *	MTBSF	-	Steps background over mt_count filemarks, tape is
 *			positioned before the last filemark.
 *
 *	MTBSFM	-	Like MTBSF, only tape is positioned after the last filemark.
 *
 *	Note:
 *
 *		MTBSF and MTBSFM are not supported when the tape doesn't
 *		support spacing over filemarks in the reverse direction.
 *		In this case, MTFSFM is also usually not supported (it is
 *		supported in the rare case in which we crossed the filemark
 *		during our read-ahead pipelined operation mode).
 *		
 *	MTWEOF	-	Writes mt_count filemarks. Tape is positioned after
 *			the last written filemark.
 *
 *	MTREW	-	Rewinds tape.
 *
 *	MTLOAD	-	Loads the tape.
 *
 *	MTOFFL	-	Puts the tape drive "Offline": Rewinds the tape and
 *	MTUNLOAD	prevents further access until the media is replaced.
 *
 *	MTNOP	-	Flushes tape buffers.
 *
 *	MTRETEN	-	Retension media. This typically consists of one end
 *			to end pass on the media.
 *
 *	MTEOM	-	Moves to the end of recorded data.
 *
 *	MTERASE	-	Erases tape.
 *
 *	MTSETBLK - 	Sets the user block size to mt_count bytes. If
 *			mt_count is 0, we will attempt to autodetect
 *			the block size.
 *
 *	MTSEEK	-	Positions the tape in a specific block number, where
 *			each block is assumed to contain which user_block_size
 *			bytes.
 *
 *	MTSETPART - 	Switches to another tape partition.
 *
 *	MTLOCK - 	Locks the tape door.
 *
 *	MTUNLOCK - 	Unlocks the tape door.
 *
 *	The following commands are currently not supported:
 *
 *	MTFSS, MTBSS, MTWSM, MTSETDENSITY,
 *	MTSETDRVBUFFER, MT_ST_BOOLEANS, MT_ST_WRITE_THRESHOLD.
 */
static int idetape_mtioctop (ide_drive_t *drive,short mt_op,int mt_count)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	int i,retval;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 1)
		printk(KERN_INFO "ide-tape: Handling MTIOCTOP ioctl: "
			"mt_op=%d, mt_count=%d\n", mt_op, mt_count);
#endif /* IDETAPE_DEBUG_LOG */
	/*
	 *	Commands which need our pipelined read-ahead stages.
	 */
	switch (mt_op) {
		case MTFSF:
		case MTFSFM:
		case MTBSF:
		case MTBSFM:
			if (!mt_count)
				return (0);
			return (idetape_space_over_filemarks(drive,mt_op,mt_count));
		default:
			break;
	}
	switch (mt_op) {
		case MTWEOF:
			if (tape->write_prot)
				return -EACCES;
			idetape_discard_read_pipeline(drive, 1);
			for (i = 0; i < mt_count; i++) {
				retval = idetape_write_filemark(drive);
				if (retval)
					return retval;
			}
			return (0);
		case MTREW:
			idetape_discard_read_pipeline(drive, 0);
			if (idetape_rewind_tape(drive))
				return -EIO;
			return 0;
		case MTLOAD:
			idetape_discard_read_pipeline(drive, 0);
			idetape_create_load_unload_cmd(drive, &pc, IDETAPE_LU_LOAD_MASK);
			return (idetape_queue_pc_tail(drive, &pc));
		case MTUNLOAD:
		case MTOFFL:
			/*
			 * If door is locked, attempt to unlock before
			 * attempting to eject.
			 */
			if (tape->door_locked) {
				if (idetape_create_prevent_cmd(drive, &pc, 0))
					if (!idetape_queue_pc_tail(drive, &pc))
						tape->door_locked = DOOR_UNLOCKED;
			}
			idetape_discard_read_pipeline(drive, 0);
			idetape_create_load_unload_cmd(drive, &pc,!IDETAPE_LU_LOAD_MASK);
			retval = idetape_queue_pc_tail(drive, &pc);
			if (!retval)
				clear_bit(IDETAPE_MEDIUM_PRESENT, &tape->flags);
			return retval;
		case MTNOP:
			idetape_discard_read_pipeline(drive, 0);
			return (idetape_flush_tape_buffers(drive));
		case MTRETEN:
			idetape_discard_read_pipeline(drive, 0);
			idetape_create_load_unload_cmd(drive, &pc,IDETAPE_LU_RETENSION_MASK | IDETAPE_LU_LOAD_MASK);
			return (idetape_queue_pc_tail(drive, &pc));
		case MTEOM:
			idetape_create_space_cmd(&pc, 0, IDETAPE_SPACE_TO_EOD);
			return (idetape_queue_pc_tail(drive, &pc));
		case MTERASE:
			(void) idetape_rewind_tape(drive);
			idetape_create_erase_cmd(&pc);
			return (idetape_queue_pc_tail(drive, &pc));
		case MTSETBLK:
			if (mt_count) {
				if (mt_count < tape->tape_block_size || mt_count % tape->tape_block_size)
					return -EIO;
				tape->user_bs_factor = mt_count / tape->tape_block_size;
				clear_bit(IDETAPE_DETECT_BS, &tape->flags);
			} else
				set_bit(IDETAPE_DETECT_BS, &tape->flags);
			return 0;
		case MTSEEK:
			idetape_discard_read_pipeline(drive, 0);
			return idetape_position_tape(drive, mt_count * tape->user_bs_factor, tape->partition, 0);
		case MTSETPART:
			idetape_discard_read_pipeline(drive, 0);
			return (idetape_position_tape(drive, 0, mt_count, 0));
		case MTFSR:
		case MTBSR:
		case MTLOCK:
			if (!idetape_create_prevent_cmd(drive, &pc, 1))
				return 0;
			retval = idetape_queue_pc_tail(drive, &pc);
			if (retval) return retval;
			tape->door_locked = DOOR_EXPLICITLY_LOCKED;
			return 0;
		case MTUNLOCK:
			if (!idetape_create_prevent_cmd(drive, &pc, 0))
				return 0;
			retval = idetape_queue_pc_tail(drive, &pc);
			if (retval) return retval;
			tape->door_locked = DOOR_UNLOCKED;
			return 0;
		default:
			printk(KERN_ERR "ide-tape: MTIO operation %d not "
				"supported\n", mt_op);
			return (-EIO);
	}
}

/*
 *	Our character device ioctls.
 *
 *	General mtio.h magnetic io commands are supported here, and not in
 *	the corresponding block interface.
 *
 *	The following ioctls are supported:
 *
 *	MTIOCTOP -	Refer to idetape_mtioctop for detailed description.
 *
 *	MTIOCGET - 	The mt_dsreg field in the returned mtget structure
 *			will be set to (user block size in bytes <<
 *			MT_ST_BLKSIZE_SHIFT) & MT_ST_BLKSIZE_MASK.
 *
 *			The mt_blkno is set to the current user block number.
 *			The other mtget fields are not supported.
 *
 *	MTIOCPOS -	The current tape "block position" is returned. We
 *			assume that each block contains user_block_size
 *			bytes.
 *
 *	Our own ide-tape ioctls are supported on both interfaces.
 */
static int idetape_chrdev_ioctl (struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ide_tape_obj *tape = ide_tape_f(file);
	ide_drive_t *drive = tape->drive;
	struct mtop mtop;
	struct mtget mtget;
	struct mtpos mtpos;
	int block_offset = 0, position = tape->first_frame_position;
	void __user *argp = (void __user *)arg;

#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 3)
		printk(KERN_INFO "ide-tape: Reached idetape_chrdev_ioctl, "
			"cmd=%u\n", cmd);
#endif /* IDETAPE_DEBUG_LOG */

	tape->restart_speed_control_req = 1;
	if (tape->chrdev_direction == idetape_direction_write) {
		idetape_empty_write_pipeline(drive);
		idetape_flush_tape_buffers(drive);
	}
	if (cmd == MTIOCGET || cmd == MTIOCPOS) {
		block_offset = idetape_pipeline_size(drive) / (tape->tape_block_size * tape->user_bs_factor);
		if ((position = idetape_read_position(drive)) < 0)
			return -EIO;
	}
	switch (cmd) {
		case MTIOCTOP:
			if (copy_from_user(&mtop, argp, sizeof (struct mtop)))
				return -EFAULT;
			return (idetape_mtioctop(drive,mtop.mt_op,mtop.mt_count));
		case MTIOCGET:
			memset(&mtget, 0, sizeof (struct mtget));
			mtget.mt_type = MT_ISSCSI2;
			mtget.mt_blkno = position / tape->user_bs_factor - block_offset;
			mtget.mt_dsreg = ((tape->tape_block_size * tape->user_bs_factor) << MT_ST_BLKSIZE_SHIFT) & MT_ST_BLKSIZE_MASK;
			if (tape->drv_write_prot) {
				mtget.mt_gstat |= GMT_WR_PROT(0xffffffff);
			}
			if (copy_to_user(argp, &mtget, sizeof(struct mtget)))
				return -EFAULT;
			return 0;
		case MTIOCPOS:
			mtpos.mt_blkno = position / tape->user_bs_factor - block_offset;
			if (copy_to_user(argp, &mtpos, sizeof(struct mtpos)))
				return -EFAULT;
			return 0;
		default:
			if (tape->chrdev_direction == idetape_direction_read)
				idetape_discard_read_pipeline(drive, 1);
			return idetape_blkdev_ioctl(drive, cmd, arg);
	}
}

static void idetape_get_blocksize_from_block_descriptor(ide_drive_t *drive);

/*
 *	Our character device open function.
 */
static int idetape_chrdev_open (struct inode *inode, struct file *filp)
{
	unsigned int minor = iminor(inode), i = minor & ~0xc0;
	ide_drive_t *drive;
	idetape_tape_t *tape;
	idetape_pc_t pc;
	int retval;

	/*
	 * We really want to do nonseekable_open(inode, filp); here, but some
	 * versions of tar incorrectly call lseek on tapes and bail out if that
	 * fails.  So we disallow pread() and pwrite(), but permit lseeks.
	 */
	filp->f_mode &= ~(FMODE_PREAD | FMODE_PWRITE);

#if IDETAPE_DEBUG_LOG
	printk(KERN_INFO "ide-tape: Reached idetape_chrdev_open\n");
#endif /* IDETAPE_DEBUG_LOG */
	
	if (i >= MAX_HWIFS * MAX_DRIVES)
		return -ENXIO;

	if (!(tape = ide_tape_chrdev_get(i)))
		return -ENXIO;

	drive = tape->drive;

	filp->private_data = tape;

	if (test_and_set_bit(IDETAPE_BUSY, &tape->flags)) {
		retval = -EBUSY;
		goto out_put_tape;
	}

	retval = idetape_wait_ready(drive, 60 * HZ);
	if (retval) {
		clear_bit(IDETAPE_BUSY, &tape->flags);
		printk(KERN_ERR "ide-tape: %s: drive not ready\n", tape->name);
		goto out_put_tape;
	}

	idetape_read_position(drive);
	if (!test_bit(IDETAPE_ADDRESS_VALID, &tape->flags))
		(void)idetape_rewind_tape(drive);

	if (tape->chrdev_direction != idetape_direction_read)
		clear_bit(IDETAPE_PIPELINE_ERROR, &tape->flags);

	/* Read block size and write protect status from drive. */
	idetape_get_blocksize_from_block_descriptor(drive);

	/* Set write protect flag if device is opened as read-only. */
	if ((filp->f_flags & O_ACCMODE) == O_RDONLY)
		tape->write_prot = 1;
	else
		tape->write_prot = tape->drv_write_prot;

	/* Make sure drive isn't write protected if user wants to write. */
	if (tape->write_prot) {
		if ((filp->f_flags & O_ACCMODE) == O_WRONLY ||
		    (filp->f_flags & O_ACCMODE) == O_RDWR) {
			clear_bit(IDETAPE_BUSY, &tape->flags);
			retval = -EROFS;
			goto out_put_tape;
		}
	}

	/*
	 * Lock the tape drive door so user can't eject.
	 */
	if (tape->chrdev_direction == idetape_direction_none) {
		if (idetape_create_prevent_cmd(drive, &pc, 1)) {
			if (!idetape_queue_pc_tail(drive, &pc)) {
				if (tape->door_locked != DOOR_EXPLICITLY_LOCKED)
					tape->door_locked = DOOR_LOCKED;
			}
		}
	}
	idetape_restart_speed_control(drive);
	tape->restart_speed_control_req = 0;
	return 0;

out_put_tape:
	ide_tape_put(tape);
	return retval;
}

static void idetape_write_release (ide_drive_t *drive, unsigned int minor)
{
	idetape_tape_t *tape = drive->driver_data;

	idetape_empty_write_pipeline(drive);
	tape->merge_stage = __idetape_kmalloc_stage(tape, 1, 0);
	if (tape->merge_stage != NULL) {
		idetape_pad_zeros(drive, tape->tape_block_size * (tape->user_bs_factor - 1));
		__idetape_kfree_stage(tape->merge_stage);
		tape->merge_stage = NULL;
	}
	idetape_write_filemark(drive);
	idetape_flush_tape_buffers(drive);
	idetape_flush_tape_buffers(drive);
}

/*
 *	Our character device release function.
 */
static int idetape_chrdev_release (struct inode *inode, struct file *filp)
{
	struct ide_tape_obj *tape = ide_tape_f(filp);
	ide_drive_t *drive = tape->drive;
	idetape_pc_t pc;
	unsigned int minor = iminor(inode);

	lock_kernel();
	tape = drive->driver_data;
#if IDETAPE_DEBUG_LOG
	if (tape->debug_level >= 3)
		printk(KERN_INFO "ide-tape: Reached idetape_chrdev_release\n");
#endif /* IDETAPE_DEBUG_LOG */

	if (tape->chrdev_direction == idetape_direction_write)
		idetape_write_release(drive, minor);
	if (tape->chrdev_direction == idetape_direction_read) {
		if (minor < 128)
			idetape_discard_read_pipeline(drive, 1);
		else
			idetape_wait_for_pipeline(drive);
	}
	if (tape->cache_stage != NULL) {
		__idetape_kfree_stage(tape->cache_stage);
		tape->cache_stage = NULL;
	}
	if (minor < 128 && test_bit(IDETAPE_MEDIUM_PRESENT, &tape->flags))
		(void) idetape_rewind_tape(drive);
	if (tape->chrdev_direction == idetape_direction_none) {
		if (tape->door_locked == DOOR_LOCKED) {
			if (idetape_create_prevent_cmd(drive, &pc, 0)) {
				if (!idetape_queue_pc_tail(drive, &pc))
					tape->door_locked = DOOR_UNLOCKED;
			}
		}
	}
	clear_bit(IDETAPE_BUSY, &tape->flags);
	ide_tape_put(tape);
	unlock_kernel();
	return 0;
}

/*
 *	idetape_identify_device is called to check the contents of the
 *	ATAPI IDENTIFY command results. We return:
 *
 *	1	If the tape can be supported by us, based on the information
 *		we have so far.
 *
 *	0 	If this tape driver is not currently supported by us.
 */
static int idetape_identify_device (ide_drive_t *drive)
{
	struct idetape_id_gcw gcw;
	struct hd_driveid *id = drive->id;
#if IDETAPE_DEBUG_INFO
	unsigned short mask,i;
#endif /* IDETAPE_DEBUG_INFO */

	if (drive->id_read == 0)
		return 1;

	*((unsigned short *) &gcw) = id->config;

#if IDETAPE_DEBUG_INFO
	printk(KERN_INFO "ide-tape: Dumping ATAPI Identify Device tape parameters\n");
	printk(KERN_INFO "ide-tape: Protocol Type: ");
	switch (gcw.protocol) {
		case 0: case 1: printk("ATA\n");break;
		case 2:	printk("ATAPI\n");break;
		case 3: printk("Reserved (Unknown to ide-tape)\n");break;
	}
	printk(KERN_INFO "ide-tape: Device Type: %x - ",gcw.device_type);	
	switch (gcw.device_type) {
		case 0: printk("Direct-access Device\n");break;
		case 1: printk("Streaming Tape Device\n");break;
		case 2: case 3: case 4: printk("Reserved\n");break;
		case 5: printk("CD-ROM Device\n");break;
		case 6: printk("Reserved\n");
		case 7: printk("Optical memory Device\n");break;
		case 0x1f: printk("Unknown or no Device type\n");break;
		default: printk("Reserved\n");
	}
	printk(KERN_INFO "ide-tape: Removable: %s",gcw.removable ? "Yes\n":"No\n");	
	printk(KERN_INFO "ide-tape: Command Packet DRQ Type: ");
	switch (gcw.drq_type) {
		case 0: printk("Microprocessor DRQ\n");break;
		case 1: printk("Interrupt DRQ\n");break;
		case 2: printk("Accelerated DRQ\n");break;
		case 3: printk("Reserved\n");break;
	}
	printk(KERN_INFO "ide-tape: Command Packet Size: ");
	switch (gcw.packet_size) {
		case 0: printk("12 bytes\n");break;
		case 1: printk("16 bytes\n");break;
		default: printk("Reserved\n");break;
	}
	printk(KERN_INFO "ide-tape: Model: %.40s\n",id->model);
	printk(KERN_INFO "ide-tape: Firmware Revision: %.8s\n",id->fw_rev);
	printk(KERN_INFO "ide-tape: Serial Number: %.20s\n",id->serial_no);
	printk(KERN_INFO "ide-tape: Write buffer size: %d bytes\n",id->buf_size*512);
	printk(KERN_INFO "ide-tape: DMA: %s",id->capability & 0x01 ? "Yes\n":"No\n");
	printk(KERN_INFO "ide-tape: LBA: %s",id->capability & 0x02 ? "Yes\n":"No\n");
	printk(KERN_INFO "ide-tape: IORDY can be disabled: %s",id->capability & 0x04 ? "Yes\n":"No\n");
	printk(KERN_INFO "ide-tape: IORDY supported: %s",id->capability & 0x08 ? "Yes\n":"Unknown\n");
	printk(KERN_INFO "ide-tape: ATAPI overlap supported: %s",id->capability & 0x20 ? "Yes\n":"No\n");
	printk(KERN_INFO "ide-tape: PIO Cycle Timing Category: %d\n",id->tPIO);
	printk(KERN_INFO "ide-tape: DMA Cycle Timing Category: %d\n",id->tDMA);
	printk(KERN_INFO "ide-tape: Single Word DMA supported modes: ");
	for (i=0,mask=1;i<8;i++,mask=mask << 1) {
		if (id->dma_1word & mask)
			printk("%d ",i);
		if (id->dma_1word & (mask << 8))
			printk("(active) ");
	}
	printk("\n");
	printk(KERN_INFO "ide-tape: Multi Word DMA supported modes: ");
	for (i=0,mask=1;i<8;i++,mask=mask << 1) {
		if (id->dma_mword & mask)
			printk("%d ",i);
		if (id->dma_mword & (mask << 8))
			printk("(active) ");
	}
	printk("\n");
	if (id->field_valid & 0x0002) {
		printk(KERN_INFO "ide-tape: Enhanced PIO Modes: %s\n",
			id->eide_pio_modes & 1 ? "Mode 3":"None");
		printk(KERN_INFO "ide-tape: Minimum Multi-word DMA cycle per word: ");
		if (id->eide_dma_min == 0)
			printk("Not supported\n");
		else
			printk("%d ns\n",id->eide_dma_min);

		printk(KERN_INFO "ide-tape: Manufacturer\'s Recommended Multi-word cycle: ");
		if (id->eide_dma_time == 0)
			printk("Not supported\n");
		else
			printk("%d ns\n",id->eide_dma_time);

		printk(KERN_INFO "ide-tape: Minimum PIO cycle without IORDY: ");
		if (id->eide_pio == 0)
			printk("Not supported\n");
		else
			printk("%d ns\n",id->eide_pio);

		printk(KERN_INFO "ide-tape: Minimum PIO cycle with IORDY: ");
		if (id->eide_pio_iordy == 0)
			printk("Not supported\n");
		else
			printk("%d ns\n",id->eide_pio_iordy);
		
	} else
		printk(KERN_INFO "ide-tape: According to the device, fields 64-70 are not valid.\n");
#endif /* IDETAPE_DEBUG_INFO */

	/* Check that we can support this device */

	if (gcw.protocol !=2 )
		printk(KERN_ERR "ide-tape: Protocol is not ATAPI\n");
	else if (gcw.device_type != 1)
		printk(KERN_ERR "ide-tape: Device type is not set to tape\n");
	else if (!gcw.removable)
		printk(KERN_ERR "ide-tape: The removable flag is not set\n");
	else if (gcw.packet_size != 0) {
		printk(KERN_ERR "ide-tape: Packet size is not 12 bytes long\n");
		if (gcw.packet_size == 1)
			printk(KERN_ERR "ide-tape: Sorry, padding to 16 bytes is still not supported\n");
	} else
		return 1;
	return 0;
}

/*
 * Use INQUIRY to get the firmware revision
 */
static void idetape_get_inquiry_results (ide_drive_t *drive)
{
	char *r;
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	idetape_inquiry_result_t *inquiry;
	
	idetape_create_inquiry_cmd(&pc);
	if (idetape_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-tape: %s: can't get INQUIRY results\n", tape->name);
		return;
	}
	inquiry = (idetape_inquiry_result_t *) pc.buffer;
	memcpy(tape->vendor_id, inquiry->vendor_id, 8);
	memcpy(tape->product_id, inquiry->product_id, 16);
	memcpy(tape->firmware_revision, inquiry->revision_level, 4);
	ide_fixstring(tape->vendor_id, 10, 0);
	ide_fixstring(tape->product_id, 18, 0);
	ide_fixstring(tape->firmware_revision, 6, 0);
	r = tape->firmware_revision;
	if (*(r + 1) == '.')
		tape->firmware_revision_num = (*r - '0') * 100 + (*(r + 2) - '0') * 10 + *(r + 3) - '0';
	printk(KERN_INFO "ide-tape: %s <-> %s: %s %s rev %s\n", drive->name, tape->name, tape->vendor_id, tape->product_id, tape->firmware_revision);
}

/*
 *	idetape_get_mode_sense_results asks the tape about its various
 *	parameters. In particular, we will adjust our data transfer buffer
 *	size to the recommended value as returned by the tape.
 */
static void idetape_get_mode_sense_results (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	idetape_mode_parameter_header_t *header;
	idetape_capabilities_page_t *capabilities;
	
	idetape_create_mode_sense_cmd(&pc, IDETAPE_CAPABILITIES_PAGE);
	if (idetape_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-tape: Can't get tape parameters - assuming some default values\n");
		tape->tape_block_size = 512;
		tape->capabilities.ctl = 52;
		tape->capabilities.speed = 450;
		tape->capabilities.buffer_size = 6 * 52;
		return;
	}
	header = (idetape_mode_parameter_header_t *) pc.buffer;
	capabilities = (idetape_capabilities_page_t *) (pc.buffer + sizeof(idetape_mode_parameter_header_t) + header->bdl);

	capabilities->max_speed = ntohs(capabilities->max_speed);
	capabilities->ctl = ntohs(capabilities->ctl);
	capabilities->speed = ntohs(capabilities->speed);
	capabilities->buffer_size = ntohs(capabilities->buffer_size);

	if (!capabilities->speed) {
		printk(KERN_INFO "ide-tape: %s: overriding capabilities->speed (assuming 650KB/sec)\n", drive->name);
		capabilities->speed = 650;
	}
	if (!capabilities->max_speed) {
		printk(KERN_INFO "ide-tape: %s: overriding capabilities->max_speed (assuming 650KB/sec)\n", drive->name);
		capabilities->max_speed = 650;
	}

	tape->capabilities = *capabilities;		/* Save us a copy */
	if (capabilities->blk512)
		tape->tape_block_size = 512;
	else if (capabilities->blk1024)
		tape->tape_block_size = 1024;

#if IDETAPE_DEBUG_INFO
	printk(KERN_INFO "ide-tape: Dumping the results of the MODE SENSE packet command\n");
	printk(KERN_INFO "ide-tape: Mode Parameter Header:\n");
	printk(KERN_INFO "ide-tape: Mode Data Length - %d\n",header->mode_data_length);
	printk(KERN_INFO "ide-tape: Medium Type - %d\n",header->medium_type);
	printk(KERN_INFO "ide-tape: Device Specific Parameter - %d\n",header->dsp);
	printk(KERN_INFO "ide-tape: Block Descriptor Length - %d\n",header->bdl);
	
	printk(KERN_INFO "ide-tape: Capabilities and Mechanical Status Page:\n");
	printk(KERN_INFO "ide-tape: Page code - %d\n",capabilities->page_code);
	printk(KERN_INFO "ide-tape: Page length - %d\n",capabilities->page_length);
	printk(KERN_INFO "ide-tape: Read only - %s\n",capabilities->ro ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Supports reverse space - %s\n",capabilities->sprev ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Supports erase initiated formatting - %s\n",capabilities->efmt ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Supports QFA two Partition format - %s\n",capabilities->qfa ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Supports locking the medium - %s\n",capabilities->lock ? "Yes":"No");
	printk(KERN_INFO "ide-tape: The volume is currently locked - %s\n",capabilities->locked ? "Yes":"No");
	printk(KERN_INFO "ide-tape: The device defaults in the prevent state - %s\n",capabilities->prevent ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Supports ejecting the medium - %s\n",capabilities->eject ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Supports error correction - %s\n",capabilities->ecc ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Supports data compression - %s\n",capabilities->cmprs ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Supports 512 bytes block size - %s\n",capabilities->blk512 ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Supports 1024 bytes block size - %s\n",capabilities->blk1024 ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Supports 32768 bytes block size / Restricted byte count for PIO transfers - %s\n",capabilities->blk32768 ? "Yes":"No");
	printk(KERN_INFO "ide-tape: Maximum supported speed in KBps - %d\n",capabilities->max_speed);
	printk(KERN_INFO "ide-tape: Continuous transfer limits in blocks - %d\n",capabilities->ctl);
	printk(KERN_INFO "ide-tape: Current speed in KBps - %d\n",capabilities->speed);	
	printk(KERN_INFO "ide-tape: Buffer size - %d\n",capabilities->buffer_size*512);
#endif /* IDETAPE_DEBUG_INFO */
}

/*
 *	ide_get_blocksize_from_block_descriptor does a mode sense page 0 with block descriptor
 *	and if it succeeds sets the tape block size with the reported value
 */
static void idetape_get_blocksize_from_block_descriptor(ide_drive_t *drive)
{

	idetape_tape_t *tape = drive->driver_data;
	idetape_pc_t pc;
	idetape_mode_parameter_header_t *header;
	idetape_parameter_block_descriptor_t *block_descrp;
	
	idetape_create_mode_sense_cmd(&pc, IDETAPE_BLOCK_DESCRIPTOR);
	if (idetape_queue_pc_tail(drive, &pc)) {
		printk(KERN_ERR "ide-tape: Can't get block descriptor\n");
		if (tape->tape_block_size == 0) {
			printk(KERN_WARNING "ide-tape: Cannot deal with zero block size, assume 32k\n");
			tape->tape_block_size =  32768;
		}
		return;
	}
	header = (idetape_mode_parameter_header_t *) pc.buffer;
	block_descrp = (idetape_parameter_block_descriptor_t *) (pc.buffer + sizeof(idetape_mode_parameter_header_t));
	tape->tape_block_size =( block_descrp->length[0]<<16) + (block_descrp->length[1]<<8) + block_descrp->length[2];
	tape->drv_write_prot = (header->dsp & 0x80) >> 7;

#if IDETAPE_DEBUG_INFO
	printk(KERN_INFO "ide-tape: Adjusted block size - %d\n", tape->tape_block_size);
#endif /* IDETAPE_DEBUG_INFO */
}

#ifdef CONFIG_IDE_PROC_FS
static void idetape_add_settings (ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

/*
 *			drive	setting name		read/write	data type	min			max			mul_factor			div_factor	data pointer				set function
 */
	ide_add_setting(drive,	"buffer",		SETTING_READ,	TYPE_SHORT,	0,			0xffff,			1,				2,		&tape->capabilities.buffer_size,	NULL);
	ide_add_setting(drive,	"pipeline_min",		SETTING_RW,	TYPE_INT,	1,			0xffff,			tape->stage_size / 1024,	1,		&tape->min_pipeline,			NULL);
	ide_add_setting(drive,	"pipeline",		SETTING_RW,	TYPE_INT,	1,			0xffff,			tape->stage_size / 1024,	1,		&tape->max_stages,			NULL);
	ide_add_setting(drive,	"pipeline_max",		SETTING_RW,	TYPE_INT,	1,			0xffff,			tape->stage_size / 1024,	1,		&tape->max_pipeline,			NULL);
	ide_add_setting(drive,	"pipeline_used",	SETTING_READ,	TYPE_INT,	0,			0xffff,			tape->stage_size / 1024,	1,		&tape->nr_stages,			NULL);
	ide_add_setting(drive,	"pipeline_pending",	SETTING_READ,	TYPE_INT,	0,			0xffff,			tape->stage_size / 1024,	1,		&tape->nr_pending_stages,		NULL);
	ide_add_setting(drive,	"speed",		SETTING_READ,	TYPE_SHORT,	0,			0xffff,			1,				1,		&tape->capabilities.speed,		NULL);
	ide_add_setting(drive,	"stage",		SETTING_READ,	TYPE_INT,	0,			0xffff,			1,				1024,		&tape->stage_size,			NULL);
	ide_add_setting(drive,	"tdsc",			SETTING_RW,	TYPE_INT,	IDETAPE_DSC_RW_MIN,	IDETAPE_DSC_RW_MAX,	1000,				HZ,		&tape->best_dsc_rw_frequency,		NULL);
	ide_add_setting(drive,	"dsc_overlap",		SETTING_RW,	TYPE_BYTE,	0,			1,			1,				1,		&drive->dsc_overlap,			NULL);
	ide_add_setting(drive,	"pipeline_head_speed_c",SETTING_READ,	TYPE_INT,	0,			0xffff,			1,				1,		&tape->controlled_pipeline_head_speed,	NULL);
	ide_add_setting(drive,	"pipeline_head_speed_u",SETTING_READ,	TYPE_INT,	0,			0xffff,			1,				1,		&tape->uncontrolled_pipeline_head_speed,NULL);
	ide_add_setting(drive,	"avg_speed",		SETTING_READ,	TYPE_INT,	0,			0xffff,			1,				1,		&tape->avg_speed,			NULL);
	ide_add_setting(drive,	"debug_level",		SETTING_RW,	TYPE_INT,	0,			0xffff,			1,				1,		&tape->debug_level,			NULL);
}
#else
static inline void idetape_add_settings(ide_drive_t *drive) { ; }
#endif

/*
 *	ide_setup is called to:
 *
 *		1.	Initialize our various state variables.
 *		2.	Ask the tape for its capabilities.
 *		3.	Allocate a buffer which will be used for data
 *			transfer. The buffer size is chosen based on
 *			the recommendation which we received in step (2).
 *
 *	Note that at this point ide.c already assigned us an irq, so that
 *	we can queue requests here and wait for their completion.
 */
static void idetape_setup (ide_drive_t *drive, idetape_tape_t *tape, int minor)
{
	unsigned long t1, tmid, tn, t;
	int speed;
	struct idetape_id_gcw gcw;
	int stage_size;
	struct sysinfo si;

	spin_lock_init(&tape->spinlock);
	drive->dsc_overlap = 1;
#ifdef CONFIG_BLK_DEV_IDEPCI
	if (HWIF(drive)->pci_dev != NULL) {
		/*
		 * These two ide-pci host adapters appear to need DSC overlap disabled.
		 * This probably needs further analysis.
		 */
		if ((HWIF(drive)->pci_dev->device == PCI_DEVICE_ID_ARTOP_ATP850UF) ||
		    (HWIF(drive)->pci_dev->device == PCI_DEVICE_ID_TTI_HPT343)) {
			printk(KERN_INFO "ide-tape: %s: disabling DSC overlap\n", tape->name);
		    	drive->dsc_overlap = 0;
		}
	}
#endif /* CONFIG_BLK_DEV_IDEPCI */
	/* Seagate Travan drives do not support DSC overlap. */
	if (strstr(drive->id->model, "Seagate STT3401"))
		drive->dsc_overlap = 0;
	tape->minor = minor;
	tape->name[0] = 'h';
	tape->name[1] = 't';
	tape->name[2] = '0' + minor;
	tape->chrdev_direction = idetape_direction_none;
	tape->pc = tape->pc_stack;
	tape->max_insert_speed = 10000;
	tape->speed_control = 1;
	*((unsigned short *) &gcw) = drive->id->config;
	if (gcw.drq_type == 1)
		set_bit(IDETAPE_DRQ_INTERRUPT, &tape->flags);

	tape->min_pipeline = tape->max_pipeline = tape->max_stages = 10;
	
	idetape_get_inquiry_results(drive);
	idetape_get_mode_sense_results(drive);
	idetape_get_blocksize_from_block_descriptor(drive);
	tape->user_bs_factor = 1;
	tape->stage_size = tape->capabilities.ctl * tape->tape_block_size;
	while (tape->stage_size > 0xffff) {
		printk(KERN_NOTICE "ide-tape: decreasing stage size\n");
		tape->capabilities.ctl /= 2;
		tape->stage_size = tape->capabilities.ctl * tape->tape_block_size;
	}
	stage_size = tape->stage_size;
	tape->pages_per_stage = stage_size / PAGE_SIZE;
	if (stage_size % PAGE_SIZE) {
		tape->pages_per_stage++;
		tape->excess_bh_size = PAGE_SIZE - stage_size % PAGE_SIZE;
	}

	/*
	 *	Select the "best" DSC read/write polling frequency
	 *	and pipeline size.
	 */
	speed = max(tape->capabilities.speed, tape->capabilities.max_speed);

	tape->max_stages = speed * 1000 * 10 / tape->stage_size;

	/*
	 * 	Limit memory use for pipeline to 10% of physical memory
	 */
	si_meminfo(&si);
	if (tape->max_stages * tape->stage_size > si.totalram * si.mem_unit / 10)
		tape->max_stages = si.totalram * si.mem_unit / (10 * tape->stage_size);
	tape->max_stages   = min(tape->max_stages, IDETAPE_MAX_PIPELINE_STAGES);
	tape->min_pipeline = min(tape->max_stages, IDETAPE_MIN_PIPELINE_STAGES);
	tape->max_pipeline = min(tape->max_stages * 2, IDETAPE_MAX_PIPELINE_STAGES);
	if (tape->max_stages == 0)
		tape->max_stages = tape->min_pipeline = tape->max_pipeline = 1;

	t1 = (tape->stage_size * HZ) / (speed * 1000);
	tmid = (tape->capabilities.buffer_size * 32 * HZ) / (speed * 125);
	tn = (IDETAPE_FIFO_THRESHOLD * tape->stage_size * HZ) / (speed * 1000);

	if (tape->max_stages)
		t = tn;
	else
		t = t1;

	/*
	 *	Ensure that the number we got makes sense; limit
	 *	it within IDETAPE_DSC_RW_MIN and IDETAPE_DSC_RW_MAX.
	 */
	tape->best_dsc_rw_frequency = max_t(unsigned long, min_t(unsigned long, t, IDETAPE_DSC_RW_MAX), IDETAPE_DSC_RW_MIN);
	printk(KERN_INFO "ide-tape: %s <-> %s: %dKBps, %d*%dkB buffer, "
		"%dkB pipeline, %lums tDSC%s\n",
		drive->name, tape->name, tape->capabilities.speed,
		(tape->capabilities.buffer_size * 512) / tape->stage_size,
		tape->stage_size / 1024,
		tape->max_stages * tape->stage_size / 1024,
		tape->best_dsc_rw_frequency * 1000 / HZ,
		drive->using_dma ? ", DMA":"");

	idetape_add_settings(drive);
}

static void ide_tape_remove(ide_drive_t *drive)
{
	idetape_tape_t *tape = drive->driver_data;

	ide_proc_unregister_driver(drive, tape->driver);

	ide_unregister_region(tape->disk);

	ide_tape_put(tape);
}

static void ide_tape_release(struct kref *kref)
{
	struct ide_tape_obj *tape = to_ide_tape(kref);
	ide_drive_t *drive = tape->drive;
	struct gendisk *g = tape->disk;

	BUG_ON(tape->first_stage != NULL || tape->merge_stage_size);

	drive->dsc_overlap = 0;
	drive->driver_data = NULL;
	device_destroy(idetape_sysfs_class, MKDEV(IDETAPE_MAJOR, tape->minor));
	device_destroy(idetape_sysfs_class, MKDEV(IDETAPE_MAJOR, tape->minor + 128));
	idetape_devs[tape->minor] = NULL;
	g->private_data = NULL;
	put_disk(g);
	kfree(tape);
}

#ifdef CONFIG_IDE_PROC_FS
static int proc_idetape_read_name
	(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	ide_drive_t	*drive = (ide_drive_t *) data;
	idetape_tape_t	*tape = drive->driver_data;
	char		*out = page;
	int		len;

	len = sprintf(out, "%s\n", tape->name);
	PROC_IDE_READ_RETURN(page, start, off, count, eof, len);
}

static ide_proc_entry_t idetape_proc[] = {
	{ "capacity",	S_IFREG|S_IRUGO,	proc_ide_read_capacity, NULL },
	{ "name",	S_IFREG|S_IRUGO,	proc_idetape_read_name,	NULL },
	{ NULL, 0, NULL, NULL }
};
#endif

static int ide_tape_probe(ide_drive_t *);

static ide_driver_t idetape_driver = {
	.gen_driver = {
		.owner		= THIS_MODULE,
		.name		= "ide-tape",
		.bus		= &ide_bus_type,
	},
	.probe			= ide_tape_probe,
	.remove			= ide_tape_remove,
	.version		= IDETAPE_VERSION,
	.media			= ide_tape,
	.supports_dsc_overlap 	= 1,
	.do_request		= idetape_do_request,
	.end_request		= idetape_end_request,
	.error			= __ide_error,
	.abort			= __ide_abort,
#ifdef CONFIG_IDE_PROC_FS
	.proc			= idetape_proc,
#endif
};

/*
 *	Our character device supporting functions, passed to register_chrdev.
 */
static const struct file_operations idetape_fops = {
	.owner		= THIS_MODULE,
	.read		= idetape_chrdev_read,
	.write		= idetape_chrdev_write,
	.ioctl		= idetape_chrdev_ioctl,
	.open		= idetape_chrdev_open,
	.release	= idetape_chrdev_release,
};

static int idetape_open(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_tape_obj *tape;

	if (!(tape = ide_tape_get(disk)))
		return -ENXIO;

	return 0;
}

static int idetape_release(struct inode *inode, struct file *filp)
{
	struct gendisk *disk = inode->i_bdev->bd_disk;
	struct ide_tape_obj *tape = ide_tape_g(disk);

	ide_tape_put(tape);

	return 0;
}

static int idetape_ioctl(struct inode *inode, struct file *file,
			unsigned int cmd, unsigned long arg)
{
	struct block_device *bdev = inode->i_bdev;
	struct ide_tape_obj *tape = ide_tape_g(bdev->bd_disk);
	ide_drive_t *drive = tape->drive;
	int err = generic_ide_ioctl(drive, file, bdev, cmd, arg);
	if (err == -EINVAL)
		err = idetape_blkdev_ioctl(drive, cmd, arg);
	return err;
}

static struct block_device_operations idetape_block_ops = {
	.owner		= THIS_MODULE,
	.open		= idetape_open,
	.release	= idetape_release,
	.ioctl		= idetape_ioctl,
};

static int ide_tape_probe(ide_drive_t *drive)
{
	idetape_tape_t *tape;
	struct gendisk *g;
	int minor;

	if (!strstr("ide-tape", drive->driver_req))
		goto failed;
	if (!drive->present)
		goto failed;
	if (drive->media != ide_tape)
		goto failed;
	if (!idetape_identify_device (drive)) {
		printk(KERN_ERR "ide-tape: %s: not supported by this version of ide-tape\n", drive->name);
		goto failed;
	}
	if (drive->scsi) {
		printk("ide-tape: passing drive %s to ide-scsi emulation.\n", drive->name);
		goto failed;
	}
	if (strstr(drive->id->model, "OnStream DI-")) {
		printk(KERN_WARNING "ide-tape: Use drive %s with ide-scsi emulation and osst.\n", drive->name);
		printk(KERN_WARNING "ide-tape: OnStream support will be removed soon from ide-tape!\n");
	}
	tape = kzalloc(sizeof (idetape_tape_t), GFP_KERNEL);
	if (tape == NULL) {
		printk(KERN_ERR "ide-tape: %s: Can't allocate a tape structure\n", drive->name);
		goto failed;
	}

	g = alloc_disk(1 << PARTN_BITS);
	if (!g)
		goto out_free_tape;

	ide_init_disk(g, drive);

	ide_proc_register_driver(drive, &idetape_driver);

	kref_init(&tape->kref);

	tape->drive = drive;
	tape->driver = &idetape_driver;
	tape->disk = g;

	g->private_data = &tape->driver;

	drive->driver_data = tape;

	mutex_lock(&idetape_ref_mutex);
	for (minor = 0; idetape_devs[minor]; minor++)
		;
	idetape_devs[minor] = tape;
	mutex_unlock(&idetape_ref_mutex);

	idetape_setup(drive, tape, minor);

	device_create(idetape_sysfs_class, &drive->gendev,
		      MKDEV(IDETAPE_MAJOR, minor), "%s", tape->name);
	device_create(idetape_sysfs_class, &drive->gendev,
			MKDEV(IDETAPE_MAJOR, minor + 128), "n%s", tape->name);

	g->fops = &idetape_block_ops;
	ide_register_region(g);

	return 0;

out_free_tape:
	kfree(tape);
failed:
	return -ENODEV;
}

MODULE_DESCRIPTION("ATAPI Streaming TAPE Driver");
MODULE_LICENSE("GPL");

static void __exit idetape_exit (void)
{
	driver_unregister(&idetape_driver.gen_driver);
	class_destroy(idetape_sysfs_class);
	unregister_chrdev(IDETAPE_MAJOR, "ht");
}

static int __init idetape_init(void)
{
	int error = 1;
	idetape_sysfs_class = class_create(THIS_MODULE, "ide_tape");
	if (IS_ERR(idetape_sysfs_class)) {
		idetape_sysfs_class = NULL;
		printk(KERN_ERR "Unable to create sysfs class for ide tapes\n");
		error = -EBUSY;
		goto out;
	}

	if (register_chrdev(IDETAPE_MAJOR, "ht", &idetape_fops)) {
		printk(KERN_ERR "ide-tape: Failed to register character device interface\n");
		error = -EBUSY;
		goto out_free_class;
	}

	error = driver_register(&idetape_driver.gen_driver);
	if (error)
		goto out_free_driver;

	return 0;

out_free_driver:
	driver_unregister(&idetape_driver.gen_driver);
out_free_class:
	class_destroy(idetape_sysfs_class);
out:
	return error;
}

MODULE_ALIAS("ide:*m-tape*");
module_init(idetape_init);
module_exit(idetape_exit);
MODULE_ALIAS_CHARDEV_MAJOR(IDETAPE_MAJOR);
