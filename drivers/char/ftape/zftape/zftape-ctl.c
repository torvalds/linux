/* 
 *      Copyright (C) 1996, 1997 Claus-Justus Heine

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/zftape/zftape-ctl.c,v $
 * $Revision: 1.2.6.2 $
 * $Date: 1997/11/14 18:07:33 $
 *
 *      This file contains the non-read/write zftape functions
 *      for the QIC-40/80/3010/3020 floppy-tape driver for Linux.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/fcntl.h>

#include <linux/zftape.h>

#include <asm/uaccess.h>

#include "../zftape/zftape-init.h"
#include "../zftape/zftape-eof.h"
#include "../zftape/zftape-ctl.h"
#include "../zftape/zftape-write.h"
#include "../zftape/zftape-read.h"
#include "../zftape/zftape-rw.h"
#include "../zftape/zftape-vtbl.h"

/*      Global vars.
 */
int zft_write_protected; /* this is when cartridge rdonly or O_RDONLY */
int zft_header_read;
int zft_offline;
unsigned int zft_unit;
int zft_resid;
int zft_mt_compression;

/*      Local vars.
 */
static int going_offline;

typedef int (mt_fun)(int *argptr);
typedef int (*mt_funp)(int *argptr);
typedef struct
{
	mt_funp function;
	unsigned offline         : 1; /* op permitted if offline or no_tape */
	unsigned write_protected : 1; /* op permitted if write-protected    */
	unsigned not_formatted   : 1; /* op permitted if tape not formatted */
	unsigned raw_mode        : 1; /* op permitted if zft_mode == 0    */
	unsigned need_idle_state : 1; /* need to call def_idle_state        */
	char     *name;
} fun_entry;

static mt_fun mt_dummy, mt_reset, mt_fsr, mt_bsr, mt_rew, mt_offl, mt_nop,
	mt_weof, mt_erase, mt_ras2, mt_setblk, mt_setdensity,
	mt_seek, mt_tell, mt_reten, mt_eom, mt_fsf, mt_bsf,
	mt_fsfm, mt_bsfm, mt_setdrvbuffer, mt_compression;

static fun_entry mt_funs[]=
{ 
	{mt_reset       , 1, 1, 1, 1, 0, "MT_RESET" }, /*  0 */
	{mt_fsf         , 0, 1, 0, 0, 1, "MT_FSF"   },
	{mt_bsf         , 0, 1, 0, 0, 1, "MT_BSF"   },
	{mt_fsr         , 0, 1, 0, 1, 1, "MT_FSR"   },
	{mt_bsr         , 0, 1, 0, 1, 1, "MT_BSR"   },
	{mt_weof        , 0, 0, 0, 0, 0, "MT_WEOF"  }, /*  5 */
	{mt_rew         , 0, 1, 1, 1, 0, "MT_REW"   },
	{mt_offl        , 0, 1, 1, 1, 0, "MT_OFFL"  },
	{mt_nop         , 1, 1, 1, 1, 0, "MT_NOP"   },
	{mt_reten       , 0, 1, 1, 1, 0, "MT_RETEN" },
	{mt_bsfm        , 0, 1, 0, 0, 1, "MT_BSFM"  }, /* 10 */
	{mt_fsfm        , 0, 1, 0, 0, 1, "MT_FSFM"  },
	{mt_eom         , 0, 1, 0, 0, 1, "MT_EOM"   },
	{mt_erase       , 0, 0, 0, 1, 0, "MT_ERASE" },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_RAS1"  },
	{mt_ras2        , 0, 0, 0, 1, 0, "MT_RAS2"  },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_RAS3"  },
	{mt_dummy       , 1, 1, 1, 1, 0, "UNKNOWN"  },
	{mt_dummy       , 1, 1, 1, 1, 0, "UNKNOWN"  },
	{mt_dummy       , 1, 1, 1, 1, 0, "UNKNOWN"  },
	{mt_setblk      , 1, 1, 1, 1, 1, "MT_SETBLK"}, /* 20 */
	{mt_setdensity  , 1, 1, 1, 1, 0, "MT_SETDENSITY"},
	{mt_seek        , 0, 1, 0, 1, 1, "MT_SEEK"  },
	{mt_dummy       , 0, 1, 0, 1, 1, "MT_TELL"  }, /* wr-only ?! */
	{mt_setdrvbuffer, 1, 1, 1, 1, 0, "MT_SETDRVBUFFER" },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_FSS"   }, /* 25 */
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_BSS"   },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_WSM"   },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_LOCK"  },
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_UNLOCK"},
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_LOAD"  }, /* 30 */
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_UNLOAD"},
	{mt_compression , 1, 1, 1, 0, 1, "MT_COMPRESSION"},
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_SETPART"},
	{mt_dummy       , 1, 1, 1, 1, 0, "MT_MKPART"}
};  

#define NR_MT_CMDS NR_ITEMS(mt_funs)

void zft_reset_position(zft_position *pos)
{
	TRACE_FUN(ft_t_flow);

	pos->seg_byte_pos =
		pos->volume_pos = 0;
	if (zft_header_read) {
		/* need to keep track of the volume table and
		 * compression map. We therefor simply
		 * position at the beginning of the first
		 * volume. This covers old ftape archives as
		 * well has various flavours of the
		 * compression map segments. The worst case is
		 * that the compression map shows up as a
		 * additional volume in front of all others.
		 */
		pos->seg_pos  = zft_find_volume(0)->start_seg;
		pos->tape_pos = zft_calc_tape_pos(pos->seg_pos);
	} else {
		pos->tape_pos =  0;
		pos->seg_pos  = -1;
	}
	zft_just_before_eof =  0;
	zft_deblock_segment = -1;
	zft_io_state        = zft_idle;
	zft_zap_read_buffers();
	zft_prevent_flush();
	/*  unlock the compresison module if it is loaded.
	 *  The zero arg means not to try to load the module.
	 */
	if (zft_cmpr_lock(0) == 0) {
		(*zft_cmpr_ops->reset)(); /* unlock */
	}
	TRACE_EXIT;
}

static void zft_init_driver(void)
{
	TRACE_FUN(ft_t_flow);

	zft_resid =
		zft_header_read          =
		zft_old_ftape            =
		zft_offline              =
		zft_write_protected      =
		going_offline            =
		zft_mt_compression       =
		zft_header_changed       =
		zft_volume_table_changed =
		zft_written_segments     = 0;
	zft_blk_sz = CONFIG_ZFT_DFLT_BLK_SZ;
	zft_reset_position(&zft_pos); /* does most of the stuff */
	ftape_zap_read_buffers();
	ftape_set_state(idle);
	TRACE_EXIT;
}

int zft_def_idle_state(void)
{ 
	int result = 0;
	TRACE_FUN(ft_t_flow);
	
	if (!zft_header_read) {
		result = zft_read_header_segments();
	} else if ((result = zft_flush_buffers()) >= 0 && zft_qic_mode) {
		/*  don't move past eof
		 */
		(void)zft_close_volume(&zft_pos);
	}
	if (ftape_abort_operation() < 0) {
		TRACE(ft_t_warn, "ftape_abort_operation() failed");
		result = -EIO;
	}
	/* clear remaining read buffers */
	zft_zap_read_buffers();
	zft_io_state = zft_idle;
	TRACE_EXIT result;
}

/*****************************************************************************
 *                                                                           *
 *  functions for the MTIOCTOP commands                                      *
 *                                                                           *
 *****************************************************************************/

static int mt_dummy(int *dummy)
{
	TRACE_FUN(ft_t_flow);
	
	TRACE_EXIT -ENOSYS;
}

static int mt_reset(int *dummy)
{        
	TRACE_FUN(ft_t_flow);
	
	(void)ftape_seek_to_bot();
	TRACE_CATCH(ftape_reset_drive(),
		    zft_init_driver(); zft_uninit_mem(); zft_offline = 1);
	/*  fake a re-open of the device. This will set all flage and 
	 *  allocate buffers as appropriate. The new tape condition will
	 *  force the open routine to do anything we need.
	 */
	TRACE_CATCH(_zft_open(-1 /* fake reopen */, 0 /* dummy */),);
	TRACE_EXIT 0;
}

static int mt_fsf(int *arg)
{
	int result;
	TRACE_FUN(ft_t_flow);

	result = zft_skip_volumes(*arg, &zft_pos);
	zft_just_before_eof = 0;
	TRACE_EXIT result;
}

static int mt_bsf(int *arg)
{
	int result = 0;
	TRACE_FUN(ft_t_flow);
	
	if (*arg != 0) {
		result = zft_skip_volumes(-*arg + 1, &zft_pos);
	}
	TRACE_EXIT result;
}

static int seek_block(__s64 data_offset,
		      __s64 block_increment,
		      zft_position *pos)
{ 
	int result      = 0;
	__s64 new_block_pos;
	__s64 vol_block_count;
	const zft_volinfo *volume;
	int exceed;
	TRACE_FUN(ft_t_flow);
	
	volume = zft_find_volume(pos->seg_pos);
	if (volume->start_seg == 0 || volume->end_seg == 0) {
		TRACE_EXIT -EIO;
	}
	new_block_pos   = (zft_div_blksz(data_offset, volume->blk_sz)
			   + block_increment);
	vol_block_count = zft_div_blksz(volume->size, volume->blk_sz);
	if (new_block_pos < 0) {
		TRACE(ft_t_noise,
		      "new_block_pos " LL_X " < 0", LL(new_block_pos));
		zft_resid     = (int)new_block_pos;
		new_block_pos = 0;
		exceed = 1;
	} else if (new_block_pos > vol_block_count) {
		TRACE(ft_t_noise,
		      "new_block_pos " LL_X " exceeds size of volume " LL_X,
		      LL(new_block_pos), LL(vol_block_count));
		zft_resid     = (int)(vol_block_count - new_block_pos);
		new_block_pos = vol_block_count;
		exceed = 1;
	} else {
		exceed = 0;
	}
	if (zft_use_compression && volume->use_compression) {
		TRACE_CATCH(zft_cmpr_lock(1 /* try to load */),);
		result = (*zft_cmpr_ops->seek)(new_block_pos, pos, volume,
					       zft_deblock_buf);
		pos->tape_pos  = zft_calc_tape_pos(pos->seg_pos);
		pos->tape_pos += pos->seg_byte_pos;
	} else {
		pos->volume_pos = zft_mul_blksz(new_block_pos, volume->blk_sz);
		pos->tape_pos   = zft_calc_tape_pos(volume->start_seg);
		pos->tape_pos  += pos->volume_pos;
		pos->seg_pos    = zft_calc_seg_byte_coord(&pos->seg_byte_pos,
							  pos->tape_pos);
	}
	zft_just_before_eof = volume->size == pos->volume_pos;
	if (zft_just_before_eof) {
		/* why this? because zft_file_no checks agains start
		 * and end segment of a volume. We do not want to
		 * advance to the next volume with this function.
		 */
		TRACE(ft_t_noise, "set zft_just_before_eof");
		zft_position_before_eof(pos, volume);
	}
	TRACE(ft_t_noise, "\n"
	      KERN_INFO "new_seg_pos : %d\n"
	      KERN_INFO "new_tape_pos: " LL_X "\n"
	      KERN_INFO "vol_size    : " LL_X "\n"
	      KERN_INFO "seg_byte_pos: %d\n"
	      KERN_INFO "blk_sz  : %d", 
	      pos->seg_pos, LL(pos->tape_pos),
	      LL(volume->size), pos->seg_byte_pos,
	      volume->blk_sz);
	if (!exceed) {
		zft_resid = new_block_pos - zft_div_blksz(pos->volume_pos,
							  volume->blk_sz);
	}
	if (zft_resid < 0) {
		zft_resid = -zft_resid;
	}
	TRACE_EXIT ((exceed || zft_resid != 0) && result >= 0) ? -EINVAL : result;
}     

static int mt_fsr(int *arg)
{ 
	int result;
	TRACE_FUN(ft_t_flow);
	
	result = seek_block(zft_pos.volume_pos,  *arg, &zft_pos);
	TRACE_EXIT result;
}

static int mt_bsr(int *arg)
{   
	int result;
	TRACE_FUN(ft_t_flow);
	
	result = seek_block(zft_pos.volume_pos, -*arg, &zft_pos);
	TRACE_EXIT result;
}

static int mt_weof(int *arg)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	TRACE_CATCH(zft_flush_buffers(),);
	result = zft_weof(*arg, &zft_pos);
	TRACE_EXIT result;
}

static int mt_rew(int *dummy)
{          
	int result;
	TRACE_FUN(ft_t_flow);
	
	if(zft_header_read) {
		(void)zft_def_idle_state();
	}
	result = ftape_seek_to_bot();
	zft_reset_position(&zft_pos);
	TRACE_EXIT result;
}

static int mt_offl(int *dummy)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	going_offline= 1;
	result = mt_rew(NULL);
	TRACE_EXIT result;
}

static int mt_nop(int *dummy)
{
	TRACE_FUN(ft_t_flow);
	/*  should we set tape status?
	 */
	if (!zft_offline) { /* offline includes no_tape */
		(void)zft_def_idle_state();
	}
	TRACE_EXIT 0; 
}

static int mt_reten(int *dummy)
{  
	int result;
	TRACE_FUN(ft_t_flow);
	
	if(zft_header_read) {
		(void)zft_def_idle_state();
	}
	result = ftape_seek_to_eot();
	if (result >= 0) {
		result = ftape_seek_to_bot();
	}
	TRACE_EXIT(result);
}

static int fsfbsfm(int arg, zft_position *pos)
{ 
	const zft_volinfo *vtbl;
	__s64 block_pos;
	TRACE_FUN(ft_t_flow);
	
	/* What to do? This should seek to the next file-mark and
	 * position BEFORE. That is, a next write would just extend
	 * the current file.  Well. Let's just seek to the end of the
	 * current file, if count == 1.  If count > 1, then do a
	 * "mt_fsf(count - 1)", and then seek to the end of that file.
	 * If count == 0, do nothing
	 */
	if (arg == 0) {
		TRACE_EXIT 0;
	}
	zft_just_before_eof = 0;
	TRACE_CATCH(zft_skip_volumes(arg < 0 ? arg : arg-1, pos),
		    if (arg > 0) {
			    zft_resid ++; 
		    });
	vtbl      = zft_find_volume(pos->seg_pos);
	block_pos = zft_div_blksz(vtbl->size, vtbl->blk_sz);
	(void)seek_block(0, block_pos, pos);
	if (pos->volume_pos != vtbl->size) {
		zft_just_before_eof = 0;
		zft_resid = 1;
		/* we didn't managed to go there */
		TRACE_ABORT(-EIO, ft_t_err, 
			    "wanted file position " LL_X ", arrived at " LL_X, 
			    LL(vtbl->size), LL(pos->volume_pos));
	}
	zft_just_before_eof = 1;
	TRACE_EXIT 0; 
}

static int mt_bsfm(int *arg)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	result = fsfbsfm(-*arg, &zft_pos);
	TRACE_EXIT result;
}

static int mt_fsfm(int *arg)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	result = fsfbsfm(*arg, &zft_pos);
	TRACE_EXIT result;
}

static int mt_eom(int *dummy)
{              
	TRACE_FUN(ft_t_flow);
	
	zft_skip_to_eom(&zft_pos);
	TRACE_EXIT 0;
}

static int mt_erase(int *dummy)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	result = zft_erase();
	TRACE_EXIT result;
}

static int mt_ras2(int *dummy)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	result = -ENOSYS;
	TRACE_EXIT result;
} 

/*  Sets the new blocksize in BYTES
 *
 */
static int mt_setblk(int *new_size)
{
	TRACE_FUN(ft_t_flow);
	
	if((unsigned int)(*new_size) > ZFT_MAX_BLK_SZ) {
		TRACE_ABORT(-EINVAL, ft_t_info,
			    "desired blk_sz (%d) should be <= %d bytes",
			    *new_size, ZFT_MAX_BLK_SZ);
	}
	if ((*new_size & (FT_SECTOR_SIZE-1)) != 0) {
		TRACE_ABORT(-EINVAL, ft_t_info,
			"desired blk_sz (%d) must be a multiple of %d bytes",
			    *new_size, FT_SECTOR_SIZE);
	}
	if (*new_size == 0) {
		if (zft_use_compression) {
			TRACE_ABORT(-EINVAL, ft_t_info,
				    "Variable block size not yet "
				    "supported with compression");
		}
		*new_size = 1;
	}
	zft_blk_sz = *new_size;
	TRACE_EXIT 0;
} 

static int mt_setdensity(int *arg)
{
	TRACE_FUN(ft_t_flow);
	
	SET_TRACE_LEVEL(*arg);
	TRACE(TRACE_LEVEL, "tracing set to %d", TRACE_LEVEL);
	if ((int)TRACE_LEVEL != *arg) {
		TRACE_EXIT -EINVAL;
	}
	TRACE_EXIT 0;
}          

static int mt_seek(int *new_block_pos)
{ 
	int result= 0;        
	TRACE_FUN(ft_t_any);
	
	result = seek_block(0, (__s64)*new_block_pos, &zft_pos);
	TRACE_EXIT result;
}

/*  OK, this is totally different from SCSI, but the worst thing that can 
 *  happen is that there is not enough defragmentated memory that can be 
 *  allocated. Also, there is a hardwired limit of 16 dma buffers in the 
 *  stock ftape module. This shouldn't bring the system down.
 *
 * NOTE: the argument specifies the total number of dma buffers to use.
 *       The driver needs at least 3 buffers to function at all.
 * 
 */
static int mt_setdrvbuffer(int *cnt)
{
	TRACE_FUN(ft_t_flow);

	if (*cnt < 3) {
		TRACE_EXIT -EINVAL;
	}
	TRACE_CATCH(ftape_set_nr_buffers(*cnt),);
	TRACE_EXIT 0;
}
/* return the block position from start of volume 
 */
static int mt_tell(int *arg)
{
	TRACE_FUN(ft_t_flow);
	
	*arg   = zft_div_blksz(zft_pos.volume_pos,
			       zft_find_volume(zft_pos.seg_pos)->blk_sz);
	TRACE_EXIT 0;
}

static int mt_compression(int *arg)
{
	TRACE_FUN(ft_t_flow);
	
	/*  Ok. We could also check whether compression is available at
	 *  all by trying to load the compression module.  We could
	 *  also check for a block size of 1 byte which is illegal
	 *  with compression.  Instead of doing it here we rely on
	 *  zftape_write() to do the proper checks.
	 */
	if ((unsigned int)*arg > 1) {
		TRACE_EXIT -EINVAL;
	}
	if (*arg != 0 && zft_blk_sz == 1) { /* variable block size */
		TRACE_ABORT(-EINVAL, ft_t_info,
			    "Compression not yet supported "
			    "with variable block size");
	}
	zft_mt_compression  = *arg;
	if ((zft_unit & ZFT_ZIP_MODE) == 0) {
		zft_use_compression = zft_mt_compression;
	}
	TRACE_EXIT 0;
}

/*  check whether write access is allowed. Write access is denied when
 *  + zft_write_protected == 1 -- this accounts for either hard write 
 *                                protection of the cartridge or for 
 *                                O_RDONLY access mode of the tape device
 *  + zft_offline == 1         -- this meany that there is either no tape 
 *                                or that the MTOFFLINE ioctl has been 
 *                                previously issued (`soft eject')
 *  + ft_formatted == 0        -- this means that the cartridge is not
 *                                formatted
 *  Then we distinuguish two cases. When zft_qic_mode is TRUE, then we try
 *  to emulate a `traditional' (aka SCSI like) UN*X tape device. Therefore we
 *  deny writes when
 *  + zft_qic_mode ==1 && 
 *       (!zft_tape_at_lbot() &&   -- tape no at logical BOT
 *        !(zft_tape_at_eom() ||   -- tape not at logical EOM (or EOD)
 *          (zft_tape_at_eom() &&
 *           zft_old_ftape())))    -- we can't add new volume to tapes 
 *                                    written by old ftape because ftape
 *                                    don't use the volume table
 *
 *  when the drive is in true raw mode (aka /dev/rawft0) then we don't 
 *  care about LBOT and EOM conditions. This device is intended for a 
 *  user level program that wants to truly implement the QIC-80 compliance
 *  at the logical data layout level of the cartridge, i.e. implement all
 *  that volume table and volume directory stuff etc.<
 */
int zft_check_write_access(zft_position *pos)
{
	TRACE_FUN(ft_t_flow);

	if (zft_offline) { /* offline includes no_tape */
		TRACE_ABORT(-ENXIO,
			    ft_t_info, "tape is offline or no cartridge");
	}
	if (!ft_formatted) {
		TRACE_ABORT(-EACCES, ft_t_info, "tape is not formatted");
	} 
	if (zft_write_protected) {
		TRACE_ABORT(-EACCES, ft_t_info, "cartridge write protected");
	} 
	if (zft_qic_mode) {
		/*  check BOT condition */
		if (!zft_tape_at_lbot(pos)) {
			/*  protect cartridges written by old ftape if
			 *  not at BOT because they use the vtbl
			 *  segment for storing data
			 */
			if (zft_old_ftape) {
				TRACE_ABORT(-EACCES, ft_t_warn, 
      "Cannot write to cartridges written by old ftape when not at BOT");
			}
			/*  not at BOT, but allow writes at EOD, of course
			 */
			if (!zft_tape_at_eod(pos)) {
				TRACE_ABORT(-EACCES, ft_t_info,
					    "tape not at BOT and not at EOD");
			}
		}
		/*  fine. Now the tape is either at BOT or at EOD. */
	}
	/* or in raw mode in which case we don't care about BOT and EOD */
	TRACE_EXIT 0;
}

/*      OPEN routine called by kernel-interface code
 *
 *      NOTE: this is also called by mt_reset() with dev_minor == -1
 *            to fake a reopen after a reset.
 */
int _zft_open(unsigned int dev_minor, unsigned int access_mode)
{
	static unsigned int tape_unit;
	static unsigned int file_access_mode;
	int result;
	TRACE_FUN(ft_t_flow);

	if ((int)dev_minor == -1) {
		/* fake reopen */
		zft_unit    = tape_unit;
		access_mode = file_access_mode;
		zft_init_driver(); /* reset all static data to defaults */
	} else {
		tape_unit        = dev_minor;
		file_access_mode = access_mode;
		if ((result = ftape_enable(FTAPE_SEL(dev_minor))) < 0) {
			TRACE_ABORT(-ENXIO, ft_t_err,
				    "ftape_enable failed: %d", result);
		}
		if (ft_new_tape || ft_no_tape || !ft_formatted ||
		    (FTAPE_SEL(zft_unit) != FTAPE_SEL(dev_minor)) ||
		    (zft_unit & ZFT_RAW_MODE) != (dev_minor & ZFT_RAW_MODE)) {
			/* reset all static data to defaults,
			 */
			zft_init_driver(); 
		}
		zft_unit = dev_minor;
	}
	zft_set_flags(zft_unit); /* decode the minor bits */
	if (zft_blk_sz == 1 && zft_use_compression) {
		ftape_disable(); /* resets ft_no_tape */
		TRACE_ABORT(-ENODEV, ft_t_warn, "Variable block size not yet "
			    "supported with compression");
	}
	/*  no need for most of the buffers when no tape or not
	 *  formatted.  for the read/write operations, it is the
	 *  regardless whether there is no tape, a not-formatted tape
	 *  or the whether the driver is soft offline.  
	 *  Nevertheless we allow some ioctls with non-formatted tapes, 
	 *  like rewind and reset.
	 */
	if (ft_no_tape || !ft_formatted) {
		zft_uninit_mem();
	}
	if (ft_no_tape) {
		zft_offline = 1; /* so we need not test two variables */
	}
	if ((access_mode == O_WRONLY || access_mode == O_RDWR) &&
	    (ft_write_protected || ft_no_tape)) {
		ftape_disable(); /* resets ft_no_tape */
		TRACE_ABORT(ft_no_tape ? -ENXIO : -EROFS,
			    ft_t_warn, "wrong access mode %s cartridge",
			    ft_no_tape ? "without a" : "with write protected");
	}
	zft_write_protected = (access_mode == O_RDONLY || 
			       ft_write_protected != 0);
	if (zft_write_protected) {
		TRACE(ft_t_noise,
		      "read only access mode: %d, "
		      "drive write protected: %d", 
		      access_mode == O_RDONLY,
		      ft_write_protected != 0);
	}
	if (!zft_offline) {
		TRACE_CATCH(zft_vmalloc_once(&zft_deblock_buf,FT_SEGMENT_SIZE),
			    ftape_disable());
	}
	/* zft_seg_pos should be greater than the vtbl segpos but not
	 * if in compatibility mode and only after we read in the
	 * header segments
	 *
	 * might also be a problem if the user makes a backup with a
	 * *qft* device and rewinds it with a raw device.
	 */
	if (zft_qic_mode         &&
	    !zft_old_ftape       &&
	    zft_pos.seg_pos >= 0 &&
	    zft_header_read      && 
	    zft_pos.seg_pos <= ft_first_data_segment) {
		TRACE(ft_t_noise, "you probably mixed up the zftape devices!");
		zft_reset_position(&zft_pos); 
	}
	TRACE_EXIT 0;
}

/*      RELEASE routine called by kernel-interface code
 */
int _zft_close(void)
{
	int result = 0;
	TRACE_FUN(ft_t_flow);
	
	if (zft_offline) {
		/* call the hardware release routine. Puts the drive offline */
		ftape_disable();
		TRACE_EXIT 0;
	}
	if (!(ft_write_protected || zft_old_ftape)) {
		result = zft_flush_buffers();
		TRACE(ft_t_noise, "writing file mark at current position");
		if (zft_qic_mode && zft_close_volume(&zft_pos) == 0) {
			zft_move_past_eof(&zft_pos);
		}
		if ((zft_tape_at_lbot(&zft_pos) ||
		     !(zft_unit & FTAPE_NO_REWIND))) {
			if (result >= 0) {
				result = zft_update_header_segments();
			} else {
				TRACE(ft_t_err,
				"Error: unable to update header segments");
			}
		}
	}
	ftape_abort_operation();
	if (!(zft_unit & FTAPE_NO_REWIND)) {
		TRACE(ft_t_noise, "rewinding tape");
		if (ftape_seek_to_bot() < 0 && result >= 0) {
			result = -EIO; /* keep old value */
		}
		zft_reset_position(&zft_pos);
	} 
	zft_zap_read_buffers();
	/*  now free up memory as much as possible. We don't destroy
	 *  the deblock buffer if it containes a valid segment.
	 */
	if (zft_deblock_segment == -1) {
		zft_vfree(&zft_deblock_buf, FT_SEGMENT_SIZE); 
	}
	/* high level driver status, forces creation of a new volume
	 * when calling ftape_write again and not zft_just_before_eof
	 */
	zft_io_state = zft_idle;  
	if (going_offline) {
		zft_init_driver();
		zft_uninit_mem();
		going_offline = 0;
		zft_offline   = 1;
	} else if (zft_cmpr_lock(0 /* don't load */) == 0) {
		(*zft_cmpr_ops->reset)(); /* unlock it again */
	}
	zft_memory_stats();
	/* call the hardware release routine. Puts the drive offline */
	ftape_disable();
	TRACE_EXIT result;
}

/*
 *  the wrapper function around the wrapper MTIOCTOP ioctl
 */
static int mtioctop(struct mtop *mtop, int arg_size)
{
	int result = 0;
	fun_entry *mt_fun_entry;
	TRACE_FUN(ft_t_flow);
	
	if (arg_size != sizeof(struct mtop) || mtop->mt_op >= NR_MT_CMDS) {
		TRACE_EXIT -EINVAL;
	}
	TRACE(ft_t_noise, "calling MTIOCTOP command: %s",
	      mt_funs[mtop->mt_op].name);
	mt_fun_entry= &mt_funs[mtop->mt_op];
	zft_resid = mtop->mt_count;
	if (!mt_fun_entry->offline && zft_offline) {
		if (ft_no_tape) {
			TRACE_ABORT(-ENXIO, ft_t_info, "no tape present");
		} else {
			TRACE_ABORT(-ENXIO, ft_t_info, "drive is offline");
		}
	}
	if (!mt_fun_entry->not_formatted && !ft_formatted) {
		TRACE_ABORT(-EACCES, ft_t_info, "tape is not formatted");
	}
	if (!mt_fun_entry->write_protected) {
		TRACE_CATCH(zft_check_write_access(&zft_pos),);
	}
	if (mt_fun_entry->need_idle_state && !(zft_offline || !ft_formatted)) {
		TRACE_CATCH(zft_def_idle_state(),);
	}
	if (!zft_qic_mode && !mt_fun_entry->raw_mode) {
		TRACE_ABORT(-EACCES, ft_t_info, 
"Drive needs to be in QIC-80 compatibility mode for this command");
	}
	result = (mt_fun_entry->function)(&mtop->mt_count);
	if (zft_tape_at_lbot(&zft_pos)) {
		TRACE_CATCH(zft_update_header_segments(),);
	}
	if (result >= 0) {
		zft_resid = 0;
	}
	TRACE_EXIT result;
}

/*
 *  standard MTIOCGET ioctl
 */
static int mtiocget(struct mtget *mtget, int arg_size)
{
	const zft_volinfo *volume;
	__s64 max_tape_pos;
	TRACE_FUN(ft_t_flow);
	
	if (arg_size != sizeof(struct mtget)) {
		TRACE_ABORT(-EINVAL, ft_t_info, "bad argument size: %d",
			    arg_size);
	}
	mtget->mt_type  = ft_drive_type.vendor_id + 0x800000;
	mtget->mt_dsreg = ft_last_status.space;
	mtget->mt_erreg = ft_last_error.space; /* error register */
	mtget->mt_resid = zft_resid; /* residuum of writes, reads and
				      * MTIOCTOP commands 
				      */
	if (!zft_offline) { /* neither no_tape nor soft offline */
		mtget->mt_gstat = GMT_ONLINE(~0UL);
		/* should rather return the status of the cartridge
		 * than the access mode of the file, therefor use
		 * ft_write_protected, not zft_write_protected 
		 */
		if (ft_write_protected) {
			mtget->mt_gstat |= GMT_WR_PROT(~0UL);
		}
		if(zft_header_read) { /* this catches non-formatted */
			volume = zft_find_volume(zft_pos.seg_pos);
			mtget->mt_fileno = volume->count;
			max_tape_pos = zft_capacity - zft_blk_sz;
			if (zft_use_compression) {
				max_tape_pos -= ZFT_CMPR_OVERHEAD;
			}
			if (zft_tape_at_eod(&zft_pos)) {
				mtget->mt_gstat |= GMT_EOD(~0UL);
			}
			if (zft_pos.tape_pos > max_tape_pos) {
				mtget->mt_gstat |= GMT_EOT(~0UL);
			}
			mtget->mt_blkno = zft_div_blksz(zft_pos.volume_pos,
							volume->blk_sz);
			if (zft_just_before_eof) {
				mtget->mt_gstat |= GMT_EOF(~0UL);
			}
			if (zft_tape_at_lbot(&zft_pos)) {
				mtget->mt_gstat |= GMT_BOT(~0UL);
			}
		} else {
			mtget->mt_fileno = mtget->mt_blkno = -1;
			if (mtget->mt_dsreg & QIC_STATUS_AT_BOT) {
				mtget->mt_gstat |= GMT_BOT(~0UL);
			}
		}
	} else {
		if (ft_no_tape) {
			mtget->mt_gstat = GMT_DR_OPEN(~0UL);
		} else {
			mtget->mt_gstat = 0UL;
		}
 		mtget->mt_fileno = mtget->mt_blkno = -1;
	}
	TRACE_EXIT 0;
}

#ifdef MTIOCRDFTSEG
/*
 *  Read a floppy tape segment. This is useful for manipulating the
 *  volume table, and read the old header segment before re-formatting
 *  the cartridge.
 */
static int mtiocrdftseg(struct mtftseg * mtftseg, int arg_size)
{
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCRDFTSEG");
	if (zft_qic_mode) {
		TRACE_ABORT(-EACCES, ft_t_info,
			    "driver needs to be in raw mode for this ioctl");
	} 
	if (arg_size != sizeof(struct mtftseg)) {
		TRACE_ABORT(-EINVAL, ft_t_info, "bad argument size: %d",
			    arg_size);
	}
	if (zft_offline) {
		TRACE_EXIT -ENXIO;
	}
	if (mtftseg->mt_mode != FT_RD_SINGLE &&
	    mtftseg->mt_mode != FT_RD_AHEAD) {
		TRACE_ABORT(-EINVAL, ft_t_info, "invalid read mode");
	}
	if (!ft_formatted) {
		TRACE_EXIT -EACCES; /* -ENXIO ? */

	}
	if (!zft_header_read) {
		TRACE_CATCH(zft_def_idle_state(),);
	}
	if (mtftseg->mt_segno > ft_last_data_segment) {
		TRACE_ABORT(-EINVAL, ft_t_info, "segment number is too large");
	}
	mtftseg->mt_result = ftape_read_segment(mtftseg->mt_segno,
						zft_deblock_buf,
						mtftseg->mt_mode);
	if (mtftseg->mt_result < 0) {
		/*  a negativ result is not an ioctl error. if
		 *  the user wants to read damaged tapes,
		 *  it's up to her/him
		 */
		TRACE_EXIT 0;
	}
	if (copy_to_user(mtftseg->mt_data,
			 zft_deblock_buf,
			 mtftseg->mt_result) != 0) {
		TRACE_EXIT -EFAULT;
	}
	TRACE_EXIT 0;
}
#endif

#ifdef MTIOCWRFTSEG
/*
 *  write a floppy tape segment. This version features writing of
 *  deleted address marks, and gracefully ignores the (software)
 *  ft_formatted flag to support writing of header segments after
 *  formatting.
 */
static int mtiocwrftseg(struct mtftseg * mtftseg, int arg_size)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCWRFTSEG");
	if (zft_write_protected || zft_qic_mode) {
		TRACE_EXIT -EACCES;
	} 
	if (arg_size != sizeof(struct mtftseg)) {
		TRACE_ABORT(-EINVAL, ft_t_info, "bad argument size: %d",
			    arg_size);
	}
	if (zft_offline) {
		TRACE_EXIT -ENXIO;
	}
	if (mtftseg->mt_mode != FT_WR_ASYNC   && 
	    mtftseg->mt_mode != FT_WR_MULTI   &&
	    mtftseg->mt_mode != FT_WR_SINGLE  &&
	    mtftseg->mt_mode != FT_WR_DELETE) {
		TRACE_ABORT(-EINVAL, ft_t_info, "invalid write mode");
	}
	/*
	 *  We don't check for ft_formatted, because this gives
	 *  only the software status of the driver.
	 *
	 *  We assume that the user knows what it is
	 *  doing. And rely on the low level stuff to fail
	 *  when the tape isn't formatted. We only make sure
	 *  that The header segment buffer is allocated,
	 *  because it holds the bad sector map.
	 */
	if (zft_hseg_buf == NULL) {
		TRACE_EXIT -ENXIO;
	}
	if (mtftseg->mt_mode != FT_WR_DELETE) {
		if (copy_from_user(zft_deblock_buf, 
				   mtftseg->mt_data,
				   FT_SEGMENT_SIZE) != 0) {
			TRACE_EXIT -EFAULT;
		}
	}
	mtftseg->mt_result = ftape_write_segment(mtftseg->mt_segno, 
						 zft_deblock_buf,
						 mtftseg->mt_mode);
	if (mtftseg->mt_result >= 0 && mtftseg->mt_mode == FT_WR_SINGLE) {
		/*  
		 *  a negativ result is not an ioctl error. if
		 *  the user wants to write damaged tapes,
		 *  it's up to her/him
		 */
		if ((result = ftape_loop_until_writes_done()) < 0) {
			mtftseg->mt_result = result;
		}
	}
	TRACE_EXIT 0;
}
#endif
  
#ifdef MTIOCVOLINFO
/*
 *  get information about volume positioned at.
 */
static int mtiocvolinfo(struct mtvolinfo *volinfo, int arg_size)
{
	const zft_volinfo *volume;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCVOLINFO");
	if (arg_size != sizeof(struct mtvolinfo)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (zft_offline) {
		TRACE_EXIT -ENXIO;
	}
	if (!ft_formatted) {
		TRACE_EXIT -EACCES;
	}
	TRACE_CATCH(zft_def_idle_state(),);
	volume = zft_find_volume(zft_pos.seg_pos);
	volinfo->mt_volno   = volume->count;
	volinfo->mt_blksz   = volume->blk_sz == 1 ? 0 : volume->blk_sz;
	volinfo->mt_size    = volume->size >> 10;
	volinfo->mt_rawsize = ((zft_calc_tape_pos(volume->end_seg + 1) >> 10) -
			       (zft_calc_tape_pos(volume->start_seg) >> 10));
	volinfo->mt_cmpr    = volume->use_compression;
	TRACE_EXIT 0;
}
#endif

#ifdef ZFT_OBSOLETE  
static int mtioc_zftape_getblksz(struct mtblksz *blksz, int arg_size)
{
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "\n"
	      KERN_INFO "Mag tape ioctl command: MTIOC_ZTAPE_GETBLKSZ\n"
	      KERN_INFO "This ioctl is here merely for compatibility.\n"
	      KERN_INFO "Please use MTIOCVOLINFO instead");
	if (arg_size != sizeof(struct mtblksz)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (zft_offline) {
		TRACE_EXIT -ENXIO;
	}
	if (!ft_formatted) {
		TRACE_EXIT -EACCES;
	}
	TRACE_CATCH(zft_def_idle_state(),);
	blksz->mt_blksz = zft_find_volume(zft_pos.seg_pos)->blk_sz;
	TRACE_EXIT 0;
}
#endif

#ifdef MTIOCGETSIZE
/*
 *  get the capacity of the tape cartridge.
 */
static int mtiocgetsize(struct mttapesize *size, int arg_size)
{
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOC_ZFTAPE_GETSIZE");
	if (arg_size != sizeof(struct mttapesize)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (zft_offline) {
		TRACE_EXIT -ENXIO;
	}
	if (!ft_formatted) {
		TRACE_EXIT -EACCES;
	}
	TRACE_CATCH(zft_def_idle_state(),);
	size->mt_capacity = (unsigned int)(zft_capacity>>10);
	size->mt_used     = (unsigned int)(zft_get_eom_pos()>>10);
	TRACE_EXIT 0;
}
#endif

static int mtiocpos(struct mtpos *mtpos, int arg_size)
{
	int result;
	TRACE_FUN(ft_t_flow);
	
	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCPOS");
	if (arg_size != sizeof(struct mtpos)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	result = mt_tell((int *)&mtpos->mt_blkno);
	TRACE_EXIT result;
}

#ifdef MTIOCFTFORMAT
/*
 * formatting of floppy tape cartridges. This is intended to be used
 * together with the MTIOCFTCMD ioctl and the new mmap feature 
 */

/* 
 *  This function uses ftape_decode_header_segment() to inform the low
 *  level ftape module about the new parameters.
 *
 *  It erases the hseg_buf. The calling process must specify all
 *  parameters to assure proper operation.
 *
 *  return values: -EINVAL - wrong argument size
 *                 -EINVAL - if ftape_decode_header_segment() failed.
 */
static int set_format_parms(struct ftfmtparms *p, __u8 *hseg_buf)
{
	ft_trace_t old_level = TRACE_LEVEL;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "MTIOCFTFORMAT operation FTFMT_SETPARMS");
	memset(hseg_buf, 0, FT_SEGMENT_SIZE);
	PUT4(hseg_buf, FT_SIGNATURE, FT_HSEG_MAGIC);

	/*  fill in user specified parameters
	 */
	hseg_buf[FT_FMT_CODE] = (__u8)p->ft_fmtcode;
	PUT2(hseg_buf, FT_SPT, p->ft_spt);
	hseg_buf[FT_TPC]      = (__u8)p->ft_tpc;
	hseg_buf[FT_FHM]      = (__u8)p->ft_fhm;
	hseg_buf[FT_FTM]      = (__u8)p->ft_ftm;

	/*  fill in sane defaults to make ftape happy.
	 */ 
	hseg_buf[FT_FSM] = (__u8)128; /* 128 is hard wired all over ftape */
	if (p->ft_fmtcode == fmt_big) {
		PUT4(hseg_buf, FT_6_HSEG_1,   0);
		PUT4(hseg_buf, FT_6_HSEG_2,   1);
		PUT4(hseg_buf, FT_6_FRST_SEG, 2);
		PUT4(hseg_buf, FT_6_LAST_SEG, p->ft_spt * p->ft_tpc - 1);
	} else {
		PUT2(hseg_buf, FT_HSEG_1,    0);
		PUT2(hseg_buf, FT_HSEG_2,    1);
		PUT2(hseg_buf, FT_FRST_SEG,  2);
		PUT2(hseg_buf, FT_LAST_SEG, p->ft_spt * p->ft_tpc - 1);
	}

	/*  Synchronize with the low level module. This is particularly
	 *  needed for unformatted cartridges as the QIC std was previously 
	 *  unknown BUT is needed to set data rate and to calculate timeouts.
	 */
	TRACE_CATCH(ftape_calibrate_data_rate(p->ft_qicstd&QIC_TAPE_STD_MASK),
		    _res = -EINVAL);

	/*  The following will also recalcualte the timeouts for the tape
	 *  length and QIC std we want to format to.
	 *  abort with -EINVAL rather than -EIO
	 */
	SET_TRACE_LEVEL(ft_t_warn);
	TRACE_CATCH(ftape_decode_header_segment(hseg_buf),
		    SET_TRACE_LEVEL(old_level); _res = -EINVAL);
	SET_TRACE_LEVEL(old_level);
	TRACE_EXIT 0;
}

/*
 *  Return the internal SOFTWARE status of the kernel driver. This does
 *  NOT query the tape drive about its status.
 */
static int get_format_parms(struct ftfmtparms *p, __u8 *hseg_buffer)
{
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "MTIOCFTFORMAT operation FTFMT_GETPARMS");
	p->ft_qicstd  = ft_qic_std;
	p->ft_fmtcode = ft_format_code;
	p->ft_fhm     = hseg_buffer[FT_FHM];
	p->ft_ftm     = hseg_buffer[FT_FTM];
	p->ft_spt     = ft_segments_per_track;
	p->ft_tpc     = ft_tracks_per_tape;
	TRACE_EXIT 0;
}

static int mtiocftformat(struct mtftformat *mtftformat, int arg_size)
{
	int result;
	union fmt_arg *arg = &mtftformat->fmt_arg;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCFTFORMAT");
	if (zft_offline) {
		if (ft_no_tape) {
			TRACE_ABORT(-ENXIO, ft_t_info, "no tape present");
		} else {
			TRACE_ABORT(-ENXIO, ft_t_info, "drive is offline");
		}
	}
	if (zft_qic_mode) {
		TRACE_ABORT(-EACCES, ft_t_info,
			    "driver needs to be in raw mode for this ioctl");
	} 
	if (zft_hseg_buf == NULL) {
		TRACE_CATCH(zft_vcalloc_once(&zft_hseg_buf, FT_SEGMENT_SIZE),);
	}
	zft_header_read = 0;
	switch(mtftformat->fmt_op) {
	case FTFMT_SET_PARMS:
		TRACE_CATCH(set_format_parms(&arg->fmt_parms, zft_hseg_buf),);
		TRACE_EXIT 0;
	case FTFMT_GET_PARMS:
		TRACE_CATCH(get_format_parms(&arg->fmt_parms, zft_hseg_buf),);
		TRACE_EXIT 0;
	case FTFMT_FORMAT_TRACK:
		if ((ft_formatted && zft_check_write_access(&zft_pos) < 0) ||
		    (!ft_formatted && zft_write_protected)) {
			TRACE_ABORT(-EACCES, ft_t_info, "Write access denied");
		}
		TRACE_CATCH(ftape_format_track(arg->fmt_track.ft_track,
					       arg->fmt_track.ft_gap3),);
		TRACE_EXIT 0;
	case FTFMT_STATUS:
		TRACE_CATCH(ftape_format_status(&arg->fmt_status.ft_segment),);
		TRACE_EXIT 0;
	case FTFMT_VERIFY:
		TRACE_CATCH(ftape_verify_segment(arg->fmt_verify.ft_segment,
				(SectorMap *)&arg->fmt_verify.ft_bsm),);
		TRACE_EXIT 0;
	default:
		TRACE_ABORT(-EINVAL, ft_t_err, "Invalid format operation");
	}
	TRACE_EXIT result;
}
#endif

#ifdef MTIOCFTCMD
/*
 *  send a QIC-117 command to the drive, with optional timeouts,
 *  parameter and result bits. This is intended to be used together
 *  with the formatting ioctl.
 */
static int mtiocftcmd(struct mtftcmd *ftcmd, int arg_size)
{
	int i;
	TRACE_FUN(ft_t_flow);

	TRACE(ft_t_noise, "Mag tape ioctl command: MTIOCFTCMD");
	if (!capable(CAP_SYS_ADMIN)) {
		TRACE_ABORT(-EPERM, ft_t_info,
			    "need CAP_SYS_ADMIN capability to send raw qic-117 commands");
	}
	if (zft_qic_mode) {
		TRACE_ABORT(-EACCES, ft_t_info,
			    "driver needs to be in raw mode for this ioctl");
	} 
	if (arg_size != sizeof(struct mtftcmd)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (ftcmd->ft_wait_before) {
		TRACE_CATCH(ftape_ready_wait(ftcmd->ft_wait_before,
					     &ftcmd->ft_status),);
	}
	if (ftcmd->ft_status & QIC_STATUS_ERROR)
		goto ftmtcmd_error;
	if (ftcmd->ft_result_bits != 0) {
		TRACE_CATCH(ftape_report_operation(&ftcmd->ft_result,
						   ftcmd->ft_cmd,
						   ftcmd->ft_result_bits),);
	} else {
		TRACE_CATCH(ftape_command(ftcmd->ft_cmd),);
		if (ftcmd->ft_status & QIC_STATUS_ERROR)
			goto ftmtcmd_error;
		for (i = 0; i < ftcmd->ft_parm_cnt; i++) {
			TRACE_CATCH(ftape_parameter(ftcmd->ft_parms[i]&0x0f),);
			if (ftcmd->ft_status & QIC_STATUS_ERROR)
				goto ftmtcmd_error;
		}
	}
	if (ftcmd->ft_wait_after != 0) {
		TRACE_CATCH(ftape_ready_wait(ftcmd->ft_wait_after,
					     &ftcmd->ft_status),);
	}
ftmtcmd_error:	       
	if (ftcmd->ft_status & QIC_STATUS_ERROR) {
		TRACE(ft_t_noise, "error status set");
		TRACE_CATCH(ftape_report_error(&ftcmd->ft_error,
					       &ftcmd->ft_cmd, 1),);
	}
	TRACE_EXIT 0; /* this is not an i/o error */
}
#endif

/*  IOCTL routine called by kernel-interface code
 */
int _zft_ioctl(unsigned int command, void __user * arg)
{
	int result;
	union { struct mtop       mtop;
		struct mtget      mtget;
		struct mtpos      mtpos;
#ifdef MTIOCRDFTSEG
		struct mtftseg    mtftseg;
#endif
#ifdef MTIOCVOLINFO
		struct mtvolinfo  mtvolinfo;
#endif
#ifdef MTIOCGETSIZE
		struct mttapesize mttapesize;
#endif
#ifdef MTIOCFTFORMAT
		struct mtftformat mtftformat;
#endif
#ifdef ZFT_OBSOLETE
		struct mtblksz mtblksz;
#endif
#ifdef MTIOCFTCMD
		struct mtftcmd mtftcmd;
#endif
	} krnl_arg;
	int arg_size = _IOC_SIZE(command);
	int dir = _IOC_DIR(command);
	TRACE_FUN(ft_t_flow);

	/* This check will only catch arguments that are too large !
	 */
	if (dir & (_IOC_READ | _IOC_WRITE) && arg_size > sizeof(krnl_arg)) {
		TRACE_ABORT(-EINVAL,
			    ft_t_info, "bad argument size: %d", arg_size);
	}
	if (dir & _IOC_WRITE) {
		if (copy_from_user(&krnl_arg, arg, arg_size) != 0) {
			TRACE_EXIT -EFAULT;
		}
	}
	TRACE(ft_t_flow, "called with ioctl command: 0x%08x", command);
	switch (command) {
	case MTIOCTOP:
		result = mtioctop(&krnl_arg.mtop, arg_size);
		break;
	case MTIOCGET:
		result = mtiocget(&krnl_arg.mtget, arg_size);
		break;
	case MTIOCPOS:
		result = mtiocpos(&krnl_arg.mtpos, arg_size);
		break;
#ifdef MTIOCVOLINFO
	case MTIOCVOLINFO:
		result = mtiocvolinfo(&krnl_arg.mtvolinfo, arg_size);
		break;
#endif
#ifdef ZFT_OBSOLETE
	case MTIOC_ZFTAPE_GETBLKSZ:
		result = mtioc_zftape_getblksz(&krnl_arg.mtblksz, arg_size);
		break;
#endif
#ifdef MTIOCRDFTSEG
	case MTIOCRDFTSEG: /* read a segment via ioctl */
		result = mtiocrdftseg(&krnl_arg.mtftseg, arg_size);
		break;
#endif
#ifdef MTIOCWRFTSEG
	case MTIOCWRFTSEG: /* write a segment via ioctl */
		result = mtiocwrftseg(&krnl_arg.mtftseg, arg_size);
		break;
#endif
#ifdef MTIOCGETSIZE
	case MTIOCGETSIZE:
		result = mtiocgetsize(&krnl_arg.mttapesize, arg_size);
		break;
#endif
#ifdef MTIOCFTFORMAT
	case MTIOCFTFORMAT:
		result = mtiocftformat(&krnl_arg.mtftformat, arg_size);
		break;
#endif
#ifdef MTIOCFTCMD
	case MTIOCFTCMD:
		result = mtiocftcmd(&krnl_arg.mtftcmd, arg_size);
		break;
#endif
	default:
		result = -EINVAL;
		break;
	}
	if ((result >= 0) && (dir & _IOC_READ)) {
		if (copy_to_user(arg, &krnl_arg, arg_size) != 0) {
			TRACE_EXIT -EFAULT;
		}
	}
	TRACE_EXIT result;
}
