/*
 *  Copyright (C) 1996-98  Erik Andersen
 *  Copyright (C) 1998-2000 Jens Axboe
 */
#ifndef _IDE_CD_H
#define _IDE_CD_H

#include <linux/cdrom.h>
#include <asm/byteorder.h>

#define IDECD_DEBUG_LOG		0

#if IDECD_DEBUG_LOG
#define ide_debug_log(lvl, fmt, args...) __ide_debug_log(lvl, fmt, args)
#else
#define ide_debug_log(lvl, fmt, args...) do {} while (0)
#endif

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

/* Structure of a MSF cdrom address. */
struct atapi_msf {
	u8 reserved;
	u8 minute;
	u8 second;
	u8 frame;
};

/* Space to hold the disk TOC. */
#define MAX_TRACKS 99
struct atapi_toc_header {
	unsigned short toc_length;
	u8 first_track;
	u8 last_track;
};

struct atapi_toc_entry {
	u8 reserved1;
#if defined(__BIG_ENDIAN_BITFIELD)
	u8 adr     : 4;
	u8 control : 4;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	u8 control : 4;
	u8 adr     : 4;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	u8 track;
	u8 reserved2;
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
	ide_drive_t		*drive;
	struct ide_driver	*driver;
	struct gendisk		*disk;
	struct device		dev;

	/* Buffer for table of contents.  NULL if we haven't allocated
	   a TOC buffer for this device yet. */

	struct atapi_toc *toc;

	/* The result of the last successful request sense command
	   on this device. */
	struct request_sense sense_data;

	struct request request_sense_request;

	u8 max_speed;		/* Max speed of the drive. */
	u8 current_speed;	/* Current speed of the drive. */

        /* Per-device info needed by cdrom.c generic driver. */
        struct cdrom_device_info devinfo;

	unsigned long write_timeout;
};

/* ide-cd_verbose.c */
void ide_cd_log_error(const char *, struct request *, struct request_sense *);

/* ide-cd.c functions used by ide-cd_ioctl.c */
int ide_cd_queue_pc(ide_drive_t *, const unsigned char *, int, void *,
		    unsigned *, struct request_sense *, int, unsigned int);
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
