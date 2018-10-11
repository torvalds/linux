/*
 *  drivers/block/ataflop.c
 *
 *  Copyright (C) 1993  Greg Harp
 *  Atari Support by Bjoern Brauel, Roman Hodek
 *
 *  Big cleanup Sep 11..14 1994 Roman Hodek:
 *   - Driver now works interrupt driven
 *   - Support for two drives; should work, but I cannot test that :-(
 *   - Reading is done in whole tracks and buffered to speed up things
 *   - Disk change detection and drive deselecting after motor-off
 *     similar to TOS
 *   - Autodetection of disk format (DD/HD); untested yet, because I
 *     don't have an HD drive :-(
 *
 *  Fixes Nov 13 1994 Martin Schaller:
 *   - Autodetection works now
 *   - Support for 5 1/4'' disks
 *   - Removed drive type (unknown on atari)
 *   - Do seeks with 8 Mhz
 *
 *  Changes by Andreas Schwab:
 *   - After errors in multiple read mode try again reading single sectors
 *  (Feb 1995):
 *   - Clean up error handling
 *   - Set blk_size for proper size checking
 *   - Initialize track register when testing presence of floppy
 *   - Implement some ioctl's
 *
 *  Changes by Torsten Lang:
 *   - When probing the floppies we should add the FDCCMDADD_H flag since
 *     the FDC will otherwise wait forever when no disk is inserted...
 *
 * ++ Freddi Aschwanden (fa) 20.9.95 fixes for medusa:
 *  - MFPDELAY() after each FDC access -> atari 
 *  - more/other disk formats
 *  - DMA to the block buffer directly if we have a 32bit DMA
 *  - for medusa, the step rate is always 3ms
 *  - on medusa, use only cache_push()
 * Roman:
 *  - Make disk format numbering independent from minors
 *  - Let user set max. supported drive type (speeds up format
 *    detection, saves buffer space)
 *
 * Roman 10/15/95:
 *  - implement some more ioctls
 *  - disk formatting
 *  
 * Andreas 95/12/12:
 *  - increase gap size at start of track for HD/ED disks
 *
 * Michael (MSch) 11/07/96:
 *  - implemented FDSETPRM and FDDEFPRM ioctl
 *
 * Andreas (97/03/19):
 *  - implemented missing BLK* ioctls
 *
 *  Things left to do:
 *   - Formatting
 *   - Maybe a better strategy for disk change detection (does anyone
 *     know one?)
 */

#include <linux/module.h>

#include <linux/fd.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/wait.h>

#include <asm/atafd.h>
#include <asm/atafdreg.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>
#include <asm/atari_stram.h>

#define	FD_MAX_UNITS 2

#undef DEBUG

static DEFINE_MUTEX(ataflop_mutex);
static struct request *fd_request;
static int fdc_queue;

/* Disk types: DD, HD, ED */
static struct atari_disk_type {
	const char	*name;
	unsigned	spt;		/* sectors per track */
	unsigned	blocks;		/* total number of blocks */
	unsigned	fdc_speed;	/* fdc_speed setting */
	unsigned 	stretch;	/* track doubling ? */
} atari_disk_type[] = {
	{ "d360",  9, 720, 0, 0},	/*  0: 360kB diskette */
	{ "D360",  9, 720, 0, 1},	/*  1: 360kb in 720k or 1.2MB drive */
	{ "D720",  9,1440, 0, 0},	/*  2: 720kb in 720k or 1.2MB drive */
	{ "D820", 10,1640, 0, 0},	/*  3: DD disk with 82 tracks/10 sectors */
/* formats above are probed for type DD */
#define	MAX_TYPE_DD 3
	{ "h1200",15,2400, 3, 0},	/*  4: 1.2MB diskette */
	{ "H1440",18,2880, 3, 0},	/*  5: 1.4 MB diskette (HD) */
	{ "H1640",20,3280, 3, 0},	/*  6: 1.64MB diskette (fat HD) 82 tr 20 sec */
/* formats above are probed for types DD and HD */
#define	MAX_TYPE_HD 6
	{ "E2880",36,5760, 3, 0},	/*  7: 2.8 MB diskette (ED) */
	{ "E3280",40,6560, 3, 0},	/*  8: 3.2 MB diskette (fat ED) 82 tr 40 sec */
/* formats above are probed for types DD, HD and ED */
#define	MAX_TYPE_ED 8
/* types below are never autoprobed */
	{ "H1680",21,3360, 3, 0},	/*  9: 1.68MB diskette (fat HD) 80 tr 21 sec */
	{ "h410",10,820, 0, 1},		/* 10: 410k diskette 41 tr 10 sec, stretch */
	{ "h1476",18,2952, 3, 0},	/* 11: 1.48MB diskette 82 tr 18 sec */
	{ "H1722",21,3444, 3, 0},	/* 12: 1.72MB diskette 82 tr 21 sec */
	{ "h420",10,840, 0, 1},		/* 13: 420k diskette 42 tr 10 sec, stretch */
	{ "H830",10,1660, 0, 0},	/* 14: 820k diskette 83 tr 10 sec */
	{ "h1494",18,2952, 3, 0},	/* 15: 1.49MB diskette 83 tr 18 sec */
	{ "H1743",21,3486, 3, 0},	/* 16: 1.74MB diskette 83 tr 21 sec */
	{ "h880",11,1760, 0, 0},	/* 17: 880k diskette 80 tr 11 sec */
	{ "D1040",13,2080, 0, 0},	/* 18: 1.04MB diskette 80 tr 13 sec */
	{ "D1120",14,2240, 0, 0},	/* 19: 1.12MB diskette 80 tr 14 sec */
	{ "h1600",20,3200, 3, 0},	/* 20: 1.60MB diskette 80 tr 20 sec */
	{ "H1760",22,3520, 3, 0},	/* 21: 1.76MB diskette 80 tr 22 sec */
	{ "H1920",24,3840, 3, 0},	/* 22: 1.92MB diskette 80 tr 24 sec */
	{ "E3200",40,6400, 3, 0},	/* 23: 3.2MB diskette 80 tr 40 sec */
	{ "E3520",44,7040, 3, 0},	/* 24: 3.52MB diskette 80 tr 44 sec */
	{ "E3840",48,7680, 3, 0},	/* 25: 3.84MB diskette 80 tr 48 sec */
	{ "H1840",23,3680, 3, 0},	/* 26: 1.84MB diskette 80 tr 23 sec */
	{ "D800",10,1600, 0, 0},	/* 27: 800k diskette 80 tr 10 sec */
};

static int StartDiskType[] = {
	MAX_TYPE_DD,
	MAX_TYPE_HD,
	MAX_TYPE_ED
};

#define	TYPE_DD		0
#define	TYPE_HD		1
#define	TYPE_ED		2

static int DriveType = TYPE_HD;

static DEFINE_SPINLOCK(ataflop_lock);

/* Array for translating minors into disk formats */
static struct {
	int 	 index;
	unsigned drive_types;
} minor2disktype[] = {
	{  0, TYPE_DD },	/*  1: d360 */
	{  4, TYPE_HD },	/*  2: h1200 */
	{  1, TYPE_DD },	/*  3: D360 */
	{  2, TYPE_DD },	/*  4: D720 */
	{  1, TYPE_DD },	/*  5: h360 = D360 */
	{  2, TYPE_DD },	/*  6: h720 = D720 */
	{  5, TYPE_HD },	/*  7: H1440 */
	{  7, TYPE_ED },	/*  8: E2880 */
/* some PC formats :-) */
	{  8, TYPE_ED },	/*  9: E3280    <- was "CompaQ" == E2880 for PC */
	{  5, TYPE_HD },	/* 10: h1440 = H1440 */
	{  9, TYPE_HD },	/* 11: H1680 */
	{ 10, TYPE_DD },	/* 12: h410  */
	{  3, TYPE_DD },	/* 13: H820     <- == D820, 82x10 */
	{ 11, TYPE_HD },	/* 14: h1476 */
	{ 12, TYPE_HD },	/* 15: H1722 */
	{ 13, TYPE_DD },	/* 16: h420  */
	{ 14, TYPE_DD },	/* 17: H830  */
	{ 15, TYPE_HD },	/* 18: h1494 */
	{ 16, TYPE_HD },	/* 19: H1743 */
	{ 17, TYPE_DD },	/* 20: h880  */
	{ 18, TYPE_DD },	/* 21: D1040 */
	{ 19, TYPE_DD },	/* 22: D1120 */
	{ 20, TYPE_HD },	/* 23: h1600 */
	{ 21, TYPE_HD },	/* 24: H1760 */
	{ 22, TYPE_HD },	/* 25: H1920 */
	{ 23, TYPE_ED },	/* 26: E3200 */
	{ 24, TYPE_ED },	/* 27: E3520 */
	{ 25, TYPE_ED },	/* 28: E3840 */
	{ 26, TYPE_HD },	/* 29: H1840 */
	{ 27, TYPE_DD },	/* 30: D800  */
	{  6, TYPE_HD },	/* 31: H1640    <- was H1600 == h1600 for PC */
};

#define NUM_DISK_MINORS ARRAY_SIZE(minor2disktype)

/*
 * Maximum disk size (in kilobytes). This default is used whenever the
 * current disk size is unknown.
 */
#define MAX_DISK_SIZE 3280

/*
 * MSch: User-provided type information. 'drive' points to
 * the respective entry of this array. Set by FDSETPRM ioctls.
 */
static struct atari_disk_type user_params[FD_MAX_UNITS];

/*
 * User-provided permanent type information. 'drive' points to
 * the respective entry of this array.  Set by FDDEFPRM ioctls, 
 * restored upon disk change by floppy_revalidate() if valid (as seen by
 * default_params[].blocks > 0 - a bit in unit[].flags might be used for this?)
 */
static struct atari_disk_type default_params[FD_MAX_UNITS];

/* current info on each unit */
static struct atari_floppy_struct {
	int connected;				/* !=0 : drive is connected */
	int autoprobe;				/* !=0 : do autoprobe	    */

	struct atari_disk_type	*disktype;	/* current type of disk */

	int track;		/* current head position or -1 if
				   unknown */
	unsigned int steprate;	/* steprate setting */
	unsigned int wpstat;	/* current state of WP signal (for
				   disk change detection) */
	int flags;		/* flags */
	struct gendisk *disk;
	int ref;
	int type;
} unit[FD_MAX_UNITS];

#define	UD	unit[drive]
#define	UDT	unit[drive].disktype
#define	SUD	unit[SelectedDrive]
#define	SUDT	unit[SelectedDrive].disktype


#define FDC_READ(reg) ({			\
    /* unsigned long __flags; */		\
    unsigned short __val;			\
    /* local_irq_save(__flags); */		\
    dma_wd.dma_mode_status = 0x80 | (reg);	\
    udelay(25);					\
    __val = dma_wd.fdc_acces_seccount;		\
    MFPDELAY();					\
    /* local_irq_restore(__flags); */		\
    __val & 0xff;				\
})

#define FDC_WRITE(reg,val)			\
    do {					\
	/* unsigned long __flags; */		\
	/* local_irq_save(__flags); */		\
	dma_wd.dma_mode_status = 0x80 | (reg);	\
	udelay(25);				\
	dma_wd.fdc_acces_seccount = (val);	\
	MFPDELAY();				\
        /* local_irq_restore(__flags); */	\
    } while(0)


/* Buffering variables:
 * First, there is a DMA buffer in ST-RAM that is used for floppy DMA
 * operations. Second, a track buffer is used to cache a whole track
 * of the disk to save read operations. These are two separate buffers
 * because that allows write operations without clearing the track buffer.
 */

static int MaxSectors[] = {
	11, 22, 44
};
static int BufferSize[] = {
	15*512, 30*512, 60*512
};

#define	BUFFER_SIZE	(BufferSize[DriveType])

unsigned char *DMABuffer;			  /* buffer for writes */
static unsigned long PhysDMABuffer;   /* physical address */

static int UseTrackbuffer = -1;		  /* Do track buffering? */
module_param(UseTrackbuffer, int, 0);

unsigned char *TrackBuffer;			  /* buffer for reads */
static unsigned long PhysTrackBuffer; /* physical address */
static int BufferDrive, BufferSide, BufferTrack;
static int read_track;		/* non-zero if we are reading whole tracks */

#define	SECTOR_BUFFER(sec)	(TrackBuffer + ((sec)-1)*512)
#define	IS_BUFFERED(drive,side,track) \
    (BufferDrive == (drive) && BufferSide == (side) && BufferTrack == (track))

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
static int SelectedDrive = 0;
static int ReqCmd, ReqBlock;
static int ReqSide, ReqTrack, ReqSector, ReqCnt;
static int HeadSettleFlag = 0;
static unsigned char *ReqData, *ReqBuffer;
static int MotorOn = 0, MotorOffTrys;
static int IsFormatting = 0, FormatError;

static int UserSteprate[FD_MAX_UNITS] = { -1, -1 };
module_param_array(UserSteprate, int, NULL, 0);

/* Synchronization of FDC access. */
static volatile int fdc_busy = 0;
static DECLARE_WAIT_QUEUE_HEAD(fdc_wait);
static DECLARE_COMPLETION(format_wait);

static unsigned long changed_floppies = 0xff, fake_change = 0;
#define	CHECK_CHANGE_DELAY	HZ/2

#define	FD_MOTOR_OFF_DELAY	(3*HZ)
#define	FD_MOTOR_OFF_MAXTRY	(10*20)

#define FLOPPY_TIMEOUT		(6*HZ)
#define RECALIBRATE_ERRORS	4	/* After this many errors the drive
					 * will be recalibrated. */
#define MAX_ERRORS		8	/* After this many errors the driver
					 * will give up. */


/*
 * The driver is trying to determine the correct media format
 * while Probing is set. fd_rwsec_done() clears it after a
 * successful access.
 */
static int Probing = 0;

/* This flag is set when a dummy seek is necessary to make the WP
 * status bit accessible.
 */
static int NeedSeek = 0;


#ifdef DEBUG
#define DPRINT(a)	printk a
#else
#define DPRINT(a)
#endif

/***************************** Prototypes *****************************/

static void fd_select_side( int side );
static void fd_select_drive( int drive );
static void fd_deselect( void );
static void fd_motor_off_timer(struct timer_list *unused);
static void check_change(struct timer_list *unused);
static irqreturn_t floppy_irq (int irq, void *dummy);
static void fd_error( void );
static int do_format(int drive, int type, struct atari_format_descr *desc);
static void do_fd_action( int drive );
static void fd_calibrate( void );
static void fd_calibrate_done( int status );
static void fd_seek( void );
static void fd_seek_done( int status );
static void fd_rwsec( void );
static void fd_readtrack_check(struct timer_list *unused);
static void fd_rwsec_done( int status );
static void fd_rwsec_done1(int status);
static void fd_writetrack( void );
static void fd_writetrack_done( int status );
static void fd_times_out(struct timer_list *unused);
static void finish_fdc( void );
static void finish_fdc_done( int dummy );
static void setup_req_params( int drive );
static void redo_fd_request( void);
static int fd_locked_ioctl(struct block_device *bdev, fmode_t mode, unsigned int
                     cmd, unsigned long param);
static void fd_probe( int drive );
static int fd_test_drive_present( int drive );
static void config_types( void );
static int floppy_open(struct block_device *bdev, fmode_t mode);
static void floppy_release(struct gendisk *disk, fmode_t mode);

/************************* End of Prototypes **************************/

static DEFINE_TIMER(motor_off_timer, fd_motor_off_timer);
static DEFINE_TIMER(readtrack_timer, fd_readtrack_check);
static DEFINE_TIMER(timeout_timer, fd_times_out);
static DEFINE_TIMER(fd_timer, check_change);
	
static void fd_end_request_cur(blk_status_t err)
{
	if (!__blk_end_request_cur(fd_request, err))
		fd_request = NULL;
}

static inline void start_motor_off_timer(void)
{
	mod_timer(&motor_off_timer, jiffies + FD_MOTOR_OFF_DELAY);
	MotorOffTrys = 0;
}

static inline void start_check_change_timer( void )
{
	mod_timer(&fd_timer, jiffies + CHECK_CHANGE_DELAY);
}

static inline void start_timeout(void)
{
	mod_timer(&timeout_timer, jiffies + FLOPPY_TIMEOUT);
}

static inline void stop_timeout(void)
{
	del_timer(&timeout_timer);
}

/* Select the side to use. */

static void fd_select_side( int side )
{
	unsigned long flags;

	/* protect against various other ints mucking around with the PSG */
	local_irq_save(flags);
  
	sound_ym.rd_data_reg_sel = 14; /* Select PSG Port A */
	sound_ym.wd_data = (side == 0) ? sound_ym.rd_data_reg_sel | 0x01 :
	                                 sound_ym.rd_data_reg_sel & 0xfe;

	local_irq_restore(flags);
}


/* Select a drive, update the FDC's track register and set the correct
 * clock speed for this disk's type.
 */

static void fd_select_drive( int drive )
{
	unsigned long flags;
	unsigned char tmp;
  
	if (drive == SelectedDrive)
	  return;

	/* protect against various other ints mucking around with the PSG */
	local_irq_save(flags);
	sound_ym.rd_data_reg_sel = 14; /* Select PSG Port A */
	tmp = sound_ym.rd_data_reg_sel;
	sound_ym.wd_data = (tmp | DSKDRVNONE) & ~(drive == 0 ? DSKDRV0 : DSKDRV1);
	atari_dont_touch_floppy_select = 1;
	local_irq_restore(flags);

	/* restore track register to saved value */
	FDC_WRITE( FDCREG_TRACK, UD.track );
	udelay(25);

	/* select 8/16 MHz */
	if (UDT)
		if (ATARIHW_PRESENT(FDCSPEED))
			dma_wd.fdc_speed = UDT->fdc_speed;
	
	SelectedDrive = drive;
}


/* Deselect both drives. */

static void fd_deselect( void )
{
	unsigned long flags;

	/* protect against various other ints mucking around with the PSG */
	local_irq_save(flags);
	atari_dont_touch_floppy_select = 0;
	sound_ym.rd_data_reg_sel=14;	/* Select PSG Port A */
	sound_ym.wd_data = (sound_ym.rd_data_reg_sel |
			    (MACH_IS_FALCON ? 3 : 7)); /* no drives selected */
	/* On Falcon, the drive B select line is used on the printer port, so
	 * leave it alone... */
	SelectedDrive = -1;
	local_irq_restore(flags);
}


/* This timer function deselects the drives when the FDC switched the
 * motor off. The deselection cannot happen earlier because the FDC
 * counts the index signals, which arrive only if one drive is selected.
 */

static void fd_motor_off_timer(struct timer_list *unused)
{
	unsigned char status;

	if (SelectedDrive < 0)
		/* no drive selected, needn't deselect anyone */
		return;

	if (stdma_islocked())
		goto retry;

	status = FDC_READ( FDCREG_STATUS );

	if (!(status & 0x80)) {
		/* motor already turned off by FDC -> deselect drives */
		MotorOn = 0;
		fd_deselect();
		return;
	}
	/* not yet off, try again */

  retry:
	/* Test again later; if tested too often, it seems there is no disk
	 * in the drive and the FDC will leave the motor on forever (or,
	 * at least until a disk is inserted). So we'll test only twice
	 * per second from then on...
	 */
	mod_timer(&motor_off_timer,
		  jiffies + (MotorOffTrys++ < FD_MOTOR_OFF_MAXTRY ? HZ/20 : HZ/2));
}


/* This function is repeatedly called to detect disk changes (as good
 * as possible) and keep track of the current state of the write protection.
 */

static void check_change(struct timer_list *unused)
{
	static int    drive = 0;

	unsigned long flags;
	unsigned char old_porta;
	int			  stat;

	if (++drive > 1 || !UD.connected)
		drive = 0;

	/* protect against various other ints mucking around with the PSG */
	local_irq_save(flags);

	if (!stdma_islocked()) {
		sound_ym.rd_data_reg_sel = 14;
		old_porta = sound_ym.rd_data_reg_sel;
		sound_ym.wd_data = (old_porta | DSKDRVNONE) &
			               ~(drive == 0 ? DSKDRV0 : DSKDRV1);
		stat = !!(FDC_READ( FDCREG_STATUS ) & FDCSTAT_WPROT);
		sound_ym.wd_data = old_porta;

		if (stat != UD.wpstat) {
			DPRINT(( "wpstat[%d] = %d\n", drive, stat ));
			UD.wpstat = stat;
			set_bit (drive, &changed_floppies);
		}
	}
	local_irq_restore(flags);

	start_check_change_timer();
}

 
/* Handling of the Head Settling Flag: This flag should be set after each
 * seek operation, because we don't use seeks with verify.
 */

static inline void set_head_settle_flag(void)
{
	HeadSettleFlag = FDCCMDADD_E;
}

static inline int get_head_settle_flag(void)
{
	int	tmp = HeadSettleFlag;
	HeadSettleFlag = 0;
	return( tmp );
}

static inline void copy_buffer(void *from, void *to)
{
	ulong *p1 = (ulong *)from, *p2 = (ulong *)to;
	int cnt;

	for (cnt = 512/4; cnt; cnt--)
		*p2++ = *p1++;
}

  
  

/* General Interrupt Handling */

static void (*FloppyIRQHandler)( int status ) = NULL;

static irqreturn_t floppy_irq (int irq, void *dummy)
{
	unsigned char status;
	void (*handler)( int );

	handler = xchg(&FloppyIRQHandler, NULL);

	if (handler) {
		nop();
		status = FDC_READ( FDCREG_STATUS );
		DPRINT(("FDC irq, status = %02x handler = %08lx\n",status,(unsigned long)handler));
		handler( status );
	}
	else {
		DPRINT(("FDC irq, no handler\n"));
	}
	return IRQ_HANDLED;
}


/* Error handling: If some error happened, retry some times, then
 * recalibrate, then try again, and fail after MAX_ERRORS.
 */

static void fd_error( void )
{
	if (IsFormatting) {
		IsFormatting = 0;
		FormatError = 1;
		complete(&format_wait);
		return;
	}

	if (!fd_request)
		return;

	fd_request->error_count++;
	if (fd_request->error_count >= MAX_ERRORS) {
		printk(KERN_ERR "fd%d: too many errors.\n", SelectedDrive );
		fd_end_request_cur(BLK_STS_IOERR);
	}
	else if (fd_request->error_count == RECALIBRATE_ERRORS) {
		printk(KERN_WARNING "fd%d: recalibrating\n", SelectedDrive );
		if (SelectedDrive != -1)
			SUD.track = -1;
	}
	redo_fd_request();
}



#define	SET_IRQ_HANDLER(proc) do { FloppyIRQHandler = (proc); } while(0)


/* ---------- Formatting ---------- */

#define FILL(n,val)		\
    do {			\
	memset( p, val, n );	\
	p += n;			\
    } while(0)

static int do_format(int drive, int type, struct atari_format_descr *desc)
{
	unsigned char	*p;
	int sect, nsect;
	unsigned long	flags;

	DPRINT(("do_format( dr=%d tr=%d he=%d offs=%d )\n",
		drive, desc->track, desc->head, desc->sect_offset ));

	wait_event(fdc_wait, cmpxchg(&fdc_busy, 0, 1) == 0);
	local_irq_save(flags);
	stdma_lock(floppy_irq, NULL);
	atari_turnon_irq( IRQ_MFP_FDC ); /* should be already, just to be sure */
	local_irq_restore(flags);

	if (type) {
		if (--type >= NUM_DISK_MINORS ||
		    minor2disktype[type].drive_types > DriveType) {
			redo_fd_request();
			return -EINVAL;
		}
		type = minor2disktype[type].index;
		UDT = &atari_disk_type[type];
	}

	if (!UDT || desc->track >= UDT->blocks/UDT->spt/2 || desc->head >= 2) {
		redo_fd_request();
		return -EINVAL;
	}

	nsect = UDT->spt;
	p = TrackBuffer;
	/* The track buffer is used for the raw track data, so its
	   contents become invalid! */
	BufferDrive = -1;
	/* stop deselect timer */
	del_timer( &motor_off_timer );

	FILL( 60 * (nsect / 9), 0x4e );
	for( sect = 0; sect < nsect; ++sect ) {
		FILL( 12, 0 );
		FILL( 3, 0xf5 );
		*p++ = 0xfe;
		*p++ = desc->track;
		*p++ = desc->head;
		*p++ = (nsect + sect - desc->sect_offset) % nsect + 1;
		*p++ = 2;
		*p++ = 0xf7;
		FILL( 22, 0x4e );
		FILL( 12, 0 );
		FILL( 3, 0xf5 );
		*p++ = 0xfb;
		FILL( 512, 0xe5 );
		*p++ = 0xf7;
		FILL( 40, 0x4e );
	}
	FILL( TrackBuffer+BUFFER_SIZE-p, 0x4e );

	IsFormatting = 1;
	FormatError = 0;
	ReqTrack = desc->track;
	ReqSide  = desc->head;
	do_fd_action( drive );

	wait_for_completion(&format_wait);

	redo_fd_request();
	return( FormatError ? -EIO : 0 );	
}


/* do_fd_action() is the general procedure for a fd request: All
 * required parameter settings (drive select, side select, track
 * position) are checked and set if needed. For each of these
 * parameters and the actual reading or writing exist two functions:
 * one that starts the setting (or skips it if possible) and one
 * callback for the "done" interrupt. Each done func calls the next
 * set function to propagate the request down to fd_rwsec_done().
 */

static void do_fd_action( int drive )
{
	DPRINT(("do_fd_action\n"));
	
	if (UseTrackbuffer && !IsFormatting) {
	repeat:
	    if (IS_BUFFERED( drive, ReqSide, ReqTrack )) {
		if (ReqCmd == READ) {
		    copy_buffer( SECTOR_BUFFER(ReqSector), ReqData );
		    if (++ReqCnt < blk_rq_cur_sectors(fd_request)) {
			/* read next sector */
			setup_req_params( drive );
			goto repeat;
		    }
		    else {
			/* all sectors finished */
			fd_end_request_cur(BLK_STS_OK);
			redo_fd_request();
			return;
		    }
		}
		else {
		    /* cmd == WRITE, pay attention to track buffer
		     * consistency! */
		    copy_buffer( ReqData, SECTOR_BUFFER(ReqSector) );
		}
	    }
	}

	if (SelectedDrive != drive)
		fd_select_drive( drive );
    
	if (UD.track == -1)
		fd_calibrate();
	else if (UD.track != ReqTrack << UDT->stretch)
		fd_seek();
	else if (IsFormatting)
		fd_writetrack();
	else
		fd_rwsec();
}


/* Seek to track 0 if the current track is unknown */

static void fd_calibrate( void )
{
	if (SUD.track >= 0) {
		fd_calibrate_done( 0 );
		return;
	}

	if (ATARIHW_PRESENT(FDCSPEED))
		dma_wd.fdc_speed = 0; 	/* always seek with 8 Mhz */;
	DPRINT(("fd_calibrate\n"));
	SET_IRQ_HANDLER( fd_calibrate_done );
	/* we can't verify, since the speed may be incorrect */
	FDC_WRITE( FDCREG_CMD, FDCCMD_RESTORE | SUD.steprate );

	NeedSeek = 1;
	MotorOn = 1;
	start_timeout();
	/* wait for IRQ */
}


static void fd_calibrate_done( int status )
{
	DPRINT(("fd_calibrate_done()\n"));
	stop_timeout();
    
	/* set the correct speed now */
	if (ATARIHW_PRESENT(FDCSPEED))
		dma_wd.fdc_speed = SUDT->fdc_speed;
	if (status & FDCSTAT_RECNF) {
		printk(KERN_ERR "fd%d: restore failed\n", SelectedDrive );
		fd_error();
	}
	else {
		SUD.track = 0;
		fd_seek();
	}
}
  
  
/* Seek the drive to the requested track. The drive must have been
 * calibrated at some point before this.
 */
  
static void fd_seek( void )
{
	if (SUD.track == ReqTrack << SUDT->stretch) {
		fd_seek_done( 0 );
		return;
	}

	if (ATARIHW_PRESENT(FDCSPEED)) {
		dma_wd.fdc_speed = 0;	/* always seek witch 8 Mhz */
		MFPDELAY();
	}

	DPRINT(("fd_seek() to track %d\n",ReqTrack));
	FDC_WRITE( FDCREG_DATA, ReqTrack << SUDT->stretch);
	udelay(25);
	SET_IRQ_HANDLER( fd_seek_done );
	FDC_WRITE( FDCREG_CMD, FDCCMD_SEEK | SUD.steprate );

	MotorOn = 1;
	set_head_settle_flag();
	start_timeout();
	/* wait for IRQ */
}


static void fd_seek_done( int status )
{
	DPRINT(("fd_seek_done()\n"));
	stop_timeout();
	
	/* set the correct speed */
	if (ATARIHW_PRESENT(FDCSPEED))
		dma_wd.fdc_speed = SUDT->fdc_speed;
	if (status & FDCSTAT_RECNF) {
		printk(KERN_ERR "fd%d: seek error (to track %d)\n",
				SelectedDrive, ReqTrack );
		/* we don't know exactly which track we are on now! */
		SUD.track = -1;
		fd_error();
	}
	else {
		SUD.track = ReqTrack << SUDT->stretch;
		NeedSeek = 0;
		if (IsFormatting)
			fd_writetrack();
		else
			fd_rwsec();
	}
}


/* This does the actual reading/writing after positioning the head
 * over the correct track.
 */

static int MultReadInProgress = 0;


static void fd_rwsec( void )
{
	unsigned long paddr, flags;
	unsigned int  rwflag, old_motoron;
	unsigned int track;
	
	DPRINT(("fd_rwsec(), Sec=%d, Access=%c\n",ReqSector, ReqCmd == WRITE ? 'w' : 'r' ));
	if (ReqCmd == WRITE) {
		if (ATARIHW_PRESENT(EXTD_DMA)) {
			paddr = virt_to_phys(ReqData);
		}
		else {
			copy_buffer( ReqData, DMABuffer );
			paddr = PhysDMABuffer;
		}
		dma_cache_maintenance( paddr, 512, 1 );
		rwflag = 0x100;
	}
	else {
		if (read_track)
			paddr = PhysTrackBuffer;
		else
			paddr = ATARIHW_PRESENT(EXTD_DMA) ? 
				virt_to_phys(ReqData) : PhysDMABuffer;
		rwflag = 0;
	}

	fd_select_side( ReqSide );
  
	/* Start sector of this operation */
	FDC_WRITE( FDCREG_SECTOR, read_track ? 1 : ReqSector );
	MFPDELAY();
	/* Cheat for track if stretch != 0 */
	if (SUDT->stretch) {
		track = FDC_READ( FDCREG_TRACK);
		MFPDELAY();
		FDC_WRITE( FDCREG_TRACK, track >> SUDT->stretch);
	}
	udelay(25);
  
	/* Setup DMA */
	local_irq_save(flags);
	dma_wd.dma_lo = (unsigned char)paddr;
	MFPDELAY();
	paddr >>= 8;
	dma_wd.dma_md = (unsigned char)paddr;
	MFPDELAY();
	paddr >>= 8;
	if (ATARIHW_PRESENT(EXTD_DMA))
		st_dma_ext_dmahi = (unsigned short)paddr;
	else
		dma_wd.dma_hi = (unsigned char)paddr;
	MFPDELAY();
	local_irq_restore(flags);
  
	/* Clear FIFO and switch DMA to correct mode */  
	dma_wd.dma_mode_status = 0x90 | rwflag;  
	MFPDELAY();
	dma_wd.dma_mode_status = 0x90 | (rwflag ^ 0x100);  
	MFPDELAY();
	dma_wd.dma_mode_status = 0x90 | rwflag;
	MFPDELAY();
  
	/* How many sectors for DMA */
	dma_wd.fdc_acces_seccount = read_track ? SUDT->spt : 1;
  
	udelay(25);  
  
	/* Start operation */
	dma_wd.dma_mode_status = FDCSELREG_STP | rwflag;
	udelay(25);
	SET_IRQ_HANDLER( fd_rwsec_done );
	dma_wd.fdc_acces_seccount =
	  (get_head_settle_flag() |
	   (rwflag ? FDCCMD_WRSEC : (FDCCMD_RDSEC | (read_track ? FDCCMDADD_M : 0))));

	old_motoron = MotorOn;
	MotorOn = 1;
	NeedSeek = 1;
	/* wait for interrupt */

	if (read_track) {
		/* If reading a whole track, wait about one disk rotation and
		 * then check if all sectors are read. The FDC will even
		 * search for the first non-existent sector and need 1 sec to
		 * recognise that it isn't present :-(
		 */
		MultReadInProgress = 1;
		mod_timer(&readtrack_timer,
			  /* 1 rot. + 5 rot.s if motor was off  */
			  jiffies + HZ/5 + (old_motoron ? 0 : HZ));
	}
	start_timeout();
}

    
static void fd_readtrack_check(struct timer_list *unused)
{
	unsigned long flags, addr, addr2;

	local_irq_save(flags);

	if (!MultReadInProgress) {
		/* This prevents a race condition that could arise if the
		 * interrupt is triggered while the calling of this timer
		 * callback function takes place. The IRQ function then has
		 * already cleared 'MultReadInProgress'  when flow of control
		 * gets here.
		 */
		local_irq_restore(flags);
		return;
	}

	/* get the current DMA address */
	/* ++ f.a. read twice to avoid being fooled by switcher */
	addr = 0;
	do {
		addr2 = addr;
		addr = dma_wd.dma_lo & 0xff;
		MFPDELAY();
		addr |= (dma_wd.dma_md & 0xff) << 8;
		MFPDELAY();
		if (ATARIHW_PRESENT( EXTD_DMA ))
			addr |= (st_dma_ext_dmahi & 0xffff) << 16;
		else
			addr |= (dma_wd.dma_hi & 0xff) << 16;
		MFPDELAY();
	} while(addr != addr2);
  
	if (addr >= PhysTrackBuffer + SUDT->spt*512) {
		/* already read enough data, force an FDC interrupt to stop
		 * the read operation
		 */
		SET_IRQ_HANDLER( NULL );
		MultReadInProgress = 0;
		local_irq_restore(flags);
		DPRINT(("fd_readtrack_check(): done\n"));
		FDC_WRITE( FDCREG_CMD, FDCCMD_FORCI );
		udelay(25);

		/* No error until now -- the FDC would have interrupted
		 * otherwise!
		 */
		fd_rwsec_done1(0);
	}
	else {
		/* not yet finished, wait another tenth rotation */
		local_irq_restore(flags);
		DPRINT(("fd_readtrack_check(): not yet finished\n"));
		mod_timer(&readtrack_timer, jiffies + HZ/5/10);
	}
}


static void fd_rwsec_done( int status )
{
	DPRINT(("fd_rwsec_done()\n"));

	if (read_track) {
		del_timer(&readtrack_timer);
		if (!MultReadInProgress)
			return;
		MultReadInProgress = 0;
	}
	fd_rwsec_done1(status);
}

static void fd_rwsec_done1(int status)
{
	unsigned int track;

	stop_timeout();
	
	/* Correct the track if stretch != 0 */
	if (SUDT->stretch) {
		track = FDC_READ( FDCREG_TRACK);
		MFPDELAY();
		FDC_WRITE( FDCREG_TRACK, track << SUDT->stretch);
	}

	if (!UseTrackbuffer) {
		dma_wd.dma_mode_status = 0x90;
		MFPDELAY();
		if (!(dma_wd.dma_mode_status & 0x01)) {
			printk(KERN_ERR "fd%d: DMA error\n", SelectedDrive );
			goto err_end;
		}
	}
	MFPDELAY();

	if (ReqCmd == WRITE && (status & FDCSTAT_WPROT)) {
		printk(KERN_NOTICE "fd%d: is write protected\n", SelectedDrive );
		goto err_end;
	}	
	if ((status & FDCSTAT_RECNF) &&
	    /* RECNF is no error after a multiple read when the FDC
	       searched for a non-existent sector! */
	    !(read_track && FDC_READ(FDCREG_SECTOR) > SUDT->spt)) {
		if (Probing) {
			if (SUDT > atari_disk_type) {
			    if (SUDT[-1].blocks > ReqBlock) {
				/* try another disk type */
				SUDT--;
				set_capacity(unit[SelectedDrive].disk,
							SUDT->blocks);
			    } else
				Probing = 0;
			}
			else {
				if (SUD.flags & FTD_MSG)
					printk(KERN_INFO "fd%d: Auto-detected floppy type %s\n",
					       SelectedDrive, SUDT->name );
				Probing=0;
			}
		} else {	
/* record not found, but not probing. Maybe stretch wrong ? Restart probing */
			if (SUD.autoprobe) {
				SUDT = atari_disk_type + StartDiskType[DriveType];
				set_capacity(unit[SelectedDrive].disk,
							SUDT->blocks);
				Probing = 1;
			}
		}
		if (Probing) {
			if (ATARIHW_PRESENT(FDCSPEED)) {
				dma_wd.fdc_speed = SUDT->fdc_speed;
				MFPDELAY();
			}
			setup_req_params( SelectedDrive );
			BufferDrive = -1;
			do_fd_action( SelectedDrive );
			return;
		}

		printk(KERN_ERR "fd%d: sector %d not found (side %d, track %d)\n",
		       SelectedDrive, FDC_READ (FDCREG_SECTOR), ReqSide, ReqTrack );
		goto err_end;
	}
	if (status & FDCSTAT_CRC) {
		printk(KERN_ERR "fd%d: CRC error (side %d, track %d, sector %d)\n",
		       SelectedDrive, ReqSide, ReqTrack, FDC_READ (FDCREG_SECTOR) );
		goto err_end;
	}
	if (status & FDCSTAT_LOST) {
		printk(KERN_ERR "fd%d: lost data (side %d, track %d, sector %d)\n",
		       SelectedDrive, ReqSide, ReqTrack, FDC_READ (FDCREG_SECTOR) );
		goto err_end;
	}

	Probing = 0;
	
	if (ReqCmd == READ) {
		if (!read_track) {
			void *addr;
			addr = ATARIHW_PRESENT( EXTD_DMA ) ? ReqData : DMABuffer;
			dma_cache_maintenance( virt_to_phys(addr), 512, 0 );
			if (!ATARIHW_PRESENT( EXTD_DMA ))
				copy_buffer (addr, ReqData);
		} else {
			dma_cache_maintenance( PhysTrackBuffer, MaxSectors[DriveType] * 512, 0 );
			BufferDrive = SelectedDrive;
			BufferSide  = ReqSide;
			BufferTrack = ReqTrack;
			copy_buffer (SECTOR_BUFFER (ReqSector), ReqData);
		}
	}
  
	if (++ReqCnt < blk_rq_cur_sectors(fd_request)) {
		/* read next sector */
		setup_req_params( SelectedDrive );
		do_fd_action( SelectedDrive );
	}
	else {
		/* all sectors finished */
		fd_end_request_cur(BLK_STS_OK);
		redo_fd_request();
	}
	return;
  
  err_end:
	BufferDrive = -1;
	fd_error();
}


static void fd_writetrack( void )
{
	unsigned long paddr, flags;
	unsigned int track;
	
	DPRINT(("fd_writetrack() Tr=%d Si=%d\n", ReqTrack, ReqSide ));

	paddr = PhysTrackBuffer;
	dma_cache_maintenance( paddr, BUFFER_SIZE, 1 );

	fd_select_side( ReqSide );
  
	/* Cheat for track if stretch != 0 */
	if (SUDT->stretch) {
		track = FDC_READ( FDCREG_TRACK);
		MFPDELAY();
		FDC_WRITE(FDCREG_TRACK,track >> SUDT->stretch);
	}
	udelay(40);
  
	/* Setup DMA */
	local_irq_save(flags);
	dma_wd.dma_lo = (unsigned char)paddr;
	MFPDELAY();
	paddr >>= 8;
	dma_wd.dma_md = (unsigned char)paddr;
	MFPDELAY();
	paddr >>= 8;
	if (ATARIHW_PRESENT( EXTD_DMA ))
		st_dma_ext_dmahi = (unsigned short)paddr;
	else
		dma_wd.dma_hi = (unsigned char)paddr;
	MFPDELAY();
	local_irq_restore(flags);
  
	/* Clear FIFO and switch DMA to correct mode */  
	dma_wd.dma_mode_status = 0x190;  
	MFPDELAY();
	dma_wd.dma_mode_status = 0x90;  
	MFPDELAY();
	dma_wd.dma_mode_status = 0x190;
	MFPDELAY();
  
	/* How many sectors for DMA */
	dma_wd.fdc_acces_seccount = BUFFER_SIZE/512;
	udelay(40);  
  
	/* Start operation */
	dma_wd.dma_mode_status = FDCSELREG_STP | 0x100;
	udelay(40);
	SET_IRQ_HANDLER( fd_writetrack_done );
	dma_wd.fdc_acces_seccount = FDCCMD_WRTRA | get_head_settle_flag(); 

	MotorOn = 1;
	start_timeout();
	/* wait for interrupt */
}


static void fd_writetrack_done( int status )
{
	DPRINT(("fd_writetrack_done()\n"));

	stop_timeout();

	if (status & FDCSTAT_WPROT) {
		printk(KERN_NOTICE "fd%d: is write protected\n", SelectedDrive );
		goto err_end;
	}	
	if (status & FDCSTAT_LOST) {
		printk(KERN_ERR "fd%d: lost data (side %d, track %d)\n",
				SelectedDrive, ReqSide, ReqTrack );
		goto err_end;
	}

	complete(&format_wait);
	return;

  err_end:
	fd_error();
}

static void fd_times_out(struct timer_list *unused)
{
	atari_disable_irq( IRQ_MFP_FDC );
	if (!FloppyIRQHandler) goto end; /* int occurred after timer was fired, but
					  * before we came here... */

	SET_IRQ_HANDLER( NULL );
	/* If the timeout occurred while the readtrack_check timer was
	 * active, we need to cancel it, else bad things will happen */
	if (UseTrackbuffer)
		del_timer( &readtrack_timer );
	FDC_WRITE( FDCREG_CMD, FDCCMD_FORCI );
	udelay( 25 );
	
	printk(KERN_ERR "floppy timeout\n" );
	fd_error();
  end:
	atari_enable_irq( IRQ_MFP_FDC );
}


/* The (noop) seek operation here is needed to make the WP bit in the
 * FDC status register accessible for check_change. If the last disk
 * operation would have been a RDSEC, this bit would always read as 0
 * no matter what :-( To save time, the seek goes to the track we're
 * already on.
 */

static void finish_fdc( void )
{
	if (!NeedSeek) {
		finish_fdc_done( 0 );
	}
	else {
		DPRINT(("finish_fdc: dummy seek started\n"));
		FDC_WRITE (FDCREG_DATA, SUD.track);
		SET_IRQ_HANDLER( finish_fdc_done );
		FDC_WRITE (FDCREG_CMD, FDCCMD_SEEK);
		MotorOn = 1;
		start_timeout();
		/* we must wait for the IRQ here, because the ST-DMA
		   is released immediately afterwards and the interrupt
		   may be delivered to the wrong driver. */
	  }
}


static void finish_fdc_done( int dummy )
{
	unsigned long flags;

	DPRINT(("finish_fdc_done entered\n"));
	stop_timeout();
	NeedSeek = 0;

	if (timer_pending(&fd_timer) && time_before(fd_timer.expires, jiffies + 5))
		/* If the check for a disk change is done too early after this
		 * last seek command, the WP bit still reads wrong :-((
		 */
		mod_timer(&fd_timer, jiffies + 5);
	else
		start_check_change_timer();
	start_motor_off_timer();

	local_irq_save(flags);
	stdma_release();
	fdc_busy = 0;
	wake_up( &fdc_wait );
	local_irq_restore(flags);

	DPRINT(("finish_fdc() finished\n"));
}

/* The detection of disk changes is a dark chapter in Atari history :-(
 * Because the "Drive ready" signal isn't present in the Atari
 * hardware, one has to rely on the "Write Protect". This works fine,
 * as long as no write protected disks are used. TOS solves this
 * problem by introducing tri-state logic ("maybe changed") and
 * looking at the serial number in block 0. This isn't possible for
 * Linux, since the floppy driver can't make assumptions about the
 * filesystem used on the disk and thus the contents of block 0. I've
 * chosen the method to always say "The disk was changed" if it is
 * unsure whether it was. This implies that every open or mount
 * invalidates the disk buffers if you work with write protected
 * disks. But at least this is better than working with incorrect data
 * due to unrecognised disk changes.
 */

static unsigned int floppy_check_events(struct gendisk *disk,
					unsigned int clearing)
{
	struct atari_floppy_struct *p = disk->private_data;
	unsigned int drive = p - unit;
	if (test_bit (drive, &fake_change)) {
		/* simulated change (e.g. after formatting) */
		return DISK_EVENT_MEDIA_CHANGE;
	}
	if (test_bit (drive, &changed_floppies)) {
		/* surely changed (the WP signal changed at least once) */
		return DISK_EVENT_MEDIA_CHANGE;
	}
	if (UD.wpstat) {
		/* WP is on -> could be changed: to be sure, buffers should be
		 * invalidated...
		 */
		return DISK_EVENT_MEDIA_CHANGE;
	}

	return 0;
}

static int floppy_revalidate(struct gendisk *disk)
{
	struct atari_floppy_struct *p = disk->private_data;
	unsigned int drive = p - unit;

	if (test_bit(drive, &changed_floppies) ||
	    test_bit(drive, &fake_change) ||
	    p->disktype == 0) {
		if (UD.flags & FTD_MSG)
			printk(KERN_ERR "floppy: clear format %p!\n", UDT);
		BufferDrive = -1;
		clear_bit(drive, &fake_change);
		clear_bit(drive, &changed_floppies);
		/* MSch: clearing geometry makes sense only for autoprobe
		   formats, for 'permanent user-defined' parameter:
		   restore default_params[] here if flagged valid! */
		if (default_params[drive].blocks == 0)
			UDT = NULL;
		else
			UDT = &default_params[drive];
	}
	return 0;
}


/* This sets up the global variables describing the current request. */

static void setup_req_params( int drive )
{
	int block = ReqBlock + ReqCnt;

	ReqTrack = block / UDT->spt;
	ReqSector = block - ReqTrack * UDT->spt + 1;
	ReqSide = ReqTrack & 1;
	ReqTrack >>= 1;
	ReqData = ReqBuffer + 512 * ReqCnt;

	if (UseTrackbuffer)
		read_track = (ReqCmd == READ && fd_request->error_count == 0);
	else
		read_track = 0;

	DPRINT(("Request params: Si=%d Tr=%d Se=%d Data=%08lx\n",ReqSide,
			ReqTrack, ReqSector, (unsigned long)ReqData ));
}

/*
 * Round-robin between our available drives, doing one request from each
 */
static struct request *set_next_request(void)
{
	struct request_queue *q;
	int old_pos = fdc_queue;
	struct request *rq = NULL;

	do {
		q = unit[fdc_queue].disk->queue;
		if (++fdc_queue == FD_MAX_UNITS)
			fdc_queue = 0;
		if (q) {
			rq = blk_fetch_request(q);
			if (rq) {
				rq->error_count = 0;
				break;
			}
		}
	} while (fdc_queue != old_pos);

	return rq;
}


static void redo_fd_request(void)
{
	int drive, type;
	struct atari_floppy_struct *floppy;

	DPRINT(("redo_fd_request: fd_request=%p dev=%s fd_request->sector=%ld\n",
		fd_request, fd_request ? fd_request->rq_disk->disk_name : "",
		fd_request ? blk_rq_pos(fd_request) : 0 ));

	IsFormatting = 0;

repeat:
	if (!fd_request) {
		fd_request = set_next_request();
		if (!fd_request)
			goto the_end;
	}

	floppy = fd_request->rq_disk->private_data;
	drive = floppy - unit;
	type = floppy->type;
	
	if (!UD.connected) {
		/* drive not connected */
		printk(KERN_ERR "Unknown Device: fd%d\n", drive );
		fd_end_request_cur(BLK_STS_IOERR);
		goto repeat;
	}
		
	if (type == 0) {
		if (!UDT) {
			Probing = 1;
			UDT = atari_disk_type + StartDiskType[DriveType];
			set_capacity(floppy->disk, UDT->blocks);
			UD.autoprobe = 1;
		}
	} 
	else {
		/* user supplied disk type */
		if (--type >= NUM_DISK_MINORS) {
			printk(KERN_WARNING "fd%d: invalid disk format", drive );
			fd_end_request_cur(BLK_STS_IOERR);
			goto repeat;
		}
		if (minor2disktype[type].drive_types > DriveType)  {
			printk(KERN_WARNING "fd%d: unsupported disk format", drive );
			fd_end_request_cur(BLK_STS_IOERR);
			goto repeat;
		}
		type = minor2disktype[type].index;
		UDT = &atari_disk_type[type];
		set_capacity(floppy->disk, UDT->blocks);
		UD.autoprobe = 0;
	}
	
	if (blk_rq_pos(fd_request) + 1 > UDT->blocks) {
		fd_end_request_cur(BLK_STS_IOERR);
		goto repeat;
	}

	/* stop deselect timer */
	del_timer( &motor_off_timer );
		
	ReqCnt = 0;
	ReqCmd = rq_data_dir(fd_request);
	ReqBlock = blk_rq_pos(fd_request);
	ReqBuffer = bio_data(fd_request->bio);
	setup_req_params( drive );
	do_fd_action( drive );

	return;

  the_end:
	finish_fdc();
}


void do_fd_request(struct request_queue * q)
{
	DPRINT(("do_fd_request for pid %d\n",current->pid));
	wait_event(fdc_wait, cmpxchg(&fdc_busy, 0, 1) == 0);
	stdma_lock(floppy_irq, NULL);

	atari_disable_irq( IRQ_MFP_FDC );
	redo_fd_request();
	atari_enable_irq( IRQ_MFP_FDC );
}

static int fd_locked_ioctl(struct block_device *bdev, fmode_t mode,
		    unsigned int cmd, unsigned long param)
{
	struct gendisk *disk = bdev->bd_disk;
	struct atari_floppy_struct *floppy = disk->private_data;
	int drive = floppy - unit;
	int type = floppy->type;
	struct atari_format_descr fmt_desc;
	struct atari_disk_type *dtp;
	struct floppy_struct getprm;
	int settype;
	struct floppy_struct setprm;
	void __user *argp = (void __user *)param;

	switch (cmd) {
	case FDGETPRM:
		if (type) {
			if (--type >= NUM_DISK_MINORS)
				return -ENODEV;
			if (minor2disktype[type].drive_types > DriveType)
				return -ENODEV;
			type = minor2disktype[type].index;
			dtp = &atari_disk_type[type];
			if (UD.flags & FTD_MSG)
			    printk (KERN_ERR "floppy%d: found dtp %p name %s!\n",
			        drive, dtp, dtp->name);
		}
		else {
			if (!UDT)
				return -ENXIO;
			else
				dtp = UDT;
		}
		memset((void *)&getprm, 0, sizeof(getprm));
		getprm.size = dtp->blocks;
		getprm.sect = dtp->spt;
		getprm.head = 2;
		getprm.track = dtp->blocks/dtp->spt/2;
		getprm.stretch = dtp->stretch;
		if (copy_to_user(argp, &getprm, sizeof(getprm)))
			return -EFAULT;
		return 0;
	}
	switch (cmd) {
	case FDSETPRM:
	case FDDEFPRM:
	        /* 
		 * MSch 7/96: simple 'set geometry' case: just set the
		 * 'default' device params (minor == 0).
		 * Currently, the drive geometry is cleared after each
		 * disk change and subsequent revalidate()! simple
		 * implementation of FDDEFPRM: save geometry from a
		 * FDDEFPRM call and restore it in floppy_revalidate() !
		 */

		/* get the parameters from user space */
		if (floppy->ref != 1 && floppy->ref != -1)
			return -EBUSY;
		if (copy_from_user(&setprm, argp, sizeof(setprm)))
			return -EFAULT;
		/* 
		 * first of all: check for floppy change and revalidate, 
		 * or the next access will revalidate - and clear UDT :-(
		 */

		if (floppy_check_events(disk, 0))
		        floppy_revalidate(disk);

		if (UD.flags & FTD_MSG)
		    printk (KERN_INFO "floppy%d: setting size %d spt %d str %d!\n",
			drive, setprm.size, setprm.sect, setprm.stretch);

		/* what if type > 0 here? Overwrite specified entry ? */
		if (type) {
		        /* refuse to re-set a predefined type for now */
			redo_fd_request();
			return -EINVAL;
		}

		/* 
		 * type == 0: first look for a matching entry in the type list,
		 * and set the UD.disktype field to use the perdefined entry.
		 * TODO: add user-defined format to head of autoprobe list ? 
		 * Useful to include the user-type for future autodetection!
		 */

		for (settype = 0; settype < NUM_DISK_MINORS; settype++) {
			int setidx = 0;
			if (minor2disktype[settype].drive_types > DriveType) {
				/* skip this one, invalid for drive ... */
				continue;
			}
			setidx = minor2disktype[settype].index;
			dtp = &atari_disk_type[setidx];

			/* found matching entry ?? */
			if (   dtp->blocks  == setprm.size 
			    && dtp->spt     == setprm.sect
			    && dtp->stretch == setprm.stretch ) {
				if (UD.flags & FTD_MSG)
				    printk (KERN_INFO "floppy%d: setting %s %p!\n",
				        drive, dtp->name, dtp);
				UDT = dtp;
				set_capacity(floppy->disk, UDT->blocks);

				if (cmd == FDDEFPRM) {
				  /* save settings as permanent default type */
				  default_params[drive].name    = dtp->name;
				  default_params[drive].spt     = dtp->spt;
				  default_params[drive].blocks  = dtp->blocks;
				  default_params[drive].fdc_speed = dtp->fdc_speed;
				  default_params[drive].stretch = dtp->stretch;
				}
				
				return 0;
			}

		}

		/* no matching disk type found above - setting user_params */

	       	if (cmd == FDDEFPRM) {
			/* set permanent type */
			dtp = &default_params[drive];
		} else
			/* set user type (reset by disk change!) */
			dtp = &user_params[drive];

		dtp->name   = "user format";
		dtp->blocks = setprm.size;
		dtp->spt    = setprm.sect;
		if (setprm.sect > 14) 
			dtp->fdc_speed = 3;
		else
			dtp->fdc_speed = 0;
		dtp->stretch = setprm.stretch;

		if (UD.flags & FTD_MSG)
			printk (KERN_INFO "floppy%d: blk %d spt %d str %d!\n",
				drive, dtp->blocks, dtp->spt, dtp->stretch);

		/* sanity check */
		if (setprm.track != dtp->blocks/dtp->spt/2 ||
		    setprm.head != 2) {
			redo_fd_request();
			return -EINVAL;
		}

		UDT = dtp;
		set_capacity(floppy->disk, UDT->blocks);

		return 0;
	case FDMSGON:
		UD.flags |= FTD_MSG;
		return 0;
	case FDMSGOFF:
		UD.flags &= ~FTD_MSG;
		return 0;
	case FDSETEMSGTRESH:
		return -EINVAL;
	case FDFMTBEG:
		return 0;
	case FDFMTTRK:
		if (floppy->ref != 1 && floppy->ref != -1)
			return -EBUSY;
		if (copy_from_user(&fmt_desc, argp, sizeof(fmt_desc)))
			return -EFAULT;
		return do_format(drive, type, &fmt_desc);
	case FDCLRPRM:
		UDT = NULL;
		/* MSch: invalidate default_params */
		default_params[drive].blocks  = 0;
		set_capacity(floppy->disk, MAX_DISK_SIZE * 2);
	case FDFMTEND:
	case FDFLUSH:
		/* invalidate the buffer track to force a reread */
		BufferDrive = -1;
		set_bit(drive, &fake_change);
		check_disk_change(bdev);
		return 0;
	default:
		return -EINVAL;
	}
}

static int fd_ioctl(struct block_device *bdev, fmode_t mode,
			     unsigned int cmd, unsigned long arg)
{
	int ret;

	mutex_lock(&ataflop_mutex);
	ret = fd_locked_ioctl(bdev, mode, cmd, arg);
	mutex_unlock(&ataflop_mutex);

	return ret;
}

/* Initialize the 'unit' variable for drive 'drive' */

static void __init fd_probe( int drive )
{
	UD.connected = 0;
	UDT  = NULL;

	if (!fd_test_drive_present( drive ))
		return;

	UD.connected = 1;
	UD.track     = 0;
	switch( UserSteprate[drive] ) {
	case 2:
		UD.steprate = FDCSTEP_2;
		break;
	case 3:
		UD.steprate = FDCSTEP_3;
		break;
	case 6:
		UD.steprate = FDCSTEP_6;
		break;
	case 12:
		UD.steprate = FDCSTEP_12;
		break;
	default: /* should be -1 for "not set by user" */
		if (ATARIHW_PRESENT( FDCSPEED ) || MACH_IS_MEDUSA)
			UD.steprate = FDCSTEP_3;
		else
			UD.steprate = FDCSTEP_6;
		break;
	}
	MotorOn = 1;	/* from probe restore operation! */
}


/* This function tests the physical presence of a floppy drive (not
 * whether a disk is inserted). This is done by issuing a restore
 * command, waiting max. 2 seconds (that should be enough to move the
 * head across the whole disk) and looking at the state of the "TR00"
 * signal. This should now be raised if there is a drive connected
 * (and there is no hardware failure :-) Otherwise, the drive is
 * declared absent.
 */

static int __init fd_test_drive_present( int drive )
{
	unsigned long timeout;
	unsigned char status;
	int ok;
	
	if (drive >= (MACH_IS_FALCON ? 1 : 2)) return( 0 );
	fd_select_drive( drive );

	/* disable interrupt temporarily */
	atari_turnoff_irq( IRQ_MFP_FDC );
	FDC_WRITE (FDCREG_TRACK, 0xff00);
	FDC_WRITE( FDCREG_CMD, FDCCMD_RESTORE | FDCCMDADD_H | FDCSTEP_6 );

	timeout = jiffies + 2*HZ+HZ/2;
	while (time_before(jiffies, timeout))
		if (!(st_mfp.par_dt_reg & 0x20))
			break;

	status = FDC_READ( FDCREG_STATUS );
	ok = (status & FDCSTAT_TR00) != 0;

	/* force interrupt to abort restore operation (FDC would try
	 * about 50 seconds!) */
	FDC_WRITE( FDCREG_CMD, FDCCMD_FORCI );
	udelay(500);
	status = FDC_READ( FDCREG_STATUS );
	udelay(20);

	if (ok) {
		/* dummy seek command to make WP bit accessible */
		FDC_WRITE( FDCREG_DATA, 0 );
		FDC_WRITE( FDCREG_CMD, FDCCMD_SEEK );
		while( st_mfp.par_dt_reg & 0x20 )
			;
		status = FDC_READ( FDCREG_STATUS );
	}

	atari_turnon_irq( IRQ_MFP_FDC );
	return( ok );
}


/* Look how many and which kind of drives are connected. If there are
 * floppies, additionally start the disk-change and motor-off timers.
 */

static void __init config_types( void )
{
	int drive, cnt = 0;

	/* for probing drives, set the FDC speed to 8 MHz */
	if (ATARIHW_PRESENT(FDCSPEED))
		dma_wd.fdc_speed = 0;

	printk(KERN_INFO "Probing floppy drive(s):\n");
	for( drive = 0; drive < FD_MAX_UNITS; drive++ ) {
		fd_probe( drive );
		if (UD.connected) {
			printk(KERN_INFO "fd%d\n", drive);
			++cnt;
		}
	}

	if (FDC_READ( FDCREG_STATUS ) & FDCSTAT_BUSY) {
		/* If FDC is still busy from probing, give it another FORCI
		 * command to abort the operation. If this isn't done, the FDC
		 * will interrupt later and its IRQ line stays low, because
		 * the status register isn't read. And this will block any
		 * interrupts on this IRQ line :-(
		 */
		FDC_WRITE( FDCREG_CMD, FDCCMD_FORCI );
		udelay(500);
		FDC_READ( FDCREG_STATUS );
		udelay(20);
	}
	
	if (cnt > 0) {
		start_motor_off_timer();
		if (cnt == 1) fd_select_drive( 0 );
		start_check_change_timer();
	}
}

/*
 * floppy_open check for aliasing (/dev/fd0 can be the same as
 * /dev/PS0 etc), and disallows simultaneous access to the same
 * drive with different device numbers.
 */

static int floppy_open(struct block_device *bdev, fmode_t mode)
{
	struct atari_floppy_struct *p = bdev->bd_disk->private_data;
	int type  = MINOR(bdev->bd_dev) >> 2;

	DPRINT(("fd_open: type=%d\n",type));
	if (p->ref && p->type != type)
		return -EBUSY;

	if (p->ref == -1 || (p->ref && mode & FMODE_EXCL))
		return -EBUSY;

	if (mode & FMODE_EXCL)
		p->ref = -1;
	else
		p->ref++;

	p->type = type;

	if (mode & FMODE_NDELAY)
		return 0;

	if (mode & (FMODE_READ|FMODE_WRITE)) {
		check_disk_change(bdev);
		if (mode & FMODE_WRITE) {
			if (p->wpstat) {
				if (p->ref < 0)
					p->ref = 0;
				else
					p->ref--;
				return -EROFS;
			}
		}
	}
	return 0;
}

static int floppy_unlocked_open(struct block_device *bdev, fmode_t mode)
{
	int ret;

	mutex_lock(&ataflop_mutex);
	ret = floppy_open(bdev, mode);
	mutex_unlock(&ataflop_mutex);

	return ret;
}

static void floppy_release(struct gendisk *disk, fmode_t mode)
{
	struct atari_floppy_struct *p = disk->private_data;
	mutex_lock(&ataflop_mutex);
	if (p->ref < 0)
		p->ref = 0;
	else if (!p->ref--) {
		printk(KERN_ERR "floppy_release with fd_ref == 0");
		p->ref = 0;
	}
	mutex_unlock(&ataflop_mutex);
}

static const struct block_device_operations floppy_fops = {
	.owner		= THIS_MODULE,
	.open		= floppy_unlocked_open,
	.release	= floppy_release,
	.ioctl		= fd_ioctl,
	.check_events	= floppy_check_events,
	.revalidate_disk= floppy_revalidate,
};

static struct kobject *floppy_find(dev_t dev, int *part, void *data)
{
	int drive = *part & 3;
	int type  = *part >> 2;
	if (drive >= FD_MAX_UNITS || type > NUM_DISK_MINORS)
		return NULL;
	*part = 0;
	return get_disk_and_module(unit[drive].disk);
}

static int __init atari_floppy_init (void)
{
	int i;

	if (!MACH_IS_ATARI)
		/* Amiga, Mac, ... don't have Atari-compatible floppy :-) */
		return -ENODEV;

	if (register_blkdev(FLOPPY_MAJOR,"fd"))
		return -EBUSY;

	for (i = 0; i < FD_MAX_UNITS; i++) {
		unit[i].disk = alloc_disk(1);
		if (!unit[i].disk)
			goto Enomem;

		unit[i].disk->queue = blk_init_queue(do_fd_request,
						     &ataflop_lock);
		if (!unit[i].disk->queue)
			goto Enomem;
	}

	if (UseTrackbuffer < 0)
		/* not set by user -> use default: for now, we turn
		   track buffering off for all Medusas, though it
		   could be used with ones that have a counter
		   card. But the test is too hard :-( */
		UseTrackbuffer = !MACH_IS_MEDUSA;

	/* initialize variables */
	SelectedDrive = -1;
	BufferDrive = -1;

	DMABuffer = atari_stram_alloc(BUFFER_SIZE+512, "ataflop");
	if (!DMABuffer) {
		printk(KERN_ERR "atari_floppy_init: cannot get dma buffer\n");
		goto Enomem;
	}
	TrackBuffer = DMABuffer + 512;
	PhysDMABuffer = atari_stram_to_phys(DMABuffer);
	PhysTrackBuffer = virt_to_phys(TrackBuffer);
	BufferDrive = BufferSide = BufferTrack = -1;

	for (i = 0; i < FD_MAX_UNITS; i++) {
		unit[i].track = -1;
		unit[i].flags = 0;
		unit[i].disk->major = FLOPPY_MAJOR;
		unit[i].disk->first_minor = i;
		sprintf(unit[i].disk->disk_name, "fd%d", i);
		unit[i].disk->fops = &floppy_fops;
		unit[i].disk->private_data = &unit[i];
		set_capacity(unit[i].disk, MAX_DISK_SIZE * 2);
		add_disk(unit[i].disk);
	}

	blk_register_region(MKDEV(FLOPPY_MAJOR, 0), 256, THIS_MODULE,
				floppy_find, NULL, NULL);

	printk(KERN_INFO "Atari floppy driver: max. %cD, %strack buffering\n",
	       DriveType == 0 ? 'D' : DriveType == 1 ? 'H' : 'E',
	       UseTrackbuffer ? "" : "no ");
	config_types();

	return 0;
Enomem:
	do {
		struct gendisk *disk = unit[i].disk;

		if (disk) {
			if (disk->queue) {
				blk_cleanup_queue(disk->queue);
				disk->queue = NULL;
			}
			put_disk(unit[i].disk);
		}
	} while (i--);

	unregister_blkdev(FLOPPY_MAJOR, "fd");
	return -ENOMEM;
}

#ifndef MODULE
static int __init atari_floppy_setup(char *str)
{
	int ints[3 + FD_MAX_UNITS];
	int i;

	if (!MACH_IS_ATARI)
		return 0;

	str = get_options(str, 3 + FD_MAX_UNITS, ints);
	
	if (ints[0] < 1) {
		printk(KERN_ERR "ataflop_setup: no arguments!\n" );
		return 0;
	}
	else if (ints[0] > 2+FD_MAX_UNITS) {
		printk(KERN_ERR "ataflop_setup: too many arguments\n" );
	}

	if (ints[1] < 0 || ints[1] > 2)
		printk(KERN_ERR "ataflop_setup: bad drive type\n" );
	else
		DriveType = ints[1];

	if (ints[0] >= 2)
		UseTrackbuffer = (ints[2] > 0);

	for( i = 3; i <= ints[0] && i-3 < FD_MAX_UNITS; ++i ) {
		if (ints[i] != 2 && ints[i] != 3 && ints[i] != 6 && ints[i] != 12)
			printk(KERN_ERR "ataflop_setup: bad steprate\n" );
		else
			UserSteprate[i-3] = ints[i];
	}
	return 1;
}

__setup("floppy=", atari_floppy_setup);
#endif

static void __exit atari_floppy_exit(void)
{
	int i;
	blk_unregister_region(MKDEV(FLOPPY_MAJOR, 0), 256);
	for (i = 0; i < FD_MAX_UNITS; i++) {
		struct request_queue *q = unit[i].disk->queue;

		del_gendisk(unit[i].disk);
		put_disk(unit[i].disk);
		blk_cleanup_queue(q);
	}
	unregister_blkdev(FLOPPY_MAJOR, "fd");

	del_timer_sync(&fd_timer);
	atari_stram_free( DMABuffer );
}

module_init(atari_floppy_init)
module_exit(atari_floppy_exit)

MODULE_LICENSE("GPL");
