/*
 * NCR 5380 generic driver routines.  These should make it *trivial*
 *	to implement 5380 SCSI drivers under Linux with a non-trantor
 *	architecture.
 *
 *	Note that these routines also work with NR53c400 family chips.
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *	+1 (303) 666-5836
 *
 * For more information, please consult
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
 *
 * NCR Microelectronics
 * 1635 Aeroplaza Drive
 * Colorado Springs, CO 80916
 * 1+ (719) 578-3400
 * 1+ (800) 334-5454
 */

/*
 * ++roman: To port the 5380 driver to the Atari, I had to do some changes in
 * this file, too:
 *
 *  - Some of the debug statements were incorrect (undefined variables and the
 *    like). I fixed that.
 *
 *  - In information_transfer(), I think a #ifdef was wrong. Looking at the
 *    possible DMA transfer size should also happen for REAL_DMA. I added this
 *    in the #if statement.
 *
 *  - When using real DMA, information_transfer() should return in a DATAOUT
 *    phase after starting the DMA. It has nothing more to do.
 *
 *  - The interrupt service routine should run main after end of DMA, too (not
 *    only after RESELECTION interrupts). Additionally, it should _not_ test
 *    for more interrupts after running main, since a DMA process may have
 *    been started and interrupts are turned on now. The new int could happen
 *    inside the execution of NCR5380_intr(), leading to recursive
 *    calls.
 *
 *  - I've added a function merge_contiguous_buffers() that tries to
 *    merge scatter-gather buffers that are located at contiguous
 *    physical addresses and can be processed with the same DMA setup.
 *    Since most scatter-gather operations work on a page (4K) of
 *    4 buffers (1K), in more than 90% of all cases three interrupts and
 *    DMA setup actions are saved.
 *
 * - I've deleted all the stuff for AUTOPROBE_IRQ, REAL_DMA_POLL, PSEUDO_DMA
 *    and USLEEP, because these were messing up readability and will never be
 *    needed for Atari SCSI.
 *
 * - I've revised the NCR5380_main() calling scheme (relax the 'main_running'
 *   stuff), and 'main' is executed in a bottom half if awoken by an
 *   interrupt.
 *
 * - The code was quite cluttered up by "#if (NDEBUG & NDEBUG_*) printk..."
 *   constructs. In my eyes, this made the source rather unreadable, so I
 *   finally replaced that by the *_PRINTK() macros.
 *
 */

/*
 * Further development / testing that should be done :
 * 1.  Test linked command handling code after Eric is ready with
 *     the high level code.
 */

/* Adapted for the sun3 by Sam Creasey. */

#include <scsi/scsi_dbg.h>
#include <scsi/scsi_transport_spi.h>

#if (NDEBUG & NDEBUG_LISTS)
#define LIST(x, y)						\
	do {							\
		printk("LINE:%d   Adding %p to %p\n",		\
		       __LINE__, (void*)(x), (void*)(y));	\
		if ((x) == (y))					\
			udelay(5);				\
	} while (0)
#define REMOVE(w, x, y, z)					\
	do {							\
		printk("LINE:%d   Removing: %p->%p  %p->%p \n",	\
		       __LINE__, (void*)(w), (void*)(x),	\
		       (void*)(y), (void*)(z));			\
		if ((x) == (y))					\
			udelay(5);				\
	} while (0)
#else
#define LIST(x,y)
#define REMOVE(w,x,y,z)
#endif

#ifndef notyet
#undef LINKED
#endif

/*
 * Design
 *
 * This is a generic 5380 driver.  To use it on a different platform,
 * one simply writes appropriate system specific macros (ie, data
 * transfer - some PC's will use the I/O bus, 68K's must use
 * memory mapped) and drops this file in their 'C' wrapper.
 *
 * As far as command queueing, two queues are maintained for
 * each 5380 in the system - commands that haven't been issued yet,
 * and commands that are currently executing.  This means that an
 * unlimited number of commands may be queued, letting
 * more commands propagate from the higher driver levels giving higher
 * throughput.  Note that both I_T_L and I_T_L_Q nexuses are supported,
 * allowing multiple commands to propagate all the way to a SCSI-II device
 * while a command is already executing.
 *
 *
 * Issues specific to the NCR5380 :
 *
 * When used in a PIO or pseudo-dma mode, the NCR5380 is a braindead
 * piece of hardware that requires you to sit in a loop polling for
 * the REQ signal as long as you are connected.  Some devices are
 * brain dead (ie, many TEXEL CD ROM drives) and won't disconnect
 * while doing long seek operations.
 *
 * The workaround for this is to keep track of devices that have
 * disconnected.  If the device hasn't disconnected, for commands that
 * should disconnect, we do something like
 *
 * while (!REQ is asserted) { sleep for N usecs; poll for M usecs }
 *
 * Some tweaking of N and M needs to be done.  An algorithm based
 * on "time to data" would give the best results as long as short time
 * to datas (ie, on the same track) were considered, however these
 * broken devices are the exception rather than the rule and I'd rather
 * spend my time optimizing for the normal case.
 *
 * Architecture :
 *
 * At the heart of the design is a coroutine, NCR5380_main,
 * which is started from a workqueue for each NCR5380 host in the
 * system.  It attempts to establish I_T_L or I_T_L_Q nexuses by
 * removing the commands from the issue queue and calling
 * NCR5380_select() if a nexus is not established.
 *
 * Once a nexus is established, the NCR5380_information_transfer()
 * phase goes through the various phases as instructed by the target.
 * if the target goes into MSG IN and sends a DISCONNECT message,
 * the command structure is placed into the per instance disconnected
 * queue, and NCR5380_main tries to find more work.  If the target is
 * idle for too long, the system will try to sleep.
 *
 * If a command has disconnected, eventually an interrupt will trigger,
 * calling NCR5380_intr()  which will in turn call NCR5380_reselect
 * to reestablish a nexus.  This will run main if necessary.
 *
 * On command termination, the done function will be called as
 * appropriate.
 *
 * SCSI pointers are maintained in the SCp field of SCSI command
 * structures, being initialized after the command is connected
 * in NCR5380_select, and set as appropriate in NCR5380_information_transfer.
 * Note that in violation of the standard, an implicit SAVE POINTERS operation
 * is done, since some BROKEN disks fail to issue an explicit SAVE POINTERS.
 */

/*
 * Using this file :
 * This file a skeleton Linux SCSI driver for the NCR 5380 series
 * of chips.  To use it, you write an architecture specific functions
 * and macros and include this file in your driver.
 *
 * These macros control options :
 * AUTOSENSE - if defined, REQUEST SENSE will be performed automatically
 *	for commands that return with a CHECK CONDITION status.
 *
 * DIFFERENTIAL - if defined, NCR53c81 chips will use external differential
 *	transceivers.
 *
 * LINKED - if defined, linked commands are supported.
 *
 * REAL_DMA - if defined, REAL DMA is used during the data transfer phases.
 *
 * SUPPORT_TAGS - if defined, SCSI-2 tagged queuing is used where possible
 *
 * These macros MUST be defined :
 *
 * NCR5380_read(register)  - read from the specified register
 *
 * NCR5380_write(register, value) - write to the specific register
 *
 * NCR5380_implementation_fields  - additional fields needed for this
 *      specific implementation of the NCR5380
 *
 * Either real DMA *or* pseudo DMA may be implemented
 * REAL functions :
 * NCR5380_REAL_DMA should be defined if real DMA is to be used.
 * Note that the DMA setup functions should return the number of bytes
 *	that they were able to program the controller for.
 *
 * Also note that generic i386/PC versions of these macros are
 *	available as NCR5380_i386_dma_write_setup,
 *	NCR5380_i386_dma_read_setup, and NCR5380_i386_dma_residual.
 *
 * NCR5380_dma_write_setup(instance, src, count) - initialize
 * NCR5380_dma_read_setup(instance, dst, count) - initialize
 * NCR5380_dma_residual(instance); - residual count
 *
 * PSEUDO functions :
 * NCR5380_pwrite(instance, src, count)
 * NCR5380_pread(instance, dst, count);
 *
 * The generic driver is initialized by calling NCR5380_init(instance),
 * after setting the appropriate host specific fields and ID.  If the
 * driver wishes to autoprobe for an IRQ line, the NCR5380_probe_irq(instance,
 * possible) function may be used.
 */

/* Macros ease life... :-) */
#define	SETUP_HOSTDATA(in)				\
    struct NCR5380_hostdata *hostdata =			\
	(struct NCR5380_hostdata *)(in)->hostdata
#define	HOSTDATA(in) ((struct NCR5380_hostdata *)(in)->hostdata)

#define	NEXT(cmd)		((struct scsi_cmnd *)(cmd)->host_scribble)
#define	SET_NEXT(cmd,next)	((cmd)->host_scribble = (void *)(next))
#define	NEXTADDR(cmd)		((struct scsi_cmnd **)&(cmd)->host_scribble)

#define	HOSTNO		instance->host_no
#define	H_NO(cmd)	(cmd)->device->host->host_no

#ifdef SUPPORT_TAGS

/*
 * Functions for handling tagged queuing
 * =====================================
 *
 * ++roman (01/96): Now I've implemented SCSI-2 tagged queuing. Some notes:
 *
 * Using consecutive numbers for the tags is no good idea in my eyes. There
 * could be wrong re-usings if the counter (8 bit!) wraps and some early
 * command has been preempted for a long time. My solution: a bitfield for
 * remembering used tags.
 *
 * There's also the problem that each target has a certain queue size, but we
 * cannot know it in advance :-( We just see a QUEUE_FULL status being
 * returned. So, in this case, the driver internal queue size assumption is
 * reduced to the number of active tags if QUEUE_FULL is returned by the
 * target. The command is returned to the mid-level, but with status changed
 * to BUSY, since --as I've seen-- the mid-level can't handle QUEUE_FULL
 * correctly.
 *
 * We're also not allowed running tagged commands as long as an untagged
 * command is active. And REQUEST SENSE commands after a contingent allegiance
 * condition _must_ be untagged. To keep track whether an untagged command has
 * been issued, the host->busy array is still employed, as it is without
 * support for tagged queuing.
 *
 * One could suspect that there are possible race conditions between
 * is_lun_busy(), cmd_get_tag() and cmd_free_tag(). But I think this isn't the
 * case: is_lun_busy() and cmd_get_tag() are both called from NCR5380_main(),
 * which already guaranteed to be running at most once. It is also the only
 * place where tags/LUNs are allocated. So no other allocation can slip
 * between that pair, there could only happen a reselection, which can free a
 * tag, but that doesn't hurt. Only the sequence in cmd_free_tag() becomes
 * important: the tag bit must be cleared before 'nr_allocated' is decreased.
 */

static void __init init_tags(struct NCR5380_hostdata *hostdata)
{
	int target, lun;
	struct tag_alloc *ta;

	if (!(hostdata->flags & FLAG_TAGGED_QUEUING))
		return;

	for (target = 0; target < 8; ++target) {
		for (lun = 0; lun < 8; ++lun) {
			ta = &hostdata->TagAlloc[target][lun];
			bitmap_zero(ta->allocated, MAX_TAGS);
			ta->nr_allocated = 0;
			/* At the beginning, assume the maximum queue size we could
			 * support (MAX_TAGS). This value will be decreased if the target
			 * returns QUEUE_FULL status.
			 */
			ta->queue_size = MAX_TAGS;
		}
	}
}


/* Check if we can issue a command to this LUN: First see if the LUN is marked
 * busy by an untagged command. If the command should use tagged queuing, also
 * check that there is a free tag and the target's queue won't overflow. This
 * function should be called with interrupts disabled to avoid race
 * conditions.
 */

static int is_lun_busy(struct scsi_cmnd *cmd, int should_be_tagged)
{
	u8 lun = cmd->device->lun;
	SETUP_HOSTDATA(cmd->device->host);

	if (hostdata->busy[cmd->device->id] & (1 << lun))
		return 1;
	if (!should_be_tagged ||
	    !(hostdata->flags & FLAG_TAGGED_QUEUING) ||
	    !cmd->device->tagged_supported)
		return 0;
	if (hostdata->TagAlloc[scmd_id(cmd)][lun].nr_allocated >=
	    hostdata->TagAlloc[scmd_id(cmd)][lun].queue_size) {
		dprintk(NDEBUG_TAGS, "scsi%d: target %d lun %d: no free tags\n",
			   H_NO(cmd), cmd->device->id, lun);
		return 1;
	}
	return 0;
}


/* Allocate a tag for a command (there are no checks anymore, check_lun_busy()
 * must be called before!), or reserve the LUN in 'busy' if the command is
 * untagged.
 */

static void cmd_get_tag(struct scsi_cmnd *cmd, int should_be_tagged)
{
	u8 lun = cmd->device->lun;
	SETUP_HOSTDATA(cmd->device->host);

	/* If we or the target don't support tagged queuing, allocate the LUN for
	 * an untagged command.
	 */
	if (!should_be_tagged ||
	    !(hostdata->flags & FLAG_TAGGED_QUEUING) ||
	    !cmd->device->tagged_supported) {
		cmd->tag = TAG_NONE;
		hostdata->busy[cmd->device->id] |= (1 << lun);
		dprintk(NDEBUG_TAGS, "scsi%d: target %d lun %d now allocated by untagged "
			   "command\n", H_NO(cmd), cmd->device->id, lun);
	} else {
		struct tag_alloc *ta = &hostdata->TagAlloc[scmd_id(cmd)][lun];

		cmd->tag = find_first_zero_bit(ta->allocated, MAX_TAGS);
		set_bit(cmd->tag, ta->allocated);
		ta->nr_allocated++;
		dprintk(NDEBUG_TAGS, "scsi%d: using tag %d for target %d lun %d "
			   "(now %d tags in use)\n",
			   H_NO(cmd), cmd->tag, cmd->device->id,
			   lun, ta->nr_allocated);
	}
}


/* Mark the tag of command 'cmd' as free, or in case of an untagged command,
 * unlock the LUN.
 */

static void cmd_free_tag(struct scsi_cmnd *cmd)
{
	u8 lun = cmd->device->lun;
	SETUP_HOSTDATA(cmd->device->host);

	if (cmd->tag == TAG_NONE) {
		hostdata->busy[cmd->device->id] &= ~(1 << lun);
		dprintk(NDEBUG_TAGS, "scsi%d: target %d lun %d untagged cmd finished\n",
			   H_NO(cmd), cmd->device->id, lun);
	} else if (cmd->tag >= MAX_TAGS) {
		printk(KERN_NOTICE "scsi%d: trying to free bad tag %d!\n",
		       H_NO(cmd), cmd->tag);
	} else {
		struct tag_alloc *ta = &hostdata->TagAlloc[scmd_id(cmd)][lun];
		clear_bit(cmd->tag, ta->allocated);
		ta->nr_allocated--;
		dprintk(NDEBUG_TAGS, "scsi%d: freed tag %d for target %d lun %d\n",
			   H_NO(cmd), cmd->tag, cmd->device->id, lun);
	}
}


static void free_all_tags(struct NCR5380_hostdata *hostdata)
{
	int target, lun;
	struct tag_alloc *ta;

	if (!(hostdata->flags & FLAG_TAGGED_QUEUING))
		return;

	for (target = 0; target < 8; ++target) {
		for (lun = 0; lun < 8; ++lun) {
			ta = &hostdata->TagAlloc[target][lun];
			bitmap_zero(ta->allocated, MAX_TAGS);
			ta->nr_allocated = 0;
		}
	}
}

#endif /* SUPPORT_TAGS */


/*
 * Function: void merge_contiguous_buffers( struct scsi_cmnd *cmd )
 *
 * Purpose: Try to merge several scatter-gather requests into one DMA
 *    transfer. This is possible if the scatter buffers lie on
 *    physical contiguous addresses.
 *
 * Parameters: struct scsi_cmnd *cmd
 *    The command to work on. The first scatter buffer's data are
 *    assumed to be already transferred into ptr/this_residual.
 */

static void merge_contiguous_buffers(struct scsi_cmnd *cmd)
{
#if !defined(CONFIG_SUN3)
	unsigned long endaddr;
#if (NDEBUG & NDEBUG_MERGING)
	unsigned long oldlen = cmd->SCp.this_residual;
	int cnt = 1;
#endif

	for (endaddr = virt_to_phys(cmd->SCp.ptr + cmd->SCp.this_residual - 1) + 1;
	     cmd->SCp.buffers_residual &&
	     virt_to_phys(sg_virt(&cmd->SCp.buffer[1])) == endaddr;) {
		dprintk(NDEBUG_MERGING, "VTOP(%p) == %08lx -> merging\n",
			   page_address(sg_page(&cmd->SCp.buffer[1])), endaddr);
#if (NDEBUG & NDEBUG_MERGING)
		++cnt;
#endif
		++cmd->SCp.buffer;
		--cmd->SCp.buffers_residual;
		cmd->SCp.this_residual += cmd->SCp.buffer->length;
		endaddr += cmd->SCp.buffer->length;
	}
#if (NDEBUG & NDEBUG_MERGING)
	if (oldlen != cmd->SCp.this_residual)
		dprintk(NDEBUG_MERGING, "merged %d buffers from %p, new length %08x\n",
			   cnt, cmd->SCp.ptr, cmd->SCp.this_residual);
#endif
#endif /* !defined(CONFIG_SUN3) */
}

/**
 * initialize_SCp - init the scsi pointer field
 * @cmd: command block to set up
 *
 * Set up the internal fields in the SCSI command.
 */

static inline void initialize_SCp(struct scsi_cmnd *cmd)
{
	/*
	 * Initialize the Scsi Pointer field so that all of the commands in the
	 * various queues are valid.
	 */

	if (scsi_bufflen(cmd)) {
		cmd->SCp.buffer = scsi_sglist(cmd);
		cmd->SCp.buffers_residual = scsi_sg_count(cmd) - 1;
		cmd->SCp.ptr = sg_virt(cmd->SCp.buffer);
		cmd->SCp.this_residual = cmd->SCp.buffer->length;
		/* ++roman: Try to merge some scatter-buffers if they are at
		 * contiguous physical addresses.
		 */
		merge_contiguous_buffers(cmd);
	} else {
		cmd->SCp.buffer = NULL;
		cmd->SCp.buffers_residual = 0;
		cmd->SCp.ptr = NULL;
		cmd->SCp.this_residual = 0;
	}
}

#include <linux/delay.h>

#if NDEBUG
static struct {
	unsigned char mask;
	const char *name;
} signals[] = {
	{ SR_DBP, "PARITY"}, { SR_RST, "RST" }, { SR_BSY, "BSY" },
	{ SR_REQ, "REQ" }, { SR_MSG, "MSG" }, { SR_CD,  "CD" }, { SR_IO, "IO" },
	{ SR_SEL, "SEL" }, {0, NULL}
}, basrs[] = {
	{BASR_ATN, "ATN"}, {BASR_ACK, "ACK"}, {0, NULL}
}, icrs[] = {
	{ICR_ASSERT_RST, "ASSERT RST"},{ICR_ASSERT_ACK, "ASSERT ACK"},
	{ICR_ASSERT_BSY, "ASSERT BSY"}, {ICR_ASSERT_SEL, "ASSERT SEL"},
	{ICR_ASSERT_ATN, "ASSERT ATN"}, {ICR_ASSERT_DATA, "ASSERT DATA"},
	{0, NULL}
}, mrs[] = {
	{MR_BLOCK_DMA_MODE, "MODE BLOCK DMA"}, {MR_TARGET, "MODE TARGET"},
	{MR_ENABLE_PAR_CHECK, "MODE PARITY CHECK"}, {MR_ENABLE_PAR_INTR,
	"MODE PARITY INTR"}, {MR_ENABLE_EOP_INTR,"MODE EOP INTR"},
	{MR_MONITOR_BSY, "MODE MONITOR BSY"},
	{MR_DMA_MODE, "MODE DMA"}, {MR_ARBITRATE, "MODE ARBITRATION"},
	{0, NULL}
};

/**
 * NCR5380_print - print scsi bus signals
 * @instance: adapter state to dump
 *
 * Print the SCSI bus signals for debugging purposes
 */

static void NCR5380_print(struct Scsi_Host *instance)
{
	unsigned char status, data, basr, mr, icr, i;
	unsigned long flags;

	local_irq_save(flags);
	data = NCR5380_read(CURRENT_SCSI_DATA_REG);
	status = NCR5380_read(STATUS_REG);
	mr = NCR5380_read(MODE_REG);
	icr = NCR5380_read(INITIATOR_COMMAND_REG);
	basr = NCR5380_read(BUS_AND_STATUS_REG);
	local_irq_restore(flags);
	printk("STATUS_REG: %02x ", status);
	for (i = 0; signals[i].mask; ++i)
		if (status & signals[i].mask)
			printk(",%s", signals[i].name);
	printk("\nBASR: %02x ", basr);
	for (i = 0; basrs[i].mask; ++i)
		if (basr & basrs[i].mask)
			printk(",%s", basrs[i].name);
	printk("\nICR: %02x ", icr);
	for (i = 0; icrs[i].mask; ++i)
		if (icr & icrs[i].mask)
			printk(",%s", icrs[i].name);
	printk("\nMODE: %02x ", mr);
	for (i = 0; mrs[i].mask; ++i)
		if (mr & mrs[i].mask)
			printk(",%s", mrs[i].name);
	printk("\n");
}

static struct {
	unsigned char value;
	const char *name;
} phases[] = {
	{PHASE_DATAOUT, "DATAOUT"}, {PHASE_DATAIN, "DATAIN"}, {PHASE_CMDOUT, "CMDOUT"},
	{PHASE_STATIN, "STATIN"}, {PHASE_MSGOUT, "MSGOUT"}, {PHASE_MSGIN, "MSGIN"},
	{PHASE_UNKNOWN, "UNKNOWN"}
};

/**
 * NCR5380_print_phase - show SCSI phase
 * @instance: adapter to dump
 *
 * Print the current SCSI phase for debugging purposes
 *
 * Locks: none
 */

static void NCR5380_print_phase(struct Scsi_Host *instance)
{
	unsigned char status;
	int i;

	status = NCR5380_read(STATUS_REG);
	if (!(status & SR_REQ))
		printk(KERN_DEBUG "scsi%d: REQ not asserted, phase unknown.\n", HOSTNO);
	else {
		for (i = 0; (phases[i].value != PHASE_UNKNOWN) &&
		     (phases[i].value != (status & PHASE_MASK)); ++i)
			;
		printk(KERN_DEBUG "scsi%d: phase %s\n", HOSTNO, phases[i].name);
	}
}

#endif

/*
 * ++roman: New scheme of calling NCR5380_main()
 *
 * If we're not in an interrupt, we can call our main directly, it cannot be
 * already running. Else, we queue it on a task queue, if not 'main_running'
 * tells us that a lower level is already executing it. This way,
 * 'main_running' needs not be protected in a special way.
 *
 * queue_main() is a utility function for putting our main onto the task
 * queue, if main_running is false. It should be called only from a
 * interrupt or bottom half.
 */

#include <linux/gfp.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>

static inline void queue_main(struct NCR5380_hostdata *hostdata)
{
	if (!hostdata->main_running) {
		/* If in interrupt and NCR5380_main() not already running,
		   queue it on the 'immediate' task queue, to be processed
		   immediately after the current interrupt processing has
		   finished. */
		schedule_work(&hostdata->main_task);
	}
	/* else: nothing to do: the running NCR5380_main() will pick up
	   any newly queued command. */
}

/**
 * NCR58380_info - report driver and host information
 * @instance: relevant scsi host instance
 *
 * For use as the host template info() handler.
 *
 * Locks: none
 */

static const char *NCR5380_info(struct Scsi_Host *instance)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);

	return hostdata->info;
}

static void prepare_info(struct Scsi_Host *instance)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);

	snprintf(hostdata->info, sizeof(hostdata->info),
	         "%s, io_port 0x%lx, n_io_port %d, "
	         "base 0x%lx, irq %d, "
	         "can_queue %d, cmd_per_lun %d, "
	         "sg_tablesize %d, this_id %d, "
	         "flags { %s}, "
	         "options { %s} ",
	         instance->hostt->name, instance->io_port, instance->n_io_port,
	         instance->base, instance->irq,
	         instance->can_queue, instance->cmd_per_lun,
	         instance->sg_tablesize, instance->this_id,
	         hostdata->flags & FLAG_TAGGED_QUEUING ? "TAGGED_QUEUING " : "",
#ifdef DIFFERENTIAL
	         "DIFFERENTIAL "
#endif
#ifdef REAL_DMA
	         "REAL_DMA "
#endif
#ifdef PARITY
	         "PARITY "
#endif
#ifdef SUPPORT_TAGS
	         "SUPPORT_TAGS "
#endif
	         "");
}

/**
 * NCR5380_print_status - dump controller info
 * @instance: controller to dump
 *
 * Print commands in the various queues, called from NCR5380_abort
 * to aid debugging.
 */

static void lprint_Scsi_Cmnd(struct scsi_cmnd *cmd)
{
	int i, s;
	unsigned char *command;
	printk("scsi%d: destination target %d, lun %llu\n",
		H_NO(cmd), cmd->device->id, cmd->device->lun);
	printk(KERN_CONT "        command = ");
	command = cmd->cmnd;
	printk(KERN_CONT "%2d (0x%02x)", command[0], command[0]);
	for (i = 1, s = COMMAND_SIZE(command[0]); i < s; ++i)
		printk(KERN_CONT " %02x", command[i]);
	printk("\n");
}

static void NCR5380_print_status(struct Scsi_Host *instance)
{
	struct NCR5380_hostdata *hostdata;
	struct scsi_cmnd *ptr;
	unsigned long flags;

	NCR5380_dprint(NDEBUG_ANY, instance);
	NCR5380_dprint_phase(NDEBUG_ANY, instance);

	hostdata = (struct NCR5380_hostdata *)instance->hostdata;

	local_irq_save(flags);
	printk("NCR5380: coroutine is%s running.\n",
		hostdata->main_running ? "" : "n't");
	if (!hostdata->connected)
		printk("scsi%d: no currently connected command\n", HOSTNO);
	else
		lprint_Scsi_Cmnd((struct scsi_cmnd *) hostdata->connected);
	printk("scsi%d: issue_queue\n", HOSTNO);
	for (ptr = (struct scsi_cmnd *)hostdata->issue_queue; ptr; ptr = NEXT(ptr))
		lprint_Scsi_Cmnd(ptr);

	printk("scsi%d: disconnected_queue\n", HOSTNO);
	for (ptr = (struct scsi_cmnd *) hostdata->disconnected_queue; ptr;
	     ptr = NEXT(ptr))
		lprint_Scsi_Cmnd(ptr);

	local_irq_restore(flags);
	printk("\n");
}

static void show_Scsi_Cmnd(struct scsi_cmnd *cmd, struct seq_file *m)
{
	int i, s;
	unsigned char *command;
	seq_printf(m, "scsi%d: destination target %d, lun %llu\n",
		H_NO(cmd), cmd->device->id, cmd->device->lun);
	seq_puts(m, "        command = ");
	command = cmd->cmnd;
	seq_printf(m, "%2d (0x%02x)", command[0], command[0]);
	for (i = 1, s = COMMAND_SIZE(command[0]); i < s; ++i)
		seq_printf(m, " %02x", command[i]);
	seq_putc(m, '\n');
}

static int __maybe_unused NCR5380_show_info(struct seq_file *m,
                                            struct Scsi_Host *instance)
{
	struct NCR5380_hostdata *hostdata;
	struct scsi_cmnd *ptr;
	unsigned long flags;

	hostdata = (struct NCR5380_hostdata *)instance->hostdata;

	local_irq_save(flags);
	seq_printf(m, "NCR5380: coroutine is%s running.\n",
		hostdata->main_running ? "" : "n't");
	if (!hostdata->connected)
		seq_printf(m, "scsi%d: no currently connected command\n", HOSTNO);
	else
		show_Scsi_Cmnd((struct scsi_cmnd *) hostdata->connected, m);
	seq_printf(m, "scsi%d: issue_queue\n", HOSTNO);
	for (ptr = (struct scsi_cmnd *)hostdata->issue_queue; ptr; ptr = NEXT(ptr))
		show_Scsi_Cmnd(ptr, m);

	seq_printf(m, "scsi%d: disconnected_queue\n", HOSTNO);
	for (ptr = (struct scsi_cmnd *) hostdata->disconnected_queue; ptr;
	     ptr = NEXT(ptr))
		show_Scsi_Cmnd(ptr, m);

	local_irq_restore(flags);
	return 0;
}

/**
 * NCR5380_init - initialise an NCR5380
 * @instance: adapter to configure
 * @flags: control flags
 *
 * Initializes *instance and corresponding 5380 chip,
 * with flags OR'd into the initial flags value.
 *
 * Notes : I assume that the host, hostno, and id bits have been
 * set correctly. I don't care about the irq and other fields.
 *
 * Returns 0 for success
 */

static int __init NCR5380_init(struct Scsi_Host *instance, int flags)
{
	int i;
	SETUP_HOSTDATA(instance);

	hostdata->host = instance;
	hostdata->aborted = 0;
	hostdata->id_mask = 1 << instance->this_id;
	hostdata->id_higher_mask = 0;
	for (i = hostdata->id_mask; i <= 0x80; i <<= 1)
		if (i > hostdata->id_mask)
			hostdata->id_higher_mask |= i;
	for (i = 0; i < 8; ++i)
		hostdata->busy[i] = 0;
#ifdef SUPPORT_TAGS
	init_tags(hostdata);
#endif
#if defined (REAL_DMA)
	hostdata->dma_len = 0;
#endif
	hostdata->targets_present = 0;
	hostdata->connected = NULL;
	hostdata->issue_queue = NULL;
	hostdata->disconnected_queue = NULL;
	hostdata->flags = flags;

	INIT_WORK(&hostdata->main_task, NCR5380_main);

	prepare_info(instance);

	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
	NCR5380_write(MODE_REG, MR_BASE);
	NCR5380_write(TARGET_COMMAND_REG, 0);
	NCR5380_write(SELECT_ENABLE_REG, 0);

	return 0;
}

/**
 * NCR5380_exit - remove an NCR5380
 * @instance: adapter to remove
 *
 * Assumes that no more work can be queued (e.g. by NCR5380_intr).
 */

static void NCR5380_exit(struct Scsi_Host *instance)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);

	cancel_work_sync(&hostdata->main_task);
}

/**
 * NCR5380_queue_command - queue a command
 * @instance: the relevant SCSI adapter
 * @cmd: SCSI command
 *
 * cmd is added to the per instance issue_queue, with minor
 * twiddling done to the host specific fields of cmd.  If the
 * main coroutine is not running, it is restarted.
 */

static int NCR5380_queue_command(struct Scsi_Host *instance,
                                 struct scsi_cmnd *cmd)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);
	struct scsi_cmnd *tmp;
	unsigned long flags;

#if (NDEBUG & NDEBUG_NO_WRITE)
	switch (cmd->cmnd[0]) {
	case WRITE_6:
	case WRITE_10:
		printk(KERN_NOTICE "scsi%d: WRITE attempted with NO_WRITE debugging flag set\n",
		       H_NO(cmd));
		cmd->result = (DID_ERROR << 16);
		cmd->scsi_done(cmd);
		return 0;
	}
#endif /* (NDEBUG & NDEBUG_NO_WRITE) */

	/*
	 * We use the host_scribble field as a pointer to the next command
	 * in a queue
	 */

	SET_NEXT(cmd, NULL);
	cmd->result = 0;

	/*
	 * Insert the cmd into the issue queue. Note that REQUEST SENSE
	 * commands are added to the head of the queue since any command will
	 * clear the contingent allegiance condition that exists and the
	 * sense data is only guaranteed to be valid while the condition exists.
	 */

	/* ++guenther: now that the issue queue is being set up, we can lock ST-DMA.
	 * Otherwise a running NCR5380_main may steal the lock.
	 * Lock before actually inserting due to fairness reasons explained in
	 * atari_scsi.c. If we insert first, then it's impossible for this driver
	 * to release the lock.
	 * Stop timer for this command while waiting for the lock, or timeouts
	 * may happen (and they really do), and it's no good if the command doesn't
	 * appear in any of the queues.
	 * ++roman: Just disabling the NCR interrupt isn't sufficient here,
	 * because also a timer int can trigger an abort or reset, which would
	 * alter queues and touch the lock.
	 */
	if (!NCR5380_acquire_dma_irq(instance))
		return SCSI_MLQUEUE_HOST_BUSY;

	local_irq_save(flags);

	/*
	 * Insert the cmd into the issue queue. Note that REQUEST SENSE
	 * commands are added to the head of the queue since any command will
	 * clear the contingent allegiance condition that exists and the
	 * sense data is only guaranteed to be valid while the condition exists.
	 */

	if (!(hostdata->issue_queue) || (cmd->cmnd[0] == REQUEST_SENSE)) {
		LIST(cmd, hostdata->issue_queue);
		SET_NEXT(cmd, hostdata->issue_queue);
		hostdata->issue_queue = cmd;
	} else {
		for (tmp = (struct scsi_cmnd *)hostdata->issue_queue;
		     NEXT(tmp); tmp = NEXT(tmp))
			;
		LIST(cmd, tmp);
		SET_NEXT(tmp, cmd);
	}
	local_irq_restore(flags);

	dprintk(NDEBUG_QUEUES, "scsi%d: command added to %s of queue\n", H_NO(cmd),
		  (cmd->cmnd[0] == REQUEST_SENSE) ? "head" : "tail");

	/* If queue_command() is called from an interrupt (real one or bottom
	 * half), we let queue_main() do the job of taking care about main. If it
	 * is already running, this is a no-op, else main will be queued.
	 *
	 * If we're not in an interrupt, we can call NCR5380_main()
	 * unconditionally, because it cannot be already running.
	 */
	if (in_interrupt() || irqs_disabled())
		queue_main(hostdata);
	else
		NCR5380_main(&hostdata->main_task);
	return 0;
}

static inline void maybe_release_dma_irq(struct Scsi_Host *instance)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);

	/* Caller does the locking needed to set & test these data atomically */
	if (!hostdata->disconnected_queue &&
	    !hostdata->issue_queue &&
	    !hostdata->connected &&
	    !hostdata->retain_dma_intr)
		NCR5380_release_dma_irq(instance);
}

/**
 * NCR5380_main - NCR state machines
 *
 * NCR5380_main is a coroutine that runs as long as more work can
 * be done on the NCR5380 host adapters in a system.  Both
 * NCR5380_queue_command() and NCR5380_intr() will try to start it
 * in case it is not running.
 *
 * Locks: called as its own thread with no locks held.
 */

static void NCR5380_main(struct work_struct *work)
{
	struct NCR5380_hostdata *hostdata =
		container_of(work, struct NCR5380_hostdata, main_task);
	struct Scsi_Host *instance = hostdata->host;
	struct scsi_cmnd *tmp, *prev;
	int done;
	unsigned long flags;

	/*
	 * We run (with interrupts disabled) until we're sure that none of
	 * the host adapters have anything that can be done, at which point
	 * we set main_running to 0 and exit.
	 *
	 * Interrupts are enabled before doing various other internal
	 * instructions, after we've decided that we need to run through
	 * the loop again.
	 *
	 * this should prevent any race conditions.
	 *
	 * ++roman: Just disabling the NCR interrupt isn't sufficient here,
	 * because also a timer int can trigger an abort or reset, which can
	 * alter queues and touch the Falcon lock.
	 */

	/* Tell int handlers main() is now already executing.  Note that
	   no races are possible here. If an int comes in before
	   'main_running' is set here, and queues/executes main via the
	   task queue, it doesn't do any harm, just this instance of main
	   won't find any work left to do. */
	if (hostdata->main_running)
		return;
	hostdata->main_running = 1;

	local_save_flags(flags);
	do {
		local_irq_disable();	/* Freeze request queues */
		done = 1;

		if (!hostdata->connected) {
			dprintk(NDEBUG_MAIN, "scsi%d: not connected\n", HOSTNO);
			/*
			 * Search through the issue_queue for a command destined
			 * for a target that's not busy.
			 */
#if (NDEBUG & NDEBUG_LISTS)
			for (tmp = (struct scsi_cmnd *) hostdata->issue_queue, prev = NULL;
			     tmp && (tmp != prev); prev = tmp, tmp = NEXT(tmp))
				;
			/*printk("%p  ", tmp);*/
			if ((tmp == prev) && tmp)
				printk(" LOOP\n");
			/* else printk("\n"); */
#endif
			for (tmp = (struct scsi_cmnd *) hostdata->issue_queue,
			     prev = NULL; tmp; prev = tmp, tmp = NEXT(tmp)) {
				u8 lun = tmp->device->lun;

				dprintk(NDEBUG_LISTS,
				        "MAIN tmp=%p target=%d busy=%d lun=%d\n",
				        tmp, scmd_id(tmp), hostdata->busy[scmd_id(tmp)],
				        lun);
				/*  When we find one, remove it from the issue queue. */
				/* ++guenther: possible race with Falcon locking */
				if (
#ifdef SUPPORT_TAGS
				    !is_lun_busy( tmp, tmp->cmnd[0] != REQUEST_SENSE)
#else
				    !(hostdata->busy[tmp->device->id] & (1 << lun))
#endif
				    ) {
					/* ++guenther: just to be sure, this must be atomic */
					local_irq_disable();
					if (prev) {
						REMOVE(prev, NEXT(prev), tmp, NEXT(tmp));
						SET_NEXT(prev, NEXT(tmp));
					} else {
						REMOVE(-1, hostdata->issue_queue, tmp, NEXT(tmp));
						hostdata->issue_queue = NEXT(tmp);
					}
					SET_NEXT(tmp, NULL);
					hostdata->retain_dma_intr++;

					/* reenable interrupts after finding one */
					local_irq_restore(flags);

					/*
					 * Attempt to establish an I_T_L nexus here.
					 * On success, instance->hostdata->connected is set.
					 * On failure, we must add the command back to the
					 *   issue queue so we can keep trying.
					 */
					dprintk(NDEBUG_MAIN, "scsi%d: main(): command for target %d "
						    "lun %d removed from issue_queue\n",
						    HOSTNO, tmp->device->id, lun);
					/*
					 * REQUEST SENSE commands are issued without tagged
					 * queueing, even on SCSI-II devices because the
					 * contingent allegiance condition exists for the
					 * entire unit.
					 */
					/* ++roman: ...and the standard also requires that
					 * REQUEST SENSE command are untagged.
					 */

#ifdef SUPPORT_TAGS
					cmd_get_tag(tmp, tmp->cmnd[0] != REQUEST_SENSE);
#endif
					if (!NCR5380_select(instance, tmp)) {
						local_irq_disable();
						hostdata->retain_dma_intr--;
						/* release if target did not response! */
						maybe_release_dma_irq(instance);
						local_irq_restore(flags);
						break;
					} else {
						local_irq_disable();
						LIST(tmp, hostdata->issue_queue);
						SET_NEXT(tmp, hostdata->issue_queue);
						hostdata->issue_queue = tmp;
#ifdef SUPPORT_TAGS
						cmd_free_tag(tmp);
#endif
						hostdata->retain_dma_intr--;
						local_irq_restore(flags);
						dprintk(NDEBUG_MAIN, "scsi%d: main(): select() failed, "
							    "returned to issue_queue\n", HOSTNO);
						if (hostdata->connected)
							break;
					}
				} /* if target/lun/target queue is not busy */
			} /* for issue_queue */
		} /* if (!hostdata->connected) */

		if (hostdata->connected
#ifdef REAL_DMA
		    && !hostdata->dma_len
#endif
		    ) {
			local_irq_restore(flags);
			dprintk(NDEBUG_MAIN, "scsi%d: main: performing information transfer\n",
				    HOSTNO);
			NCR5380_information_transfer(instance);
			dprintk(NDEBUG_MAIN, "scsi%d: main: done set false\n", HOSTNO);
			done = 0;
		}
	} while (!done);

	/* Better allow ints _after_ 'main_running' has been cleared, else
	   an interrupt could believe we'll pick up the work it left for
	   us, but we won't see it anymore here... */
	hostdata->main_running = 0;
	local_irq_restore(flags);
}


#ifdef REAL_DMA
/*
 * Function : void NCR5380_dma_complete (struct Scsi_Host *instance)
 *
 * Purpose : Called by interrupt handler when DMA finishes or a phase
 *	mismatch occurs (which would finish the DMA transfer).
 *
 * Inputs : instance - this instance of the NCR5380.
 *
 */

static void NCR5380_dma_complete(struct Scsi_Host *instance)
{
	SETUP_HOSTDATA(instance);
	int transferred;
	unsigned char **data;
	volatile int *count;
	int saved_data = 0, overrun = 0;
	unsigned char p;

	if (!hostdata->connected) {
		printk(KERN_WARNING "scsi%d: received end of DMA interrupt with "
		       "no connected cmd\n", HOSTNO);
		return;
	}

	if (hostdata->read_overruns) {
		p = hostdata->connected->SCp.phase;
		if (p & SR_IO) {
			udelay(10);
			if ((NCR5380_read(BUS_AND_STATUS_REG) &
			     (BASR_PHASE_MATCH|BASR_ACK)) ==
			    (BASR_PHASE_MATCH|BASR_ACK)) {
				saved_data = NCR5380_read(INPUT_DATA_REG);
				overrun = 1;
				dprintk(NDEBUG_DMA, "scsi%d: read overrun handled\n", HOSTNO);
			}
		}
	}

	dprintk(NDEBUG_DMA, "scsi%d: real DMA transfer complete, basr 0x%X, sr 0x%X\n",
		   HOSTNO, NCR5380_read(BUS_AND_STATUS_REG),
		   NCR5380_read(STATUS_REG));

#if defined(CONFIG_SUN3)
	if ((sun3scsi_dma_finish(rq_data_dir(hostdata->connected->request)))) {
		pr_err("scsi%d: overrun in UDC counter -- not prepared to deal with this!\n",
		       instance->host_no);
		BUG();
	}

	/* make sure we're not stuck in a data phase */
	if ((NCR5380_read(BUS_AND_STATUS_REG) & (BASR_PHASE_MATCH | BASR_ACK)) ==
	    (BASR_PHASE_MATCH | BASR_ACK)) {
		pr_err("scsi%d: BASR %02x\n", instance->host_no,
		       NCR5380_read(BUS_AND_STATUS_REG));
		pr_err("scsi%d: bus stuck in data phase -- probably a single byte overrun!\n",
		       instance->host_no);
		BUG();
	}
#endif

	(void)NCR5380_read(RESET_PARITY_INTERRUPT_REG);
	NCR5380_write(MODE_REG, MR_BASE);
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);

	transferred = hostdata->dma_len - NCR5380_dma_residual(instance);
	hostdata->dma_len = 0;

	data = (unsigned char **)&hostdata->connected->SCp.ptr;
	count = &hostdata->connected->SCp.this_residual;
	*data += transferred;
	*count -= transferred;

	if (hostdata->read_overruns) {
		int cnt, toPIO;

		if ((NCR5380_read(STATUS_REG) & PHASE_MASK) == p && (p & SR_IO)) {
			cnt = toPIO = hostdata->read_overruns;
			if (overrun) {
				dprintk(NDEBUG_DMA, "Got an input overrun, using saved byte\n");
				*(*data)++ = saved_data;
				(*count)--;
				cnt--;
				toPIO--;
			}
			dprintk(NDEBUG_DMA, "Doing %d-byte PIO to 0x%08lx\n", cnt, (long)*data);
			NCR5380_transfer_pio(instance, &p, &cnt, data);
			*count -= toPIO - cnt;
		}
	}
}
#endif /* REAL_DMA */


/**
 * NCR5380_intr - generic NCR5380 irq handler
 * @irq: interrupt number
 * @dev_id: device info
 *
 * Handle interrupts, reestablishing I_T_L or I_T_L_Q nexuses
 * from the disconnected queue, and restarting NCR5380_main()
 * as required.
 */

static irqreturn_t NCR5380_intr(int irq, void *dev_id)
{
	struct Scsi_Host *instance = dev_id;
	int done = 1, handled = 0;
	unsigned char basr;

	dprintk(NDEBUG_INTR, "scsi%d: NCR5380 irq triggered\n", HOSTNO);

	/* Look for pending interrupts */
	basr = NCR5380_read(BUS_AND_STATUS_REG);
	dprintk(NDEBUG_INTR, "scsi%d: BASR=%02x\n", HOSTNO, basr);
	/* dispatch to appropriate routine if found and done=0 */
	if (basr & BASR_IRQ) {
		NCR5380_dprint(NDEBUG_INTR, instance);
		if ((NCR5380_read(STATUS_REG) & (SR_SEL|SR_IO)) == (SR_SEL|SR_IO)) {
			done = 0;
			dprintk(NDEBUG_INTR, "scsi%d: SEL interrupt\n", HOSTNO);
			NCR5380_reselect(instance);
			(void)NCR5380_read(RESET_PARITY_INTERRUPT_REG);
		} else if (basr & BASR_PARITY_ERROR) {
			dprintk(NDEBUG_INTR, "scsi%d: PARITY interrupt\n", HOSTNO);
			(void)NCR5380_read(RESET_PARITY_INTERRUPT_REG);
		} else if ((NCR5380_read(STATUS_REG) & SR_RST) == SR_RST) {
			dprintk(NDEBUG_INTR, "scsi%d: RESET interrupt\n", HOSTNO);
			(void)NCR5380_read(RESET_PARITY_INTERRUPT_REG);
		} else {
			/*
			 * The rest of the interrupt conditions can occur only during a
			 * DMA transfer
			 */

#if defined(REAL_DMA)
			/*
			 * We should only get PHASE MISMATCH and EOP interrupts if we have
			 * DMA enabled, so do a sanity check based on the current setting
			 * of the MODE register.
			 */

			if ((NCR5380_read(MODE_REG) & MR_DMA_MODE) &&
			    ((basr & BASR_END_DMA_TRANSFER) ||
			     !(basr & BASR_PHASE_MATCH))) {

				dprintk(NDEBUG_INTR, "scsi%d: PHASE MISM or EOP interrupt\n", HOSTNO);
				NCR5380_dma_complete( instance );
				done = 0;
			} else
#endif /* REAL_DMA */
			{
/* MS: Ignore unknown phase mismatch interrupts (caused by EOP interrupt) */
				if (basr & BASR_PHASE_MATCH)
					dprintk(NDEBUG_INTR, "scsi%d: unknown interrupt, "
					       "BASR 0x%x, MR 0x%x, SR 0x%x\n",
					       HOSTNO, basr, NCR5380_read(MODE_REG),
					       NCR5380_read(STATUS_REG));
				(void)NCR5380_read(RESET_PARITY_INTERRUPT_REG);
#ifdef SUN3_SCSI_VME
				dregs->csr |= CSR_DMA_ENABLE;
#endif
			}
		} /* if !(SELECTION || PARITY) */
		handled = 1;
	} /* BASR & IRQ */ else {
		printk(KERN_NOTICE "scsi%d: interrupt without IRQ bit set in BASR, "
		       "BASR 0x%X, MR 0x%X, SR 0x%x\n", HOSTNO, basr,
		       NCR5380_read(MODE_REG), NCR5380_read(STATUS_REG));
		(void)NCR5380_read(RESET_PARITY_INTERRUPT_REG);
#ifdef SUN3_SCSI_VME
		dregs->csr |= CSR_DMA_ENABLE;
#endif
	}

	if (!done) {
		dprintk(NDEBUG_INTR, "scsi%d: in int routine, calling main\n", HOSTNO);
		/* Put a call to NCR5380_main() on the queue... */
		queue_main(shost_priv(instance));
	}
	return IRQ_RETVAL(handled);
}

/*
 * Function : int NCR5380_select(struct Scsi_Host *instance,
 *                               struct scsi_cmnd *cmd)
 *
 * Purpose : establishes I_T_L or I_T_L_Q nexus for new or existing command,
 *	including ARBITRATION, SELECTION, and initial message out for
 *	IDENTIFY and queue messages.
 *
 * Inputs : instance - instantiation of the 5380 driver on which this
 *	target lives, cmd - SCSI command to execute.
 *
 * Returns : -1 if selection could not execute for some reason,
 *	0 if selection succeeded or failed because the target
 *	did not respond.
 *
 * Side effects :
 *	If bus busy, arbitration failed, etc, NCR5380_select() will exit
 *		with registers as they should have been on entry - ie
 *		SELECT_ENABLE will be set appropriately, the NCR5380
 *		will cease to drive any SCSI bus signals.
 *
 *	If successful : I_T_L or I_T_L_Q nexus will be established,
 *		instance->connected will be set to cmd.
 *		SELECT interrupt will be disabled.
 *
 *	If failed (no target) : cmd->scsi_done() will be called, and the
 *		cmd->result host byte set to DID_BAD_TARGET.
 */

static int NCR5380_select(struct Scsi_Host *instance, struct scsi_cmnd *cmd)
{
	SETUP_HOSTDATA(instance);
	unsigned char tmp[3], phase;
	unsigned char *data;
	int len;
	unsigned long timeout;
	unsigned long flags;

	hostdata->restart_select = 0;
	NCR5380_dprint(NDEBUG_ARBITRATION, instance);
	dprintk(NDEBUG_ARBITRATION, "scsi%d: starting arbitration, id = %d\n", HOSTNO,
		   instance->this_id);

	/*
	 * Set the phase bits to 0, otherwise the NCR5380 won't drive the
	 * data bus during SELECTION.
	 */

	local_irq_save(flags);
	if (hostdata->connected) {
		local_irq_restore(flags);
		return -1;
	}
	NCR5380_write(TARGET_COMMAND_REG, 0);

	/*
	 * Start arbitration.
	 */

	NCR5380_write(OUTPUT_DATA_REG, hostdata->id_mask);
	NCR5380_write(MODE_REG, MR_ARBITRATE);

	local_irq_restore(flags);

	/* Wait for arbitration logic to complete */
#if defined(NCR_TIMEOUT)
	{
		unsigned long timeout = jiffies + 2*NCR_TIMEOUT;

		while (!(NCR5380_read(INITIATOR_COMMAND_REG) & ICR_ARBITRATION_PROGRESS) &&
		       time_before(jiffies, timeout) && !hostdata->connected)
			;
		if (time_after_eq(jiffies, timeout)) {
			printk("scsi : arbitration timeout at %d\n", __LINE__);
			NCR5380_write(MODE_REG, MR_BASE);
			NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
			return -1;
		}
	}
#else /* NCR_TIMEOUT */
	while (!(NCR5380_read(INITIATOR_COMMAND_REG) & ICR_ARBITRATION_PROGRESS) &&
	       !hostdata->connected)
		;
#endif

	dprintk(NDEBUG_ARBITRATION, "scsi%d: arbitration complete\n", HOSTNO);

	if (hostdata->connected) {
		NCR5380_write(MODE_REG, MR_BASE);
		return -1;
	}
	/*
	 * The arbitration delay is 2.2us, but this is a minimum and there is
	 * no maximum so we can safely sleep for ceil(2.2) usecs to accommodate
	 * the integral nature of udelay().
	 *
	 */

	udelay(3);

	/* Check for lost arbitration */
	if ((NCR5380_read(INITIATOR_COMMAND_REG) & ICR_ARBITRATION_LOST) ||
	    (NCR5380_read(CURRENT_SCSI_DATA_REG) & hostdata->id_higher_mask) ||
	    (NCR5380_read(INITIATOR_COMMAND_REG) & ICR_ARBITRATION_LOST) ||
	    hostdata->connected) {
		NCR5380_write(MODE_REG, MR_BASE);
		dprintk(NDEBUG_ARBITRATION, "scsi%d: lost arbitration, deasserting MR_ARBITRATE\n",
			   HOSTNO);
		return -1;
	}

	/* after/during arbitration, BSY should be asserted.
	   IBM DPES-31080 Version S31Q works now */
	/* Tnx to Thomas_Roesch@m2.maus.de for finding this! (Roman) */
	NCR5380_write(INITIATOR_COMMAND_REG,
		      ICR_BASE | ICR_ASSERT_SEL | ICR_ASSERT_BSY);

	if ((NCR5380_read(INITIATOR_COMMAND_REG) & ICR_ARBITRATION_LOST) ||
	    hostdata->connected) {
		NCR5380_write(MODE_REG, MR_BASE);
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
		dprintk(NDEBUG_ARBITRATION, "scsi%d: lost arbitration, deasserting ICR_ASSERT_SEL\n",
			   HOSTNO);
		return -1;
	}

	/*
	 * Again, bus clear + bus settle time is 1.2us, however, this is
	 * a minimum so we'll udelay ceil(1.2)
	 */

#ifdef CONFIG_ATARI_SCSI_TOSHIBA_DELAY
	/* ++roman: But some targets (see above :-) seem to need a bit more... */
	udelay(15);
#else
	udelay(2);
#endif

	if (hostdata->connected) {
		NCR5380_write(MODE_REG, MR_BASE);
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
		return -1;
	}

	dprintk(NDEBUG_ARBITRATION, "scsi%d: won arbitration\n", HOSTNO);

	/*
	 * Now that we have won arbitration, start Selection process, asserting
	 * the host and target ID's on the SCSI bus.
	 */

	NCR5380_write(OUTPUT_DATA_REG, (hostdata->id_mask | (1 << cmd->device->id)));

	/*
	 * Raise ATN while SEL is true before BSY goes false from arbitration,
	 * since this is the only way to guarantee that we'll get a MESSAGE OUT
	 * phase immediately after selection.
	 */

	NCR5380_write(INITIATOR_COMMAND_REG, (ICR_BASE | ICR_ASSERT_BSY |
		      ICR_ASSERT_DATA | ICR_ASSERT_ATN | ICR_ASSERT_SEL ));
	NCR5380_write(MODE_REG, MR_BASE);

	/*
	 * Reselect interrupts must be turned off prior to the dropping of BSY,
	 * otherwise we will trigger an interrupt.
	 */

	if (hostdata->connected) {
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
		return -1;
	}

	NCR5380_write(SELECT_ENABLE_REG, 0);

	/*
	 * The initiator shall then wait at least two deskew delays and release
	 * the BSY signal.
	 */
	udelay(1);        /* wingel -- wait two bus deskew delay >2*45ns */

	/* Reset BSY */
	NCR5380_write(INITIATOR_COMMAND_REG, (ICR_BASE | ICR_ASSERT_DATA |
		      ICR_ASSERT_ATN | ICR_ASSERT_SEL));

	/*
	 * Something weird happens when we cease to drive BSY - looks
	 * like the board/chip is letting us do another read before the
	 * appropriate propagation delay has expired, and we're confusing
	 * a BSY signal from ourselves as the target's response to SELECTION.
	 *
	 * A small delay (the 'C++' frontend breaks the pipeline with an
	 * unnecessary jump, making it work on my 386-33/Trantor T128, the
	 * tighter 'C' code breaks and requires this) solves the problem -
	 * the 1 us delay is arbitrary, and only used because this delay will
	 * be the same on other platforms and since it works here, it should
	 * work there.
	 *
	 * wingel suggests that this could be due to failing to wait
	 * one deskew delay.
	 */

	udelay(1);

	dprintk(NDEBUG_SELECTION, "scsi%d: selecting target %d\n", HOSTNO, cmd->device->id);

	/*
	 * The SCSI specification calls for a 250 ms timeout for the actual
	 * selection.
	 */

	timeout = jiffies + (250 * HZ / 1000);

	/*
	 * XXX very interesting - we're seeing a bounce where the BSY we
	 * asserted is being reflected / still asserted (propagation delay?)
	 * and it's detecting as true.  Sigh.
	 */

#if 0
	/* ++roman: If a target conformed to the SCSI standard, it wouldn't assert
	 * IO while SEL is true. But again, there are some disks out the in the
	 * world that do that nevertheless. (Somebody claimed that this announces
	 * reselection capability of the target.) So we better skip that test and
	 * only wait for BSY... (Famous german words: Der Klügere gibt nach :-)
	 */

	while (time_before(jiffies, timeout) &&
	       !(NCR5380_read(STATUS_REG) & (SR_BSY | SR_IO)))
		;

	if ((NCR5380_read(STATUS_REG) & (SR_SEL | SR_IO)) == (SR_SEL | SR_IO)) {
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
		NCR5380_reselect(instance);
		printk(KERN_ERR "scsi%d: reselection after won arbitration?\n",
		       HOSTNO);
		NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
		return -1;
	}
#else
	while (time_before(jiffies, timeout) && !(NCR5380_read(STATUS_REG) & SR_BSY))
		;
#endif

	/*
	 * No less than two deskew delays after the initiator detects the
	 * BSY signal is true, it shall release the SEL signal and may
	 * change the DATA BUS.                                     -wingel
	 */

	udelay(1);

	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ATN);

	if (!(NCR5380_read(STATUS_REG) & SR_BSY)) {
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
		if (hostdata->targets_present & (1 << cmd->device->id)) {
			printk(KERN_ERR "scsi%d: weirdness\n", HOSTNO);
			if (hostdata->restart_select)
				printk(KERN_NOTICE "\trestart select\n");
			NCR5380_dprint(NDEBUG_ANY, instance);
			NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
			return -1;
		}
		cmd->result = DID_BAD_TARGET << 16;
#ifdef SUPPORT_TAGS
		cmd_free_tag(cmd);
#endif
		cmd->scsi_done(cmd);
		NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
		dprintk(NDEBUG_SELECTION, "scsi%d: target did not respond within 250ms\n", HOSTNO);
		NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
		return 0;
	}

	hostdata->targets_present |= (1 << cmd->device->id);

	/*
	 * Since we followed the SCSI spec, and raised ATN while SEL
	 * was true but before BSY was false during selection, the information
	 * transfer phase should be a MESSAGE OUT phase so that we can send the
	 * IDENTIFY message.
	 *
	 * If SCSI-II tagged queuing is enabled, we also send a SIMPLE_QUEUE_TAG
	 * message (2 bytes) with a tag ID that we increment with every command
	 * until it wraps back to 0.
	 *
	 * XXX - it turns out that there are some broken SCSI-II devices,
	 *	     which claim to support tagged queuing but fail when more than
	 *	     some number of commands are issued at once.
	 */

	/* Wait for start of REQ/ACK handshake */
	while (!(NCR5380_read(STATUS_REG) & SR_REQ))
		;

	dprintk(NDEBUG_SELECTION, "scsi%d: target %d selected, going into MESSAGE OUT phase.\n",
		   HOSTNO, cmd->device->id);
	tmp[0] = IDENTIFY(1, cmd->device->lun);

#ifdef SUPPORT_TAGS
	if (cmd->tag != TAG_NONE) {
		tmp[1] = hostdata->last_message = SIMPLE_QUEUE_TAG;
		tmp[2] = cmd->tag;
		len = 3;
	} else
		len = 1;
#else
	len = 1;
	cmd->tag = 0;
#endif /* SUPPORT_TAGS */

	/* Send message(s) */
	data = tmp;
	phase = PHASE_MSGOUT;
	NCR5380_transfer_pio(instance, &phase, &len, &data);
	dprintk(NDEBUG_SELECTION, "scsi%d: nexus established.\n", HOSTNO);
	/* XXX need to handle errors here */
	hostdata->connected = cmd;
#ifndef SUPPORT_TAGS
	hostdata->busy[cmd->device->id] |= (1 << cmd->device->lun);
#endif
#ifdef SUN3_SCSI_VME
	dregs->csr |= CSR_INTR;
#endif

	initialize_SCp(cmd);

	return 0;
}

/*
 * Function : int NCR5380_transfer_pio (struct Scsi_Host *instance,
 *      unsigned char *phase, int *count, unsigned char **data)
 *
 * Purpose : transfers data in given phase using polled I/O
 *
 * Inputs : instance - instance of driver, *phase - pointer to
 *	what phase is expected, *count - pointer to number of
 *	bytes to transfer, **data - pointer to data pointer.
 *
 * Returns : -1 when different phase is entered without transferring
 *	maximum number of bytes, 0 if all bytes are transferred or exit
 *	is in same phase.
 *
 *	Also, *phase, *count, *data are modified in place.
 *
 * XXX Note : handling for bus free may be useful.
 */

/*
 * Note : this code is not as quick as it could be, however it
 * IS 100% reliable, and for the actual data transfer where speed
 * counts, we will always do a pseudo DMA or DMA transfer.
 */

static int NCR5380_transfer_pio(struct Scsi_Host *instance,
				unsigned char *phase, int *count,
				unsigned char **data)
{
	register unsigned char p = *phase, tmp;
	register int c = *count;
	register unsigned char *d = *data;

	/*
	 * The NCR5380 chip will only drive the SCSI bus when the
	 * phase specified in the appropriate bits of the TARGET COMMAND
	 * REGISTER match the STATUS REGISTER
	 */

	NCR5380_write(TARGET_COMMAND_REG, PHASE_SR_TO_TCR(p));

	do {
		/*
		 * Wait for assertion of REQ, after which the phase bits will be
		 * valid
		 */
		while (!((tmp = NCR5380_read(STATUS_REG)) & SR_REQ))
			;

		dprintk(NDEBUG_HANDSHAKE, "scsi%d: REQ detected\n", HOSTNO);

		/* Check for phase mismatch */
		if ((tmp & PHASE_MASK) != p) {
			dprintk(NDEBUG_PIO, "scsi%d: phase mismatch\n", HOSTNO);
			NCR5380_dprint_phase(NDEBUG_PIO, instance);
			break;
		}

		/* Do actual transfer from SCSI bus to / from memory */
		if (!(p & SR_IO))
			NCR5380_write(OUTPUT_DATA_REG, *d);
		else
			*d = NCR5380_read(CURRENT_SCSI_DATA_REG);

		++d;

		/*
		 * The SCSI standard suggests that in MSGOUT phase, the initiator
		 * should drop ATN on the last byte of the message phase
		 * after REQ has been asserted for the handshake but before
		 * the initiator raises ACK.
		 */

		if (!(p & SR_IO)) {
			if (!((p & SR_MSG) && c > 1)) {
				NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_DATA);
				NCR5380_dprint(NDEBUG_PIO, instance);
				NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE |
					      ICR_ASSERT_DATA | ICR_ASSERT_ACK);
			} else {
				NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE |
					      ICR_ASSERT_DATA | ICR_ASSERT_ATN);
				NCR5380_dprint(NDEBUG_PIO, instance);
				NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE |
					      ICR_ASSERT_DATA | ICR_ASSERT_ATN | ICR_ASSERT_ACK);
			}
		} else {
			NCR5380_dprint(NDEBUG_PIO, instance);
			NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ACK);
		}

		while (NCR5380_read(STATUS_REG) & SR_REQ)
			;

		dprintk(NDEBUG_HANDSHAKE, "scsi%d: req false, handshake complete\n", HOSTNO);

		/*
		 * We have several special cases to consider during REQ/ACK handshaking :
		 * 1.  We were in MSGOUT phase, and we are on the last byte of the
		 *	message.  ATN must be dropped as ACK is dropped.
		 *
		 * 2.  We are in a MSGIN phase, and we are on the last byte of the
		 *	message.  We must exit with ACK asserted, so that the calling
		 *	code may raise ATN before dropping ACK to reject the message.
		 *
		 * 3.  ACK and ATN are clear and the target may proceed as normal.
		 */
		if (!(p == PHASE_MSGIN && c == 1)) {
			if (p == PHASE_MSGOUT && c > 1)
				NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ATN);
			else
				NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
		}
	} while (--c);

	dprintk(NDEBUG_PIO, "scsi%d: residual %d\n", HOSTNO, c);

	*count = c;
	*data = d;
	tmp = NCR5380_read(STATUS_REG);
	/* The phase read from the bus is valid if either REQ is (already)
	 * asserted or if ACK hasn't been released yet. The latter is the case if
	 * we're in MSGIN and all wanted bytes have been received.
	 */
	if ((tmp & SR_REQ) || (p == PHASE_MSGIN && c == 0))
		*phase = tmp & PHASE_MASK;
	else
		*phase = PHASE_UNKNOWN;

	if (!c || (*phase == p))
		return 0;
	else
		return -1;
}

/*
 * Function : do_abort (Scsi_Host *host)
 *
 * Purpose : abort the currently established nexus.  Should only be
 *	called from a routine which can drop into a
 *
 * Returns : 0 on success, -1 on failure.
 */

static int do_abort(struct Scsi_Host *instance)
{
	unsigned char tmp, *msgptr, phase;
	int len;

	/* Request message out phase */
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ATN);

	/*
	 * Wait for the target to indicate a valid phase by asserting
	 * REQ.  Once this happens, we'll have either a MSGOUT phase
	 * and can immediately send the ABORT message, or we'll have some
	 * other phase and will have to source/sink data.
	 *
	 * We really don't care what value was on the bus or what value
	 * the target sees, so we just handshake.
	 */

	while (!((tmp = NCR5380_read(STATUS_REG)) & SR_REQ))
		;

	NCR5380_write(TARGET_COMMAND_REG, PHASE_SR_TO_TCR(tmp));

	if ((tmp & PHASE_MASK) != PHASE_MSGOUT) {
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ATN |
			      ICR_ASSERT_ACK);
		while (NCR5380_read(STATUS_REG) & SR_REQ)
			;
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ATN);
	}

	tmp = ABORT;
	msgptr = &tmp;
	len = 1;
	phase = PHASE_MSGOUT;
	NCR5380_transfer_pio(instance, &phase, &len, &msgptr);

	/*
	 * If we got here, and the command completed successfully,
	 * we're about to go into bus free state.
	 */

	return len ? -1 : 0;
}

#if defined(REAL_DMA)
/*
 * Function : int NCR5380_transfer_dma (struct Scsi_Host *instance,
 *      unsigned char *phase, int *count, unsigned char **data)
 *
 * Purpose : transfers data in given phase using either real
 *	or pseudo DMA.
 *
 * Inputs : instance - instance of driver, *phase - pointer to
 *	what phase is expected, *count - pointer to number of
 *	bytes to transfer, **data - pointer to data pointer.
 *
 * Returns : -1 when different phase is entered without transferring
 *	maximum number of bytes, 0 if all bytes or transferred or exit
 *	is in same phase.
 *
 *	Also, *phase, *count, *data are modified in place.
 *
 */


static int NCR5380_transfer_dma(struct Scsi_Host *instance,
				unsigned char *phase, int *count,
				unsigned char **data)
{
	SETUP_HOSTDATA(instance);
	register int c = *count;
	register unsigned char p = *phase;
	unsigned long flags;

#if defined(CONFIG_SUN3)
	/* sanity check */
	if (!sun3_dma_setup_done) {
		pr_err("scsi%d: transfer_dma without setup!\n",
		       instance->host_no);
		BUG();
	}
	hostdata->dma_len = c;

	dprintk(NDEBUG_DMA, "scsi%d: initializing DMA for %s, %d bytes %s %p\n",
		instance->host_no, (p & SR_IO) ? "reading" : "writing",
		c, (p & SR_IO) ? "to" : "from", *data);

	/* netbsd turns off ints here, why not be safe and do it too */
	local_irq_save(flags);

	/* send start chain */
	sun3scsi_dma_start(c, *data);

	if (p & SR_IO) {
		NCR5380_write(TARGET_COMMAND_REG, 1);
		NCR5380_read(RESET_PARITY_INTERRUPT_REG);
		NCR5380_write(INITIATOR_COMMAND_REG, 0);
		NCR5380_write(MODE_REG,
			      (NCR5380_read(MODE_REG) | MR_DMA_MODE | MR_ENABLE_EOP_INTR));
		NCR5380_write(START_DMA_INITIATOR_RECEIVE_REG, 0);
	} else {
		NCR5380_write(TARGET_COMMAND_REG, 0);
		NCR5380_read(RESET_PARITY_INTERRUPT_REG);
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_ASSERT_DATA);
		NCR5380_write(MODE_REG,
			      (NCR5380_read(MODE_REG) | MR_DMA_MODE | MR_ENABLE_EOP_INTR));
		NCR5380_write(START_DMA_SEND_REG, 0);
	}

#ifdef SUN3_SCSI_VME
	dregs->csr |= CSR_DMA_ENABLE;
#endif

	local_irq_restore(flags);

	sun3_dma_active = 1;

#else /* !defined(CONFIG_SUN3) */
	register unsigned char *d = *data;
	unsigned char tmp;

	if ((tmp = (NCR5380_read(STATUS_REG) & PHASE_MASK)) != p) {
		*phase = tmp;
		return -1;
	}

	if (hostdata->read_overruns && (p & SR_IO))
		c -= hostdata->read_overruns;

	dprintk(NDEBUG_DMA, "scsi%d: initializing DMA for %s, %d bytes %s %p\n",
		   HOSTNO, (p & SR_IO) ? "reading" : "writing",
		   c, (p & SR_IO) ? "to" : "from", d);

	NCR5380_write(TARGET_COMMAND_REG, PHASE_SR_TO_TCR(p));

#ifdef REAL_DMA
	NCR5380_write(MODE_REG, MR_BASE | MR_DMA_MODE | MR_ENABLE_EOP_INTR | MR_MONITOR_BSY);
#endif /* def REAL_DMA  */

	if (!(hostdata->flags & FLAG_LATE_DMA_SETUP)) {
		/* On the Medusa, it is a must to initialize the DMA before
		 * starting the NCR. This is also the cleaner way for the TT.
		 */
		local_irq_save(flags);
		hostdata->dma_len = (p & SR_IO) ?
			NCR5380_dma_read_setup(instance, d, c) :
			NCR5380_dma_write_setup(instance, d, c);
		local_irq_restore(flags);
	}

	if (p & SR_IO)
		NCR5380_write(START_DMA_INITIATOR_RECEIVE_REG, 0);
	else {
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_DATA);
		NCR5380_write(START_DMA_SEND_REG, 0);
	}

	if (hostdata->flags & FLAG_LATE_DMA_SETUP) {
		/* On the Falcon, the DMA setup must be done after the last */
		/* NCR access, else the DMA setup gets trashed!
		 */
		local_irq_save(flags);
		hostdata->dma_len = (p & SR_IO) ?
			NCR5380_dma_read_setup(instance, d, c) :
			NCR5380_dma_write_setup(instance, d, c);
		local_irq_restore(flags);
	}
#endif /* !defined(CONFIG_SUN3) */

	return 0;
}
#endif /* defined(REAL_DMA) */

/*
 * Function : NCR5380_information_transfer (struct Scsi_Host *instance)
 *
 * Purpose : run through the various SCSI phases and do as the target
 *	directs us to.  Operates on the currently connected command,
 *	instance->connected.
 *
 * Inputs : instance, instance for which we are doing commands
 *
 * Side effects : SCSI things happen, the disconnected queue will be
 *	modified if a command disconnects, *instance->connected will
 *	change.
 *
 * XXX Note : we need to watch for bus free or a reset condition here
 *	to recover from an unexpected bus free condition.
 */

static void NCR5380_information_transfer(struct Scsi_Host *instance)
{
	SETUP_HOSTDATA(instance);
	unsigned long flags;
	unsigned char msgout = NOP;
	int sink = 0;
	int len;
#if defined(REAL_DMA)
	int transfersize;
#endif
	unsigned char *data;
	unsigned char phase, tmp, extended_msg[10], old_phase = 0xff;
	struct scsi_cmnd *cmd = (struct scsi_cmnd *) hostdata->connected;

#ifdef SUN3_SCSI_VME
	dregs->csr |= CSR_INTR;
#endif

	while (1) {
		tmp = NCR5380_read(STATUS_REG);
		/* We only have a valid SCSI phase when REQ is asserted */
		if (tmp & SR_REQ) {
			phase = (tmp & PHASE_MASK);
			if (phase != old_phase) {
				old_phase = phase;
				NCR5380_dprint_phase(NDEBUG_INFORMATION, instance);
			}
#if defined(CONFIG_SUN3)
			if (phase == PHASE_CMDOUT) {
#if defined(REAL_DMA)
				void *d;
				unsigned long count;

				if (!cmd->SCp.this_residual && cmd->SCp.buffers_residual) {
					count = cmd->SCp.buffer->length;
					d = sg_virt(cmd->SCp.buffer);
				} else {
					count = cmd->SCp.this_residual;
					d = cmd->SCp.ptr;
				}
				/* this command setup for dma yet? */
				if ((count >= DMA_MIN_SIZE) && (sun3_dma_setup_done != cmd)) {
					if (cmd->request->cmd_type == REQ_TYPE_FS) {
						sun3scsi_dma_setup(d, count,
						                   rq_data_dir(cmd->request));
						sun3_dma_setup_done = cmd;
					}
				}
#endif
#ifdef SUN3_SCSI_VME
				dregs->csr |= CSR_INTR;
#endif
			}
#endif /* CONFIG_SUN3 */

			if (sink && (phase != PHASE_MSGOUT)) {
				NCR5380_write(TARGET_COMMAND_REG, PHASE_SR_TO_TCR(tmp));

				NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ATN |
					      ICR_ASSERT_ACK);
				while (NCR5380_read(STATUS_REG) & SR_REQ)
					;
				NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE |
					      ICR_ASSERT_ATN);
				sink = 0;
				continue;
			}

			switch (phase) {
			case PHASE_DATAOUT:
#if (NDEBUG & NDEBUG_NO_DATAOUT)
				printk("scsi%d: NDEBUG_NO_DATAOUT set, attempted DATAOUT "
				       "aborted\n", HOSTNO);
				sink = 1;
				do_abort(instance);
				cmd->result = DID_ERROR << 16;
				cmd->scsi_done(cmd);
				return;
#endif
			case PHASE_DATAIN:
				/*
				 * If there is no room left in the current buffer in the
				 * scatter-gather list, move onto the next one.
				 */

				if (!cmd->SCp.this_residual && cmd->SCp.buffers_residual) {
					++cmd->SCp.buffer;
					--cmd->SCp.buffers_residual;
					cmd->SCp.this_residual = cmd->SCp.buffer->length;
					cmd->SCp.ptr = sg_virt(cmd->SCp.buffer);
					/* ++roman: Try to merge some scatter-buffers if
					 * they are at contiguous physical addresses.
					 */
					merge_contiguous_buffers(cmd);
					dprintk(NDEBUG_INFORMATION, "scsi%d: %d bytes and %d buffers left\n",
						   HOSTNO, cmd->SCp.this_residual,
						   cmd->SCp.buffers_residual);
				}

				/*
				 * The preferred transfer method is going to be
				 * PSEUDO-DMA for systems that are strictly PIO,
				 * since we can let the hardware do the handshaking.
				 *
				 * For this to work, we need to know the transfersize
				 * ahead of time, since the pseudo-DMA code will sit
				 * in an unconditional loop.
				 */

				/* ++roman: I suggest, this should be
				 *   #if def(REAL_DMA)
				 * instead of leaving REAL_DMA out.
				 */

#if defined(REAL_DMA)
				if (
#if !defined(CONFIG_SUN3)
				    !cmd->device->borken &&
#endif
				    (transfersize = NCR5380_dma_xfer_len(instance, cmd, phase)) >= DMA_MIN_SIZE) {
					len = transfersize;
					cmd->SCp.phase = phase;
					if (NCR5380_transfer_dma(instance, &phase,
					    &len, (unsigned char **)&cmd->SCp.ptr)) {
						/*
						 * If the watchdog timer fires, all future
						 * accesses to this device will use the
						 * polled-IO. */
						scmd_printk(KERN_INFO, cmd,
							"switching to slow handshake\n");
						cmd->device->borken = 1;
						NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE |
							ICR_ASSERT_ATN);
						sink = 1;
						do_abort(instance);
						cmd->result = DID_ERROR << 16;
						cmd->scsi_done(cmd);
						/* XXX - need to source or sink data here, as appropriate */
					} else {
#ifdef REAL_DMA
						/* ++roman: When using real DMA,
						 * information_transfer() should return after
						 * starting DMA since it has nothing more to
						 * do.
						 */
						return;
#else
						cmd->SCp.this_residual -= transfersize - len;
#endif
					}
				} else
#endif /* defined(REAL_DMA) */
					NCR5380_transfer_pio(instance, &phase,
							     (int *)&cmd->SCp.this_residual,
							     (unsigned char **)&cmd->SCp.ptr);
#if defined(CONFIG_SUN3) && defined(REAL_DMA)
				/* if we had intended to dma that command clear it */
				if (sun3_dma_setup_done == cmd)
					sun3_dma_setup_done = NULL;
#endif
				break;
			case PHASE_MSGIN:
				len = 1;
				data = &tmp;
				NCR5380_write(SELECT_ENABLE_REG, 0);	/* disable reselects */
				NCR5380_transfer_pio(instance, &phase, &len, &data);
				cmd->SCp.Message = tmp;

				switch (tmp) {
				/*
				 * Linking lets us reduce the time required to get the
				 * next command out to the device, hopefully this will
				 * mean we don't waste another revolution due to the delays
				 * required by ARBITRATION and another SELECTION.
				 *
				 * In the current implementation proposal, low level drivers
				 * merely have to start the next command, pointed to by
				 * next_link, done() is called as with unlinked commands.
				 */
#ifdef LINKED
				case LINKED_CMD_COMPLETE:
				case LINKED_FLG_CMD_COMPLETE:
					/* Accept message by clearing ACK */
					NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);

					dprintk(NDEBUG_LINKED, "scsi%d: target %d lun %llu linked command "
						   "complete.\n", HOSTNO, cmd->device->id, cmd->device->lun);

					/* Enable reselect interrupts */
					NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
					/*
					 * Sanity check : A linked command should only terminate
					 * with one of these messages if there are more linked
					 * commands available.
					 */

					if (!cmd->next_link) {
						 printk(KERN_NOTICE "scsi%d: target %d lun %llu "
							"linked command complete, no next_link\n",
							HOSTNO, cmd->device->id, cmd->device->lun);
						sink = 1;
						do_abort(instance);
						return;
					}

					initialize_SCp(cmd->next_link);
					/* The next command is still part of this process; copy it
					 * and don't free it! */
					cmd->next_link->tag = cmd->tag;
					cmd->result = cmd->SCp.Status | (cmd->SCp.Message << 8);
					dprintk(NDEBUG_LINKED, "scsi%d: target %d lun %llu linked request "
						   "done, calling scsi_done().\n",
						   HOSTNO, cmd->device->id, cmd->device->lun);
					cmd->scsi_done(cmd);
					cmd = hostdata->connected;
					break;
#endif /* def LINKED */
				case ABORT:
				case COMMAND_COMPLETE:
					/* Accept message by clearing ACK */
					NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
					dprintk(NDEBUG_QUEUES, "scsi%d: command for target %d, lun %llu "
						  "completed\n", HOSTNO, cmd->device->id, cmd->device->lun);

					local_irq_save(flags);
					hostdata->retain_dma_intr++;
					hostdata->connected = NULL;
#ifdef SUPPORT_TAGS
					cmd_free_tag(cmd);
					if (status_byte(cmd->SCp.Status) == QUEUE_FULL) {
						/* Turn a QUEUE FULL status into BUSY, I think the
						 * mid level cannot handle QUEUE FULL :-( (The
						 * command is retried after BUSY). Also update our
						 * queue size to the number of currently issued
						 * commands now.
						 */
						/* ++Andreas: the mid level code knows about
						   QUEUE_FULL now. */
						struct tag_alloc *ta = &hostdata->TagAlloc[scmd_id(cmd)][cmd->device->lun];
						dprintk(NDEBUG_TAGS, "scsi%d: target %d lun %llu returned "
							   "QUEUE_FULL after %d commands\n",
							   HOSTNO, cmd->device->id, cmd->device->lun,
							   ta->nr_allocated);
						if (ta->queue_size > ta->nr_allocated)
							ta->nr_allocated = ta->queue_size;
					}
#else
					hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
#endif
					/* Enable reselect interrupts */
					NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);

					/*
					 * I'm not sure what the correct thing to do here is :
					 *
					 * If the command that just executed is NOT a request
					 * sense, the obvious thing to do is to set the result
					 * code to the values of the stored parameters.
					 *
					 * If it was a REQUEST SENSE command, we need some way to
					 * differentiate between the failure code of the original
					 * and the failure code of the REQUEST sense - the obvious
					 * case is success, where we fall through and leave the
					 * result code unchanged.
					 *
					 * The non-obvious place is where the REQUEST SENSE failed
					 */

					if (cmd->cmnd[0] != REQUEST_SENSE)
						cmd->result = cmd->SCp.Status | (cmd->SCp.Message << 8);
					else if (status_byte(cmd->SCp.Status) != GOOD)
						cmd->result = (cmd->result & 0x00ffff) | (DID_ERROR << 16);

					if ((cmd->cmnd[0] == REQUEST_SENSE) &&
						hostdata->ses.cmd_len) {
						scsi_eh_restore_cmnd(cmd, &hostdata->ses);
						hostdata->ses.cmd_len = 0 ;
					}

					if ((cmd->cmnd[0] != REQUEST_SENSE) &&
					    (status_byte(cmd->SCp.Status) == CHECK_CONDITION)) {
						scsi_eh_prep_cmnd(cmd, &hostdata->ses, NULL, 0, ~0);

						dprintk(NDEBUG_AUTOSENSE, "scsi%d: performing request sense\n", HOSTNO);

						LIST(cmd,hostdata->issue_queue);
						SET_NEXT(cmd, hostdata->issue_queue);
						hostdata->issue_queue = (struct scsi_cmnd *) cmd;
						dprintk(NDEBUG_QUEUES, "scsi%d: REQUEST SENSE added to head of "
							  "issue queue\n", H_NO(cmd));
					} else {
						cmd->scsi_done(cmd);
					}

					local_irq_restore(flags);

					NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
					/*
					 * Restore phase bits to 0 so an interrupted selection,
					 * arbitration can resume.
					 */
					NCR5380_write(TARGET_COMMAND_REG, 0);

					while ((NCR5380_read(STATUS_REG) & SR_BSY) && !hostdata->connected)
						barrier();

					local_irq_save(flags);
					hostdata->retain_dma_intr--;
					/* ++roman: For Falcon SCSI, release the lock on the
					 * ST-DMA here if no other commands are waiting on the
					 * disconnected queue.
					 */
					maybe_release_dma_irq(instance);
					local_irq_restore(flags);
					return;
				case MESSAGE_REJECT:
					/* Accept message by clearing ACK */
					NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
					/* Enable reselect interrupts */
					NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
					switch (hostdata->last_message) {
					case HEAD_OF_QUEUE_TAG:
					case ORDERED_QUEUE_TAG:
					case SIMPLE_QUEUE_TAG:
						/* The target obviously doesn't support tagged
						 * queuing, even though it announced this ability in
						 * its INQUIRY data ?!? (maybe only this LUN?) Ok,
						 * clear 'tagged_supported' and lock the LUN, since
						 * the command is treated as untagged further on.
						 */
						cmd->device->tagged_supported = 0;
						hostdata->busy[cmd->device->id] |= (1 << cmd->device->lun);
						cmd->tag = TAG_NONE;
						dprintk(NDEBUG_TAGS, "scsi%d: target %d lun %llu rejected "
							   "QUEUE_TAG message; tagged queuing "
							   "disabled\n",
							   HOSTNO, cmd->device->id, cmd->device->lun);
						break;
					}
					break;
				case DISCONNECT:
					/* Accept message by clearing ACK */
					NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
					local_irq_save(flags);
					cmd->device->disconnect = 1;
					LIST(cmd,hostdata->disconnected_queue);
					SET_NEXT(cmd, hostdata->disconnected_queue);
					hostdata->connected = NULL;
					hostdata->disconnected_queue = cmd;
					local_irq_restore(flags);
					dprintk(NDEBUG_QUEUES, "scsi%d: command for target %d lun %llu was "
						  "moved from connected to the "
						  "disconnected_queue\n", HOSTNO,
						  cmd->device->id, cmd->device->lun);
					/*
					 * Restore phase bits to 0 so an interrupted selection,
					 * arbitration can resume.
					 */
					NCR5380_write(TARGET_COMMAND_REG, 0);

					/* Enable reselect interrupts */
					NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
					/* Wait for bus free to avoid nasty timeouts */
					while ((NCR5380_read(STATUS_REG) & SR_BSY) && !hostdata->connected)
						barrier();
#ifdef SUN3_SCSI_VME
					dregs->csr |= CSR_DMA_ENABLE;
#endif
					return;
					/*
					 * The SCSI data pointer is *IMPLICITLY* saved on a disconnect
					 * operation, in violation of the SCSI spec so we can safely
					 * ignore SAVE/RESTORE pointers calls.
					 *
					 * Unfortunately, some disks violate the SCSI spec and
					 * don't issue the required SAVE_POINTERS message before
					 * disconnecting, and we have to break spec to remain
					 * compatible.
					 */
				case SAVE_POINTERS:
				case RESTORE_POINTERS:
					/* Accept message by clearing ACK */
					NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
					/* Enable reselect interrupts */
					NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
					break;
				case EXTENDED_MESSAGE:
					/*
					 * Extended messages are sent in the following format :
					 * Byte
					 * 0		EXTENDED_MESSAGE == 1
					 * 1		length (includes one byte for code, doesn't
					 *		include first two bytes)
					 * 2		code
					 * 3..length+1	arguments
					 *
					 * Start the extended message buffer with the EXTENDED_MESSAGE
					 * byte, since spi_print_msg() wants the whole thing.
					 */
					extended_msg[0] = EXTENDED_MESSAGE;
					/* Accept first byte by clearing ACK */
					NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);

					dprintk(NDEBUG_EXTENDED, "scsi%d: receiving extended message\n", HOSTNO);

					len = 2;
					data = extended_msg + 1;
					phase = PHASE_MSGIN;
					NCR5380_transfer_pio(instance, &phase, &len, &data);
					dprintk(NDEBUG_EXTENDED, "scsi%d: length=%d, code=0x%02x\n", HOSTNO,
						   (int)extended_msg[1], (int)extended_msg[2]);

					if (!len && extended_msg[1] <=
					    (sizeof(extended_msg) - 1)) {
						/* Accept third byte by clearing ACK */
						NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
						len = extended_msg[1] - 1;
						data = extended_msg + 3;
						phase = PHASE_MSGIN;

						NCR5380_transfer_pio(instance, &phase, &len, &data);
						dprintk(NDEBUG_EXTENDED, "scsi%d: message received, residual %d\n",
							   HOSTNO, len);

						switch (extended_msg[2]) {
						case EXTENDED_SDTR:
						case EXTENDED_WDTR:
						case EXTENDED_MODIFY_DATA_POINTER:
						case EXTENDED_EXTENDED_IDENTIFY:
							tmp = 0;
						}
					} else if (len) {
						printk(KERN_NOTICE "scsi%d: error receiving "
						       "extended message\n", HOSTNO);
						tmp = 0;
					} else {
						printk(KERN_NOTICE "scsi%d: extended message "
							   "code %02x length %d is too long\n",
							   HOSTNO, extended_msg[2], extended_msg[1]);
						tmp = 0;
					}
					/* Fall through to reject message */

					/*
					 * If we get something weird that we aren't expecting,
					 * reject it.
					 */
				default:
					if (!tmp) {
						printk(KERN_INFO "scsi%d: rejecting message ",
						       instance->host_no);
						spi_print_msg(extended_msg);
						printk("\n");
					} else if (tmp != EXTENDED_MESSAGE)
						scmd_printk(KERN_INFO, cmd,
						            "rejecting unknown message %02x\n",
						            tmp);
					else
						scmd_printk(KERN_INFO, cmd,
						            "rejecting unknown extended message code %02x, length %d\n",
						            extended_msg[1], extended_msg[0]);

					msgout = MESSAGE_REJECT;
					NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ATN);
					break;
				} /* switch (tmp) */
				break;
			case PHASE_MSGOUT:
				len = 1;
				data = &msgout;
				hostdata->last_message = msgout;
				NCR5380_transfer_pio(instance, &phase, &len, &data);
				if (msgout == ABORT) {
					local_irq_save(flags);
#ifdef SUPPORT_TAGS
					cmd_free_tag(cmd);
#else
					hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
#endif
					hostdata->connected = NULL;
					cmd->result = DID_ERROR << 16;
					NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
					maybe_release_dma_irq(instance);
					local_irq_restore(flags);
					cmd->scsi_done(cmd);
					return;
				}
				msgout = NOP;
				break;
			case PHASE_CMDOUT:
				len = cmd->cmd_len;
				data = cmd->cmnd;
				/*
				 * XXX for performance reasons, on machines with a
				 * PSEUDO-DMA architecture we should probably
				 * use the dma transfer function.
				 */
				NCR5380_transfer_pio(instance, &phase, &len, &data);
				break;
			case PHASE_STATIN:
				len = 1;
				data = &tmp;
				NCR5380_transfer_pio(instance, &phase, &len, &data);
				cmd->SCp.Status = tmp;
				break;
			default:
				printk("scsi%d: unknown phase\n", HOSTNO);
				NCR5380_dprint(NDEBUG_ANY, instance);
			} /* switch(phase) */
		} /* if (tmp * SR_REQ) */
	} /* while (1) */
}

/*
 * Function : void NCR5380_reselect (struct Scsi_Host *instance)
 *
 * Purpose : does reselection, initializing the instance->connected
 *	field to point to the scsi_cmnd for which the I_T_L or I_T_L_Q
 *	nexus has been reestablished,
 *
 * Inputs : instance - this instance of the NCR5380.
 *
 */


/* it might eventually prove necessary to do a dma setup on
   reselection, but it doesn't seem to be needed now -- sam */

static void NCR5380_reselect(struct Scsi_Host *instance)
{
	SETUP_HOSTDATA(instance);
	unsigned char target_mask;
	unsigned char lun;
#ifdef SUPPORT_TAGS
	unsigned char tag;
#endif
	unsigned char msg[3];
	int __maybe_unused len;
	unsigned char __maybe_unused *data, __maybe_unused phase;
	struct scsi_cmnd *tmp = NULL, *prev;

	/*
	 * Disable arbitration, etc. since the host adapter obviously
	 * lost, and tell an interrupted NCR5380_select() to restart.
	 */

	NCR5380_write(MODE_REG, MR_BASE);
	hostdata->restart_select = 1;

	target_mask = NCR5380_read(CURRENT_SCSI_DATA_REG) & ~(hostdata->id_mask);

	dprintk(NDEBUG_RESELECTION, "scsi%d: reselect\n", HOSTNO);

	/*
	 * At this point, we have detected that our SCSI ID is on the bus,
	 * SEL is true and BSY was false for at least one bus settle delay
	 * (400 ns).
	 *
	 * We must assert BSY ourselves, until the target drops the SEL
	 * signal.
	 */

	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_BSY);

	while (NCR5380_read(STATUS_REG) & SR_SEL)
		;
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);

	/*
	 * Wait for target to go into MSGIN.
	 */

	while (!(NCR5380_read(STATUS_REG) & SR_REQ))
		;

#if defined(CONFIG_SUN3) && defined(REAL_DMA)
	/* acknowledge toggle to MSGIN */
	NCR5380_write(TARGET_COMMAND_REG, PHASE_SR_TO_TCR(PHASE_MSGIN));

	/* peek at the byte without really hitting the bus */
	msg[0] = NCR5380_read(CURRENT_SCSI_DATA_REG);
#else
	len = 1;
	data = msg;
	phase = PHASE_MSGIN;
	NCR5380_transfer_pio(instance, &phase, &len, &data);
#endif

	if (!(msg[0] & 0x80)) {
		printk(KERN_DEBUG "scsi%d: expecting IDENTIFY message, got ", HOSTNO);
		spi_print_msg(msg);
		do_abort(instance);
		return;
	}
	lun = (msg[0] & 0x07);

#if defined(SUPPORT_TAGS) && !defined(CONFIG_SUN3)
	/* If the phase is still MSGIN, the target wants to send some more
	 * messages. In case it supports tagged queuing, this is probably a
	 * SIMPLE_QUEUE_TAG for the I_T_L_Q nexus.
	 */
	tag = TAG_NONE;
	if (phase == PHASE_MSGIN && (hostdata->flags & FLAG_TAGGED_QUEUING)) {
		/* Accept previous IDENTIFY message by clearing ACK */
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
		len = 2;
		data = msg + 1;
		if (!NCR5380_transfer_pio(instance, &phase, &len, &data) &&
		    msg[1] == SIMPLE_QUEUE_TAG)
			tag = msg[2];
		dprintk(NDEBUG_TAGS, "scsi%d: target mask %02x, lun %d sent tag %d at "
			   "reselection\n", HOSTNO, target_mask, lun, tag);
	}
#endif

	/*
	 * Find the command corresponding to the I_T_L or I_T_L_Q  nexus we
	 * just reestablished, and remove it from the disconnected queue.
	 */

	for (tmp = (struct scsi_cmnd *) hostdata->disconnected_queue, prev = NULL;
	     tmp; prev = tmp, tmp = NEXT(tmp)) {
		if ((target_mask == (1 << tmp->device->id)) && (lun == tmp->device->lun)
#ifdef SUPPORT_TAGS
		    && (tag == tmp->tag)
#endif
		    ) {
			if (prev) {
				REMOVE(prev, NEXT(prev), tmp, NEXT(tmp));
				SET_NEXT(prev, NEXT(tmp));
			} else {
				REMOVE(-1, hostdata->disconnected_queue, tmp, NEXT(tmp));
				hostdata->disconnected_queue = NEXT(tmp);
			}
			SET_NEXT(tmp, NULL);
			break;
		}
	}

	if (!tmp) {
		printk(KERN_WARNING "scsi%d: warning: target bitmask %02x lun %d "
#ifdef SUPPORT_TAGS
		       "tag %d "
#endif
		       "not in disconnected_queue.\n",
		       HOSTNO, target_mask, lun
#ifdef SUPPORT_TAGS
		       , tag
#endif
			);
		/*
		 * Since we have an established nexus that we can't do anything
		 * with, we must abort it.
		 */
		do_abort(instance);
		return;
	}

#if defined(CONFIG_SUN3) && defined(REAL_DMA)
	/* engage dma setup for the command we just saw */
	{
		void *d;
		unsigned long count;

		if (!tmp->SCp.this_residual && tmp->SCp.buffers_residual) {
			count = tmp->SCp.buffer->length;
			d = sg_virt(tmp->SCp.buffer);
		} else {
			count = tmp->SCp.this_residual;
			d = tmp->SCp.ptr;
		}
		/* setup this command for dma if not already */
		if ((count >= DMA_MIN_SIZE) && (sun3_dma_setup_done != tmp)) {
			sun3scsi_dma_setup(d, count, rq_data_dir(tmp->request));
			sun3_dma_setup_done = tmp;
		}
	}

	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ACK);
#endif

	/* Accept message by clearing ACK */
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);

#if defined(SUPPORT_TAGS) && defined(CONFIG_SUN3)
	/* If the phase is still MSGIN, the target wants to send some more
	 * messages. In case it supports tagged queuing, this is probably a
	 * SIMPLE_QUEUE_TAG for the I_T_L_Q nexus.
	 */
	tag = TAG_NONE;
	if (phase == PHASE_MSGIN && setup_use_tagged_queuing) {
		/* Accept previous IDENTIFY message by clearing ACK */
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
		len = 2;
		data = msg + 1;
		if (!NCR5380_transfer_pio(instance, &phase, &len, &data) &&
		    msg[1] == SIMPLE_QUEUE_TAG)
			tag = msg[2];
		dprintk(NDEBUG_TAGS, "scsi%d: target mask %02x, lun %d sent tag %d at reselection\n"
			HOSTNO, target_mask, lun, tag);
	}
#endif

	hostdata->connected = tmp;
	dprintk(NDEBUG_RESELECTION, "scsi%d: nexus established, target = %d, lun = %llu, tag = %d\n",
		   HOSTNO, tmp->device->id, tmp->device->lun, tmp->tag);
}


/*
 * Function : int NCR5380_abort (struct scsi_cmnd *cmd)
 *
 * Purpose : abort a command
 *
 * Inputs : cmd - the scsi_cmnd to abort, code - code to set the
 *	host byte of the result field to, if zero DID_ABORTED is
 *	used.
 *
 * Returns : SUCCESS - success, FAILED on failure.
 *
 * XXX - there is no way to abort the command that is currently
 *	 connected, you have to wait for it to complete.  If this is
 *	 a problem, we could implement longjmp() / setjmp(), setjmp()
 *	 called where the loop started in NCR5380_main().
 */

static
int NCR5380_abort(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *instance = cmd->device->host;
	SETUP_HOSTDATA(instance);
	struct scsi_cmnd *tmp, **prev;
	unsigned long flags;

	scmd_printk(KERN_NOTICE, cmd, "aborting command\n");

	NCR5380_print_status(instance);

	local_irq_save(flags);

	dprintk(NDEBUG_ABORT, "scsi%d: abort called basr 0x%02x, sr 0x%02x\n", HOSTNO,
		    NCR5380_read(BUS_AND_STATUS_REG),
		    NCR5380_read(STATUS_REG));

#if 1
	/*
	 * Case 1 : If the command is the currently executing command,
	 * we'll set the aborted flag and return control so that
	 * information transfer routine can exit cleanly.
	 */

	if (hostdata->connected == cmd) {

		dprintk(NDEBUG_ABORT, "scsi%d: aborting connected command\n", HOSTNO);
		/*
		 * We should perform BSY checking, and make sure we haven't slipped
		 * into BUS FREE.
		 */

		/*	NCR5380_write(INITIATOR_COMMAND_REG, ICR_ASSERT_ATN); */
		/*
		 * Since we can't change phases until we've completed the current
		 * handshake, we have to source or sink a byte of data if the current
		 * phase is not MSGOUT.
		 */

		/*
		 * Return control to the executing NCR drive so we can clear the
		 * aborted flag and get back into our main loop.
		 */

		if (do_abort(instance) == 0) {
			hostdata->aborted = 1;
			hostdata->connected = NULL;
			cmd->result = DID_ABORT << 16;
#ifdef SUPPORT_TAGS
			cmd_free_tag(cmd);
#else
			hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
#endif
			maybe_release_dma_irq(instance);
			local_irq_restore(flags);
			cmd->scsi_done(cmd);
			return SUCCESS;
		} else {
			local_irq_restore(flags);
			printk("scsi%d: abort of connected command failed!\n", HOSTNO);
			return FAILED;
		}
	}
#endif

	/*
	 * Case 2 : If the command hasn't been issued yet, we simply remove it
	 *	    from the issue queue.
	 */
	for (prev = (struct scsi_cmnd **)&(hostdata->issue_queue),
	     tmp = (struct scsi_cmnd *)hostdata->issue_queue;
	     tmp; prev = NEXTADDR(tmp), tmp = NEXT(tmp)) {
		if (cmd == tmp) {
			REMOVE(5, *prev, tmp, NEXT(tmp));
			(*prev) = NEXT(tmp);
			SET_NEXT(tmp, NULL);
			tmp->result = DID_ABORT << 16;
			maybe_release_dma_irq(instance);
			local_irq_restore(flags);
			dprintk(NDEBUG_ABORT, "scsi%d: abort removed command from issue queue.\n",
				    HOSTNO);
			/* Tagged queuing note: no tag to free here, hasn't been assigned
			 * yet... */
			tmp->scsi_done(tmp);
			return SUCCESS;
		}
	}

	/*
	 * Case 3 : If any commands are connected, we're going to fail the abort
	 *	    and let the high level SCSI driver retry at a later time or
	 *	    issue a reset.
	 *
	 *	    Timeouts, and therefore aborted commands, will be highly unlikely
	 *          and handling them cleanly in this situation would make the common
	 *	    case of noresets less efficient, and would pollute our code.  So,
	 *	    we fail.
	 */

	if (hostdata->connected) {
		local_irq_restore(flags);
		dprintk(NDEBUG_ABORT, "scsi%d: abort failed, command connected.\n", HOSTNO);
		return FAILED;
	}

	/*
	 * Case 4: If the command is currently disconnected from the bus, and
	 *	there are no connected commands, we reconnect the I_T_L or
	 *	I_T_L_Q nexus associated with it, go into message out, and send
	 *      an abort message.
	 *
	 * This case is especially ugly. In order to reestablish the nexus, we
	 * need to call NCR5380_select().  The easiest way to implement this
	 * function was to abort if the bus was busy, and let the interrupt
	 * handler triggered on the SEL for reselect take care of lost arbitrations
	 * where necessary, meaning interrupts need to be enabled.
	 *
	 * When interrupts are enabled, the queues may change - so we
	 * can't remove it from the disconnected queue before selecting it
	 * because that could cause a failure in hashing the nexus if that
	 * device reselected.
	 *
	 * Since the queues may change, we can't use the pointers from when we
	 * first locate it.
	 *
	 * So, we must first locate the command, and if NCR5380_select()
	 * succeeds, then issue the abort, relocate the command and remove
	 * it from the disconnected queue.
	 */

	for (tmp = (struct scsi_cmnd *) hostdata->disconnected_queue; tmp;
	     tmp = NEXT(tmp)) {
		if (cmd == tmp) {
			local_irq_restore(flags);
			dprintk(NDEBUG_ABORT, "scsi%d: aborting disconnected command.\n", HOSTNO);

			if (NCR5380_select(instance, cmd))
				return FAILED;

			dprintk(NDEBUG_ABORT, "scsi%d: nexus reestablished.\n", HOSTNO);

			do_abort(instance);

			local_irq_save(flags);
			for (prev = (struct scsi_cmnd **)&(hostdata->disconnected_queue),
			     tmp = (struct scsi_cmnd *)hostdata->disconnected_queue;
			     tmp; prev = NEXTADDR(tmp), tmp = NEXT(tmp)) {
				if (cmd == tmp) {
					REMOVE(5, *prev, tmp, NEXT(tmp));
					*prev = NEXT(tmp);
					SET_NEXT(tmp, NULL);
					tmp->result = DID_ABORT << 16;
					/* We must unlock the tag/LUN immediately here, since the
					 * target goes to BUS FREE and doesn't send us another
					 * message (COMMAND_COMPLETE or the like)
					 */
#ifdef SUPPORT_TAGS
					cmd_free_tag(tmp);
#else
					hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
#endif
					maybe_release_dma_irq(instance);
					local_irq_restore(flags);
					tmp->scsi_done(tmp);
					return SUCCESS;
				}
			}
		}
	}

	/* Maybe it is sufficient just to release the ST-DMA lock... (if
	 * possible at all) At least, we should check if the lock could be
	 * released after the abort, in case it is kept due to some bug.
	 */
	maybe_release_dma_irq(instance);
	local_irq_restore(flags);

	/*
	 * Case 5 : If we reached this point, the command was not found in any of
	 *	    the queues.
	 *
	 * We probably reached this point because of an unlikely race condition
	 * between the command completing successfully and the abortion code,
	 * so we won't panic, but we will notify the user in case something really
	 * broke.
	 */

	printk(KERN_INFO "scsi%d: warning : SCSI command probably completed successfully before abortion\n", HOSTNO);

	return FAILED;
}


/*
 * Function : int NCR5380_reset (struct scsi_cmnd *cmd)
 *
 * Purpose : reset the SCSI bus.
 *
 * Returns : SUCCESS or FAILURE
 *
 */

static int NCR5380_bus_reset(struct scsi_cmnd *cmd)
{
	struct Scsi_Host *instance = cmd->device->host;
	struct NCR5380_hostdata *hostdata = shost_priv(instance);
	int i;
	unsigned long flags;

	NCR5380_print_status(instance);

	/* get in phase */
	NCR5380_write(TARGET_COMMAND_REG,
		      PHASE_SR_TO_TCR(NCR5380_read(STATUS_REG)));
	/* assert RST */
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_RST);
	udelay(40);
	/* reset NCR registers */
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
	NCR5380_write(MODE_REG, MR_BASE);
	NCR5380_write(TARGET_COMMAND_REG, 0);
	NCR5380_write(SELECT_ENABLE_REG, 0);
	/* ++roman: reset interrupt condition! otherwise no interrupts don't get
	 * through anymore ... */
	(void)NCR5380_read(RESET_PARITY_INTERRUPT_REG);

	/* After the reset, there are no more connected or disconnected commands
	 * and no busy units; so clear the low-level status here to avoid
	 * conflicts when the mid-level code tries to wake up the affected
	 * commands!
	 */

	if (hostdata->issue_queue)
		dprintk(NDEBUG_ABORT, "scsi%d: reset aborted issued command(s)\n", H_NO(cmd));
	if (hostdata->connected)
		dprintk(NDEBUG_ABORT, "scsi%d: reset aborted a connected command\n", H_NO(cmd));
	if (hostdata->disconnected_queue)
		dprintk(NDEBUG_ABORT, "scsi%d: reset aborted disconnected command(s)\n", H_NO(cmd));

	local_irq_save(flags);
	hostdata->issue_queue = NULL;
	hostdata->connected = NULL;
	hostdata->disconnected_queue = NULL;
#ifdef SUPPORT_TAGS
	free_all_tags(hostdata);
#endif
	for (i = 0; i < 8; ++i)
		hostdata->busy[i] = 0;
#ifdef REAL_DMA
	hostdata->dma_len = 0;
#endif

	maybe_release_dma_irq(instance);
	local_irq_restore(flags);

	return SUCCESS;
}
