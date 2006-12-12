/*
 * acsi_slm.c -- Device driver for the Atari SLM laser printer
 *
 * Copyright 1995 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 * 
 */

/*

Notes:

The major number for SLM printers is 28 (like ACSI), but as a character
device, not block device. The minor number is the number of the printer (if
you have more than one SLM; currently max. 2 (#define-constant) SLMs are
supported). The device can be opened for reading and writing. If reading it,
you get some status infos (MODE SENSE data). Writing mode is used for the data
to be printed. Some ioctls allow to get the printer status and to tune printer
modes and some internal variables.

A special problem of the SLM driver is the timing and thus the buffering of
the print data. The problem is that all the data for one page must be present
in memory when printing starts, else --when swapping occurs-- the timing could
not be guaranteed. There are several ways to assure this:

 1) Reserve a buffer of 1196k (maximum page size) statically by
    atari_stram_alloc(). The data are collected there until they're complete,
	and then printing starts. Since the buffer is reserved, no further
	considerations about memory and swapping are needed. So this is the
	simplest method, but it needs a lot of memory for just the SLM.

    An striking advantage of this method is (supposed the SLM_CONT_CNT_REPROG
	method works, see there), that there are no timing problems with the DMA
	anymore.
	
 2) The other method would be to reserve the buffer dynamically each time
    printing is required. I could think of looking at mem_map where the
	largest unallocted ST-RAM area is, taking the area, and then extending it
	by swapping out the neighbored pages, until the needed size is reached.
	This requires some mm hacking, but seems possible. The only obstacle could
	be pages that cannot be swapped out (reserved pages)...

 3) Another possibility would be to leave the real data in user space and to
    work with two dribble buffers of about 32k in the driver: While the one
	buffer is DMAed to the SLM, the other can be filled with new data. But
	to keep the timing, that requires that the user data remain in memory and
	are not swapped out. Requires mm hacking, too, but maybe not so bad as
	method 2).

*/

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>

#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_acsi.h>
#include <asm/atari_stdma.h>
#include <asm/atari_stram.h>
#include <asm/atari_SLM.h>


#undef	DEBUG

/* Define this if the page data are continuous in physical memory. That
 * requires less reprogramming of the ST-DMA */
#define	SLM_CONTINUOUS_DMA

/* Use continuous reprogramming of the ST-DMA counter register. This is
 * --strictly speaking-- not allowed, Atari recommends not to look at the
 * counter register while a DMA is going on. But I don't know if that applies
 * only for reading the register, or also writing to it. Writing only works
 * fine for me... The advantage is that the timing becomes absolutely
 * uncritical: Just update each, say 200ms, the counter reg to its maximum,
 * and the DMA will work until the status byte interrupt occurs.
 */
#define	SLM_CONT_CNT_REPROG

#define CMDSET_TARG_LUN(cmd,targ,lun)			\
    do {										\
		cmd[0] = (cmd[0] & ~0xe0) | (targ)<<5;	\
		cmd[1] = (cmd[1] & ~0xe0) | (lun)<<5;	\
	} while(0)

#define	START_TIMER(to)	mod_timer(&slm_timer, jiffies + (to))
#define	STOP_TIMER()	del_timer(&slm_timer)


static char slmreqsense_cmd[6] = { 0x03, 0, 0, 0, 0, 0 };
static char slmprint_cmd[6]    = { 0x0a, 0, 0, 0, 0, 0 };
static char slminquiry_cmd[6]  = { 0x12, 0, 0, 0, 0, 0x80 };
static char slmmsense_cmd[6]   = { 0x1a, 0, 0, 0, 255, 0 };
#if 0
static char slmmselect_cmd[6]  = { 0x15, 0, 0, 0, 0, 0 };
#endif


#define	MAX_SLM		2

static struct slm {
	unsigned	target;			/* target number */
	unsigned	lun;			/* LUN in target controller */
	atomic_t	wr_ok; 			/* set to 0 if output part busy */
	atomic_t	rd_ok;			/* set to 0 if status part busy */
} slm_info[MAX_SLM];

int N_SLM_Printers = 0;

/* printer buffer */
static unsigned char	*SLMBuffer;	/* start of buffer */
static unsigned char	*BufferP;	/* current position in buffer */
static int				BufferSize;	/* length of buffer for page size */

typedef enum { IDLE, FILLING, PRINTING } SLMSTATE;
static SLMSTATE			SLMState;
static int				SLMBufOwner;	/* SLM# currently using the buffer */

/* DMA variables */
#ifndef SLM_CONT_CNT_REPROG
static unsigned long	SLMCurAddr;		/* current base addr of DMA chunk */
static unsigned long	SLMEndAddr;		/* expected end addr */
static unsigned long	SLMSliceSize;	/* size of one DMA chunk */
#endif
static int				SLMError;

/* wait queues */
static DECLARE_WAIT_QUEUE_HEAD(slm_wait);	/* waiting for buffer */
static DECLARE_WAIT_QUEUE_HEAD(print_wait);	/* waiting for printing finished */

/* status codes */
#define	SLMSTAT_OK		0x00
#define	SLMSTAT_ORNERY	0x02
#define	SLMSTAT_TONER	0x03
#define	SLMSTAT_WARMUP	0x04
#define	SLMSTAT_PAPER	0x05
#define	SLMSTAT_DRUM	0x06
#define	SLMSTAT_INJAM	0x07
#define	SLMSTAT_THRJAM	0x08
#define	SLMSTAT_OUTJAM	0x09
#define	SLMSTAT_COVER	0x0a
#define	SLMSTAT_FUSER	0x0b
#define	SLMSTAT_IMAGER	0x0c
#define	SLMSTAT_MOTOR	0x0d
#define	SLMSTAT_VIDEO	0x0e
#define	SLMSTAT_SYSTO	0x10
#define	SLMSTAT_OPCODE	0x12
#define	SLMSTAT_DEVNUM	0x15
#define	SLMSTAT_PARAM	0x1a
#define	SLMSTAT_ACSITO	0x1b	/* driver defined */
#define	SLMSTAT_NOTALL	0x1c	/* driver defined */

static char *SLMErrors[] = {
	/* 0x00 */	"OK and ready",
	/* 0x01 */	NULL,
	/* 0x02 */	"ornery printer",
	/* 0x03 */	"toner empty",
	/* 0x04 */	"warming up",
	/* 0x05 */	"paper empty",
	/* 0x06 */	"drum empty",
	/* 0x07 */	"input jam",
	/* 0x08 */	"through jam",
	/* 0x09 */	"output jam",
	/* 0x0a */	"cover open",
	/* 0x0b */	"fuser malfunction",
	/* 0x0c */	"imager malfunction",
	/* 0x0d */	"motor malfunction",
	/* 0x0e */	"video malfunction",
	/* 0x0f */	NULL,
	/* 0x10 */	"printer system timeout",
	/* 0x11 */	NULL,
	/* 0x12 */	"invalid operation code",
	/* 0x13 */	NULL,
	/* 0x14 */	NULL,
	/* 0x15 */	"invalid device number",
	/* 0x16 */	NULL,
	/* 0x17 */	NULL,
	/* 0x18 */	NULL,
	/* 0x19 */	NULL,
	/* 0x1a */	"invalid parameter list",
	/* 0x1b */	"ACSI timeout",
	/* 0x1c */	"not all printed"
};

#define	N_ERRORS	(sizeof(SLMErrors)/sizeof(*SLMErrors))

/* real (driver caused) error? */
#define	IS_REAL_ERROR(x)	(x > 0x10)


static struct {
	char	*name;
	int 	w, h;
} StdPageSize[] = {
	{ "Letter", 2400, 3180 },
	{ "Legal",  2400, 4080 },
	{ "A4",     2336, 3386 },
	{ "B5",     2016, 2914 }
};

#define	N_STD_SIZES		(sizeof(StdPageSize)/sizeof(*StdPageSize))

#define	SLM_BUFFER_SIZE	(2336*3386/8)	/* A4 for now */
#define	SLM_DMA_AMOUNT	255				/* #sectors to program the DMA for */

#ifdef	SLM_CONTINUOUS_DMA
# define	SLM_DMA_INT_OFFSET	0		/* DMA goes until seccnt 0, no offs */
# define	SLM_DMA_END_OFFSET	32		/* 32 Byte ST-DMA FIFO */
# define	SLM_SLICE_SIZE(w) 	(255*512)
#else
# define	SLM_DMA_INT_OFFSET	32		/* 32 Byte ST-DMA FIFO */
# define	SLM_DMA_END_OFFSET	32		/* 32 Byte ST-DMA FIFO */
# define	SLM_SLICE_SIZE(w)	((254*512)/(w/8)*(w/8))
#endif

/* calculate the number of jiffies to wait for 'n' bytes */
#ifdef SLM_CONT_CNT_REPROG
#define	DMA_TIME_FOR(n)		50
#define	DMA_STARTUP_TIME	0
#else
#define	DMA_TIME_FOR(n)		(n/1400-1)
#define	DMA_STARTUP_TIME	650
#endif

/***************************** Prototypes *****************************/

static char *slm_errstr( int stat );
static int slm_getstats( char *buffer, int device );
static ssize_t slm_read( struct file* file, char *buf, size_t count, loff_t
                         *ppos );
static void start_print( int device );
static irqreturn_t slm_interrupt(int irc, void *data);
static void slm_test_ready( unsigned long dummy );
static void set_dma_addr( unsigned long paddr );
static unsigned long get_dma_addr( void );
static ssize_t slm_write( struct file *file, const char *buf, size_t count,
                          loff_t *ppos );
static int slm_ioctl( struct inode *inode, struct file *file, unsigned int
                      cmd, unsigned long arg );
static int slm_open( struct inode *inode, struct file *file );
static int slm_release( struct inode *inode, struct file *file );
static int slm_req_sense( int device );
static int slm_mode_sense( int device, char *buffer, int abs_flag );
#if 0
static int slm_mode_select( int device, char *buffer, int len, int
                            default_flag );
#endif
static int slm_get_pagesize( int device, int *w, int *h );

/************************* End of Prototypes **************************/


static DEFINE_TIMER(slm_timer, slm_test_ready, 0, 0);

static struct file_operations slm_fops = {
	.owner =	THIS_MODULE,
	.read =		slm_read,
	.write =	slm_write,
	.ioctl =	slm_ioctl,
	.open =		slm_open,
	.release =	slm_release,
};


/* ---------------------------------------------------------------------- */
/*							   Status Functions							  */


static char *slm_errstr( int stat )

{	char *p;
	static char	str[22];

	stat &= 0x1f;
	if (stat >= 0 && stat < N_ERRORS && (p = SLMErrors[stat]))
		return( p );
	sprintf( str, "unknown status 0x%02x", stat );
	return( str );
}


static int slm_getstats( char *buffer, int device )

{	int 			len = 0, stat, i, w, h;
	unsigned char	buf[256];
	
	stat = slm_mode_sense( device, buf, 0 );
	if (IS_REAL_ERROR(stat))
		return( -EIO );
	
#define SHORTDATA(i)		((buf[i] << 8) | buf[i+1])
#define	BOOLDATA(i,mask)	((buf[i] & mask) ? "on" : "off")

	w = SHORTDATA( 3 );
	h = SHORTDATA( 1 );
		
	len += sprintf( buffer+len, "Status\t\t%s\n",
					slm_errstr( stat ) );
	len += sprintf( buffer+len, "Page Size\t%dx%d",
					w, h );

	for( i = 0; i < N_STD_SIZES; ++i ) {
		if (w == StdPageSize[i].w && h == StdPageSize[i].h)
			break;
	}
	if (i < N_STD_SIZES)
		len += sprintf( buffer+len, " (%s)", StdPageSize[i].name );
	buffer[len++] = '\n';

	len += sprintf( buffer+len, "Top/Left Margin\t%d/%d\n",
					SHORTDATA( 5 ), SHORTDATA( 7 ) );
	len += sprintf( buffer+len, "Manual Feed\t%s\n",
					BOOLDATA( 9, 0x01 ) );
	len += sprintf( buffer+len, "Input Select\t%d\n",
					(buf[9] >> 1) & 7 );
	len += sprintf( buffer+len, "Auto Select\t%s\n",
					BOOLDATA( 9, 0x10 ) );
	len += sprintf( buffer+len, "Prefeed Paper\t%s\n",
					BOOLDATA( 9, 0x20 ) );
	len += sprintf( buffer+len, "Thick Pixels\t%s\n",
					BOOLDATA( 9, 0x40 ) );
	len += sprintf( buffer+len, "H/V Resol.\t%d/%d dpi\n",
					SHORTDATA( 12 ), SHORTDATA( 10 ) );
	len += sprintf( buffer+len, "System Timeout\t%d\n",
					buf[14] );
	len += sprintf( buffer+len, "Scan Time\t%d\n",
					SHORTDATA( 15 ) );
	len += sprintf( buffer+len, "Page Count\t%d\n",
					SHORTDATA( 17 ) );
	len += sprintf( buffer+len, "In/Out Cap.\t%d/%d\n",
					SHORTDATA( 19 ), SHORTDATA( 21 ) );
	len += sprintf( buffer+len, "Stagger Output\t%s\n",
					BOOLDATA( 23, 0x01 ) );
	len += sprintf( buffer+len, "Output Select\t%d\n",
					(buf[23] >> 1) & 7 );
	len += sprintf( buffer+len, "Duplex Print\t%s\n",
					BOOLDATA( 23, 0x10 ) );
	len += sprintf( buffer+len, "Color Sep.\t%s\n",
					BOOLDATA( 23, 0x20 ) );

	return( len );
}


static ssize_t slm_read( struct file *file, char *buf, size_t count,
						 loff_t *ppos )

{
	struct inode *node = file->f_path.dentry->d_inode;
	unsigned long page;
	int length;
	int end;

	if (!(page = __get_free_page( GFP_KERNEL )))
		return( -ENOMEM );
	
	length = slm_getstats( (char *)page, iminor(node) );
	if (length < 0) {
		count = length;
		goto out;
	}
	if (file->f_pos >= length) {
		count = 0;
		goto out;
	}
	if (count + file->f_pos > length)
		count = length - file->f_pos;
	end = count + file->f_pos;
	if (copy_to_user(buf, (char *)page + file->f_pos, count)) {
		count = -EFAULT;
		goto out;
	}
	file->f_pos = end;
out:	free_page( page );
	return( count );
}


/* ---------------------------------------------------------------------- */
/*								   Printing								  */


static void start_print( int device )

{	struct slm *sip = &slm_info[device];
	unsigned char	*cmd;
	unsigned long	paddr;
	int				i;
	
	stdma_lock( slm_interrupt, NULL );

	CMDSET_TARG_LUN( slmprint_cmd, sip->target, sip->lun );
	cmd = slmprint_cmd;
	paddr = virt_to_phys( SLMBuffer );
	dma_cache_maintenance( paddr, virt_to_phys(BufferP)-paddr, 1 );
	DISABLE_IRQ();

	/* Low on A1 */
	dma_wd.dma_mode_status = 0x88;
	MFPDELAY();

	/* send the command bytes except the last */
	for( i = 0; i < 5; ++i ) {
		DMA_LONG_WRITE( *cmd++, 0x8a );
		udelay(20);
		if (!acsi_wait_for_IRQ( HZ/2 )) {
			SLMError = 1;
			return; /* timeout */
		}
	}
	/* last command byte */
	DMA_LONG_WRITE( *cmd++, 0x82 );
	MFPDELAY();
	/* set DMA address */
	set_dma_addr( paddr );
	/* program DMA for write and select sector counter reg */
	dma_wd.dma_mode_status = 0x192;
	MFPDELAY();
	/* program for 255*512 bytes and start DMA */
	DMA_LONG_WRITE( SLM_DMA_AMOUNT, 0x112 );

#ifndef SLM_CONT_CNT_REPROG
	SLMCurAddr = paddr;
	SLMEndAddr = paddr + SLMSliceSize + SLM_DMA_INT_OFFSET;
#endif
	START_TIMER( DMA_STARTUP_TIME + DMA_TIME_FOR( SLMSliceSize ));
#if !defined(SLM_CONT_CNT_REPROG) && defined(DEBUG)
	printk( "SLM: CurAddr=%#lx EndAddr=%#lx timer=%ld\n",
			SLMCurAddr, SLMEndAddr, DMA_TIME_FOR( SLMSliceSize ) );
#endif
	
	ENABLE_IRQ();
}


/* Only called when an error happened or at the end of a page */

static irqreturn_t slm_interrupt(int irc, void *data)

{	unsigned long	addr;
	int				stat;
	
	STOP_TIMER();
	addr = get_dma_addr();
	stat = acsi_getstatus();
	SLMError = (stat < 0)             ? SLMSTAT_ACSITO :
		       (addr < virt_to_phys(BufferP)) ? SLMSTAT_NOTALL :
									    stat;

	dma_wd.dma_mode_status = 0x80;
	MFPDELAY();
#ifdef DEBUG
	printk( "SLM: interrupt, addr=%#lx, error=%d\n", addr, SLMError );
#endif

	wake_up( &print_wait );
	stdma_release();
	ENABLE_IRQ();
	return IRQ_HANDLED;
}


static void slm_test_ready( unsigned long dummy )

{
#ifdef SLM_CONT_CNT_REPROG
	/* program for 255*512 bytes again */
	dma_wd.fdc_acces_seccount = SLM_DMA_AMOUNT;
	START_TIMER( DMA_TIME_FOR(0) );
#ifdef DEBUG
	printk( "SLM: reprogramming timer for %d jiffies, addr=%#lx\n",
			DMA_TIME_FOR(0), get_dma_addr() );
#endif
	
#else /* !SLM_CONT_CNT_REPROG */

	unsigned long	flags, addr;
	int				d, ti;
#ifdef DEBUG
	struct timeval start_tm, end_tm;
	int			   did_wait = 0;
#endif

	local_irq_save(flags);

	addr = get_dma_addr();
	if ((d = SLMEndAddr - addr) > 0) {
		local_irq_restore(flags);
		
		/* slice not yet finished, decide whether to start another timer or to
		 * busy-wait */
		ti = DMA_TIME_FOR( d );
		if (ti > 0) {
#ifdef DEBUG
			printk( "SLM: reprogramming timer for %d jiffies, rest %d bytes\n",
					ti, d );
#endif
			START_TIMER( ti );
			return;
		}
		/* wait for desired end address to be reached */
#ifdef DEBUG
		do_gettimeofday( &start_tm );
		did_wait = 1;
#endif
		local_irq_disable();
		while( get_dma_addr() < SLMEndAddr )
			barrier();
	}

	/* slice finished, start next one */
	SLMCurAddr += SLMSliceSize;

#ifdef SLM_CONTINUOUS_DMA
	/* program for 255*512 bytes again */
	dma_wd.fdc_acces_seccount = SLM_DMA_AMOUNT;
#else
	/* set DMA address;
	 * add 2 bytes for the ones in the SLM controller FIFO! */
	set_dma_addr( SLMCurAddr + 2 );
	/* toggle DMA to write and select sector counter reg */
	dma_wd.dma_mode_status = 0x92;
	MFPDELAY();
	dma_wd.dma_mode_status = 0x192;
	MFPDELAY();
	/* program for 255*512 bytes and start DMA */
	DMA_LONG_WRITE( SLM_DMA_AMOUNT, 0x112 );
#endif
	
	local_irq_restore(flags);

#ifdef DEBUG
	if (did_wait) {
		int ms;
		do_gettimeofday( &end_tm );
		ms = (end_tm.tv_sec*1000000+end_tm.tv_usec) -
			 (start_tm.tv_sec*1000000+start_tm.tv_usec); 
		printk( "SLM: did %ld.%ld ms busy waiting for %d bytes\n",
				ms/1000, ms%1000, d );
	}
	else
		printk( "SLM: didn't wait (!)\n" );
#endif

	if ((unsigned char *)PTOV( SLMCurAddr + SLMSliceSize ) >= BufferP) {
		/* will be last slice, no timer necessary */
#ifdef DEBUG
		printk( "SLM: CurAddr=%#lx EndAddr=%#lx last slice -> no timer\n",
				SLMCurAddr, SLMEndAddr );
#endif
	}
	else {
		/* not last slice */
		SLMEndAddr = SLMCurAddr + SLMSliceSize + SLM_DMA_INT_OFFSET;
		START_TIMER( DMA_TIME_FOR( SLMSliceSize ));
#ifdef DEBUG
		printk( "SLM: CurAddr=%#lx EndAddr=%#lx timer=%ld\n",
				SLMCurAddr, SLMEndAddr, DMA_TIME_FOR( SLMSliceSize ) );
#endif
	}
#endif /* SLM_CONT_CNT_REPROG */
}


static void set_dma_addr( unsigned long paddr )

{	unsigned long flags;

	local_irq_save(flags);
	dma_wd.dma_lo = (unsigned char)paddr;
	paddr >>= 8;
	MFPDELAY();
	dma_wd.dma_md = (unsigned char)paddr;
	paddr >>= 8;
	MFPDELAY();
	if (ATARIHW_PRESENT( EXTD_DMA ))
		st_dma_ext_dmahi = (unsigned short)paddr;
	else
		dma_wd.dma_hi = (unsigned char)paddr;
	MFPDELAY();
	local_irq_restore(flags);
}


static unsigned long get_dma_addr( void )

{	unsigned long	addr;
	
	addr = dma_wd.dma_lo & 0xff;
	MFPDELAY();
	addr |= (dma_wd.dma_md & 0xff) << 8;
	MFPDELAY();
	addr |= (dma_wd.dma_hi & 0xff) << 16;
	MFPDELAY();

	return( addr );
}


static ssize_t slm_write( struct file *file, const char *buf, size_t count,
						  loff_t *ppos )

{
	struct inode *node = file->f_path.dentry->d_inode;
	int		device = iminor(node);
	int		n, filled, w, h;

	while( SLMState == PRINTING ||
		   (SLMState == FILLING && SLMBufOwner != device) ) {
		interruptible_sleep_on( &slm_wait );
		if (signal_pending(current))
			return( -ERESTARTSYS );
	}
	if (SLMState == IDLE) {
		/* first data of page: get current page size  */
		if (slm_get_pagesize( device, &w, &h ))
			return( -EIO );
		BufferSize = w*h/8;
		if (BufferSize > SLM_BUFFER_SIZE)
			return( -ENOMEM );

		SLMState = FILLING;
		SLMBufOwner = device;
	}

	n = count;
	filled = BufferP - SLMBuffer;
	if (filled + n > BufferSize)
		n = BufferSize - filled;

	if (copy_from_user(BufferP, buf, n))
		return -EFAULT;
	BufferP += n;
	filled += n;

	if (filled == BufferSize) {
		/* Check the paper size again! The user may have switched it in the
		 * time between starting the data and finishing them. Would end up in
		 * a trashy page... */
		if (slm_get_pagesize( device, &w, &h ))
			return( -EIO );
		if (BufferSize != w*h/8) {
			printk( KERN_NOTICE "slm%d: page size changed while printing\n",
					device );
			return( -EAGAIN );
		}

		SLMState = PRINTING;
		/* choose a slice size that is a multiple of the line size */
#ifndef SLM_CONT_CNT_REPROG
		SLMSliceSize = SLM_SLICE_SIZE(w);
#endif
		
		start_print( device );
		sleep_on( &print_wait );
		if (SLMError && IS_REAL_ERROR(SLMError)) {
			printk( KERN_ERR "slm%d: %s\n", device, slm_errstr(SLMError) );
			n = -EIO;
		}

		SLMState = IDLE;
		BufferP = SLMBuffer;
		wake_up_interruptible( &slm_wait );
	}
	
	return( n );
}


/* ---------------------------------------------------------------------- */
/*							   ioctl Functions							  */


static int slm_ioctl( struct inode *inode, struct file *file,
					  unsigned int cmd, unsigned long arg )

{	int		device = iminor(inode), err;
	
	/* I can think of setting:
	 *  - manual feed
	 *  - paper format
	 *  - copy count
	 *  - ...
	 * but haven't implemented that yet :-)
	 * BTW, has anybody better docs about the MODE SENSE/MODE SELECT data?
	 */
	switch( cmd ) {

	  case SLMIORESET:		/* reset buffer, i.e. empty the buffer */
		if (!(file->f_mode & 2))
			return( -EINVAL );
		if (SLMState == PRINTING)
			return( -EBUSY );
		SLMState = IDLE;
		BufferP = SLMBuffer;
		wake_up_interruptible( &slm_wait );
		return( 0 );
		
	  case SLMIOGSTAT: {	/* get status */
		int stat;
		char *str;

		stat = slm_req_sense( device );
		if (arg) {
			str = slm_errstr( stat );
			if (put_user(stat,
    	    	    	    	     (long *)&((struct SLM_status *)arg)->stat))
    	    	    	    	return -EFAULT;
			if (copy_to_user( ((struct SLM_status *)arg)->str, str,
						 strlen(str) + 1))
				return -EFAULT;
		}
		return( stat );
	  }
		
	  case SLMIOGPSIZE: {	/* get paper size */
		int w, h;
		
		if ((err = slm_get_pagesize( device, &w, &h ))) return( err );
		
    	    	if (put_user(w, (long *)&((struct SLM_paper_size *)arg)->width))
			return -EFAULT;
		if (put_user(h, (long *)&((struct SLM_paper_size *)arg)->height))
			return -EFAULT;
		return( 0 );
	  }
		
	  case SLMIOGMFEED:	/* get manual feed */
		return( -EINVAL );

	  case SLMIOSPSIZE:	/* set paper size */
		return( -EINVAL );

	  case SLMIOSMFEED:	/* set manual feed */
		return( -EINVAL );

	}
	return( -EINVAL );
}


/* ---------------------------------------------------------------------- */
/*							 Opening and Closing						  */


static int slm_open( struct inode *inode, struct file *file )

{	int device;
	struct slm *sip;
	
	device = iminor(inode);
	if (device >= N_SLM_Printers)
		return( -ENXIO );
	sip = &slm_info[device];

	if (file->f_mode & 2) {
		/* open for writing is exclusive */
		if ( !atomic_dec_and_test(&sip->wr_ok) ) {
			atomic_inc(&sip->wr_ok);	
			return( -EBUSY );
		}
	}
	if (file->f_mode & 1) {
		/* open for reading is exclusive */
                if ( !atomic_dec_and_test(&sip->rd_ok) ) {
                        atomic_inc(&sip->rd_ok);
                        return( -EBUSY );
                }
	}

	return( 0 );
}


static int slm_release( struct inode *inode, struct file *file )

{	int device;
	struct slm *sip;
	
	device = iminor(inode);
	sip = &slm_info[device];

	if (file->f_mode & 2)
		atomic_inc( &sip->wr_ok );
	if (file->f_mode & 1)
		atomic_inc( &sip->rd_ok );
	
	return( 0 );
}


/* ---------------------------------------------------------------------- */
/*						 ACSI Primitives for the SLM					  */


static int slm_req_sense( int device )

{	int			stat, rv;
	struct slm *sip = &slm_info[device];
	
	stdma_lock( NULL, NULL );

	CMDSET_TARG_LUN( slmreqsense_cmd, sip->target, sip->lun );
	if (!acsicmd_nodma( slmreqsense_cmd, 0 ) ||
		(stat = acsi_getstatus()) < 0)
		rv = SLMSTAT_ACSITO;
	else
		rv = stat & 0x1f;

	ENABLE_IRQ();
	stdma_release();
	return( rv );
}


static int slm_mode_sense( int device, char *buffer, int abs_flag )

{	unsigned char	stat, len;
	int				rv = 0;
	struct slm		*sip = &slm_info[device];
	
	stdma_lock( NULL, NULL );

	CMDSET_TARG_LUN( slmmsense_cmd, sip->target, sip->lun );
	slmmsense_cmd[5] = abs_flag ? 0x80 : 0;
	if (!acsicmd_nodma( slmmsense_cmd, 0 )) {
		rv = SLMSTAT_ACSITO;
		goto the_end;
	}

	if (!acsi_extstatus( &stat, 1 )) {
		acsi_end_extstatus();
		rv = SLMSTAT_ACSITO;
		goto the_end;
	}
	
	if (!acsi_extstatus( &len, 1 )) {
		acsi_end_extstatus();
		rv = SLMSTAT_ACSITO;
		goto the_end;
	}
	buffer[0] = len;
	if (!acsi_extstatus( buffer+1, len )) {
		acsi_end_extstatus();
		rv = SLMSTAT_ACSITO;
		goto the_end;
	}
	
	acsi_end_extstatus();
	rv = stat & 0x1f;

  the_end:
	ENABLE_IRQ();
	stdma_release();
	return( rv );
}


#if 0
/* currently unused */
static int slm_mode_select( int device, char *buffer, int len,
							int default_flag )

{	int			stat, rv;
	struct slm	*sip = &slm_info[device];
	
	stdma_lock( NULL, NULL );

	CMDSET_TARG_LUN( slmmselect_cmd, sip->target, sip->lun );
	slmmselect_cmd[5] = default_flag ? 0x80 : 0;
	if (!acsicmd_nodma( slmmselect_cmd, 0 )) {
		rv = SLMSTAT_ACSITO;
		goto the_end;
	}

	if (!default_flag) {
		unsigned char c = len;
		if (!acsi_extcmd( &c, 1 )) {
			rv = SLMSTAT_ACSITO;
			goto the_end;
		}
		if (!acsi_extcmd( buffer, len )) {
			rv = SLMSTAT_ACSITO;
			goto the_end;
		}
	}
	
	stat = acsi_getstatus();
	rv = (stat < 0 ? SLMSTAT_ACSITO : stat);

  the_end:
	ENABLE_IRQ();
	stdma_release();
	return( rv );
}
#endif


static int slm_get_pagesize( int device, int *w, int *h )

{	char	buf[256];
	int		stat;
	
	stat = slm_mode_sense( device, buf, 0 );
	ENABLE_IRQ();
	stdma_release();

	if (stat != SLMSTAT_OK)
		return( -EIO );

	*w = (buf[3] << 8) | buf[4];
	*h = (buf[1] << 8) | buf[2];
	return( 0 );
}


/* ---------------------------------------------------------------------- */
/*								Initialization							  */


int attach_slm( int target, int lun )

{	static int	did_register;
	int			len;

	if (N_SLM_Printers >= MAX_SLM) {
		printk( KERN_WARNING "Too much SLMs\n" );
		return( 0 );
	}
	
	/* do an INQUIRY */
	udelay(100);
	CMDSET_TARG_LUN( slminquiry_cmd, target, lun );
	if (!acsicmd_nodma( slminquiry_cmd, 0 )) {
	  inq_timeout:
		printk( KERN_ERR "SLM inquiry command timed out.\n" );
	  inq_fail:
		acsi_end_extstatus();
		return( 0 );
	}
	/* read status and header of return data */
	if (!acsi_extstatus( SLMBuffer, 6 ))
		goto inq_timeout;

	if (SLMBuffer[1] != 2) { /* device type == printer? */
		printk( KERN_ERR "SLM inquiry returned device type != printer\n" );
		goto inq_fail;
	}
	len = SLMBuffer[5];
	
	/* read id string */
	if (!acsi_extstatus( SLMBuffer, len ))
		goto inq_timeout;
	acsi_end_extstatus();
	SLMBuffer[len] = 0;

	if (!did_register) {
		did_register = 1;
	}

	slm_info[N_SLM_Printers].target = target;
	slm_info[N_SLM_Printers].lun    = lun;
	atomic_set(&slm_info[N_SLM_Printers].wr_ok, 1 ); 
	atomic_set(&slm_info[N_SLM_Printers].rd_ok, 1 );
	
	printk( KERN_INFO "  Printer: %s\n", SLMBuffer );
	printk( KERN_INFO "Detected slm%d at id %d lun %d\n",
			N_SLM_Printers, target, lun );
	N_SLM_Printers++;
	return( 1 );
}

int slm_init( void )

{
	int i;
	if (register_chrdev( ACSI_MAJOR, "slm", &slm_fops )) {
		printk( KERN_ERR "Unable to get major %d for ACSI SLM\n", ACSI_MAJOR );
		return -EBUSY;
	}
	
	if (!(SLMBuffer = atari_stram_alloc( SLM_BUFFER_SIZE, "SLM" ))) {
		printk( KERN_ERR "Unable to get SLM ST-Ram buffer.\n" );
		unregister_chrdev( ACSI_MAJOR, "slm" );
		return -ENOMEM;
	}
	BufferP = SLMBuffer;
	SLMState = IDLE;
	
	return 0;
}

#ifdef MODULE

/* from acsi.c */
void acsi_attach_SLMs( int (*attach_func)( int, int ) );

int init_module(void)
{
	int err;

	if ((err = slm_init()))
		return( err );
	/* This calls attach_slm() for every target/lun where acsi.c detected a
	 * printer */
	acsi_attach_SLMs( attach_slm );
	return( 0 );
}

void cleanup_module(void)
{
	if (unregister_chrdev( ACSI_MAJOR, "slm" ) != 0)
		printk( KERN_ERR "acsi_slm: cleanup_module failed\n");
	atari_stram_free( SLMBuffer );
}
#endif
