/* 
 * NCR 5380 generic driver routines.  These should make it *trivial*
 * 	to implement 5380 SCSI drivers under Linux with a non-trantor
 *	architecture.
 *
 *	Note that these routines also work with NR53c400 family chips.
 *
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing 
 *	(Unix and Linux consulting and custom programming)
 * 	drew@colorado.edu
 *	+1 (303) 666-5836
 *
 * DISTRIBUTION RELEASE 6. 
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
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_transport_spi.h>

#if (NDEBUG & NDEBUG_LISTS)
#define LIST(x,y) \
  { printk("LINE:%d   Adding %p to %p\n", __LINE__, (void*)(x), (void*)(y)); \
    if ((x)==(y)) udelay(5); }
#define REMOVE(w,x,y,z) \
  { printk("LINE:%d   Removing: %p->%p  %p->%p \n", __LINE__, \
	   (void*)(w), (void*)(x), (void*)(y), (void*)(z)); \
    if ((x)==(y)) udelay(5); }
#else
#define LIST(x,y)
#define REMOVE(w,x,y,z)
#endif

#ifndef notyet
#undef LINKED
#endif

/*
 * Design
 * Issues :
 *
 * The other Linux SCSI drivers were written when Linux was Intel PC-only,
 * and specifically for each board rather than each chip.  This makes their
 * adaptation to platforms like the Mac (Some of which use NCR5380's)
 * more difficult than it has to be.
 *
 * Also, many of the SCSI drivers were written before the command queuing
 * routines were implemented, meaning their implementations of queued 
 * commands were hacked on rather than designed in from the start.
 *
 * When I designed the Linux SCSI drivers I figured that 
 * while having two different SCSI boards in a system might be useful
 * for debugging things, two of the same type wouldn't be used.
 * Well, I was wrong and a number of users have mailed me about running
 * multiple high-performance SCSI boards in a server.
 *
 * Finally, when I get questions from users, I have no idea what 
 * revision of my driver they are running.
 *
 * This driver attempts to address these problems :
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
 * To solve the multiple-boards-in-the-same-system problem, 
 * there is a separate instance structure for each instance
 * of a 5380 in the system.  So, multiple NCR5380 drivers will
 * be able to coexist with appropriate changes to the high level
 * SCSI code.  
 *
 * A NCR5380_PUBLIC_REVISION macro is provided, with the release
 * number (updated for each public release) printed by the 
 * NCR5380_print_options command, which should be called from the 
 * wrapper detect function, so that I know what release of the driver
 * users are using.
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
 * which is started when not running by the interrupt handler,
 * timer, and queue command function.  It attempts to establish
 * I_T_L or I_T_L_Q nexuses by removing the commands from the 
 * issue queue and calling NCR5380_select() if a nexus 
 * is not established. 
 *
 * Once a nexus is established, the NCR5380_information_transfer()
 * phase goes through the various phases as instructed by the target.
 * if the target goes into MSG IN and sends a DISCONNECT message,
 * the command structure is placed into the per instance disconnected
 * queue, and NCR5380_main tries to find more work.  If USLEEP
 * was defined, and the target is idle for too long, the system
 * will try to sleep.
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
 * If nothing specific to this implementation needs doing (ie, with external
 * hardware), you must also define 
 *  
 * NCR5380_queue_command
 * NCR5380_reset
 * NCR5380_abort
 * NCR5380_proc_info
 *
 * to be the global entry points into the specific driver, ie 
 * #define NCR5380_queue_command t128_queue_command.
 *
 * If this is not done, the routines will be defined as static functions
 * with the NCR5380* names and the user must provide a globally
 * accessible wrapper function.
 *
 * The generic driver is initialized by calling NCR5380_init(instance),
 * after setting the appropriate host specific fields and ID.  If the 
 * driver wishes to autoprobe for an IRQ line, the NCR5380_probe_irq(instance,
 * possible) function may be used.  Before the specific driver initialization
 * code finishes, NCR5380_print_options should be called.
 */

static struct Scsi_Host *first_instance = NULL;
static struct scsi_host_template *the_template = NULL;

/* Macros ease life... :-) */
#define	SETUP_HOSTDATA(in)				\
    struct NCR5380_hostdata *hostdata =			\
	(struct NCR5380_hostdata *)(in)->hostdata
#define	HOSTDATA(in) ((struct NCR5380_hostdata *)(in)->hostdata)

#define	NEXT(cmd)	((Scsi_Cmnd *)((cmd)->host_scribble))
#define	NEXTADDR(cmd)	((Scsi_Cmnd **)&((cmd)->host_scribble))

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

/* -1 for TAG_NONE is not possible with unsigned char cmd->tag */
#undef TAG_NONE
#define TAG_NONE 0xff

typedef struct {
    DECLARE_BITMAP(allocated, MAX_TAGS);
    int		nr_allocated;
    int		queue_size;
} TAG_ALLOC;

static TAG_ALLOC TagAlloc[8][8]; /* 8 targets and 8 LUNs */


static void __init init_tags( void )
{
    int target, lun;
    TAG_ALLOC *ta;
    
    if (!setup_use_tagged_queuing)
	return;
    
    for( target = 0; target < 8; ++target ) {
	for( lun = 0; lun < 8; ++lun ) {
	    ta = &TagAlloc[target][lun];
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

static int is_lun_busy( Scsi_Cmnd *cmd, int should_be_tagged )
{
    SETUP_HOSTDATA(cmd->device->host);

    if (hostdata->busy[cmd->device->id] & (1 << cmd->device->lun))
	return( 1 );
    if (!should_be_tagged ||
	!setup_use_tagged_queuing || !cmd->device->tagged_supported)
	return( 0 );
    if (TagAlloc[cmd->device->id][cmd->device->lun].nr_allocated >=
	TagAlloc[cmd->device->id][cmd->device->lun].queue_size ) {
	TAG_PRINTK( "scsi%d: target %d lun %d: no free tags\n",
		    H_NO(cmd), cmd->device->id, cmd->device->lun );
	return( 1 );
    }
    return( 0 );
}


/* Allocate a tag for a command (there are no checks anymore, check_lun_busy()
 * must be called before!), or reserve the LUN in 'busy' if the command is
 * untagged.
 */

static void cmd_get_tag( Scsi_Cmnd *cmd, int should_be_tagged )
{
    SETUP_HOSTDATA(cmd->device->host);

    /* If we or the target don't support tagged queuing, allocate the LUN for
     * an untagged command.
     */
    if (!should_be_tagged ||
	!setup_use_tagged_queuing || !cmd->device->tagged_supported) {
	cmd->tag = TAG_NONE;
	hostdata->busy[cmd->device->id] |= (1 << cmd->device->lun);
	TAG_PRINTK( "scsi%d: target %d lun %d now allocated by untagged "
		    "command\n", H_NO(cmd), cmd->device->id, cmd->device->lun );
    }
    else {
	TAG_ALLOC *ta = &TagAlloc[cmd->device->id][cmd->device->lun];

	cmd->tag = find_first_zero_bit( ta->allocated, MAX_TAGS );
	set_bit( cmd->tag, ta->allocated );
	ta->nr_allocated++;
	TAG_PRINTK( "scsi%d: using tag %d for target %d lun %d "
		    "(now %d tags in use)\n",
		    H_NO(cmd), cmd->tag, cmd->device->id, cmd->device->lun,
		    ta->nr_allocated );
    }
}


/* Mark the tag of command 'cmd' as free, or in case of an untagged command,
 * unlock the LUN.
 */

static void cmd_free_tag( Scsi_Cmnd *cmd )
{
    SETUP_HOSTDATA(cmd->device->host);

    if (cmd->tag == TAG_NONE) {
	hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
	TAG_PRINTK( "scsi%d: target %d lun %d untagged cmd finished\n",
		    H_NO(cmd), cmd->device->id, cmd->device->lun );
    }
    else if (cmd->tag >= MAX_TAGS) {
	printk(KERN_NOTICE "scsi%d: trying to free bad tag %d!\n",
		H_NO(cmd), cmd->tag );
    }
    else {
	TAG_ALLOC *ta = &TagAlloc[cmd->device->id][cmd->device->lun];
	clear_bit( cmd->tag, ta->allocated );
	ta->nr_allocated--;
	TAG_PRINTK( "scsi%d: freed tag %d for target %d lun %d\n",
		    H_NO(cmd), cmd->tag, cmd->device->id, cmd->device->lun );
    }
}


static void free_all_tags( void )
{
    int target, lun;
    TAG_ALLOC *ta;

    if (!setup_use_tagged_queuing)
	return;
    
    for( target = 0; target < 8; ++target ) {
	for( lun = 0; lun < 8; ++lun ) {
	    ta = &TagAlloc[target][lun];
	    bitmap_zero(ta->allocated, MAX_TAGS);
	    ta->nr_allocated = 0;
	}
    }
}

#endif /* SUPPORT_TAGS */


/*
 * Function: void merge_contiguous_buffers( Scsi_Cmnd *cmd )
 *
 * Purpose: Try to merge several scatter-gather requests into one DMA
 *    transfer. This is possible if the scatter buffers lie on
 *    physical contiguous addresses.
 *
 * Parameters: Scsi_Cmnd *cmd
 *    The command to work on. The first scatter buffer's data are
 *    assumed to be already transfered into ptr/this_residual.
 */

static void merge_contiguous_buffers( Scsi_Cmnd *cmd )
{
    unsigned long endaddr;
#if (NDEBUG & NDEBUG_MERGING)
    unsigned long oldlen = cmd->SCp.this_residual;
    int		  cnt = 1;
#endif

    for (endaddr = virt_to_phys(cmd->SCp.ptr + cmd->SCp.this_residual - 1) + 1;
	 cmd->SCp.buffers_residual &&
	 virt_to_phys(page_address(cmd->SCp.buffer[1].page)+
		      cmd->SCp.buffer[1].offset) == endaddr; ) {
	MER_PRINTK("VTOP(%p) == %08lx -> merging\n",
		   cmd->SCp.buffer[1].address, endaddr);
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
	MER_PRINTK("merged %d buffers from %p, new length %08x\n",
		   cnt, cmd->SCp.ptr, cmd->SCp.this_residual);
#endif
}

/*
 * Function : void initialize_SCp(Scsi_Cmnd *cmd)
 *
 * Purpose : initialize the saved data pointers for cmd to point to the 
 *	start of the buffer.
 *
 * Inputs : cmd - Scsi_Cmnd structure to have pointers reset.
 */

static __inline__ void initialize_SCp(Scsi_Cmnd *cmd)
{
    /* 
     * Initialize the Scsi Pointer field so that all of the commands in the 
     * various queues are valid.
     */

    if (cmd->use_sg) {
	cmd->SCp.buffer = (struct scatterlist *) cmd->request_buffer;
	cmd->SCp.buffers_residual = cmd->use_sg - 1;
	cmd->SCp.ptr = (char *)page_address(cmd->SCp.buffer->page)+
		       cmd->SCp.buffer->offset;
	cmd->SCp.this_residual = cmd->SCp.buffer->length;
	/* ++roman: Try to merge some scatter-buffers if they are at
	 * contiguous physical addresses.
	 */
	merge_contiguous_buffers( cmd );
    } else {
	cmd->SCp.buffer = NULL;
	cmd->SCp.buffers_residual = 0;
	cmd->SCp.ptr = (char *) cmd->request_buffer;
	cmd->SCp.this_residual = cmd->request_bufflen;
    }
}

#include <linux/delay.h>

#if NDEBUG
static struct {
    unsigned char mask;
    const char * name;} 
signals[] = {{ SR_DBP, "PARITY"}, { SR_RST, "RST" }, { SR_BSY, "BSY" }, 
    { SR_REQ, "REQ" }, { SR_MSG, "MSG" }, { SR_CD,  "CD" }, { SR_IO, "IO" }, 
    { SR_SEL, "SEL" }, {0, NULL}}, 
basrs[] = {{BASR_ATN, "ATN"}, {BASR_ACK, "ACK"}, {0, NULL}},
icrs[] = {{ICR_ASSERT_RST, "ASSERT RST"},{ICR_ASSERT_ACK, "ASSERT ACK"},
    {ICR_ASSERT_BSY, "ASSERT BSY"}, {ICR_ASSERT_SEL, "ASSERT SEL"}, 
    {ICR_ASSERT_ATN, "ASSERT ATN"}, {ICR_ASSERT_DATA, "ASSERT DATA"}, 
    {0, NULL}},
mrs[] = {{MR_BLOCK_DMA_MODE, "MODE BLOCK DMA"}, {MR_TARGET, "MODE TARGET"}, 
    {MR_ENABLE_PAR_CHECK, "MODE PARITY CHECK"}, {MR_ENABLE_PAR_INTR, 
    "MODE PARITY INTR"}, {MR_ENABLE_EOP_INTR,"MODE EOP INTR"},
    {MR_MONITOR_BSY, "MODE MONITOR BSY"},
    {MR_DMA_MODE, "MODE DMA"}, {MR_ARBITRATE, "MODE ARBITRATION"}, 
    {0, NULL}};

/*
 * Function : void NCR5380_print(struct Scsi_Host *instance)
 *
 * Purpose : print the SCSI bus signals for debugging purposes
 *
 * Input : instance - which NCR5380
 */

static void NCR5380_print(struct Scsi_Host *instance) {
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
    for (i = 0; signals[i].mask ; ++i) 
	if (status & signals[i].mask)
	    printk(",%s", signals[i].name);
    printk("\nBASR: %02x ", basr);
    for (i = 0; basrs[i].mask ; ++i) 
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
    {PHASE_UNKNOWN, "UNKNOWN"}};

/* 
 * Function : void NCR5380_print_phase(struct Scsi_Host *instance)
 *
 * Purpose : print the current SCSI phase for debugging purposes
 *
 * Input : instance - which NCR5380
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
	    (phases[i].value != (status & PHASE_MASK)); ++i); 
	printk(KERN_DEBUG "scsi%d: phase %s\n", HOSTNO, phases[i].name);
    }
}

#else /* !NDEBUG */

/* dummies... */
__inline__ void NCR5380_print(struct Scsi_Host *instance) { };
__inline__ void NCR5380_print_phase(struct Scsi_Host *instance) { };

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

#include <linux/workqueue.h>
#include <linux/interrupt.h>

static volatile int main_running = 0;
static DECLARE_WORK(NCR5380_tqueue, (void (*)(void*))NCR5380_main, NULL);

static __inline__ void queue_main(void)
{
    if (!main_running) {
	/* If in interrupt and NCR5380_main() not already running,
	   queue it on the 'immediate' task queue, to be processed
	   immediately after the current interrupt processing has
	   finished. */
	schedule_work(&NCR5380_tqueue);
    }
    /* else: nothing to do: the running NCR5380_main() will pick up
       any newly queued command. */
}


static inline void NCR5380_all_init (void)
{
    static int done = 0;
    if (!done) {
	INI_PRINTK("scsi : NCR5380_all_init()\n");
	done = 1;
    }
}

 
/*
 * Function : void NCR58380_print_options (struct Scsi_Host *instance)
 *
 * Purpose : called by probe code indicating the NCR5380 driver
 *	     options that were selected.
 *
 * Inputs : instance, pointer to this instance.  Unused.
 */

static void __init NCR5380_print_options (struct Scsi_Host *instance)
{
    printk(" generic options"
#ifdef AUTOSENSE 
    " AUTOSENSE"
#endif
#ifdef REAL_DMA
    " REAL DMA"
#endif
#ifdef PARITY
    " PARITY"
#endif
#ifdef SUPPORT_TAGS
    " SCSI-2 TAGGED QUEUING"
#endif
    );
    printk(" generic release=%d", NCR5380_PUBLIC_RELEASE);
}

/*
 * Function : void NCR5380_print_status (struct Scsi_Host *instance)
 *
 * Purpose : print commands in the various queues, called from
 *	NCR5380_abort and NCR5380_debug to aid debugging.
 *
 * Inputs : instance, pointer to this instance.  
 */

static void NCR5380_print_status (struct Scsi_Host *instance)
{
    char *pr_bfr;
    char *start;
    int len;

    NCR_PRINT(NDEBUG_ANY);
    NCR_PRINT_PHASE(NDEBUG_ANY);

    pr_bfr = (char *) __get_free_page(GFP_ATOMIC);
    if (!pr_bfr) {
	printk("NCR5380_print_status: no memory for print buffer\n");
	return;
    }
    len = NCR5380_proc_info(pr_bfr, &start, 0, PAGE_SIZE, HOSTNO, 0);
    pr_bfr[len] = 0;
    printk("\n%s\n", pr_bfr);
    free_page((unsigned long) pr_bfr);
}


/******************************************/
/*
 * /proc/scsi/[dtc pas16 t128 generic]/[0-ASC_NUM_BOARD_SUPPORTED]
 *
 * *buffer: I/O buffer
 * **start: if inout == FALSE pointer into buffer where user read should start
 * offset: current offset
 * length: length of buffer
 * hostno: Scsi_Host host_no
 * inout: TRUE - user is writing; FALSE - user is reading
 *
 * Return the number of bytes read from or written
*/

#undef SPRINTF
#define SPRINTF(fmt,args...) \
  do { if (pos + strlen(fmt) + 20 /* slop */ < buffer + length) \
	 pos += sprintf(pos, fmt , ## args); } while(0)
static
char *lprint_Scsi_Cmnd (Scsi_Cmnd *cmd, char *pos, char *buffer, int length);

static
int NCR5380_proc_info (struct Scsi_Host *instance, char *buffer, char **start, off_t offset,
		       int length, int inout)
{
    char *pos = buffer;
    struct NCR5380_hostdata *hostdata;
    Scsi_Cmnd *ptr;
    unsigned long flags;
    off_t begin = 0;
#define check_offset()				\
    do {					\
	if (pos - buffer < offset - begin) {	\
	    begin += pos - buffer;		\
	    pos = buffer;			\
	}					\
    } while (0)

    hostdata = (struct NCR5380_hostdata *)instance->hostdata;

    if (inout) { /* Has data been written to the file ? */
	return(-ENOSYS);  /* Currently this is a no-op */
    }
    SPRINTF("NCR5380 core release=%d.\n", NCR5380_PUBLIC_RELEASE);
    check_offset();
    local_irq_save(flags);
    SPRINTF("NCR5380: coroutine is%s running.\n", main_running ? "" : "n't");
    check_offset();
    if (!hostdata->connected)
	SPRINTF("scsi%d: no currently connected command\n", HOSTNO);
    else
	pos = lprint_Scsi_Cmnd ((Scsi_Cmnd *) hostdata->connected,
				pos, buffer, length);
    SPRINTF("scsi%d: issue_queue\n", HOSTNO);
    check_offset();
    for (ptr = (Scsi_Cmnd *) hostdata->issue_queue; ptr; ptr = NEXT(ptr)) {
	pos = lprint_Scsi_Cmnd (ptr, pos, buffer, length);
	check_offset();
    }

    SPRINTF("scsi%d: disconnected_queue\n", HOSTNO);
    check_offset();
    for (ptr = (Scsi_Cmnd *) hostdata->disconnected_queue; ptr;
	 ptr = NEXT(ptr)) {
	pos = lprint_Scsi_Cmnd (ptr, pos, buffer, length);
	check_offset();
    }

    local_irq_restore(flags);
    *start = buffer + (offset - begin);
    if (pos - buffer < offset - begin)
	return 0;
    else if (pos - buffer - (offset - begin) < length)
	return pos - buffer - (offset - begin);
    return length;
}

static char *
lprint_Scsi_Cmnd (Scsi_Cmnd *cmd, char *pos, char *buffer, int length)
{
    int i, s;
    unsigned char *command;
    SPRINTF("scsi%d: destination target %d, lun %d\n",
	    H_NO(cmd), cmd->device->id, cmd->device->lun);
    SPRINTF("        command = ");
    command = cmd->cmnd;
    SPRINTF("%2d (0x%02x)", command[0], command[0]);
    for (i = 1, s = COMMAND_SIZE(command[0]); i < s; ++i)
	SPRINTF(" %02x", command[i]);
    SPRINTF("\n");
    return pos;
}


/* 
 * Function : void NCR5380_init (struct Scsi_Host *instance)
 *
 * Purpose : initializes *instance and corresponding 5380 chip.
 *
 * Inputs : instance - instantiation of the 5380 driver.  
 *
 * Notes : I assume that the host, hostno, and id bits have been
 * 	set correctly.  I don't care about the irq and other fields. 
 * 
 */

static int NCR5380_init (struct Scsi_Host *instance, int flags)
{
    int i;
    SETUP_HOSTDATA(instance);

    NCR5380_all_init();

    hostdata->aborted = 0;
    hostdata->id_mask = 1 << instance->this_id;
    hostdata->id_higher_mask = 0;
    for (i = hostdata->id_mask; i <= 0x80; i <<= 1)
	if (i > hostdata->id_mask)
	    hostdata->id_higher_mask |= i;
    for (i = 0; i < 8; ++i)
	hostdata->busy[i] = 0;
#ifdef SUPPORT_TAGS
    init_tags();
#endif
#if defined (REAL_DMA)
    hostdata->dma_len = 0;
#endif
    hostdata->targets_present = 0;
    hostdata->connected = NULL;
    hostdata->issue_queue = NULL;
    hostdata->disconnected_queue = NULL;
    hostdata->flags = FLAG_CHECK_LAST_BYTE_SENT;

    if (!the_template) {
	the_template = instance->hostt;
	first_instance = instance;
    }
	

#ifndef AUTOSENSE
    if ((instance->cmd_per_lun > 1) || (instance->can_queue > 1))
	 printk("scsi%d: WARNING : support for multiple outstanding commands enabled\n"
	        "        without AUTOSENSE option, contingent allegiance conditions may\n"
	        "        be incorrectly cleared.\n", HOSTNO);
#endif /* def AUTOSENSE */

    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
    NCR5380_write(MODE_REG, MR_BASE);
    NCR5380_write(TARGET_COMMAND_REG, 0);
    NCR5380_write(SELECT_ENABLE_REG, 0);

    return 0;
}

/* 
 * Function : int NCR5380_queue_command (Scsi_Cmnd *cmd, 
 *	void (*done)(Scsi_Cmnd *)) 
 *
 * Purpose :  enqueues a SCSI command
 *
 * Inputs : cmd - SCSI command, done - function called on completion, with
 *	a pointer to the command descriptor.
 * 
 * Returns : 0
 *
 * Side effects : 
 *      cmd is added to the per instance issue_queue, with minor 
 *	twiddling done to the host specific fields of cmd.  If the 
 *	main coroutine is not running, it is restarted.
 *
 */

static
int NCR5380_queue_command (Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
    SETUP_HOSTDATA(cmd->device->host);
    Scsi_Cmnd *tmp;
    int oldto;
    unsigned long flags;
    extern int update_timeout(Scsi_Cmnd * SCset, int timeout);

#if (NDEBUG & NDEBUG_NO_WRITE)
    switch (cmd->cmnd[0]) {
    case WRITE_6:
    case WRITE_10:
	printk(KERN_NOTICE "scsi%d: WRITE attempted with NO_WRITE debugging flag set\n",
	       H_NO(cmd));
	cmd->result = (DID_ERROR << 16);
	done(cmd);
	return 0;
    }
#endif /* (NDEBUG & NDEBUG_NO_WRITE) */


#ifdef NCR5380_STATS
# if 0
    if (!hostdata->connected && !hostdata->issue_queue &&
	!hostdata->disconnected_queue) {
	hostdata->timebase = jiffies;
    }
# endif
# ifdef NCR5380_STAT_LIMIT
    if (cmd->request_bufflen > NCR5380_STAT_LIMIT)
# endif
	switch (cmd->cmnd[0])
	{
	    case WRITE:
	    case WRITE_6:
	    case WRITE_10:
		hostdata->time_write[cmd->device->id] -= (jiffies - hostdata->timebase);
		hostdata->bytes_write[cmd->device->id] += cmd->request_bufflen;
		hostdata->pendingw++;
		break;
	    case READ:
	    case READ_6:
	    case READ_10:
		hostdata->time_read[cmd->device->id] -= (jiffies - hostdata->timebase);
		hostdata->bytes_read[cmd->device->id] += cmd->request_bufflen;
		hostdata->pendingr++;
		break;
	}
#endif

    /* 
     * We use the host_scribble field as a pointer to the next command  
     * in a queue 
     */

    NEXT(cmd) = NULL;
    cmd->scsi_done = done;

    cmd->result = 0;


    /* 
     * Insert the cmd into the issue queue. Note that REQUEST SENSE 
     * commands are added to the head of the queue since any command will
     * clear the contingent allegiance condition that exists and the 
     * sense data is only guaranteed to be valid while the condition exists.
     */

    local_irq_save(flags);
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
    if (!IS_A_TT()) {
	oldto = update_timeout(cmd, 0);
	falcon_get_lock();
	update_timeout(cmd, oldto);
    }
    if (!(hostdata->issue_queue) || (cmd->cmnd[0] == REQUEST_SENSE)) {
	LIST(cmd, hostdata->issue_queue);
	NEXT(cmd) = hostdata->issue_queue;
	hostdata->issue_queue = cmd;
    } else {
	for (tmp = (Scsi_Cmnd *)hostdata->issue_queue;
	     NEXT(tmp); tmp = NEXT(tmp))
	    ;
	LIST(cmd, tmp);
	NEXT(tmp) = cmd;
    }
    local_irq_restore(flags);

    QU_PRINTK("scsi%d: command added to %s of queue\n", H_NO(cmd),
	      (cmd->cmnd[0] == REQUEST_SENSE) ? "head" : "tail");

    /* If queue_command() is called from an interrupt (real one or bottom
     * half), we let queue_main() do the job of taking care about main. If it
     * is already running, this is a no-op, else main will be queued.
     *
     * If we're not in an interrupt, we can call NCR5380_main()
     * unconditionally, because it cannot be already running.
     */
    if (in_interrupt() || ((flags >> 8) & 7) >= 6)
	queue_main();
    else
	NCR5380_main(NULL);
    return 0;
}

/*
 * Function : NCR5380_main (void) 
 *
 * Purpose : NCR5380_main is a coroutine that runs as long as more work can 
 *	be done on the NCR5380 host adapters in a system.  Both 
 *	NCR5380_queue_command() and NCR5380_intr() will try to start it 
 *	in case it is not running.
 * 
 * NOTE : NCR5380_main exits with interrupts *disabled*, the caller should 
 *  reenable them.  This prevents reentrancy and kernel stack overflow.
 */ 	
    
static void NCR5380_main (void *bl)
{
    Scsi_Cmnd *tmp, *prev;
    struct Scsi_Host *instance = first_instance;
    struct NCR5380_hostdata *hostdata = HOSTDATA(instance);
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
    if (main_running)
    	return;
    main_running = 1;

    local_save_flags(flags);
    do {
	local_irq_disable(); /* Freeze request queues */
	done = 1;
	
	if (!hostdata->connected) {
	    MAIN_PRINTK( "scsi%d: not connected\n", HOSTNO );
	    /*
	     * Search through the issue_queue for a command destined
	     * for a target that's not busy.
	     */
#if (NDEBUG & NDEBUG_LISTS)
	    for (tmp = (Scsi_Cmnd *) hostdata->issue_queue, prev = NULL;
		 tmp && (tmp != prev); prev = tmp, tmp = NEXT(tmp))
		;
		/*printk("%p  ", tmp);*/
	    if ((tmp == prev) && tmp) printk(" LOOP\n");/* else printk("\n");*/
#endif
	    for (tmp = (Scsi_Cmnd *) hostdata->issue_queue, 
		 prev = NULL; tmp; prev = tmp, tmp = NEXT(tmp) ) {

#if (NDEBUG & NDEBUG_LISTS)
		if (prev != tmp)
		    printk("MAIN tmp=%p   target=%d   busy=%d lun=%d\n",
			   tmp, tmp->device->id, hostdata->busy[tmp->device->id],
			   tmp->device->lun);
#endif
		/*  When we find one, remove it from the issue queue. */
		/* ++guenther: possible race with Falcon locking */
		if (
#ifdef SUPPORT_TAGS
		    !is_lun_busy( tmp, tmp->cmnd[0] != REQUEST_SENSE)
#else
		    !(hostdata->busy[tmp->device->id] & (1 << tmp->device->lun))
#endif
		    ) {
		    /* ++guenther: just to be sure, this must be atomic */
		    local_irq_disable();
		    if (prev) {
		        REMOVE(prev, NEXT(prev), tmp, NEXT(tmp));
			NEXT(prev) = NEXT(tmp);
		    } else {
		        REMOVE(-1, hostdata->issue_queue, tmp, NEXT(tmp));
			hostdata->issue_queue = NEXT(tmp);
		    }
		    NEXT(tmp) = NULL;
		    falcon_dont_release++;
		    
		    /* reenable interrupts after finding one */
		    local_irq_restore(flags);
		    
		    /* 
		     * Attempt to establish an I_T_L nexus here. 
		     * On success, instance->hostdata->connected is set.
		     * On failure, we must add the command back to the
		     *   issue queue so we can keep trying.	
		     */
		    MAIN_PRINTK("scsi%d: main(): command for target %d "
				"lun %d removed from issue_queue\n",
				HOSTNO, tmp->device->id, tmp->device->lun);
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
		    cmd_get_tag( tmp, tmp->cmnd[0] != REQUEST_SENSE );
#endif
		    if (!NCR5380_select(instance, tmp, 
			    (tmp->cmnd[0] == REQUEST_SENSE) ? TAG_NONE : 
			    TAG_NEXT)) {
			falcon_dont_release--;
			/* release if target did not response! */
			falcon_release_lock_if_possible( hostdata );
			break;
		    } else {
			local_irq_disable();
			LIST(tmp, hostdata->issue_queue);
			NEXT(tmp) = hostdata->issue_queue;
			hostdata->issue_queue = tmp;
#ifdef SUPPORT_TAGS
			cmd_free_tag( tmp );
#endif
			falcon_dont_release--;
			local_irq_restore(flags);
			MAIN_PRINTK("scsi%d: main(): select() failed, "
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
	    MAIN_PRINTK("scsi%d: main: performing information transfer\n",
			HOSTNO);
	    NCR5380_information_transfer(instance);
	    MAIN_PRINTK("scsi%d: main: done set false\n", HOSTNO);
	    done = 0;
	}
    } while (!done);

    /* Better allow ints _after_ 'main_running' has been cleared, else
       an interrupt could believe we'll pick up the work it left for
       us, but we won't see it anymore here... */
    main_running = 0;
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

static void NCR5380_dma_complete( struct Scsi_Host *instance )
{
    SETUP_HOSTDATA(instance);
    int           transfered, saved_data = 0, overrun = 0, cnt, toPIO;
    unsigned char **data, p;
    volatile int  *count;

    if (!hostdata->connected) {
	printk(KERN_WARNING "scsi%d: received end of DMA interrupt with "
	       "no connected cmd\n", HOSTNO);
	return;
    }
    
    if (atari_read_overruns) {
	p = hostdata->connected->SCp.phase;
	if (p & SR_IO) {
	    udelay(10);
	    if ((((NCR5380_read(BUS_AND_STATUS_REG)) &
		  (BASR_PHASE_MATCH|BASR_ACK)) ==
		 (BASR_PHASE_MATCH|BASR_ACK))) {
		saved_data = NCR5380_read(INPUT_DATA_REG);
		overrun = 1;
		DMA_PRINTK("scsi%d: read overrun handled\n", HOSTNO);
	    }
	}
    }

    DMA_PRINTK("scsi%d: real DMA transfer complete, basr 0x%X, sr 0x%X\n",
	       HOSTNO, NCR5380_read(BUS_AND_STATUS_REG),
	       NCR5380_read(STATUS_REG));

    (void) NCR5380_read(RESET_PARITY_INTERRUPT_REG);
    NCR5380_write(MODE_REG, MR_BASE);
    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);

    transfered = hostdata->dma_len - NCR5380_dma_residual(instance);
    hostdata->dma_len = 0;

    data = (unsigned char **) &(hostdata->connected->SCp.ptr);
    count = &(hostdata->connected->SCp.this_residual);
    *data += transfered;
    *count -= transfered;

    if (atari_read_overruns) {
	if ((NCR5380_read(STATUS_REG) & PHASE_MASK) == p && (p & SR_IO)) {
	    cnt = toPIO = atari_read_overruns;
	    if (overrun) {
		DMA_PRINTK("Got an input overrun, using saved byte\n");
		*(*data)++ = saved_data;
		(*count)--;
		cnt--;
		toPIO--;
	    }
	    DMA_PRINTK("Doing %d-byte PIO to 0x%08lx\n", cnt, (long)*data);
	    NCR5380_transfer_pio(instance, &p, &cnt, data);
	    *count -= toPIO - cnt;
	}
    }
}
#endif /* REAL_DMA */


/*
 * Function : void NCR5380_intr (int irq)
 * 
 * Purpose : handle interrupts, reestablishing I_T_L or I_T_L_Q nexuses
 *	from the disconnected queue, and restarting NCR5380_main() 
 *	as required.
 *
 * Inputs : int irq, irq that caused this interrupt.
 *
 */

static irqreturn_t NCR5380_intr (int irq, void *dev_id)
{
    struct Scsi_Host *instance = first_instance;
    int done = 1, handled = 0;
    unsigned char basr;

    INT_PRINTK("scsi%d: NCR5380 irq triggered\n", HOSTNO);

    /* Look for pending interrupts */
    basr = NCR5380_read(BUS_AND_STATUS_REG);
    INT_PRINTK("scsi%d: BASR=%02x\n", HOSTNO, basr);
    /* dispatch to appropriate routine if found and done=0 */
    if (basr & BASR_IRQ) {
	NCR_PRINT(NDEBUG_INTR);
	if ((NCR5380_read(STATUS_REG) & (SR_SEL|SR_IO)) == (SR_SEL|SR_IO)) {
	    done = 0;
	    ENABLE_IRQ();
	    INT_PRINTK("scsi%d: SEL interrupt\n", HOSTNO);
	    NCR5380_reselect(instance);
	    (void) NCR5380_read(RESET_PARITY_INTERRUPT_REG);
	}
	else if (basr & BASR_PARITY_ERROR) {
	    INT_PRINTK("scsi%d: PARITY interrupt\n", HOSTNO);
	    (void) NCR5380_read(RESET_PARITY_INTERRUPT_REG);
	}
	else if ((NCR5380_read(STATUS_REG) & SR_RST) == SR_RST) {
	    INT_PRINTK("scsi%d: RESET interrupt\n", HOSTNO);
	    (void)NCR5380_read(RESET_PARITY_INTERRUPT_REG);
	}
	else {
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
		    
		INT_PRINTK("scsi%d: PHASE MISM or EOP interrupt\n", HOSTNO);
		NCR5380_dma_complete( instance );
		done = 0;
		ENABLE_IRQ();
	    } else
#endif /* REAL_DMA */
	    {
/* MS: Ignore unknown phase mismatch interrupts (caused by EOP interrupt) */
		if (basr & BASR_PHASE_MATCH)
		    printk(KERN_NOTICE "scsi%d: unknown interrupt, "
			   "BASR 0x%x, MR 0x%x, SR 0x%x\n",
			   HOSTNO, basr, NCR5380_read(MODE_REG),
			   NCR5380_read(STATUS_REG));
		(void) NCR5380_read(RESET_PARITY_INTERRUPT_REG);
	    }
	} /* if !(SELECTION || PARITY) */
	handled = 1;
    } /* BASR & IRQ */
    else {
	printk(KERN_NOTICE "scsi%d: interrupt without IRQ bit set in BASR, "
	       "BASR 0x%X, MR 0x%X, SR 0x%x\n", HOSTNO, basr,
	       NCR5380_read(MODE_REG), NCR5380_read(STATUS_REG));
	(void) NCR5380_read(RESET_PARITY_INTERRUPT_REG);
    }
    
    if (!done) {
	INT_PRINTK("scsi%d: in int routine, calling main\n", HOSTNO);
	/* Put a call to NCR5380_main() on the queue... */
	queue_main();
    }
    return IRQ_RETVAL(handled);
}

#ifdef NCR5380_STATS
static void collect_stats(struct NCR5380_hostdata* hostdata, Scsi_Cmnd* cmd)
{
# ifdef NCR5380_STAT_LIMIT
    if (cmd->request_bufflen > NCR5380_STAT_LIMIT)
# endif
	switch (cmd->cmnd[0])
	{
	    case WRITE:
	    case WRITE_6:
	    case WRITE_10:
		hostdata->time_write[cmd->device->id] += (jiffies - hostdata->timebase);
		/*hostdata->bytes_write[cmd->device->id] += cmd->request_bufflen;*/
		hostdata->pendingw--;
		break;
	    case READ:
	    case READ_6:
	    case READ_10:
		hostdata->time_read[cmd->device->id] += (jiffies - hostdata->timebase);
		/*hostdata->bytes_read[cmd->device->id] += cmd->request_bufflen;*/
		hostdata->pendingr--;
		break;
	}
}
#endif

/* 
 * Function : int NCR5380_select (struct Scsi_Host *instance, Scsi_Cmnd *cmd, 
 *	int tag);
 *
 * Purpose : establishes I_T_L or I_T_L_Q nexus for new or existing command,
 *	including ARBITRATION, SELECTION, and initial message out for 
 *	IDENTIFY and queue messages. 
 *
 * Inputs : instance - instantiation of the 5380 driver on which this 
 * 	target lives, cmd - SCSI command to execute, tag - set to TAG_NEXT for 
 *	new tag, TAG_NONE for untagged queueing, otherwise set to the tag for 
 *	the command that is presently connected.
 * 
 * Returns : -1 if selection could not execute for some reason,
 *	0 if selection succeeded or failed because the target 
 * 	did not respond.
 *
 * Side effects : 
 * 	If bus busy, arbitration failed, etc, NCR5380_select() will exit 
 *		with registers as they should have been on entry - ie
 *		SELECT_ENABLE will be set appropriately, the NCR5380
 *		will cease to drive any SCSI bus signals.
 *
 *	If successful : I_T_L or I_T_L_Q nexus will be established, 
 *		instance->connected will be set to cmd.  
 * 		SELECT interrupt will be disabled.
 *
 *	If failed (no target) : cmd->scsi_done() will be called, and the 
 *		cmd->result host byte set to DID_BAD_TARGET.
 */

static int NCR5380_select (struct Scsi_Host *instance, Scsi_Cmnd *cmd, int tag)
{
    SETUP_HOSTDATA(instance);
    unsigned char tmp[3], phase;
    unsigned char *data;
    int len;
    unsigned long timeout;
    unsigned long flags;

    hostdata->restart_select = 0;
    NCR_PRINT(NDEBUG_ARBITRATION);
    ARB_PRINTK("scsi%d: starting arbitration, id = %d\n", HOSTNO,
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
#if NCR_TIMEOUT
    {
      unsigned long timeout = jiffies + 2*NCR_TIMEOUT;

      while (!(NCR5380_read(INITIATOR_COMMAND_REG) & ICR_ARBITRATION_PROGRESS)
	   && time_before(jiffies, timeout) && !hostdata->connected)
	;
      if (time_after_eq(jiffies, timeout))
      {
	printk("scsi : arbitration timeout at %d\n", __LINE__);
	NCR5380_write(MODE_REG, MR_BASE);
	NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
	return -1;
      }
    }
#else /* NCR_TIMEOUT */
    while (!(NCR5380_read(INITIATOR_COMMAND_REG) & ICR_ARBITRATION_PROGRESS)
	 && !hostdata->connected);
#endif

    ARB_PRINTK("scsi%d: arbitration complete\n", HOSTNO);

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
	ARB_PRINTK("scsi%d: lost arbitration, deasserting MR_ARBITRATE\n",
		   HOSTNO);
	return -1;
    }

     /* after/during arbitration, BSY should be asserted.
	IBM DPES-31080 Version S31Q works now */
     /* Tnx to Thomas_Roesch@m2.maus.de for finding this! (Roman) */
    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_SEL |
					 ICR_ASSERT_BSY ) ;
    
    if ((NCR5380_read(INITIATOR_COMMAND_REG) & ICR_ARBITRATION_LOST) ||
	hostdata->connected) {
	NCR5380_write(MODE_REG, MR_BASE);
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
	ARB_PRINTK("scsi%d: lost arbitration, deasserting ICR_ASSERT_SEL\n",
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

    ARB_PRINTK("scsi%d: won arbitration\n", HOSTNO);

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

    SEL_PRINTK("scsi%d: selecting target %d\n", HOSTNO, cmd->device->id);

    /* 
     * The SCSI specification calls for a 250 ms timeout for the actual 
     * selection.
     */

    timeout = jiffies + 25; 

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

    while (time_before(jiffies, timeout) && !(NCR5380_read(STATUS_REG) & 
	(SR_BSY | SR_IO)));

    if ((NCR5380_read(STATUS_REG) & (SR_SEL | SR_IO)) == 
	    (SR_SEL | SR_IO)) {
	    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
	    NCR5380_reselect(instance);
	    printk (KERN_ERR "scsi%d: reselection after won arbitration?\n",
		    HOSTNO);
	    NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
	    return -1;
    }
#else
    while (time_before(jiffies, timeout) && !(NCR5380_read(STATUS_REG) & SR_BSY));
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
	    NCR_PRINT(NDEBUG_ANY);
	    NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
	    return -1;
	}
	cmd->result = DID_BAD_TARGET << 16;
#ifdef NCR5380_STATS
	collect_stats(hostdata, cmd);
#endif
#ifdef SUPPORT_TAGS
	cmd_free_tag( cmd );
#endif
	cmd->scsi_done(cmd);
	NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
	SEL_PRINTK("scsi%d: target did not respond within 250ms\n", HOSTNO);
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
    while (!(NCR5380_read(STATUS_REG) & SR_REQ));

    SEL_PRINTK("scsi%d: target %d selected, going into MESSAGE OUT phase.\n",
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
    cmd->tag=0;
#endif /* SUPPORT_TAGS */

    /* Send message(s) */
    data = tmp;
    phase = PHASE_MSGOUT;
    NCR5380_transfer_pio(instance, &phase, &len, &data);
    SEL_PRINTK("scsi%d: nexus established.\n", HOSTNO);
    /* XXX need to handle errors here */
    hostdata->connected = cmd;
#ifndef SUPPORT_TAGS
    hostdata->busy[cmd->device->id] |= (1 << cmd->device->lun);
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
 *	maximum number of bytes, 0 if all bytes are transfered or exit
 *	is in same phase.
 *
 * 	Also, *phase, *count, *data are modified in place.
 *
 * XXX Note : handling for bus free may be useful.
 */

/*
 * Note : this code is not as quick as it could be, however it 
 * IS 100% reliable, and for the actual data transfer where speed
 * counts, we will always do a pseudo DMA or DMA transfer.
 */

static int NCR5380_transfer_pio( struct Scsi_Host *instance, 
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
	while (!((tmp = NCR5380_read(STATUS_REG)) & SR_REQ));

	HSH_PRINTK("scsi%d: REQ detected\n", HOSTNO);

	/* Check for phase mismatch */	
	if ((tmp & PHASE_MASK) != p) {
	    PIO_PRINTK("scsi%d: phase mismatch\n", HOSTNO);
	    NCR_PRINT_PHASE(NDEBUG_PIO);
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
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | 
		    ICR_ASSERT_DATA);
		NCR_PRINT(NDEBUG_PIO);
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | 
			ICR_ASSERT_DATA | ICR_ASSERT_ACK);
	    } else {
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE |
		    ICR_ASSERT_DATA | ICR_ASSERT_ATN);
		NCR_PRINT(NDEBUG_PIO);
		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | 
		    ICR_ASSERT_DATA | ICR_ASSERT_ATN | ICR_ASSERT_ACK);
	    }
	} else {
	    NCR_PRINT(NDEBUG_PIO);
	    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ACK);
	}

	while (NCR5380_read(STATUS_REG) & SR_REQ);

	HSH_PRINTK("scsi%d: req false, handshake complete\n", HOSTNO);

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

    PIO_PRINTK("scsi%d: residual %d\n", HOSTNO, c);

    *count = c;
    *data = d;
    tmp = NCR5380_read(STATUS_REG);
    /* The phase read from the bus is valid if either REQ is (already)
     * asserted or if ACK hasn't been released yet. The latter is the case if
     * we're in MSGIN and all wanted bytes have been received. */
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
 * 	called from a routine which can drop into a 
 * 
 * Returns : 0 on success, -1 on failure.
 */

static int do_abort (struct Scsi_Host *host) 
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
    
    while (!(tmp = NCR5380_read(STATUS_REG)) & SR_REQ);

    NCR5380_write(TARGET_COMMAND_REG, PHASE_SR_TO_TCR(tmp));

    if ((tmp & PHASE_MASK) != PHASE_MSGOUT) {
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ATN | 
		      ICR_ASSERT_ACK);
	while (NCR5380_read(STATUS_REG) & SR_REQ);
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ATN);
    }
   
    tmp = ABORT;
    msgptr = &tmp;
    len = 1;
    phase = PHASE_MSGOUT;
    NCR5380_transfer_pio (host, &phase, &len, &msgptr);

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
 *	maximum number of bytes, 0 if all bytes or transfered or exit
 *	is in same phase.
 *
 * 	Also, *phase, *count, *data are modified in place.
 *
 */


static int NCR5380_transfer_dma( struct Scsi_Host *instance, 
				 unsigned char *phase, int *count,
				 unsigned char **data)
{
    SETUP_HOSTDATA(instance);
    register int c = *count;
    register unsigned char p = *phase;
    register unsigned char *d = *data;
    unsigned char tmp;
    unsigned long flags;

    if ((tmp = (NCR5380_read(STATUS_REG) & PHASE_MASK)) != p) {
        *phase = tmp;
        return -1;
    }

    if (atari_read_overruns && (p & SR_IO)) {
	c -= atari_read_overruns;
    }

    DMA_PRINTK("scsi%d: initializing DMA for %s, %d bytes %s %p\n",
	       HOSTNO, (p & SR_IO) ? "reading" : "writing",
	       c, (p & SR_IO) ? "to" : "from", d);

    NCR5380_write(TARGET_COMMAND_REG, PHASE_SR_TO_TCR(p));

#ifdef REAL_DMA
    NCR5380_write(MODE_REG, MR_BASE | MR_DMA_MODE | MR_ENABLE_EOP_INTR | MR_MONITOR_BSY);
#endif /* def REAL_DMA  */

    if (IS_A_TT()) {
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

    if (!IS_A_TT()) {
	/* On the Falcon, the DMA setup must be done after the last */
	/* NCR access, else the DMA setup gets trashed!
	 */
	local_irq_save(flags);
	hostdata->dma_len = (p & SR_IO) ?
	    NCR5380_dma_read_setup(instance, d, c) : 
	    NCR5380_dma_write_setup(instance, d, c);
	local_irq_restore(flags);
    }
    return 0;
}
#endif /* defined(REAL_DMA) */

/*
 * Function : NCR5380_information_transfer (struct Scsi_Host *instance)
 *
 * Purpose : run through the various SCSI phases and do as the target 
 * 	directs us to.  Operates on the currently connected command, 
 *	instance->connected.
 *
 * Inputs : instance, instance for which we are doing commands
 *
 * Side effects : SCSI things happen, the disconnected queue will be 
 *	modified if a command disconnects, *instance->connected will
 *	change.
 *
 * XXX Note : we need to watch for bus free or a reset condition here 
 * 	to recover from an unexpected bus free condition.
 */
 
static void NCR5380_information_transfer (struct Scsi_Host *instance)
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
    unsigned char phase, tmp, extended_msg[10], old_phase=0xff;
    Scsi_Cmnd *cmd = (Scsi_Cmnd *) hostdata->connected;

    while (1) {
	tmp = NCR5380_read(STATUS_REG);
	/* We only have a valid SCSI phase when REQ is asserted */
	if (tmp & SR_REQ) {
	    phase = (tmp & PHASE_MASK); 
	    if (phase != old_phase) {
		old_phase = phase;
		NCR_PRINT_PHASE(NDEBUG_INFORMATION);
	    }
	    
	    if (sink && (phase != PHASE_MSGOUT)) {
		NCR5380_write(TARGET_COMMAND_REG, PHASE_SR_TO_TCR(tmp));

		NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ATN | 
		    ICR_ASSERT_ACK);
		while (NCR5380_read(STATUS_REG) & SR_REQ);
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
		cmd->result = DID_ERROR  << 16;
		cmd->done(cmd);
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
		    cmd->SCp.ptr = page_address(cmd->SCp.buffer->page)+
				   cmd->SCp.buffer->offset;
		    /* ++roman: Try to merge some scatter-buffers if
		     * they are at contiguous physical addresses.
		     */
		    merge_contiguous_buffers( cmd );
		    INF_PRINTK("scsi%d: %d bytes and %d buffers left\n",
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
		if (!cmd->device->borken &&
		    (transfersize = NCR5380_dma_xfer_len(instance,cmd,phase)) > 31) {
		    len = transfersize;
		    cmd->SCp.phase = phase;
		    if (NCR5380_transfer_dma(instance, &phase,
			&len, (unsigned char **) &cmd->SCp.ptr)) {
			/*
			 * If the watchdog timer fires, all future
			 * accesses to this device will use the
			 * polled-IO. */ 
			printk(KERN_NOTICE "scsi%d: switching target %d "
			       "lun %d to slow handshake\n", HOSTNO,
			       cmd->device->id, cmd->device->lun);
			cmd->device->borken = 1;
			NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | 
			    ICR_ASSERT_ATN);
			sink = 1;
			do_abort(instance);
			cmd->result = DID_ERROR  << 16;
			cmd->done(cmd);
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
		    (int *) &cmd->SCp.this_residual, (unsigned char **)
		    &cmd->SCp.ptr);
		break;
	    case PHASE_MSGIN:
		len = 1;
		data = &tmp;
		NCR5380_write(SELECT_ENABLE_REG, 0); 	/* disable reselects */
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
		    
		    LNK_PRINTK("scsi%d: target %d lun %d linked command "
			       "complete.\n", HOSTNO, cmd->device->id, cmd->device->lun);

		    /* Enable reselect interrupts */
		    NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
		    /*
		     * Sanity check : A linked command should only terminate
		     * with one of these messages if there are more linked
		     * commands available.
		     */

		    if (!cmd->next_link) {
			 printk(KERN_NOTICE "scsi%d: target %d lun %d "
				"linked command complete, no next_link\n",
				HOSTNO, cmd->device->id, cmd->device->lun);
			    sink = 1;
			    do_abort (instance);
			    return;
		    }

		    initialize_SCp(cmd->next_link);
		    /* The next command is still part of this process; copy it
		     * and don't free it! */
		    cmd->next_link->tag = cmd->tag;
		    cmd->result = cmd->SCp.Status | (cmd->SCp.Message << 8); 
		    LNK_PRINTK("scsi%d: target %d lun %d linked request "
			       "done, calling scsi_done().\n",
			       HOSTNO, cmd->device->id, cmd->device->lun);
#ifdef NCR5380_STATS
		    collect_stats(hostdata, cmd);
#endif
		    cmd->scsi_done(cmd);
		    cmd = hostdata->connected;
		    break;
#endif /* def LINKED */
		case ABORT:
		case COMMAND_COMPLETE: 
		    /* Accept message by clearing ACK */
		    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
		    /* ++guenther: possible race with Falcon locking */
		    falcon_dont_release++;
		    hostdata->connected = NULL;
		    QU_PRINTK("scsi%d: command for target %d, lun %d "
			      "completed\n", HOSTNO, cmd->device->id, cmd->device->lun);
#ifdef SUPPORT_TAGS
		    cmd_free_tag( cmd );
		    if (status_byte(cmd->SCp.Status) == QUEUE_FULL) {
			/* Turn a QUEUE FULL status into BUSY, I think the
			 * mid level cannot handle QUEUE FULL :-( (The
			 * command is retried after BUSY). Also update our
			 * queue size to the number of currently issued
			 * commands now.
			 */
			/* ++Andreas: the mid level code knows about
			   QUEUE_FULL now. */
			TAG_ALLOC *ta = &TagAlloc[cmd->device->id][cmd->device->lun];
			TAG_PRINTK("scsi%d: target %d lun %d returned "
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
		    
#ifdef AUTOSENSE
		    if ((cmd->cmnd[0] != REQUEST_SENSE) && 
			(status_byte(cmd->SCp.Status) == CHECK_CONDITION)) {
			ASEN_PRINTK("scsi%d: performing request sense\n",
				    HOSTNO);
			cmd->cmnd[0] = REQUEST_SENSE;
			cmd->cmnd[1] &= 0xe0;
			cmd->cmnd[2] = 0;
			cmd->cmnd[3] = 0;
			cmd->cmnd[4] = sizeof(cmd->sense_buffer);
			cmd->cmnd[5] = 0;
			cmd->cmd_len = COMMAND_SIZE(cmd->cmnd[0]);

			cmd->use_sg = 0;
			/* this is initialized from initialize_SCp 
			cmd->SCp.buffer = NULL;
			cmd->SCp.buffers_residual = 0;
			*/
			cmd->request_buffer = (char *) cmd->sense_buffer;
			cmd->request_bufflen = sizeof(cmd->sense_buffer);

			local_irq_save(flags);
			LIST(cmd,hostdata->issue_queue);
			NEXT(cmd) = hostdata->issue_queue;
		        hostdata->issue_queue = (Scsi_Cmnd *) cmd;
		        local_irq_restore(flags);
			QU_PRINTK("scsi%d: REQUEST SENSE added to head of "
				  "issue queue\n", H_NO(cmd));
		   } else
#endif /* def AUTOSENSE */
		   {
#ifdef NCR5380_STATS
		       collect_stats(hostdata, cmd);
#endif
		       cmd->scsi_done(cmd);
		    }

		    NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
		    /* 
		     * Restore phase bits to 0 so an interrupted selection, 
		     * arbitration can resume.
		     */
		    NCR5380_write(TARGET_COMMAND_REG, 0);
		    
		    while ((NCR5380_read(STATUS_REG) & SR_BSY) && !hostdata->connected)
			barrier();

		    falcon_dont_release--;
		    /* ++roman: For Falcon SCSI, release the lock on the
		     * ST-DMA here if no other commands are waiting on the
		     * disconnected queue.
		     */
		    falcon_release_lock_if_possible( hostdata );
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
			TAG_PRINTK("scsi%d: target %d lun %d rejected "
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
		    NEXT(cmd) = hostdata->disconnected_queue;
		    hostdata->connected = NULL;
		    hostdata->disconnected_queue = cmd;
		    local_irq_restore(flags);
		    QU_PRINTK("scsi%d: command for target %d lun %d was "
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
 * 2 		code
 * 3..length+1	arguments
 *
 * Start the extended message buffer with the EXTENDED_MESSAGE
 * byte, since spi_print_msg() wants the whole thing.  
 */
		    extended_msg[0] = EXTENDED_MESSAGE;
		    /* Accept first byte by clearing ACK */
		    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);

		    EXT_PRINTK("scsi%d: receiving extended message\n", HOSTNO);

		    len = 2;
		    data = extended_msg + 1;
		    phase = PHASE_MSGIN;
		    NCR5380_transfer_pio(instance, &phase, &len, &data);
		    EXT_PRINTK("scsi%d: length=%d, code=0x%02x\n", HOSTNO,
			       (int)extended_msg[1], (int)extended_msg[2]);

		    if (!len && extended_msg[1] <= 
			(sizeof (extended_msg) - 1)) {
			/* Accept third byte by clearing ACK */
			NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
			len = extended_msg[1] - 1;
			data = extended_msg + 3;
			phase = PHASE_MSGIN;

			NCR5380_transfer_pio(instance, &phase, &len, &data);
			EXT_PRINTK("scsi%d: message received, residual %d\n",
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
			printk(KERN_DEBUG "scsi%d: rejecting message ", HOSTNO);
			spi_print_msg(extended_msg);
			printk("\n");
		    } else if (tmp != EXTENDED_MESSAGE)
			printk(KERN_DEBUG "scsi%d: rejecting unknown "
			       "message %02x from target %d, lun %d\n",
			       HOSTNO, tmp, cmd->device->id, cmd->device->lun);
		    else
			printk(KERN_DEBUG "scsi%d: rejecting unknown "
			       "extended message "
			       "code %02x, length %d from target %d, lun %d\n",
			       HOSTNO, extended_msg[1], extended_msg[0],
			       cmd->device->id, cmd->device->lun);
   

		    msgout = MESSAGE_REJECT;
		    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | 
			ICR_ASSERT_ATN);
		    break;
		} /* switch (tmp) */
		break;
	    case PHASE_MSGOUT:
		len = 1;
		data = &msgout;
		hostdata->last_message = msgout;
		NCR5380_transfer_pio(instance, &phase, &len, &data);
		if (msgout == ABORT) {
#ifdef SUPPORT_TAGS
		    cmd_free_tag( cmd );
#else
		    hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
#endif
		    hostdata->connected = NULL;
		    cmd->result = DID_ERROR << 16;
#ifdef NCR5380_STATS
		    collect_stats(hostdata, cmd);
#endif
		    cmd->scsi_done(cmd);
		    NCR5380_write(SELECT_ENABLE_REG, hostdata->id_mask);
		    falcon_release_lock_if_possible( hostdata );
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
		NCR5380_transfer_pio(instance, &phase, &len, 
		    &data);
		break;
	    case PHASE_STATIN:
		len = 1;
		data = &tmp;
		NCR5380_transfer_pio(instance, &phase, &len, &data);
		cmd->SCp.Status = tmp;
		break;
	    default:
		printk("scsi%d: unknown phase\n", HOSTNO);
		NCR_PRINT(NDEBUG_ANY);
	    } /* switch(phase) */
	} /* if (tmp * SR_REQ) */ 
    } /* while (1) */
}

/*
 * Function : void NCR5380_reselect (struct Scsi_Host *instance)
 *
 * Purpose : does reselection, initializing the instance->connected 
 *	field to point to the Scsi_Cmnd for which the I_T_L or I_T_L_Q 
 *	nexus has been reestablished,
 *	
 * Inputs : instance - this instance of the NCR5380.
 *
 */


static void NCR5380_reselect (struct Scsi_Host *instance)
{
    SETUP_HOSTDATA(instance);
    unsigned char target_mask;
    unsigned char lun, phase;
    int len;
#ifdef SUPPORT_TAGS
    unsigned char tag;
#endif
    unsigned char msg[3];
    unsigned char *data;
    Scsi_Cmnd *tmp = NULL, *prev;
/*    unsigned long flags; */

    /*
     * Disable arbitration, etc. since the host adapter obviously
     * lost, and tell an interrupted NCR5380_select() to restart.
     */

    NCR5380_write(MODE_REG, MR_BASE);
    hostdata->restart_select = 1;

    target_mask = NCR5380_read(CURRENT_SCSI_DATA_REG) & ~(hostdata->id_mask);

    RSL_PRINTK("scsi%d: reselect\n", HOSTNO);

    /* 
     * At this point, we have detected that our SCSI ID is on the bus,
     * SEL is true and BSY was false for at least one bus settle delay
     * (400 ns).
     *
     * We must assert BSY ourselves, until the target drops the SEL
     * signal.
     */

    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_BSY);
    
    while (NCR5380_read(STATUS_REG) & SR_SEL);
    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);

    /*
     * Wait for target to go into MSGIN.
     */

    while (!(NCR5380_read(STATUS_REG) & SR_REQ));

    len = 1;
    data = msg;
    phase = PHASE_MSGIN;
    NCR5380_transfer_pio(instance, &phase, &len, &data);

    if (!(msg[0] & 0x80)) {
	printk(KERN_DEBUG "scsi%d: expecting IDENTIFY message, got ", HOSTNO);
	spi_print_msg(msg);
	do_abort(instance);
	return;
    }
    lun = (msg[0] & 0x07);

#ifdef SUPPORT_TAGS
    /* If the phase is still MSGIN, the target wants to send some more
     * messages. In case it supports tagged queuing, this is probably a
     * SIMPLE_QUEUE_TAG for the I_T_L_Q nexus.
     */
    tag = TAG_NONE;
    if (phase == PHASE_MSGIN && setup_use_tagged_queuing) {
	/* Accept previous IDENTIFY message by clearing ACK */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE );
	len = 2;
	data = msg+1;
	if (!NCR5380_transfer_pio(instance, &phase, &len, &data) &&
	    msg[1] == SIMPLE_QUEUE_TAG)
	    tag = msg[2];
	TAG_PRINTK("scsi%d: target mask %02x, lun %d sent tag %d at "
		   "reselection\n", HOSTNO, target_mask, lun, tag);
    }
#endif
    
    /* 
     * Find the command corresponding to the I_T_L or I_T_L_Q  nexus we 
     * just reestablished, and remove it from the disconnected queue.
     */

    for (tmp = (Scsi_Cmnd *) hostdata->disconnected_queue, prev = NULL; 
	 tmp; prev = tmp, tmp = NEXT(tmp) ) {
	if ((target_mask == (1 << tmp->device->id)) && (lun == tmp->device->lun)
#ifdef SUPPORT_TAGS
	    && (tag == tmp->tag) 
#endif
	    ) {
	    /* ++guenther: prevent race with falcon_release_lock */
	    falcon_dont_release++;
	    if (prev) {
		REMOVE(prev, NEXT(prev), tmp, NEXT(tmp));
		NEXT(prev) = NEXT(tmp);
	    } else {
		REMOVE(-1, hostdata->disconnected_queue, tmp, NEXT(tmp));
		hostdata->disconnected_queue = NEXT(tmp);
	    }
	    NEXT(tmp) = NULL;
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

    /* Accept message by clearing ACK */
    NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);

    hostdata->connected = tmp;
    RSL_PRINTK("scsi%d: nexus established, target = %d, lun = %d, tag = %d\n",
	       HOSTNO, tmp->device->id, tmp->device->lun, tmp->tag);
    falcon_dont_release--;
}


/*
 * Function : int NCR5380_abort (Scsi_Cmnd *cmd)
 *
 * Purpose : abort a command
 *
 * Inputs : cmd - the Scsi_Cmnd to abort, code - code to set the 
 * 	host byte of the result field to, if zero DID_ABORTED is 
 *	used.
 *
 * Returns : 0 - success, -1 on failure.
 *
 * XXX - there is no way to abort the command that is currently 
 * 	 connected, you have to wait for it to complete.  If this is 
 *	 a problem, we could implement longjmp() / setjmp(), setjmp()
 * 	 called where the loop started in NCR5380_main().
 */

static
int NCR5380_abort (Scsi_Cmnd *cmd)
{
    struct Scsi_Host *instance = cmd->device->host;
    SETUP_HOSTDATA(instance);
    Scsi_Cmnd *tmp, **prev;
    unsigned long flags;

    printk(KERN_NOTICE "scsi%d: aborting command\n", HOSTNO);
    scsi_print_command(cmd);

    NCR5380_print_status (instance);

    local_irq_save(flags);
    
    if (!IS_A_TT() && !falcon_got_lock)
	printk(KERN_ERR "scsi%d: !!BINGO!! Falcon has no lock in NCR5380_abort\n",
	       HOSTNO);

    ABRT_PRINTK("scsi%d: abort called basr 0x%02x, sr 0x%02x\n", HOSTNO,
		NCR5380_read(BUS_AND_STATUS_REG),
		NCR5380_read(STATUS_REG));

#if 1
/* 
 * Case 1 : If the command is the currently executing command, 
 * we'll set the aborted flag and return control so that 
 * information transfer routine can exit cleanly.
 */

    if (hostdata->connected == cmd) {

	ABRT_PRINTK("scsi%d: aborting connected command\n", HOSTNO);
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
	  cmd_free_tag( cmd );
#else
	  hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
#endif
	  local_irq_restore(flags);
	  cmd->scsi_done(cmd);
	  falcon_release_lock_if_possible( hostdata );
	  return SCSI_ABORT_SUCCESS;
	} else {
/*	  local_irq_restore(flags); */
	  printk("scsi%d: abort of connected command failed!\n", HOSTNO);
	  return SCSI_ABORT_ERROR;
	} 
   }
#endif

/* 
 * Case 2 : If the command hasn't been issued yet, we simply remove it 
 * 	    from the issue queue.
 */
    for (prev = (Scsi_Cmnd **) &(hostdata->issue_queue), 
	tmp = (Scsi_Cmnd *) hostdata->issue_queue;
	tmp; prev = NEXTADDR(tmp), tmp = NEXT(tmp) )
	if (cmd == tmp) {
	    REMOVE(5, *prev, tmp, NEXT(tmp));
	    (*prev) = NEXT(tmp);
	    NEXT(tmp) = NULL;
	    tmp->result = DID_ABORT << 16;
	    local_irq_restore(flags);
	    ABRT_PRINTK("scsi%d: abort removed command from issue queue.\n",
			HOSTNO);
	    /* Tagged queuing note: no tag to free here, hasn't been assigned
	     * yet... */
	    tmp->scsi_done(tmp);
	    falcon_release_lock_if_possible( hostdata );
	    return SCSI_ABORT_SUCCESS;
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
	ABRT_PRINTK("scsi%d: abort failed, command connected.\n", HOSTNO);
        return SCSI_ABORT_SNOOZE;
    }

/*
 * Case 4: If the command is currently disconnected from the bus, and 
 * 	there are no connected commands, we reconnect the I_T_L or 
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

    for (tmp = (Scsi_Cmnd *) hostdata->disconnected_queue; tmp;
	 tmp = NEXT(tmp)) 
        if (cmd == tmp) {
            local_irq_restore(flags);
	    ABRT_PRINTK("scsi%d: aborting disconnected command.\n", HOSTNO);
  
            if (NCR5380_select (instance, cmd, (int) cmd->tag)) 
		return SCSI_ABORT_BUSY;

	    ABRT_PRINTK("scsi%d: nexus reestablished.\n", HOSTNO);

	    do_abort (instance);

	    local_irq_save(flags);
	    for (prev = (Scsi_Cmnd **) &(hostdata->disconnected_queue), 
		tmp = (Scsi_Cmnd *) hostdata->disconnected_queue;
		tmp; prev = NEXTADDR(tmp), tmp = NEXT(tmp) )
		    if (cmd == tmp) {
		    REMOVE(5, *prev, tmp, NEXT(tmp));
		    *prev = NEXT(tmp);
		    NEXT(tmp) = NULL;
		    tmp->result = DID_ABORT << 16;
		    /* We must unlock the tag/LUN immediately here, since the
		     * target goes to BUS FREE and doesn't send us another
		     * message (COMMAND_COMPLETE or the like)
		     */
#ifdef SUPPORT_TAGS
		    cmd_free_tag( tmp );
#else
		    hostdata->busy[cmd->device->id] &= ~(1 << cmd->device->lun);
#endif
		    local_irq_restore(flags);
		    tmp->scsi_done(tmp);
		    falcon_release_lock_if_possible( hostdata );
		    return SCSI_ABORT_SUCCESS;
		}
	}

/*
 * Case 5 : If we reached this point, the command was not found in any of 
 *	    the queues.
 *
 * We probably reached this point because of an unlikely race condition
 * between the command completing successfully and the abortion code,
 * so we won't panic, but we will notify the user in case something really
 * broke.
 */

    local_irq_restore(flags);
    printk(KERN_INFO "scsi%d: warning : SCSI command probably completed successfully\n"
           KERN_INFO "        before abortion\n", HOSTNO); 

/* Maybe it is sufficient just to release the ST-DMA lock... (if
 * possible at all) At least, we should check if the lock could be
 * released after the abort, in case it is kept due to some bug.
 */
    falcon_release_lock_if_possible( hostdata );

    return SCSI_ABORT_NOT_RUNNING;
}


/* 
 * Function : int NCR5380_reset (Scsi_Cmnd *cmd)
 * 
 * Purpose : reset the SCSI bus.
 *
 * Returns : SCSI_RESET_WAKEUP
 *
 */ 

static int NCR5380_bus_reset( Scsi_Cmnd *cmd)
{
    SETUP_HOSTDATA(cmd->device->host);
    int           i;
    unsigned long flags;
#if 1
    Scsi_Cmnd *connected, *disconnected_queue;
#endif

    if (!IS_A_TT() && !falcon_got_lock)
	printk(KERN_ERR "scsi%d: !!BINGO!! Falcon has no lock in NCR5380_reset\n",
	       H_NO(cmd) );

    NCR5380_print_status (cmd->device->host);

    /* get in phase */
    NCR5380_write( TARGET_COMMAND_REG,
		   PHASE_SR_TO_TCR( NCR5380_read(STATUS_REG) ));
    /* assert RST */
    NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_RST );
    udelay (40);
    /* reset NCR registers */
    NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE );
    NCR5380_write( MODE_REG, MR_BASE );
    NCR5380_write( TARGET_COMMAND_REG, 0 );
    NCR5380_write( SELECT_ENABLE_REG, 0 );
    /* ++roman: reset interrupt condition! otherwise no interrupts don't get
     * through anymore ... */
    (void)NCR5380_read( RESET_PARITY_INTERRUPT_REG );

#if 1 /* XXX Should now be done by midlevel code, but it's broken XXX */
      /* XXX see below                                            XXX */

    /* MSch: old-style reset: actually abort all command processing here */

    /* After the reset, there are no more connected or disconnected commands
     * and no busy units; to avoid problems with re-inserting the commands
     * into the issue_queue (via scsi_done()), the aborted commands are
     * remembered in local variables first.
     */
    local_irq_save(flags);
    connected = (Scsi_Cmnd *)hostdata->connected;
    hostdata->connected = NULL;
    disconnected_queue = (Scsi_Cmnd *)hostdata->disconnected_queue;
    hostdata->disconnected_queue = NULL;
#ifdef SUPPORT_TAGS
    free_all_tags();
#endif
    for( i = 0; i < 8; ++i )
	hostdata->busy[i] = 0;
#ifdef REAL_DMA
    hostdata->dma_len = 0;
#endif
    local_irq_restore(flags);

    /* In order to tell the mid-level code which commands were aborted, 
     * set the command status to DID_RESET and call scsi_done() !!!
     * This ultimately aborts processing of these commands in the mid-level.
     */

    if ((cmd = connected)) {
	ABRT_PRINTK("scsi%d: reset aborted a connected command\n", H_NO(cmd));
	cmd->result = (cmd->result & 0xffff) | (DID_RESET << 16);
	cmd->scsi_done( cmd );
    }

    for (i = 0; (cmd = disconnected_queue); ++i) {
	disconnected_queue = NEXT(cmd);
	NEXT(cmd) = NULL;
	cmd->result = (cmd->result & 0xffff) | (DID_RESET << 16);
	cmd->scsi_done( cmd );
    }
    if (i > 0)
	ABRT_PRINTK("scsi: reset aborted %d disconnected command(s)\n", i);

/* The Falcon lock should be released after a reset...
 */
/* ++guenther: moved to atari_scsi_reset(), to prevent a race between
 * unlocking and enabling dma interrupt.
 */
/*    falcon_release_lock_if_possible( hostdata );*/

    /* since all commands have been explicitly terminated, we need to tell
     * the midlevel code that the reset was SUCCESSFUL, and there is no 
     * need to 'wake up' the commands by a request_sense
     */
    return SCSI_RESET_SUCCESS | SCSI_RESET_BUS_RESET;
#else /* 1 */

    /* MSch: new-style reset handling: let the mid-level do what it can */

    /* ++guenther: MID-LEVEL IS STILL BROKEN.
     * Mid-level is supposed to requeue all commands that were active on the
     * various low-level queues. In fact it does this, but that's not enough
     * because all these commands are subject to timeout. And if a timeout
     * happens for any removed command, *_abort() is called but all queues
     * are now empty. Abort then gives up the falcon lock, which is fatal,
     * since the mid-level will queue more commands and must have the lock
     * (it's all happening inside timer interrupt handler!!).
     * Even worse, abort will return NOT_RUNNING for all those commands not
     * on any queue, so they won't be retried ...
     *
     * Conclusion: either scsi.c disables timeout for all resetted commands
     * immediately, or we lose!  As of linux-2.0.20 it doesn't.
     */

    /* After the reset, there are no more connected or disconnected commands
     * and no busy units; so clear the low-level status here to avoid 
     * conflicts when the mid-level code tries to wake up the affected 
     * commands!
     */

    if (hostdata->issue_queue)
	ABRT_PRINTK("scsi%d: reset aborted issued command(s)\n", H_NO(cmd));
    if (hostdata->connected) 
	ABRT_PRINTK("scsi%d: reset aborted a connected command\n", H_NO(cmd));
    if (hostdata->disconnected_queue)
	ABRT_PRINTK("scsi%d: reset aborted disconnected command(s)\n", H_NO(cmd));

    local_irq_save(flags);
    hostdata->issue_queue = NULL;
    hostdata->connected = NULL;
    hostdata->disconnected_queue = NULL;
#ifdef SUPPORT_TAGS
    free_all_tags();
#endif
    for( i = 0; i < 8; ++i )
	hostdata->busy[i] = 0;
#ifdef REAL_DMA
    hostdata->dma_len = 0;
#endif
    local_irq_restore(flags);

    /* we did no complete reset of all commands, so a wakeup is required */
    return SCSI_RESET_WAKEUP | SCSI_RESET_BUS_RESET;
#endif /* 1 */
}

/* Local Variables: */
/* tab-width: 8     */
/* End:             */
