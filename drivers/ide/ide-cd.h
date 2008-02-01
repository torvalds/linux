/*
 *  Copyright (C) 1996-98  Erik Andersen
 *  Copyright (C) 1998-2000 Jens Axboe
 */
#ifndef _IDE_CD_H
#define _IDE_CD_H

#include <linux/cdrom.h>
#include <asm/byteorder.h>

/*
 * typical timeout for packet command
 */
#define ATAPI_WAIT_PC		(60 * HZ)
#define ATAPI_WAIT_WRITE_BUSY	(10 * HZ)

/************************************************************************/

#define SECTOR_BITS 		9
#ifndef SECTOR_SIZE
#define SECTOR_SIZE		(1 << SECTOR_BITS)
#endif
#define SECTORS_PER_FRAME	(CD_FRAMESIZE >> SECTOR_BITS)
#define SECTOR_BUFFER_SIZE	(CD_FRAMESIZE * 32)

/* Capabilities Page size including 8 bytes of Mode Page Header */
#define ATAPI_CAPABILITIES_PAGE_SIZE		(8 + 20)
#define ATAPI_CAPABILITIES_PAGE_PAD_SIZE	4

enum {
	/* Device sends an interrupt when ready for a packet command. */
	IDE_CD_FLAG_DRQ_INTERRUPT	= (1 << 0),
	/* Drive cannot lock the door. */
	IDE_CD_FLAG_NO_DOORLOCK		= (1 << 1),
	/* Drive cannot eject the disc. */
	IDE_CD_FLAG_NO_EJECT		= (1 << 2),
	/* Drive is a pre ATAPI 1.2 drive. */
	IDE_CD_FLAG_PRE_ATAPI12		= (1 << 3),
	/* TOC addresses are in BCD. */
	IDE_CD_FLAG_TOCADDR_AS_BCD	= (1 << 4),
	/* TOC track numbers are in BCD. */
	IDE_CD_FLAG_TOCTRACKS_AS_BCD	= (1 << 5),
	/*
	 * Drive does not provide data in multiples of SECTOR_SIZE
	 * when more than one interrupt is needed.
	 */
	IDE_CD_FLAG_LIMIT_NFRAMES	= (1 << 6),
	/* Seeking in progress. */
	IDE_CD_FLAG_SEEKING		= (1 << 7),
	/* Driver has noticed a media change. */
	IDE_CD_FLAG_MEDIA_CHANGED	= (1 << 8),
	/* Saved TOC information is current. */
	IDE_CD_FLAG_TOC_VALID		= (1 << 9),
	/* We think that the drive door is locked. */
	IDE_CD_FLAG_DOOR_LOCKED		= (1 << 10),
	/* SET_CD_SPEED command is unsupported. */
	IDE_CD_FLAG_NO_SPEED_SELECT	= (1 << 11),
	IDE_CD_FLAG_VERTOS_300_SSD	= (1 << 12),
	IDE_CD_FLAG_VERTOS_600_ESD	= (1 << 13),
	IDE_CD_FLAG_SANYO_3CD		= (1 << 14),
	IDE_CD_FLAG_FULL_CAPS_PAGE	= (1 << 15),
	IDE_CD_FLAG_PLAY_AUDIO_OK	= (1 << 16),
	IDE_CD_FLAG_LE_SPEED_FIELDS	= (1 << 17),
};

/* Structure of a MSF cdrom address. */
struct atapi_msf {
	byte reserved;
	byte minute;
	byte second;
	byte frame;
};

/* Space to hold the disk TOC. */
#define MAX_TRACKS 99
struct atapi_toc_header {
	unsigned short toc_length;
	byte first_track;
	byte last_track;
};

struct atapi_toc_entry {
	byte reserved1;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8 adr     : 4;
	__u8 control : 4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8 control : 4;
	__u8 adr     : 4;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	byte track;
	byte reserved2;
	union {
		unsigned lba;
		struct atapi_msf msf;
	} addr;
};

struct atapi_toc {
	int    last_session_lba;
	int    xa_flag;
	unsigned long capacity;
	struct atapi_toc_header hdr;
	struct atapi_toc_entry  ent[MAX_TRACKS+1];
	  /* One extra for the leadout. */
};

/* Extra per-device info for cdrom drives. */
struct cdrom_info {
	ide_drive_t	*drive;
	ide_driver_t	*driver;
	struct gendisk	*disk;
	struct kref	kref;

	/* Buffer for table of contents.  NULL if we haven't allocated
	   a TOC buffer for this device yet. */

	struct atapi_toc *toc;

	unsigned long	sector_buffered;
	unsigned long	nsectors_buffered;
	unsigned char	*buffer;

	/* The result of the last successful request sense command
	   on this device. */
	struct request_sense sense_data;

	struct request request_sense_request;
	int dma;
	unsigned long last_block;
	unsigned long start_seek;

	unsigned int cd_flags;

	u8 max_speed;		/* Max speed of the drive. */
	u8 current_speed;	/* Current speed of the drive. */

        /* Per-device info needed by cdrom.c generic driver. */
        struct cdrom_device_info devinfo;

	unsigned long write_timeout;
};

/* ide-cd_verbose.c */
void ide_cd_log_error(const char *, struct request *, struct request_sense *);

/* ide-cd.c functions used by ide-cd_ioctl.c */
void ide_cd_init_rq(ide_drive_t *, struct request *);
int ide_cd_queue_pc(ide_drive_t *, struct request *);
int ide_cd_read_toc(ide_drive_t *, struct request_sense *);
int ide_cdrom_get_capabilities(ide_drive_t *, u8 *);
void ide_cdrom_update_speed(ide_drive_t *, u8 *);
int cdrom_check_status(ide_drive_t *, struct request_sense *);

/* ide-cd_ioctl.c */
int ide_cdrom_open_real(struct cdrom_device_info *, int);
void ide_cdrom_release_real(struct cdrom_device_info *);
int ide_cdrom_drive_status(struct cdrom_device_info *, int);
int ide_cdrom_check_media_change_real(struct cdrom_device_info *, int);
int ide_cdrom_tray_move(struct cdrom_device_info *, int);
int ide_cdrom_lock_door(struct cdrom_device_info *, int);
int ide_cdrom_select_speed(struct cdrom_device_info *, int);
int ide_cdrom_get_last_session(struct cdrom_device_info *,
			       struct cdrom_multisession *);
int ide_cdrom_get_mcn(struct cdrom_device_info *, struct cdrom_mcn *);
int ide_cdrom_reset(struct cdrom_device_info *cdi);
int ide_cdrom_audio_ioctl(struct cdrom_device_info *, unsigned int, void *);
int ide_cdrom_packet(struct cdrom_device_info *, struct packet_command *);

#endif /* _IDE_CD_H */
