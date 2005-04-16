#ifndef SONYCD535_H
#define SONYCD535_H

/*
 * define all the commands recognized by the CDU-531/5
 */
#define SONY535_REQUEST_DRIVE_STATUS_1		(0x80)
#define SONY535_REQUEST_SENSE			(0x82)
#define SONY535_REQUEST_DRIVE_STATUS_2		(0x84)
#define SONY535_REQUEST_ERROR_STATUS		(0x86)
#define SONY535_REQUEST_AUDIO_STATUS		(0x88)
#define SONY535_INQUIRY				(0x8a)

#define SONY535_SET_INACTIVITY_TIME		(0x90)

#define SONY535_SEEK_AND_READ_N_BLOCKS_1	(0xa0)
#define SONY535_SEEK_AND_READ_N_BLOCKS_2	(0xa4)
#define SONY535_PLAY_AUDIO			(0xa6)

#define SONY535_REQUEST_DISC_CAPACITY		(0xb0)
#define SONY535_REQUEST_TOC_DATA		(0xb2)
#define SONY535_REQUEST_SUB_Q_DATA		(0xb4)
#define SONY535_REQUEST_ISRC			(0xb6)
#define SONY535_REQUEST_UPC_EAN			(0xb8)

#define SONY535_SET_DRIVE_MODE			(0xc0)
#define SONY535_REQUEST_DRIVE_MODE		(0xc2)
#define SONY535_SET_RETRY_COUNT			(0xc4)

#define SONY535_DIAGNOSTIC_1			(0xc6)
#define SONY535_DIAGNOSTIC_4			(0xcc)
#define SONY535_DIAGNOSTIC_5			(0xce)

#define SONY535_EJECT_CADDY			(0xd0)
#define SONY535_DISABLE_EJECT_BUTTON		(0xd2)
#define SONY535_ENABLE_EJECT_BUTTON		(0xd4)

#define SONY535_HOLD				(0xe0)
#define SONY535_AUDIO_PAUSE_ON_OFF		(0xe2)
#define SONY535_SET_VOLUME			(0xe8)

#define SONY535_STOP				(0xf0)
#define SONY535_SPIN_UP				(0xf2)
#define SONY535_SPIN_DOWN			(0xf4)

#define SONY535_CLEAR_PARAMETERS		(0xf6)
#define SONY535_CLEAR_ENDING_ADDRESS		(0xf8)

/*
 * define some masks
 */
#define SONY535_DATA_NOT_READY_BIT		(0x1)
#define SONY535_RESULT_NOT_READY_BIT		(0x2)

/*
 *  drive status 1
 */
#define SONY535_STATUS1_COMMAND_ERROR		(0x1)
#define SONY535_STATUS1_DATA_ERROR		(0x2)
#define SONY535_STATUS1_SEEK_ERROR		(0x4)
#define SONY535_STATUS1_DISC_TYPE_ERROR		(0x8)
#define SONY535_STATUS1_NOT_SPINNING		(0x10)
#define SONY535_STATUS1_EJECT_BUTTON_PRESSED	(0x20)
#define SONY535_STATUS1_CADDY_NOT_INSERTED	(0x40)
#define SONY535_STATUS1_BYTE_TWO_FOLLOWS	(0x80)

/*
 * drive status 2
 */
#define SONY535_CDD_LOADING_ERROR		(0x7)
#define SONY535_CDD_NO_DISC			(0x8)
#define SONY535_CDD_UNLOADING_ERROR		(0x9)
#define SONY535_CDD_CADDY_NOT_INSERTED		(0xd)
#define SONY535_ATN_RESET_OCCURRED		(0x2)
#define SONY535_ATN_DISC_CHANGED		(0x4)
#define SONY535_ATN_RESET_AND_DISC_CHANGED	(0x6)
#define SONY535_ATN_EJECT_IN_PROGRESS		(0xe)
#define SONY535_ATN_BUSY			(0xf)

/*
 * define some parameters
 */
#define SONY535_AUDIO_DRIVE_MODE		(0)
#define SONY535_CDROM_DRIVE_MODE		(0xe0)

#define SONY535_PLAY_OP_PLAYBACK		(0)
#define SONY535_PLAY_OP_ENTER_HOLD		(1)
#define SONY535_PLAY_OP_SET_AUDIO_ENDING_ADDR	(2)
#define SONY535_PLAY_OP_SCAN_FORWARD		(3)
#define SONY535_PLAY_OP_SCAN_BACKWARD		(4)

/*
 *  convert from msf format to block number 
 */
#define SONY_BLOCK_NUMBER(m,s,f) (((m)*60L+(s))*75L+(f))
#define SONY_BLOCK_NUMBER_MSF(x) (((x)[0]*60L+(x)[1])*75L+(x)[2])

/*
 *  error return values from the doSonyCmd() routines
 */
#define TIME_OUT			(-1)
#define NO_CDROM			(-2)
#define BAD_STATUS			(-3)
#define CD_BUSY				(-4)
#define NOT_DATA_CD			(-5)
#define NO_ROOM				(-6)

#define LOG_START_OFFSET        150     /* Offset of first logical sector */

#define SONY_JIFFIES_TIMEOUT	(5*HZ)	/* Maximum time
					   the drive will wait/try for an
					   operation */
#define SONY_READY_RETRIES      (50000)  /* How many times to retry a
                                                  spin waiting for a register
                                                  to come ready */
#define SONY535_FAST_POLLS	(10000)   /* how many times recheck 
                                                  status waiting for a data
                                                  to become ready */

typedef unsigned char Byte;

/*
 * This is the complete status returned from the drive configuration request
 * command.
 */
struct s535_sony_drive_config
{
   char vendor_id[8];
   char product_id[16];
   char product_rev_level[4];
};

/* The following is returned from the request sub-q data command */
struct s535_sony_subcode
{
   unsigned char address        :4;
   unsigned char control        :4;
   unsigned char track_num;
   unsigned char index_num;
   unsigned char rel_msf[3];
   unsigned char abs_msf[3];
};

struct s535_sony_disc_capacity
{
   Byte mFirstTrack, sFirstTrack, fFirstTrack;
   Byte mLeadOut, sLeadOut, fLeadOut;
};

/*
 * The following is returned from the request TOC (Table Of Contents) command.
 * (last_track_num-first_track_num+1) values are valid in tracks.
 */
struct s535_sony_toc
{
   unsigned char reserved0      :4;
   unsigned char control0       :4;
   unsigned char point0;
   unsigned char first_track_num;
   unsigned char reserved0a;
   unsigned char reserved0b;
   unsigned char reserved1      :4;
   unsigned char control1       :4;
   unsigned char point1;
   unsigned char last_track_num;
   unsigned char dummy1;
   unsigned char dummy2;
   unsigned char reserved2      :4;
   unsigned char control2       :4;
   unsigned char point2;
   unsigned char lead_out_start_msf[3];
   struct
   {
      unsigned char reserved    :4;
      unsigned char control     :4;
      unsigned char track;
      unsigned char track_start_msf[3];
   } tracks[100];

   unsigned int lead_out_start_lba;
};

#endif /* SONYCD535_H */
